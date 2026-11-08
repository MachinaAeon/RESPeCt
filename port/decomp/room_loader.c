/* Room object loader: the step machine that brings a room's enemies and
 * item pickups into memory, a step per frame, while the room plays
 * (TEST overlay, 0x80074AD0-0x80077044).
 *
 * The room's setup data (pointer at 0x800BBB88) holds, from +4, a list of
 * 0x18-byte enemy placements ended by a type with 0xF000 in the top bits:
 *   +0 s16 type (byte 0 is copied to the record's +0x315), +2 s16 game flag
 *   (placement skipped when set), +4/+8/+0xC s32 position, +0x10 u16 yaw,
 *   +0x12 s16 (pose/animation), +0x14 u16 health (bit 15: start hidden),
 *   +0x16 u16 (+0x31E).
 * A second list (0x800BBB78), ended by a negative type, places item
 * pickups (same entry size; +0x10..+0x14 rotation, +0x12 also kept in the
 * pickup record). Type 0x11 is a random grenade (0xB, 0xD, 0xE or 0xF).
 *
 * Every enemy and pickup gets a 0x8C4-byte object record (copied from the
 * template at 0x800B42D0), in an array (0x800AB510) carved off the end of
 * the memory before 0x800BBCD0. Models load from the enemy files (types
 * 0x32+: 3 files per type in a sector table) or from the stage's own file;
 * each enemy of a model gets its own copy of the model's work data (model
 * records: 0x1C bytes at 0x800B3F10). Pickups get the four records at
 * 0x800BBC18 (type, flag, object, data, +0xC, flags).
 *
 * File reads go through one asynchronous request (0x800B3E40: state, name,
 * sector table, index, destination, byte offset, byte size) that
 * cd_Update advances every frame.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"
#include "libgs.h"
#include "modmem.h"
#include "rt.h"

/* ---- loader state ---- */
#define g_loaderStep     GV(s32, 0x800A0CBC)   /* index into LOADER_STEPS */
#define LOADER_STEPS     0x800A1014u           /* 24 step functions */
#define g_setupData      GV(u32, 0x800BBB88)   /* room setup: enemy list at +4 */
#define g_itemList       GV(u32, 0x800BBB78)   /* room item pickups */
#define g_setupEntry     GV(u32, 0x800B4DBC)   /* entry being loaded */
#define g_setupType      GV(s32, 0x800B4DB4)   /* its type */
#define g_setupPrevType  GV(s32, 0x800B4DB8)   /* the previous entry's type (-1 none) */
#define g_loadPtr        GV(u32, 0x800B4D78)   /* allocation pointer (from 0x80150000) */
#define g_partA          GV(u32, 0x800B4DC0)   /* where the model's parts were loaded */
#define g_partB          GV(u32, 0x800B4DC4)
#define g_texEnd         GV(u32, 0x800B4DC8)   /* end of the textures loaded to 0x800F8000 */
#define g_enemyFile      GV(s32, 0x800B4DA8)   /* first file of the enemy type */
#define g_texShareMode   GV(s32, 0x800B4D80)
#define g_instance       GV(s32, 0x800B4DAC)   /* copy of the model / pickup slot */
#define g_listIndex      GV(s32, 0x800B4DCC)   /* index into g_fileList */
#define g_itemVariant    GV(s32, 0x800B4DD0)   /* 1 random grenade, 2 flagged 0x4000 */
#define g_fileList       0x800AF540u           /* 3 x {s32 id (<0 end), s32 kind/file, s32 arg} */
#define g_objects        GV(u32, 0x800AB510)   /* object records */
#define g_objectCount    GV(s32, 0x800AF530)
#define g_objectWords    GV(s32, 0x800AF56C)
#define OBJ_TEMPLATE     0x800B42D0u
#define OBJ_SIZE         0x8C4u
#define g_modelCount     GV(s32, 0x800B4D7C)
#define MODELS           0x800B3F10u           /* 0x1C each */
#define MODEL_SIZE       0x1Cu
#define TYPE_SLOTS       0x800BB020u           /* {s16 type, s16 used} */
#define PICKUPS          0x800BBC18u           /* 4 x 0x14 */

/* ---- file request ---- */
#define CDREQ            0x800B3E40u
#define g_cdState        GV(s32, CDREQ)        /* 0 idle, 1-4 busy */
#define g_cdSize         GV(s32, CDREQ + 0x18) /* byte size of the last request */

#define FLAGS_ENEMY      0x80062F60u           /* game flag sets */
#define FLAGS_ITEM       0x80062FA0u
#define FLAGS_STORY      0x80062FC0u
#define ENEMY_FILE_NAME  0x80058534u
#define ENEMY_FILE_TBL   0x800A195Cu
#define STAGE_FILE_NAME  0x80058530u
#define STAGE_FILE_TBL   0x800A0CD8u
#define SOUND_FILE_NAME  0x80058510u
#define ITEM_FILE_NAME   0x8005855Cu
#define TEX_BUF          0x800F8000u           /* textures / compressed parts */
#define WORK_BUF         0x80108000u
#define HEAP_START       0x80150000u

#define F_FLAG_TEST      0x8008F64Cu   /* (flag set, flag): nonzero when set */
#define F_DECOMPRESS     0x80016F78u   /* (src, dst): size */
#define F_UPLOAD_TIMS    0x8006F370u
#define F_MODEL_SECTION  0x8007C558u
#define F_LOAD_IMAGE     0x80035620u
#define F_STORE_IMAGE    0x80035680u
#define F_DRAW_SYNC      0x80035374u
#define F_TYPE_SLOT      0x8008DC94u
#define F_SOUND_FILES    0x80080C60u
#define F_SOUND_SLOT     0x80011BB0u
#define F_SOUND_SEND     0x80011BBCu
#define F_TYPE_EXTRA     0x8009815Cu
#define F_EXTRA_UPLOAD   0x800927D8u
#define F_RANDOM         0x80011570u
#define F_ITEM_FILES     0x8008EA80u
#define F_ITEM_FILE_TBL  0x8008EAF4u
#define F_ITEM_FLAGS     0x8008EB00u
#define F_MODEL_INIT     0x8006E304u
#define F_OBJ_MATRIX2    0x80082760u
#define F_ENEMY_INIT     0x8008DE84u
#define F_ENEMY_POSE     0x80082CA4u
#define F_ENEMY_C38      0x80082C38u
#define F_ENEMY_9D8      0x800829D8u
#define F_LOAD_FILE      0x800115C0u   /* (name, file, table, index, [dest]): words */
#define F_LOADER_RESET   0x80073F88u
#define F_CD_SEARCH      0x80029F0Cu
#define F_CD_INT_TO_POS  0x80029E08u
#define F_CD_READ        0x80015D44u
#define F_CD_READ_SYNC   0x80014B14u
#define F_CD_READ_END    0x80015AE8u

void n_cd_RequestRead(void);
void n_loader_Reload(void);
void n_room_LoadAll(void);

static u32 obj_addr(s32 i) { return g_objects + (u32)i * OBJ_SIZE; }
static u32 model_addr(s32 i) { return MODELS + (u32)i * MODEL_SIZE; }
static u32 flag_set(u32 set, s16 flag) { return GCALL2(F_FLAG_TEST, set, (s32)flag); }

static u32 cd_request(u32 name, u32 table, s32 index, u32 dest) {
    A0 = name; A1 = table; A2 = (u32)index; A3 = dest;
    n_cd_RequestRead();
    return V0;
}

/* LoadImage / StoreImage of the 128x256 area at (512, 0) from/to TEX_BUF.
 * The RECT is the caller's local at sp + 0x10. */
static void vram_transfer(u32 sp, u32 func) {
    u32 rect = sp + 0x10;
    GS(s16, rect) = 0x200;
    GS(s16, rect + 2) = 0;
    GS(s16, rect + 4) = 0x80;
    GS(s16, rect + 6) = 0x100;
    GCALL2(func, rect, TEX_BUF);
    GCALL1(F_DRAW_SYNC, 0);
}

static s32 end_of_list(s16 type) { return (type & 0xF000) == 0xF000; }

/* Boot-cached sizes only: preserve CD requests, sectors and timing.
 * Shared payloads are counted once for identical consecutive types; work
 * uses a conservative upper bound from its containing part/file. */
static u32 model_sizes(u32 table, s32 file, u32 parts[3]) {
    u32 packed;
    if (!modfs_model_request_size(table, file, &packed, parts))
        rt_fatal("mods: no model size metadata for table %08X file %d", table, file);
    return packed;
}
static int needs_first_part(s32 t, s32 prev) {
    switch(t) {
    case 0: case 1: case 2: case 8: return prev == -1 || (u32)(prev-3)<5;
    case 4: case 5: case 6: return !((u32)(prev-4)<3);
    case 13: case 14: return !((u32)(prev-13)<2);
    default: return t != prev;
    }
}
static int sync_first_part(s32 type,s32 prev);
static void room_budget(int synchronous) {
    uint64_t bytes = 0, scratch = 0, work_bound = 0;
    s32 prev = -1;
    u32 models = 0,objects = 0;
    u32 p = g_setupData + 4;
    s32 cached=GV(s16,p);
    for (u32 count = 0; !end_of_list(GV(s16,p)); count++, p += 0x18) {
        if (count == 512 || p >= 0x80200000u - 0x18)
            rt_fatal("mods: invalid room placement list before model preflight");
        if (flag_set(FLAGS_ENEMY, GV(s16,p+2))) continue;
        s32 t = synchronous ? cached : GV(s16,p);
        if (synchronous && ++objects>7)
            rt_fatal("mods: synchronous preflight exceeds original seven object slots");
        u32 parts[3];
        if ((t!=prev || (u32)(t-0x1F)<3 || (synchronous && t>=0x32)) && ++models>MODMEM_MODELS)
            rt_fatal("mods: room exceeds original model-record table capacity");
        if ((u32)(t-0x1F)<3) {
            bytes += (uint64_t)model_sizes(ENEMY_FILE_TBL,t+0x171,parts) + 16;
            work_bound = 4;
        } else if (t==prev && !(synchronous && t>=0x32)) {
            bytes += work_bound + 4;
        } else if (t >= 0x32) {
            bytes += (uint64_t)model_sizes(ENEMY_FILE_TBL,(t-0x32)*3,parts) + 16;
            work_bound = t==0x9A ? 4 : model_sizes(ENEMY_FILE_TBL,(t-0x32)*3+2,parts);
            if (t!=0x9A) bytes += work_bound + 16;
        } else {
            u32 packed=model_sizes(t==0x1E ? ENEMY_FILE_TBL : STAGE_FILE_TBL,t==0x1E ? 0x196 : t,parts);
            u32 table=t==0x1E ? ENEMY_FILE_TBL : STAGE_FILE_TBL, file=t==0x1E ? 0x196 : (u32)t;
            int staged=modmem_split_identity(table,file);
            if ((!staged && packed > HEAP_START-(synchronous ? WORK_BUF : TEX_BUF)) || !parts[0] || !parts[1] || !parts[2])
                rt_fatal("mods: invalid or oversized compressed room model before read");
            uint64_t temp=(uint64_t)parts[0]+48+(staged ? (uint64_t)packed+32 : 0);
            if (temp>scratch) scratch=temp;
            bytes += (uint64_t)parts[2] + 40;
            if (synchronous ? sync_first_part(t,prev) : needs_first_part(t,prev)) bytes += (uint64_t)parts[1] + 40;
            work_bound=parts[2];
        }
        bytes += 0xDC;
        if (synchronous && (t==0x22 || t==0x23)) t=0x10;
        prev=t;
        if (synchronous) {
            cached=GV(s16,p+0x18);
            if (cached==0x22 || cached==0x23) cached=0x10;
        }
    }
    /* Pickups and script-created extras have data-dependent sizes and get
     * their own capacity checks before reads, decode and final placement. */
    modmem_budget(bytes, scratch);
}
static void model_slot_check(void) {
    if (modmem_active() && (g_modelCount<0 || (u32)g_modelCount>=MODMEM_MODELS))
        rt_fatal("mods: original model-record table is full before model loading");
}
static void model_decode_check(u32 src, u32 dest) {
    modmem_require_fresh();
    if (modmem_active()) modmem_require(dest, (uint64_t)GV(u32,src) + 34, "model/animation decompression");
}

/* Initial and event/reset loading has a separate, synchronous implementation
 * at 8007428C. Without mods it's room_LoadAll, the decompiled original (it
 * keeps the original's frame, so save states made inside it continue). Its
 * seven object slots and sharing rules differ from Begin/Run. */
static void sync_busy_leave(int *busy) { (void)busy; modmem_sync_end(); }
static u32 sync_file(u32 sp,u32 name,u32 table,s32 index,u32 dest) {
    u32 parts[3],bytes=model_sizes(table,index,parts);
    if (dest>=0x80200000u) {
        if (!modmem_stage_read(table,(u32)index,dest,bytes)) modmem_require(dest,bytes,"synchronous model file read");
    } else if ((dest==WORK_BUF && bytes>HEAP_START-WORK_BUF) ||
               (dest==0x800C3E50u && bytes>0x800F2C50u-0x800C3E50u))
        rt_fatal("mods: synchronous resource exceeds its original fixed staging buffer");
    GS(u32,sp+0x10)=dest;
    u32 words=GCALL4(F_LOAD_FILE,table==STAGE_FILE_TBL ? 0x8006C230u : 0x8006C21Cu,name,table,index);
    if ((uint64_t)words*4>bytes) rt_fatal("mods: synchronous model read returned an invalid size");
    return words;
}
static int sync_first_part(s32 type,s32 prev) {
    if (type<3 || type==8 || type==9)
        return prev==-1 || (u32)(prev-3)<5 || prev>=10;
    if ((u32)(type-4)<3) return !((u32)(prev-4)<3);
    if ((u32)(type-13)<2) return !((u32)(prev-13)<2);
    return 1;
}
static void sync_packed_record(u32 entry,u32 data) {
    u32 rec=model_addr(g_modelCount);
    GV(u32,rec+0x10)=data;
    u32 hdr=GCALL2(F_MODEL_SECTION,data,3);
    u32 sec=data+GV(u32,hdr+4)*4;
    GV(s32,rec)=GV(s16,entry);
    GV(u32,rec+8)=data+GV(u32,sec+0xC)*4;
    GV(u32,rec+4)=0;
    GV(u32,rec+0xC)=data+GV(u32,sec+0x10)*4;
    GV(u32,rec+0x14)=GCALL2(F_MODEL_SECTION,data,2);
    GV(u32,rec+0x18)=g_loadPtr-data;
    g_modelCount++;
}
void n_loader_LoadSync(void) {
    if (!modmem_enabled() || !modfs_dynamic_models()) { n_room_LoadAll(); return; }
    modmem_sync_begin();
    __attribute__((cleanup(sync_busy_leave))) int busy=1;
    FRAME(0x48);
    u32 texture_buffer=A0,saved_s3=R[19];
    R[19]=0x800B0000u;                         /* implicit odds input of roomscript_Branch */
    GS(u32,sp+0x48)=texture_buffer;
    g_objectWords=0xF57;                         /* original seven 0x8C4-byte slots */
    u32 end=GV(u32,0x800BBCD0);
    if (end<0x80000000u+0x3D5C || end>0x80200000u) rt_fatal("mods: invalid synchronous object pool");
    g_objects=end-0x3D5C;
    for (u32 i=0;i<0xF57;i++) {
        GV(u32,g_objects+i*4)=0;
        if (i+1<0xF57) LOOP_POLL(0x800742F4);
    }
    copy_record_polled(OBJ_TEMPLATE,g_objects,0x80074334);
    for (s32 i=11;;) {
        GV(u32,0x800A8850+(u32)i*4)=0;
        if (--i<0) break;
        LOOP_POLL(0x80074360);
    }
    GV(u32,0x800AB51C)=0;
    for (s32 i=4;;) {
        GV(s16,0x800B3EA8+(u32)i*12)=-1;
        if (--i<0) break;
        LOOP_POLL(0x8007438C);
    }
    GV(u32,0x800BB018)=0;
    GV(s16,TYPE_SLOTS+6)=0; GV(s16,TYPE_SLOTS+2)=0;
    GV(s32,0x800B6090)=-1;
    g_objectCount=0; g_modelCount=0;
    GV(u32,0x800BAA30)=0; g_texShareMode=0;
    g_loadPtr=HEAP_START;
    GCALL0(F_LOADER_RESET);                     /* selects the original script variant */
    modmem_begin(g_setupData);
    room_budget(1);
    g_loadPtr=(modfs_static_end()+15u)&~15u;
    u32 entry=g_setupData+4,part_a=0,instance=0;
    s32 type=GV(s16,entry),prev=-1;
    for (u32 visited=0;!end_of_list(GV(s16,entry));visited++) {
        if (visited>=512 || entry>=0x80200000u-0x18) rt_fatal("mods: invalid synchronous placement list");
        if (flag_set(FLAGS_ENEMY,GV(s16,entry+2))) {
            /* The original skips this entry without replacing its cached type. */
            entry+=0x18;
            if (!end_of_list(GV(s16,entry))) LOOP_POLL(0x80074A98);
            continue;
        }
        if (g_objectCount>=7) rt_fatal("mods: synchronous loader exceeds its original seven object slots");
        model_slot_check();
        if (type>=0x32) {
            s32 file=(type-0x32)*3;
            if (type==0x9A) {
                u32 data=g_loadPtr;
                g_loadPtr+=sync_file(sp,ENEMY_FILE_NAME,ENEMY_FILE_TBL,file,data)*4;
                sync_packed_record(entry,data);
            } else {
                sync_file(sp,ENEMY_FILE_NAME,ENEMY_FILE_TBL,file+1,0x800C3E50u);
                GCALL1(F_UPLOAD_TIMS,texture_buffer);
                part_a=g_loadPtr;
                g_loadPtr+=sync_file(sp,ENEMY_FILE_NAME,ENEMY_FILE_TBL,file,g_loadPtr)*4;
                u32 part_b=g_loadPtr;
                g_loadPtr+=sync_file(sp,ENEMY_FILE_NAME,ENEMY_FILE_TBL,file+2,g_loadPtr)*4;
                u32 rec=model_addr(g_modelCount++);
                GV(s32,rec)=GV(s16,entry);
                GV(u32,rec+8)=part_a+(GV(u32,part_a+4)&~3u);
                GV(u32,rec+0xC)=part_a+(GV(u32,part_a+8)&~3u);
                GV(u32,rec+4)=part_b+(GV(u32,part_b+4)&~3u);
                GV(u32,rec+0x14)=0;
                u32 work=part_b+(GV(u32,part_b+8)&~3u);
                GV(u32,rec+0x10)=work;
                GV(u32,rec+0x18)=g_loadPtr-work;
                instance=0;
            }
        } else if ((u32)(type-0x1F)<3) {
            u32 data=g_loadPtr;
            g_loadPtr+=sync_file(sp,ENEMY_FILE_NAME,ENEMY_FILE_TBL,type+0x171,data)*4;
            sync_packed_record(entry,data);
        } else if (type==prev) {
            u32 rec=model_addr(g_modelCount-1),src=GV(u32,rec+0x10),stride=GV(u32,rec+0x18)&~3u;
            if (stride && (uint64_t)src+(uint64_t)stride*(instance+1)!=g_loadPtr)
                rt_fatal("mods: synchronous instance work would cease to be contiguous");
            modmem_require(g_loadPtr,stride?stride:4,"synchronous contiguous model work");
            u32 stop=src+stride;
            do {
                GV(u32,g_loadPtr)=GV(u32,src);src+=4;g_loadPtr+=4;
                if (src<stop) LOOP_POLL(0x80074908);
            } while (src<stop);
            instance++;
        } else {
            u32 table=type==0x1E ? ENEMY_FILE_TBL : STAGE_FILE_TBL;
            s32 file=type==0x1E ? 0x196 : type;
            u32 parts[3],bytes=model_sizes(table,file,parts),source=WORK_BUF;
            if (!parts[0] || !parts[1] || !parts[2]) rt_fatal("mods: missing synchronous split model metadata");
            if (modmem_split_identity(table,(u32)file)) source=modmem_stage_acquire(table,(u32)file,bytes);
            sync_file(sp,type==0x1E ? ENEMY_FILE_NAME : STAGE_FILE_NAME,table,file,source);
            u32 src=source+GV(u32,source+8);
            if (sync_first_part(type,prev)) {
                if (type==0x22 || type==0x23) type=0x10;
                part_a=g_loadPtr;model_decode_check(src,g_loadPtr);
                g_loadPtr+=(GCALL2(F_DECOMPRESS,src,g_loadPtr)&~3u);
            }
            int shared_texture=(prev==0&&type==4)||(prev==1&&type==5)||(prev==2&&type==6)||
                               (prev==4&&type==0)||(prev==5&&type==1)||(prev==6&&type==2);
            if (!shared_texture) {
                src=source+GV(u32,source+4);
                u32 temp=modmem_scratch((uint64_t)GV(u32,src)+34,"synchronous room texture decode");
                GCALL2(F_DECOMPRESS,src,temp);GCALL1(F_UPLOAD_TIMS,temp);
            }
            src=source+GV(u32,source+0xC);
            u32 part_b=g_loadPtr;model_decode_check(src,part_b);
            g_loadPtr+=(GCALL2(F_DECOMPRESS,src,part_b)&~3u);
            modmem_stage_release();
            u32 rec=model_addr(g_modelCount++);
            GV(s32,rec)=GV(s16,entry);
            GV(u32,rec+8)=part_a+(GV(u32,part_a+4)&~3u);
            GV(u32,rec+0xC)=part_a+(GV(u32,part_a+8)&~3u);
            GV(u32,rec+4)=part_b+(GV(u32,part_b+4)&~3u);
            GV(u32,rec+0x14)=0;
            u32 work=part_b+(GV(u32,part_b+8)&~3u);
            GV(u32,rec+0x10)=work;GV(u32,rec+0x18)=g_loadPtr-work;
            instance=0;
        }
        modmem_checkpoint((u32)g_modelCount,g_loadPtr);
        u32 object=obj_addr(g_objectCount),rec=model_addr(g_modelCount-1);
        copy_record_polled(object,OBJ_TEMPLATE,0x80074970);
        GV(u8,object+0x314)=(u8)g_objectCount;
        GV(u8,object+0x315)=GV(u8,entry);
        GV(u32,object+0x88C)=rec;
        GV(u32,object+0x848)=entry;
        GV(u32,object+0x890)=GV(u32,rec+0x10)+((GV(u32,rec+0x18)*instance)&~3u);
        if (GV(u8,object+0x315)<0x32) {
            GS(s32,sp+0x18)=type;
            s32 slot=(s32)GCALL3(F_TYPE_SLOT,object,sp+0x18,0);
            if ((u32)slot>=2) rt_fatal("mods: invalid synchronous enemy program slot");
            GV(s16,TYPE_SLOTS+(u32)slot*4+2)=1;
            GV(u16,TYPE_SLOTS+(u32)slot*4)=GS(u16,sp+0x18);
            GV(s16,object+0x84E)=(s16)slot;
            GCALL2(0x80080A28u,(u32)slot,GS(u32,sp+0x18));
            GCALL2(0x80098100u,GS(u32,sp+0x18),0x800C3E50u);
        }
        entry+=0x18;prev=type;g_objectCount++;
        type=GV(s16,entry);
        if (type==0x22 || type==0x23) type=0x10;
        if (!end_of_list(GV(s16,entry))) LOOP_POLL(0x80074A98);
    }
    R[19]=saved_s3;
    V0=(u32)GV(u16,entry)&0xF000u;             /* end marker bits, as left by the original */
}

/* 80074AD0 (step 0): idle. */
void n_loader_Idle(void) { V0 = 0; }

/* 80074AD8 (step 1): start loading the room: reset the loader's tables,
 * count the enemies and pickups that will appear, carve their records off
 * the end of memory and fill them from the template. */
void n_loader_Begin(void) {
    FRAME(0x28);
    modmem_begin(g_setupData);
    if (modmem_active()) room_budget(0);
    for (s32 i = 11;; ) {
        GV(u32, 0x800A8850 + (u32)i * 4) = 0;
        if (--i < 0) break;
        LOOP_POLL(0x80074B0C);
    }
    GV(u32, 0x800AB51C) = 0;
    for (s32 i = 4;; ) {
        GV(s16, 0x800B3EA8 + (u32)i * 12) = -1;
        if (--i < 0) break;
        LOOP_POLL(0x80074B38);
    }
    GV(u32, 0x800BB018) = 0;
    GV(s16, TYPE_SLOTS + 6) = 0;
    GV(s16, TYPE_SLOTS + 2) = 0;
    GV(s32, 0x800B6090) = -1;
    g_objectCount = 0;
    g_modelCount = 0;
    GV(u32, 0x800BAA30) = 0;
    g_texShareMode = 0;
    GCALL0(F_LOADER_RESET);
    g_partA = 0;
    g_partB = 0;
    g_instance = 0;

    u32 setup = g_setupData;
    g_setupEntry = setup + 4;
    g_setupPrevType = -1;
    g_setupType = GV(s16, setup + 4);
    g_loaderStep = end_of_list(GV(s16, setup + 4)) ? 0x11 : 2;

    s32 enemies = 0;
    if (!end_of_list(GV(s16, g_setupEntry))) {
        u32 off = 0;
        do {
            if (!flag_set(FLAGS_ENEMY, GV(s16, g_setupEntry + off + 2))) enemies++;
            off += 0x18;
            if (end_of_list(GV(s16, g_setupEntry + off))) break;
            LOOP_POLL(0x80074C54);
        } while (1);
    }
    s32 items = 0;
    if (GV(s16, g_itemList) != -1) {
        u32 off = 0;
        do {
            if (!flag_set(FLAGS_ITEM, GV(s16, g_itemList + off + 2))) items++;
            off += 0x18;
            if (GV(s16, g_itemList + off) == -1) break;
            LOOP_POLL(0x80074CBC);
        } while (1);
    }
    s32 n = enemies + items;
    s32 words = n * (s32)(OBJ_SIZE / 4);
    g_objectWords = words;
    g_loadPtr = modmem_active() ? ((modfs_static_end() + 15u) & ~15u) : HEAP_START;
    g_objects = GV(u32, 0x800BBCD0) - (u32)words * 4;
    for (s32 i = 0; i < n - 1; ) {
        copy_record_polled(obj_addr(i), OBJ_TEMPLATE, 0x80074D54);
        if (++i >= n - 1) break;
        LOOP_POLL(0x80074D70);
    }
    V0 = 0;
}

/* Advance the allocation pointer past the file just requested. */
static void skip_loaded_trunc(void) { g_loadPtr += (u32)(g_cdSize / 4 * 4); }

/* 80074D9C (step 2): next enemy placement: skip it if its flag is set,
 * otherwise request its model (enemy files: model, then textures; stage
 * types: textures to TEX_BUF), unless the previous entry had the same
 * type (its model data is copied instead). */
void n_loader_RequestModel(void) {
    modmem_require_fresh();
    FRAME(0x20);
    g_texEnd = TEX_BUF;
    if (flag_set(FLAGS_ENEMY, GV(s16, g_setupEntry + 2))) {
        u32 e = g_setupEntry;
        g_setupEntry = e + 0x18;
        s16 t = GV(s16, e + 0x18);
        if (end_of_list(t)) g_loaderStep = 0x11;
        else { g_setupType = t; g_loaderStep = 2; }
        V0 = 0;
        return;
    }
    s32 type = g_setupType;
    if (type >= 0x32) {
        if ((u32)(type - 0x32) < 10 || type == 0x55) g_texShareMode = 1;
        if (g_setupPrevType == g_setupType) { g_loaderStep = 7; V0 = 0; return; }
        model_slot_check();
        s32 file = (g_setupType - 0x32) * 3;
        u32 dest = g_loadPtr;
        g_enemyFile = file;
        g_partA = dest;
        cd_request(ENEMY_FILE_NAME, ENEMY_FILE_TBL, file, dest);
        g_loaderStep = 3;
        g_loadPtr += (u32)(g_cdSize >> 2 << 2);
        V0 = 0;
        return;
    }
    if ((u32)(type - 0x1F) < 3) {               /* types 0x1F-0x21: one packed file */
        model_slot_check();
        cd_request(ENEMY_FILE_NAME, ENEMY_FILE_TBL, type + 0x171, g_loadPtr);
        u32 p = g_loadPtr;
        GV(u32, model_addr(g_modelCount) + 0x10) = p;
        g_loadPtr = p + (u32)(g_cdSize / 4 * 4);
        g_loaderStep = 3;
        V0 = 0;
        return;
    }
    if (type == g_setupPrevType) { g_loaderStep = 7; V0 = 0; return; }
    model_slot_check();
    vram_transfer(sp, F_LOAD_IMAGE);
    type = g_setupType;
    if (type == 0x12) g_texShareMode = 2;
    u32 table=type==0x1E ? ENEMY_FILE_TBL : STAGE_FILE_TBL;
    s32 file=type==0x1E ? 0x196 : type;
    u32 source=TEX_BUF;
    if (modmem_active() && modmem_split_identity(table,(u32)file)) {
        if (g_cdState) { V0=0; return; }
        u32 parts[3], bytes=model_sizes(table,file,parts);
        if (!parts[0] || !parts[1] || !parts[2]) rt_fatal("mods: missing split model metadata before staging read");
        source=modmem_stage_acquire(table,(u32)file,bytes);
    }
    cd_request(type==0x1E ? ENEMY_FILE_NAME : STAGE_FILE_NAME,table,file,source);
    u32 end = source + (u32)(g_cdSize >> 2 << 2);
    g_texEnd = end;
    GV(s32, end) = -1;
    g_loaderStep = 3;
    V0 = 0;
}

static u32 split_source(void) {
    u32 source=modmem_stage_source();
    return source ? source : TEX_BUF;
}

/* 80075050 (step 3): once the read is done, unpack the model's first part
 * for stage types (unless it can stay from the previous type). */
void n_loader_UnpackModel(void) {
    FRAME(0x18);
    if (g_cdState != 0) { V0 = 0; return; }
    s32 type = g_setupType;
    if (type < 0x32) {
        if ((u32)(type - 0x1F) < 3) { g_loaderStep = 6; V0 = 0; return; }
        u32 src = GV(u32, split_source() + 8) + split_source();
        s32 prev = g_setupPrevType;
        s32 unpack;
        if ((u32)type < 0xF) LOOP_POLL(0x800750CC);   /* the jump table */
        switch (type) {
        case 0: case 1: case 2: case 8:
            unpack = prev == -1 || (u32)(prev - 3) < 5;
            break;
        case 4: case 5: case 6:
            unpack = !((u32)(prev - 4) < 3);
            break;
        case 13: case 14:
            unpack = !((u32)(prev - 0xD) < 2);
            break;
        default:
            unpack = g_setupType != g_setupPrevType;
            break;
        }
        if (unpack) {
            model_decode_check(src, g_loadPtr);
            g_partA = g_loadPtr;
            u32 size = GCALL2(F_DECOMPRESS, src, g_loadPtr);
            g_loadPtr += size >> 2 << 2;
        }
    }
    g_loaderStep = g_setupType == 0x9A ? 6 : 4;
    V0 = 0;
}

/* 800751CC (step 4): the textures: stage types unpack them (except for
 * pairs of types that share them), enemy types 0x3C+ request theirs. */
void n_loader_Textures(void) {
    modmem_require_fresh();
    FRAME(0x18);
    s32 t = g_setupType;
    if (t < 7) {
        s32 p = g_setupPrevType;
        if ((p == 0 && t == 4) || (p == 1 && t == 5) || (p == 2 && t == 6) ||
            (p == 4 && t == 0) || (p == 5 && t == 1) || (p == 6 && t == 2)) {
            g_loaderStep = 5; V0 = 0; return;
        }
    }
    t = g_setupType;
    if (t < 0x32) {
        u32 src = GV(u32, split_source() + 4) + split_source();
        if (modmem_active()) g_texEnd = modmem_scratch((uint64_t)GV(u32,src) + 34, "room textures");
        GCALL2(F_DECOMPRESS, src, g_texEnd);
    } else if (t >= 0x3C) {
        cd_request(ENEMY_FILE_NAME, ENEMY_FILE_TBL, (t - 0x32) * 3 + 1, WORK_BUF);
    }
    g_loaderStep = 5;
    V0 = 0;
}

/* 800752E8 (step 5): upload the textures; enemy types request their
 * second part (animations). */
void n_loader_UploadTextures(void) {
    FRAME(0x18);
    s32 t = g_setupType;
    if (t < 0x32) {
        u32 end = g_texEnd;
        if (GV(s32, end) != -1) GCALL1(F_UPLOAD_TIMS, end);
        g_loaderStep = 6;
        V0 = 0;
        return;
    }
    s32 go = 0;
    if (t < 0x3C) go = 1;
    else if (g_cdState == 0) { GCALL1(F_UPLOAD_TIMS, WORK_BUF); go = 1; }
    if (go) {
        u32 dest = g_loadPtr;
        g_partB = dest;
        cd_request(ENEMY_FILE_NAME, ENEMY_FILE_TBL, (g_setupType - 0x32) * 3 + 2, dest);
        g_loaderStep = 6;
        skip_loaded_trunc();
    }
    V0 = 0;
}

/* Fill a model record from a packed model (types 0x1F-0x21 and 0x9A):
 * sections 3 and 2 of the data at +0x10. */
static void packed_model_record(void) {
    u32 rec = model_addr(g_modelCount);
    u32 hdr = GCALL2(F_MODEL_SECTION, GV(u32, rec + 0x10), 3);
    rec = model_addr(g_modelCount);
    u32 base = GV(u32, rec + 0x10);
    u32 sec = base + GV(u32, hdr + 4) * 4;
    GV(s32, rec) = GV(s16, g_setupEntry);
    GV(u32, rec + 8) = base + GV(u32, sec + 0xC) * 4;
    GV(u32, rec + 4) = 0;
    GV(u32, rec + 0xC) = base + (GV(u32, sec + 0x10) << 2);
    u32 anim = GCALL2(F_MODEL_SECTION, base, 2);
    s32 n = g_modelCount;
    g_instance = 0;
    g_loaderStep = 8;
    rec = model_addr(n);
    GV(u32, rec + 0x14) = anim;
    GV(u32, rec + 0x18) = 0;
    g_modelCount = n + 1;
}

/* 800753F0 (step 6): fill the model record. Stage types unpack their
 * other part and read the textures back (VRAM -> TEX_BUF). */
void n_loader_ModelRecord(void) {
    FRAME(0x28);
    model_slot_check();
    s32 t = g_setupType;
    if (t < 0x32) {
        if ((u32)(t - 0x1F) < 3) {
            packed_model_record();
            V0 = 0;
            return;
        }
        u32 dst = g_loadPtr;
        u32 src = GV(u32, split_source() + 0xC) + split_source();
        model_decode_check(src, dst);
        g_partB = dst;
        u32 size = GCALL2(F_DECOMPRESS, src, dst);
        g_loadPtr += size >> 2 << 2;
        if (modmem_active()) modmem_stage_release();
        vram_transfer(sp, F_STORE_IMAGE);
    }
    if (g_setupType == 0x9A) {
        u32 a = g_partA;
        GV(u32, model_addr(g_modelCount) + 0x10) = a;
        u32 hdr = GCALL2(F_MODEL_SECTION, a, 3);
        u32 rec = model_addr(g_modelCount);
        u32 base = GV(u32, rec + 0x10);
        u32 sec = base + GV(u32, hdr + 4) * 4;
        GV(s32, rec) = GV(s16, g_setupEntry);
        GV(u32, rec + 8) = base + GV(u32, sec + 0xC) * 4;
        GV(u32, rec + 4) = 0;
        GV(u32, rec + 0xC) = base + (GV(u32, sec + 0x10) << 2);
        u32 anim = GCALL2(F_MODEL_SECTION, base, 2);
        s32 n = g_modelCount;
        g_instance = 0;
        g_loaderStep = 8;
        rec = model_addr(n);
        GV(u32, rec + 0x14) = anim;
        g_modelCount = n + 1;
        GV(u32, rec + 0x18) = g_partA - GV(u32, rec + 0x10);
        V0 = 0;
        return;
    }
    if (g_cdState != 0) { V0 = 0; return; }
    s32 n = g_modelCount;
    u32 rec = model_addr(n);
    u32 a = g_partA;
    GV(s32, rec) = GV(s16, g_setupEntry);
    GV(u32, rec + 8) = a + (GV(u32, a + 4) >> 2 << 2);
    u32 b = g_partB;
    GV(u32, rec + 0xC) = a + (GV(u32, a + 8) >> 2 << 2);
    g_instance = 0;
    GV(u32, rec + 4) = b + (GV(u32, b + 4) >> 2 << 2);
    g_loaderStep = 8;
    g_modelCount = n + 1;
    GV(u32, rec + 0x14) = 0;
    u32 work = b + (GV(u32, b + 8) >> 2 << 2);      /* per-instance work data */
    GV(u32, rec + 0x18) = g_loadPtr - work;         /* its size */
    GV(u32, rec + 0x10) = work;
    V0 = 0;
}

/* 8007575C (step 7): same type as the previous entry: copy the last
 * model's work data for this instance. */
void n_loader_CopyModelWork(void) {
    u32 rec = model_addr(g_modelCount - 1);
    u32 src = GV(u32, rec + 0x10);
    u32 stride = GV(u32, rec + 0x18) >> 2 << 2;
    if (modmem_active()) {
        uint64_t expected=(uint64_t)src+(uint64_t)stride*((uint64_t)(u32)g_instance+1);
        if (stride && expected!=g_loadPtr) rt_fatal("mods: instance work would cease to be contiguous");
        modmem_require(g_loadPtr, stride ? stride : 4, "contiguous instance work copy");
    }
    g_partB = src + stride;
    for (;;) {
        GV(u32, g_loadPtr) = GV(u32, src);
        src += 4;
        g_loadPtr += 4;
        if (!(src < g_partB)) break;
        LOOP_POLL(0x800757C4);
    }
    g_loaderStep = 8;
    g_instance++;
    V0 = 0;
}

/* 800757F0 (step 8): the enemy's object record, pointing at its model
 * record, placement and work data. */
void n_loader_EnemyRecord(void) {
    if (modmem_active()) modmem_checkpoint((u32)g_modelCount, g_loadPtr);
    u32 e = obj_addr(g_objectCount);
    copy_record_polled(e, OBJ_TEMPLATE, 0x8007584C);
    GV(u8, e + 0x314) = (u8)g_objectCount;
    u32 rec = model_addr(g_modelCount - 1);
    GV(u32, e + 0x88C) = rec;
    u32 off = (u32)(GV(s32, rec + 0x18) * g_instance);
    g_loaderStep = 9;
    GV(u32, e + 0x848) = g_setupEntry;
    GV(u32, e + 0x890) = GV(u32, rec + 0x10) + (off >> 2 << 2);
    V0 = 0;
}

/* 800758F4 (step 9): stage types get a type slot (sounds, extras). */
void n_loader_TypeSlot(void) {
    FRAME(0x20);
    s32 t = g_setupType;
    if (t >= 0x32) { g_loaderStep = 0xB; V0 = 0; return; }
    u32 local = sp + 0x10;                    /* the type, in and out */
    GS(s32, local) = t;
    u32 e = obj_addr(g_objectCount);
    s32 slot = (s32)GCALL3(F_TYPE_SLOT, e, local, 1);
    u32 s = TYPE_SLOTS + (u32)slot * 4;
    GV(s16, s + 2) = 1;
    g_loaderStep = 0xA;
    GV(u16, s) = GS(u16, local);
    GV(s16, e + 0x84E) = (s16)slot;
    V0 = 0;
}

/* 800759A8 (step 10): wait for the file read. */
void n_loader_WaitRead(void) {
    if (g_cdState == 0) g_loaderStep = 0xB;
    V0 = 0;
}

/* 800759CC (step 11): list the type's sound files. */
void n_loader_ListSounds(void) {
    FRAME(0x18);
    u32 e = obj_addr(g_objectCount);
    if (g_setupType < 0x32) {
        s16 slot = GV(s16, e + 0x84E);
        GCALL2(F_SOUND_FILES, (s32)slot, (s32)GV(s16, TYPE_SLOTS + (u32)(s32)slot * 4));
        if (GV(s32, g_fileList) >= 0) {
            g_listIndex = 0;
            g_loaderStep = 0xC;
            V0 = 0;
            return;
        }
    }
    g_loaderStep = 0xE;
    V0 = 0;
}

/* 80075A74 (step 12): request the next sound file (at most three). */
void n_loader_RequestSound(void) {
    FRAME(0x20);
    s32 i = g_listIndex;
    u32 f = g_fileList + (u32)i * 12;
    if (GV(s32, f) < 0 || i >= 3) { g_loaderStep = 0xE; V0 = 0; return; }
    u32 tbl = GCALL0(F_SOUND_SLOT);
    f = g_fileList + (u32)g_listIndex * 12;
    cd_request(SOUND_FILE_NAME, tbl, GV(s32, f + 4), GV(u32, f + 8));
    g_loaderStep = 0xD;
    V0 = 0;
}

/* 80075B28 (step 13): when read, send it to the sound chip; next file. */
void n_loader_SendSound(void) {
    FRAME(0x18);
    if (g_cdState == 0) {
        u32 f = g_fileList + (u32)g_listIndex * 12;
        GCALL3(F_SOUND_SEND, GV(u32, f), GV(u32, f + 4), GV(u32, f + 8));
        g_loaderStep = 0xC;
        g_listIndex++;
    }
    V0 = 0;
}

/* 80075BA0 (step 14): stage types may have extra data to load. */
void n_loader_TypeExtra(void) {
    FRAME(0x18);
    u32 e = obj_addr(g_objectCount);
    if (GV(u8, e + 0x315) >= 0x32) { g_loaderStep = 0x10; V0 = 0; return; }
    s16 slot = GV(s16, e + 0x84E);
    u32 r = GCALL2(F_TYPE_EXTRA, (s32)GV(s16, TYPE_SLOTS + (u32)(s32)slot * 4), WORK_BUF);
    g_loaderStep = r ? 0xF : 0x10;
    V0 = 0;
}

/* 80075C3C (step 15): when read, use the extra data. */
void n_loader_UseExtra(void) {
    FRAME(0x18);
    if (g_cdState == 0) {
        GCALL2(F_EXTRA_UPLOAD, WORK_BUF, (u32)-1);
        g_loaderStep = 0x10;
    }
    V0 = 0;
}

/* 80075C7C (step 16): on to the next placement (types 0x22 and 0x23
 * load as 0x10); at the end of the list, the pickups. */
void n_loader_NextEnemy(void) {
    g_objectCount++;
    u32 e = g_setupEntry;
    g_setupEntry = e + 0x18;
    g_setupPrevType = g_setupType;
    g_setupType = GV(s16, e + 0x18);
    if (g_setupType == 0x22) g_setupType = 0x10;
    if (g_setupType == 0x23) g_setupType = 0x10;
    g_loaderStep = end_of_list(GV(s16, e + 0x18)) ? 0x11 : 2;
    V0 = 0;
}

/* 80075D04 (step 17): start on the item pickups. */
void n_loader_BeginItems(void) {
    for (u32 i = 0;; ) {
        u32 r = PICKUPS + i * 0x14;
        GV(s16, r) = -1;
        GV(s16, r + 2) = -1;
        GV(u32, r + 4) = 0;
        GV(u32, r + 8) = 0;
        GV(u32, r + 12) = 0;
        if (++i >= 4) break;
        LOOP_POLL(0x80075D30);
    }
    GV(u32, 0x800B4DB0) = 0;
    g_instance = 0;
    g_listIndex = 0;
    g_loaderStep = 0x12;
    g_setupEntry = g_itemList;
    V0 = 0;
}

/* 80075D74 (step 18): next pickup not yet taken: pick a random grenade
 * type for type 0x11, list its files. At the end the loader is done
 * (story flags 0xAD and 0xB5 unset: 0x800A0CC0 = 1). */
void n_loader_NextItem(void) {
    FRAME(0x20);
    while (GV(s16, g_setupEntry) >= 0) {
        if (flag_set(FLAGS_ITEM, GV(s16, g_setupEntry + 2))) {
            g_setupEntry += 0x18;
            if (GV(s16, g_setupEntry) < 0) break;
            LOOP_POLL(0x80075EC4);
            continue;
        }
        if (GV(s16, g_setupEntry) == 0x11) {
            g_itemVariant = 1;
            u32 r = GCALL0(F_RANDOM) & 0xFF;
            s16 t = r < 0x34 ? 0xD : r < 0x48 ? 0xE : r < 0x5C ? 0xF : 0xB;
            GV(s16, g_setupEntry) = t;
        } else {
            g_itemVariant = 0;
        }
        u32 p = g_setupEntry;
        if (GV(u16, p) & 0x4000) {
            g_itemVariant = 2;
            GV(u16, p) &= 0xBFFF;
        }
        GCALL1(F_ITEM_FILES, (s32)GV(s16, g_setupEntry));
        g_listIndex = 0;
        g_loaderStep = 0x13;
        V0 = 0;
        return;
    }
    g_loaderStep = 0;
    if (flag_set(FLAGS_STORY, 0xAD) || flag_set(FLAGS_STORY, 0xB5)) g_loaderStep = 0;
    else GV(s32, 0x800A0CC0) = 1;
    V0 = 0;
}

/* 80075F28 (step 19): request the pickup's next file: kind 1 its model
 * (to the heap), kinds 0 and 2 to WORK_BUF. */
void n_loader_RequestItemFile(void) {
    modmem_require_fresh();
    FRAME(0x20);
    s32 i = g_listIndex;
    u32 f = g_fileList + (u32)i * 12;
    if (GV(s32, f) < 0 || i >= 3) { g_loaderStep = 0x15; V0 = 0; return; }
    s32 kind = GV(s32, f + 4);
    if (kind == 1) {
        u32 tbl = GCALL0(F_ITEM_FILE_TBL);
        cd_request(ITEM_FILE_NAME, tbl, GV(s32, g_fileList + (u32)g_listIndex * 12), g_loadPtr);
        GV(u32, PICKUPS + (u32)g_instance * 0x14 + 8) = g_loadPtr;
    } else if (kind == 0 || kind == 2) {
        u32 tbl = GCALL0(F_ITEM_FILE_TBL);
        cd_request(ITEM_FILE_NAME, tbl, GV(s32, g_fileList + (u32)g_listIndex * 12), WORK_BUF);
    }
    g_loaderStep = 0x14;
    V0 = 0;
}

/* The pickup's object record (after its model file). */
static void item_record(void) {
    u32 e = obj_addr(g_objectCount);
    copy_record_polled(e, OBJ_TEMPLATE, 0x80076260);
    GCALL3(F_MODEL_INIT, 0, g_loadPtr, e);
    GV(s32, e + 0x170) = 0x1000;
    GV(s32, e + 0x174) = 0x1000;
    GV(s32, e + 0x178) = 0x1000;
    GCALL2(F_OBJ_MATRIX2, e + 0x180, e + 0x170);
    GV(u8, e + 0x2A) = 0;
    GV(u8, e + 0x29) = 1;
    GV(s32, e + 0x8C0) = 0x190;
    GV(u8, e + 0x314) = (u8)g_objectCount;
    for (u32 i = 0; i < GV(u8, e + 0x28); ) {
        GV(u8, e + i) = 1;
        if (!(++i < GV(u8, e + 0x28))) break;
        LOOP_POLL(0x800762E8);
    }
    g_loadPtr += (u32)(g_cdSize / 4 * 4);

    u32 p = g_setupEntry;
    GV(u32, e + 0x198) = GV(u32, p + 4);
    GV(u32, e + 0x19C) = GV(u32, p + 8);
    GV(u32, e + 0x1A0) = GV(u32, p + 0xC);
    GV(u16, e + 0x1C4) = GV(u16, p + 0x10);
    GV(u16, e + 0x1C6) = GV(u16, p + 0x12);
    GV(u16, e + 0x1C8) = GV(u16, p + 0x14);
    GV(u16, e + 0x850) = GV(u16, p + 2);
    GV(u8, e + 0x315) = GV(u8, p);

    u32 pk = PICKUPS + (u32)g_instance * 0x14;
    GV(u16, pk) = GV(u16, p);
    GV(u32, pk + 4) = e;
    GV(u16, pk + 2) = GV(u16, p + 2);
    GV(s32, pk + 0xC) = GV(s16, p + 0x12);
    if (GV(u16, p) < 0x5A) {
        u32 fl = GCALL1(F_ITEM_FLAGS, (s32)GV(s16, pk));
        GV(u32, PICKUPS + (u32)g_instance * 0x14 + 0x10) = fl;
    } else {
        GV(u32, pk + 0x10) = 0;
    }
    pk = PICKUPS + (u32)g_instance * 0x14;
    if (GV(u32, pk + 0x10) & 8) GV(u8, e + 0x29) = 0;
    if (g_itemVariant == 1) { GV(u32, pk + 0x10) |= 0x10; GV(u8, e + 0x29) = 0; }
    if (g_itemVariant == 2) { GV(u32, pk + 0x10) |= 0x20; GV(u8, e + 0x29) = 0; }
    g_loaderStep = 0x12;
    g_objectCount++;
    g_instance++;
    g_setupEntry += 0x18;
}

/* 8007607C (step 20): when the file is read: kind 0 uploads textures,
 * kind 2 unpacks the model (and textures) and copies its data to the
 * heap, then (kinds 1 and 2) the pickup's object record. */
void n_loader_ItemFileDone(void) {
    modmem_require_fresh();
    FRAME(0x20);
    if (g_cdState != 0) { V0 = 0; return; }
    s32 kind = GV(s32, g_fileList + (u32)g_listIndex * 12 + 4);
    if (kind == 2) {
        u32 data = (u32)g_cdSize + WORK_BUF;
        if (modmem_active()) data = modmem_scratch((uint64_t)GV(u32,WORK_BUF) + 34, "pickup decode");
        GCALL2(F_DECOMPRESS, WORK_BUF, data);
        if (modmem_active()) {
            u32 first = GV(u32,data+4), end = GV(u32,data+8);
            if (end < first || end > GV(u32,WORK_BUF)) rt_fatal("mods: invalid pickup model span");
            modmem_require(g_loadPtr, (uint64_t)end-first, "pickup model copy");
        }
        s16 t = GV(s16, g_setupEntry);
        if (t >= 0x5D || t < 0x5B) GCALL1(F_UPLOAD_TIMS, data + GV(u32, data + 8));
        u32 dst = g_loadPtr;
        for (s32 i = 0; i < (GV(s32, data + 8) - GV(s32, data + 4)) / 4; i++) {
            GV(u32, dst) = GV(u32, data + (u32)(GV(s32, data + 4) / 4 * 4) + (u32)i * 4);
            dst += 4;
            LOOP_POLL(0x800761BC);                  /* the loop's jump back */
        }
        g_cdSize = GV(s32, data + 8) - GV(s32, data + 4);
        GV(u32, PICKUPS + (u32)g_instance * 0x14 + 8) = g_loadPtr;
        kind = 1;
    }
    if (kind == 1) {
        item_record();
        V0 = 0;
        return;
    }
    if (kind == 0) GCALL1(F_UPLOAD_TIMS, WORK_BUF);
    g_loaderStep = 0x13;
    g_listIndex++;
    V0 = 0;
}

/* 80076634: load the room's objects again, from the setup's second entry
 * on: the last three object records and models are dropped (the memory
 * pointer goes back to the end of what the last kept model uses, its
 * +0x14 -> +4 -> +4) and the loader starts over. Returns 0. */
void n_loader_Reload(void) {
    FRAME(0x18);
    GV(u32, 0x800BAA30) = 0;
    g_texShareMode = 0;
    g_objectCount -= 3;
    g_modelCount -= 3;
    GCALL0(F_LOADER_RESET);
    g_partA = 0;
    g_partB = 0;
    g_instance = 0;
    u32 setup = g_setupData;
    g_setupEntry = setup + 0x1C;
    g_setupPrevType = -1;
    g_setupType = GV(s16, setup + 0x1C);
    g_loaderStep = end_of_list(GV(s16, setup + 0x1C)) ? 0x11 : 2;
    u32 m = GV(u32, model_addr(g_modelCount - 1) + 0x14);
    g_loadPtr = modmem_active() ? modmem_retain((u32)g_modelCount) : GV(u32, GV(u32, m + 4) + 4);
    V0 = 0;
}

/* 8007672C: run loader steps until one returns 0 (once a frame). */
void n_loader_Run(void) {
    FRAME(0x20);
    for (;;) {
        gcall(GV(u32, LOADER_STEPS + (u32)g_loaderStep * 4), A0, A1, A2, A3);
        if (V0 == 0) break;
        LOOP_POLL(0x80076768);
    }
}

/* 80076784 (first object, entry offset in halfwords): put the room's
 * enemies back to their placements (re-entering the room): the same walk
 * as the loader (copies of a model shift their texture pages), reading
 * each placement into its record again, or hiding it if its flag is now
 * set. */
/* Returns 0xF000 like the original (the end marker's bits). */
void n_loader_ResetEnemies(void) {
    FRAME(0x30);
    s32 prev = -1;
    u32 p = g_setupData + 4 + A1 * 2;
    s32 type = GV(s16, p);
    s32 copy = 0;
    if (end_of_list((s16)type)) { V0 = 0xF000; return; }
    u32 off = A0 * OBJ_SIZE;
    for (;;) {
        if (flag_set(FLAGS_ENEMY, GV(s16, p + 2))) {
            p += 0x18;
            s32 t = GV(s16, p);
            if (t != -1) type = t;
            if (end_of_list((s16)t)) { V0 = 0xF000; return; }
            LOOP_POLL(0x80076AE4);
            continue;
        }
        /* copies of the same model in a row (types 0-2/8/9 and 4-6 count
         * as one) */
        if (type < 3 || type == 8 || type == 9) {
            if (prev < 3 || prev == 8 || prev == 9) copy++; else copy = 0;
        } else if ((u32)(type - 4) < 3) {
            if (prev == -1 || (u32)(prev - 4) < 3) copy++; else copy = 0;
        } else {
            if (type == prev) copy++; else copy = 0;
        }

        u32 e = g_objects + off;
        GCALL3(F_MODEL_INIT, 1, GV(u32, e + 0x890), e);
        u32 n = GV(u8, e + 0x1D0);
        for (u32 i = 0; i < n; ) {
            u32 part = GV(u32, e + 0x1D4 + i * 4);
            GV(u16, part + 0xE) += (u16)(copy * (s32)n);
            if (++i >= n) break;
            LOOP_POLL(0x800768F0);
        }

        if (GV(s16, p) >= 0 && !flag_set(FLAGS_ENEMY, GV(s16, p + 2))) {
            GV(u32, e + 0x320) = 0;
            GV(u8, e + 0x315) = GV(u8, p);
            GV(u16, e + 0x850) = GV(u16, p + 2);
            GV(u32, e + 0x198) = GV(u32, p + 4);
            GV(u32, e + 0x19C) = GV(u32, p + 8);
            GV(u32, e + 0x1A0) = GV(u32, p + 0xC);
            GV(u8, e + 0x2B) = 0;
            GV(u16, e + 0x1C6) = GV(u16, p + 0x10);
            GCALL2(F_ENEMY_INIT, e, type < 0xB ? 0 : type);
            GCALL2(F_ENEMY_POSE, e, (s32)GV(s16, p + 0x12));
            GV(u16, e + 0x318) = (u16)GCALL2(F_ENEMY_C38, e, 0);
            GV(s16, e + 0x31A) = -1;
            GV(u8, e + 0x29) = (GV(s16, p + 0x14) & 0x8000) ? 0 : 1;
            u16 hp = GV(u16, p + 0x14);
            GV(s16, e + 0x31C) = 0;
            GV(s16, e + 0x316) = 1;
            GV(u16, e + 0x85E) = hp & 0x7FFF;
            GV(u16, e + 0x31E) = GV(u16, p + 0x16);
            p += 0x18;
            GV(u16, e + 0x31E) = (u16)GCALL2(F_ENEMY_9D8, e, 2);
        } else {
            GV(u8, e + 0x2B) = 0;
            GCALL2(F_ENEMY_INIT, e, type < 0xB ? 0 : type);
            p += 0x18;
            GV(u8, e + 0x29) = 0;
        }
        GV(s32, e + 0x170) = 0x1000;
        GV(s32, e + 0x174) = 0x1000;
        GV(s32, e + 0x178) = 0x1000;
        GCALL2(F_OBJ_MATRIX, e + 0x180, e + 0x170);
        GV(u8, e + 0x2A) = 1;
        GV(u32, e + 0x87C) = 0;
        GV(s16, e + 0x324) = -1;
        GV(u16, e + 0x860) = GV(u16, e + 0x85E);
        GV(s32, e + 0x8C0) = (s16)GCALL2(F_ENEMY_9D8, e, 0xA);
        GV(u32, e + 0x89C) = 0;
        for (u32 i = 0; i < GV(u8, e + 0x28); ) {
            GV(u8, e + i) = 1;
            if (!(++i < GV(u8, e + 0x28))) break;
            LOOP_POLL(0x80076AA8);
        }

        if (type == 0x22 || type == 0x23) type = 0x10;
        prev = type;
        type = GV(s16, p);
        off += OBJ_SIZE;
        if (end_of_list(GV(s16, p))) { V0 = 0xF000; return; }
        LOOP_POLL(0x80076AE4);
    }
}

/* 80076B14 (name, sector table, file, dest): start reading a file: busy
 * -> 0, else 1. Offset and size come from the sector table (2048-byte
 * sectors). */
void n_cd_RequestRead(void) {
    if (g_cdState != 0) { V0 = 0; return; }
    u32 ent = A1 + A2 * 4;
    if (modmem_active()) {
        uint64_t first = GV(u32,ent), last = GV(u32,ent+4);
        if (last < first || last-first > UINT32_MAX/2048u) rt_fatal("mods: invalid model read extent");
        uint64_t bytes = (last-first)*2048;
        if (A3 >= 0x80200000u) {
            if (!modmem_stage_read(A1,A2,A3,bytes)) modmem_require(A3,bytes,"model file read");
        }
        else if ((A3 == TEX_BUF || A3 == WORK_BUF) && bytes > HEAP_START-A3)
            rt_fatal("mods: compressed model exceeds original staging buffer");
    }
    GV(u32, CDREQ + 4) = A0;
    GV(u32, CDREQ + 8) = A1;
    GV(u32, CDREQ + 0xC) = A2;
    GV(u32, CDREQ + 0x10) = A3;
    u32 start = GV(u32, ent) << 11;
    GV(u32, CDREQ + 0x14) = start;
    u32 end = GV(u32, ent + 4) << 11;
    g_cdState = 1;
    GV(u32, CDREQ + 0x18) = end - start;
    V0 = 1;
}

/* 80076B74: cancel the file request. */
void n_cd_CancelRead(void) {
    if (modmem_active()) modmem_stage_cancel();
    g_cdState = 0;
    V0 = 0x800B0000u;                         /* as left by the original */
}

/* 80076B80 (request): advance a file request, once a frame: 1 wait for
 * the drive, 2 find the file and start the read, 3 wait for it, 4 done
 * (state 0, returns 1). */
void n_cd_Update(void) {
    FRAME(0x18);
    u32 req = A0;
    switch (GV(s32, req)) {
    case 2: {
        s32 sector = (s32)GCALL1(F_CD_SEARCH, GV(u32, req + 4));
        u32 pos = GCALL2(F_CD_INT_TO_POS, (u32)(sector + GV(s32, req + 0x14) / 2048), 0x80062F44);
        GCALL3(F_CD_READ, GV(u32, req + 0x10), GV(s32, req + 0x18) / 2048, pos);
        GV(s32, req)++;
        V0 = 0;
        return;
    }
    case 1:
    case 3:
        if (GCALL1(F_CD_READ_SYNC, 0x40) != 0) { V0 = 0; return; }
        GV(s32, req)++;
        V0 = 0;
        return;
    case 4:
        GCALL1(F_CD_READ_END, 0);
        GV(s32, req) = 0;
        V0 = 1;
        return;
    default:
        V0 = 0;
        return;
    }
}

/* 80077044: an extra object record (boss parts, from enemy overlays):
 * template copy, model from stage file 0x71 on the heap; returns it. */
void n_loader_ExtraObject(void) {
    modmem_require_fresh();
    FRAME(0x28);
    u32 e = g_loadPtr;
    if (modmem_active()) {
        u32 bytes, parts[3];
        if (!modfs_model_request_size(STAGE_FILE_TBL,0x71,&bytes,parts)) rt_fatal("mods: missing extra-object size");
        modmem_require(e, (uint64_t)0x2310 + bytes, "extra object and model");
    }
    g_loadPtr = e + 0x2310;
    copy_record_polled(e, OBJ_TEMPLATE, 0x80077098);
    GV(u8, e + 0x314) = (u8)g_objectCount;
    GV(u32, e + 0x890) = g_loadPtr;
    GS(u32, sp + 0x10) = g_loadPtr;            /* argument 5: destination */
    u32 words = GCALL4(F_LOAD_FILE, 0x8006C230, STAGE_FILE_NAME, STAGE_FILE_TBL, 0x71);
    g_loadPtr += words * 4;
    GCALL3(F_MODEL_INIT, 0, GV(u32, e + 0x890), e);
    GV(u8, e + 0x29) = 1;
    GV(s32, e + 0x170) = 0x1000;
    GV(s32, e + 0x174) = 0x1000;
    GV(s32, e + 0x178) = 0x1000;
    GCALL2(F_OBJ_MATRIX, e + 0x180, e + 0x170);
    for (u32 i = 0; i < GV(u8, e + 0x28); ) {
        GV(u8, e + i) = 0;
        if (!(++i < GV(u8, e + 0x28))) break;
        LOOP_POLL(0x80077150);
    }
    for (s32 i = 0; i < GV(s32, e + 0x1D0); ) {
        GV(u32, GV(u32, e + 0x16C) + (u32)i * 0x50 + 0x4C) = e + 0x180;
        if (!(++i < GV(s32, e + 0x1D0))) break;
        LOOP_POLL(0x8007718C);
    }
    V0 = e;
}

/* 8007428C (the work area): the room's enemies, all at once (the loader
 * of a stage's first room; later rooms use the step machine above): the
 * object array carved off the memory before 0x800BBCD0 (0xF57 words,
 * cleared) and its template (0x800B42D0) taken from its first record;
 * the lists and the heap (0x80150000) reset; then for each placement not
 * flagged done: its model loaded (type 0x9A, 0x1F-0x21: animation packs
 * from the enemy files; 0x32+: an enemy program and its model; same type
 * as the last: its data copied; others: a model from the stage's file,
 * unpacked, its textures uploaded unless the last type shares them), a
 * model record (0x800B3F10, 0x1C bytes), the object record set up and,
 * for an enemy (type below 0x32), its program slot, its sounds and
 * effects. The file reads wait over frames: every call is gcall_ss with
 * the s registers and $fp as the original keeps them (s3 s6 s7
 * 0x800B0000, fp 0x800B3F10, s5 the placement, s1 its type, s0 the last
 * type, s2 and s4 by the step). */
void n_room_LoadAll(void) {
    FRAME(0x48);
    GS(u32, sp + 0x48) = A0;
    GS(u32, sp + 0x28) = R[18];
    GS(u32, sp + 0x44) = R[31];
    GS(u32, sp + 0x40) = R[30];
    GS(u32, sp + 0x3C) = R[23];
    GS(u32, sp + 0x38) = R[22];
    GS(u32, sp + 0x34) = R[21];
    GS(u32, sp + 0x30) = R[20];
    GS(u32, sp + 0x2C) = R[19];
    GS(u32, sp + 0x24) = R[17];
    GS(u32, sp + 0x20) = R[16];
    u32 in_s[8];
    for (int i = 0; i < 8; i++) in_s[i] = R[16 + i];
    const u32 in_fp = R[30];
#define HEAP GV(u32, 0x800B4D78u)
#define NMOD GV(u32, 0x800B4D7Cu)
#define MOD(i) (0x800B3F10u + (u32)(i) * 0x1C)
    GV(u32, 0x800AF56Cu) = 0xF57;
    {
        const u32 base = GV(u32, 0x800BBCD0u) - 0x3D5C;
        GV(u32, 0x800AB510u) = base;
        for (u32 i = 0;;) {
            GV(u32, base + i * 4) = 0;
            i++;
            if (!((s32)i < 0xF57)) break;
            LOOP_POLL(0x800742F4u);
        }
    }
    copy_record_polled(0x800B42D0u, GV(u32, 0x800AB510u), 0x80074334u);
    for (s32 i = 0xB;;) {
        GV(u32, 0x800A8850u + (u32)i * 4) = 0;
        i--;
        if (i < 0) break;
        LOOP_POLL(0x80074360u);
    }
    GV(u32, 0x800AB51Cu) = 0;
    for (s32 i = 4;;) {
        GV(u16, 0x800B3EA8u + (u32)i * 0xC) = 0xFFFF;
        i--;
        if (i < 0) break;
        LOOP_POLL(0x8007438Cu);
    }
    GV(u32, 0x800BB018u) = 0;
    GV(u16, 0x800BB026u) = 0;
    GV(u16, 0x800BB022u) = 0;
    GV(u32, 0x800B6090u) = 0xFFFFFFFFu;
    R[22] = 0x800B0000u;
    GV(u32, 0x800AF530u) = 0;
    R[19] = 0x800B0000u;
    GV(u32, 0x800BAA30u) = 0;
    HEAP = 0x80150000u;
    NMOD = 0;
    GV(u32, 0x800B4D80u) = 0;
    R[18] = 0;
    gcall_ss(0x800743E8u, 0x80073F88u, A0, A1, A2, A3);
    R[20] = 0;
    R[18] = 0;
    R[21] = GV(u32, 0x800BBB88u) + 4;
    R[17] = (u32)(s32)GV(s16, R[21]);
    R[16] = 0xFFFFFFFFu;
    if ((R[17] & 0xF000) != 0xF000) {
        R[30] = 0x800B3F10u;
        R[23] = R[22];
        for (;;) {
            const s32 kind = (s32)R[17];
            if (gcall_ss(0x80074430u, 0x8008F64Cu, 0x80062F60u, (u32)(s32)GV(s16, R[21] + 2), A2, A3) != 0) {
                R[21] += 0x18;
                goto next;
            }
            u32 v;                                   /* the size to add to the heap, in words */
            if (kind >= 0x32) {                      /* an enemy program and its model */
                R[16] = (u32)(kind - 0x32) * 3;
                if (kind == 0x9A) goto anim;
                GS(u32, sp + 0x10) = 0x800C3E50u;
                gcall_ss(0x800744ACu, 0x800115C0u, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, R[16] + 1);
                gcall_ss(0x800744B8u, 0x8006F370u, GS(u32, sp + 0x48), A1, A2, A3);
                R[20] = HEAP;
                GS(u32, sp + 0x10) = R[20];
                v = gcall_ss(0x800744E0u, 0x800115C0u, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, R[16]);
                {
                    const u32 a3 = R[16] + 2;
                    R[16] = HEAP + (v << 2);
                    HEAP = R[16];
                    GS(u32, sp + 0x10) = R[16];
                    v = gcall_ss(0x80074518u, 0x800115C0u, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, a3);
                }
                goto model;
            }
            if ((u32)(kind - 0x1F) < 3) {            /* an animation pack (and 0x9A's) */
            anim: {
                const u32 a3 = kind == 0x9A ? R[16] : (u32)kind + 0x171;
                {
                    const u32 m = MOD(NMOD);
                    GV(u32, m + 0x10) = HEAP;
                    GS(u32, sp + 0x10) = HEAP;
                }
                v = gcall_ss(0x80074568u, 0x800115C0u, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, a3);
                }
                {
                    u32 m = MOD(NMOD);
                    const u32 pk = GV(u32, m + 0x10);
                    HEAP = HEAP + (v << 2);
                    const u32 r = gcall_ss(0x80074598u, 0x8007C558u, pk, 3, A2, A3);
                    m = MOD(NMOD);
                    const u32 a0 = GV(u32, m + 0x10);
                    GV(u32, m) = (u32)(s32)GV(s16, R[21]);
                    const u32 a1 = a0 + (GV(u32, r + 4) << 2);
                    GV(u32, m + 8) = a0 + (GV(u32, a1 + 0xC) << 2);
                    GV(u32, m + 4) = 0;
                    GV(u32, m + 0xC) = a0 + (GV(u32, a1 + 0x10) << 2);
                    const u32 r2 = gcall_ss(0x800745FCu, 0x8007C558u, a0, 2, A2, A3);
                    const u32 n = NMOD;
                    m = MOD(n);
                    GV(u32, m + 0x14) = r2;
                    const u32 hp = HEAP, st = GV(u32, m + 0x10);
                    NMOD = n + 1;
                    GV(u32, m + 0x18) = hp - st;
                }
                goto record;
            }
            if ((s32)R[17] == (s32)R[16]) {          /* the same type again: copy */
                const u32 m = MOD(NMOD - 1);
                u32 a0 = GV(u32, m + 0x10);
                R[16] = a0 + ((GV(u32, m + 0x18) >> 2) << 2);
                for (;;) {
                    GV(u32, HEAP) = GV(u32, a0);
                    HEAP += 4;
                    a0 += 4;
                    if (!(a0 < R[16])) break;
                    LOOP_POLL(0x80074908u);
                }
                R[18]++;
                goto record;
            }
            {                                        /* a model from the stage's file */
                R[18] = 0x80108000u;
                GS(u32, sp + 0x10) = R[18];
                if (kind == 0x1E) gcall_ss(0x80074694u, 0x800115C0u, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, 0x196);
                else gcall_ss(0x80074694u, 0x800115C0u, 0x8006C230u, 0x80058530u, 0x800A0CD8u, (u32)kind);
                const u32 a0 = R[18] + GV(u32, R[18] + 8);
                const s32 last = (s32)R[16];
                int share = 0;                       /* textures shared with the last type: skip them */
                if (kind < 3 || kind == 8 || kind == 9) {
                    if (last != -1 && !((u32)(last - 3) < 5) && last < 0xA) share = 1;
                } else if ((u32)(kind - 4) < 3) {
                    if ((u32)(last - 4) < 3) share = 1;
                } else if ((u32)(kind - 0xD) < 2) {
                    if ((u32)(last - 0xD) < 2) share = 1;
                } else {
                    if (R[17] == 0x22) R[17] = 0x10;
                    if (R[17] == 0x23) R[17] = 0x10;
                }
                if (!share) {
                    R[20] = HEAP;
                    const u32 r = gcall_ss(0x80074754u, 0x80016F78u, a0, R[20], A2, A3);
                    HEAP = HEAP + ((r >> 2) << 2);
                }
                const s32 k = (s32)R[17];
                int keep = 0;                        /* the last type's textures still loaded */
                if (k < 7) {
                    if ((last == 0 && k == 4) || (last == 1 && k == 5) || (last == 2 && k == 6)
                        || (last == 4 && k == 0) || (last == 5 && k == 1) || (last == 6 && k == 2)) keep = 1;
                }
                if (!(k < 7) || !keep) {
                    gcall_ss(0x800747F0u, 0x80016F78u, R[18] + GV(u32, R[18] + 4), GS(u32, sp + 0x48), A2, A3);
                    gcall_ss(0x800747FCu, 0x8006F370u, GS(u32, sp + 0x48), A1, A2, A3);
                }
                R[16] = HEAP;
                v = gcall_ss(0x80074810u, 0x80016F78u, R[18] + GV(u32, R[18] + 0xC), R[16], A2, A3) >> 2;
            }
        model:
            {
                HEAP = HEAP + (v << 2);
                const u32 n = NMOD;
                const u32 m = MOD(n);
                GV(u32, m) = (u32)(s32)GV(s16, R[21]);
                GV(u32, m + 8) = R[20] + ((GV(u32, R[20] + 4) >> 2) << 2);
                NMOD = n + 1;
                GV(u32, m + 0xC) = R[20] + ((GV(u32, R[20] + 8) >> 2) << 2);
                GV(u32, m + 4) = R[16] + ((GV(u32, R[16] + 4) >> 2) << 2);
                const u32 hp = HEAP;
                R[18] = 0;
                GV(u32, m + 0x14) = 0;
                const u32 st = R[16] + ((GV(u32, R[16] + 8) >> 2) << 2);
                GV(u32, m + 0x10) = st;
                GV(u32, m + 0x18) = hp - st;
            }
        record:
            {
                R[16] = GV(u32, 0x800AB510u) + GV(u32, 0x800AF530u) * 0x8C4;
                copy_record_polled(R[16], 0x800B42D0u, 0x80074970u);
                R[22] = 0x800B0000u;
                const u32 o = R[16];
                GV(u8, o + 0x314) = GV(u8, 0x800AF530u);
                GV(u8, o + 0x315) = GV(u8, R[21]);
                const u32 n = NMOD;
                GV(u32, o + 0x88C) = 0x800B3EF4u + n * 0x1C;
                const u32 m = MOD(n - 1);
                GV(u32, o + 0x848) = R[21];
                GV(u32, o + 0x890) = GV(u32, m + 0x10) + (((GV(u32, m + 0x18) * R[18]) >> 2) << 2);
                if (GV(u8, o + 0x315) < 0x32) {
                    GS(u32, sp + 0x18) = R[17];
                    const u32 slot = gcall_ss(0x80074A18u, 0x8008DC94u, o, sp + 0x18, 0, A3);
                    const u32 rec = 0x800BB020u + slot * 4;
                    const u32 a1 = GS(u32, sp + 0x18);
                    const u16 a2 = GS(u16, sp + 0x18);
                    GV(u16, rec + 2) = 1;
                    GV(u16, rec) = a2;
                    GV(u16, o + 0x84E) = (u16)slot;
                    gcall_ss(0x80074A48u, 0x80080A28u, slot, a1, a2, A3);
                    gcall_ss(0x80074A58u, 0x80098100u, GS(u32, sp + 0x18), 0x800C3E50u, A2, A3);
                }
                R[21] += 0x18;
                GV(u32, 0x800AF530u) += 1;
                R[16] = R[17];
                R[17] = (u32)(s32)GV(s16, R[21]);
                if (R[17] == 0x22) R[17] = 0x10;
                if (R[17] == 0x23) R[17] = 0x10;
            }
        next:
            if ((GV(s16, R[21]) & 0xF000) == 0xF000) break;
            LOOP_POLL(0x80074A98u);
        }
    }
#undef HEAP
#undef NMOD
#undef MOD
    for (int i = 0; i < 8; i++) R[16 + i] = in_s[i];
    R[30] = in_fp;
}
