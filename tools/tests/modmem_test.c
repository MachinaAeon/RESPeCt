/* Asset-free production loader tests. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "../../port/decomp/room_loader.c"
#include "../../port/src/config.h"
Cpu g_cpu;
u8 g_ram[RAM_SIZE], g_scratch[SCRATCH_SIZE];
int g_native_depth, g_ram_ext_used;
int32_t g_rt_budget = 100000000;
uint32_t g_poll_pc;
Config g_cfg;
static jmp_buf fail;
static int fatal_expected;
static u32 prefix = 0x80200130u;
static int model_mods = 1;
static u32 fixture_packed = 2048;
static int metadata_raw_small;
u32 modfs_static_end(void) { return prefix; }
int modfs_dynamic_models(void) { return model_mods; }
int modfs_model_request_size(u32 table, int index, u32 *packed, u32 sizes[3]) {
    (void)table; (void)index;
    *packed = metadata_raw_small && table==ENEMY_FILE_TBL ? 2048 : fixture_packed; sizes[0]=65556; sizes[1]=296848; sizes[2]=58452; return 1;
}
void rt_log(const char *fmt, ...) { (void)fmt; }
void rt_fatal(const char *fmt, ...) { (void)fmt; if (fatal_expected) longjmp(fail,1); abort(); }
void rt_poll(void) { }
void n_lz_Unpack(void);
void n_heap_Top(void);
void n_heap_Alloc(void);
void n_enemy_PlaceAll(void);
static int decodes,original_sync_calls,sync_raw_fixture,sync_packed_fixture,sync_flag_fixture;
static u32 sync_order[16],sync_order_count;
static u8 *sync_archive;
static u32 sync_archive_bytes,sync_reads;
void n_loader_LoadSync(void);
int modmem_sync_busy(void);
void f_test_8007428C(void) { assert(!modmem_sync_busy());original_sync_calls++;V0=0x12345678; }

void rt_call(u32 addr) {
    if(addr==F_DECOMPRESS) { decodes++; n_lz_Unpack(); }
    else if(addr==F_FLAG_TEST && sync_flag_fixture) V0=A1==1;
    else if(addr==F_LOADER_RESET) { if(modmem_sync_busy()) assert(R[19]==0x800B0000u);V0=0; }
    else if(addr==0x800735A4u) n_heap_Top();
    else if(addr==0x800735B4u) n_heap_Alloc();
    else if(addr==F_MODEL_SECTION && sync_packed_fixture) V0=A0+(A1==3?16:80);
    else if(addr==F_LOAD_FILE) {
        if(sync_raw_fixture || sync_packed_fixture) {
            u32 dest=GV(u32,R[29]+0x10);sync_order[sync_order_count++]=A3;
            memset(GA(u8,dest),0,2048);GV(u32,dest)=2;GV(u32,dest+4)=12;GV(u32,dest+8)=16;GV(u32,dest+16)=0x77665544;
            if(sync_packed_fixture){GV(u32,dest+20)=8;GV(u32,dest+32+12)=16;GV(u32,dest+32+16)=17;}
            V0=512;
        }
        else if(sync_archive){assert(modmem_sync_busy());u32 dest=GV(u32,R[29]+0x10);memcpy(GA(u8,dest),sync_archive,sync_archive_bytes);sync_reads++;V0=sync_archive_bytes/4;}
        else V0=10; /* synchronous CD boundary, fixture already in staging */
    }
    else V0=0;
}
static void init(void) {
    memset(g_ram,0,sizeof g_ram); memset(&g_cpu,0,sizeof g_cpu);
    R[29]=0x801ffe00; g_rt_budget=100000000;
    GV(u32,0x800BBB88)=0x80040000; GV(s16,0x80040004)=-1;
    GV(u32,0x800BBB78)=0x80041000; GV(s16,0x80041000)=-1;
    GV(u32,0x800BBCD0)=0x801ffff0;
}
void n_heap_Alloc(void);
void n_shadow_Add(void);
#define EXPECT_FATAL(expr) do { fatal_expected=1; if(!setjmp(fail)) { expr; assert(!"expected capacity rejection before writing"); } fatal_expected=0; } while(0)
/* Literal-only LZ fixture, independent of the production decoder. */
static u32 literal(u32 dest,const u8 *data,u32 n) {
    GV(u32,dest)=n;u32 p=dest+4;
    for(u32 i=0;i<n;){GV(u8,p++)=255;for(u32 k=0;k<8&&i<n;k++)GV(u8,p++)=data[i++];}
    return (p-dest+3)&~3u;
}
static void split_staging_flow(u32 sectors,s32 type) {
    init();fixture_packed=sectors*2048;n_loader_Begin();
    u32 table=type==0x1E?ENEMY_FILE_TBL:STAGE_FILE_TBL,index=type==0x1E?0x196:0;
    g_setupType=type;g_setupPrevType=-1;GV(u32,table+index*4)=0;GV(u32,table+index*4+4)=sectors;
    n_loader_RequestModel();u32 packed=GV(u32,CDREQ+0x10),heap=g_loadPtr;
    assert(packed>=0x80200000u && heap==prefix);
    u8 *texture=malloc(65556);memset(texture,0x33,65556);texture[0]=0xff;texture[1]=texture[2]=texture[3]=0xff;
    u32 a[4]={2,12,16,0xABCD},b[5]={2,12,16,0x55,0x77665544};
    GV(u32,packed)=3;GV(u32,packed+4)=16;
    u32 off=16+literal(packed+16,texture,65556);GV(u32,packed+8)=off;
    off+=literal(packed+off,(u8*)a,sizeof a);GV(u32,packed+12)=off;
    literal(packed+off,(u8*)b,sizeof b);
    u32 marker=GV(u32,packed+off);g_cdState=0;n_loader_UnpackModel();
    assert(g_partA==heap && GV(u32,heap+12)==0xABCD);
    n_loader_Textures();assert(g_texEnd+65556<=packed && GV(u32,packed+off)==marker);
    assert(!memcmp(GA(u8,g_texEnd),texture,65556));
    n_loader_UploadTextures();n_loader_ModelRecord();
    assert(g_partB==heap+16 && GV(u32,g_partB+16)==0x77665544 && g_loadPtr==heap+36);
    assert(!modmem_stage_source() && GV(u32,model_addr(0)+0x18)==4);
    free(texture);fixture_packed=2048;
}
static void sync_budget_guards(void) {
    const char *selected=getenv("SYNC_BUDGET_CASE");int only=selected?atoi(selected):0;
    /* Exact LZ header metadata of the split fixture: no mocked smaller payload. */
    init();modmem_clear();fixture_packed=400u*2048;sync_archive_bytes=fixture_packed;sync_archive=calloc(1,sync_archive_bytes);
    u8 *large=malloc(296848);memset(large,0x33,296848);u32 header[3]={2,12,16};memcpy(large,header,12);
    u32 tmp=0x80200000;GV(u32,tmp)=3;GV(u32,tmp+4)=16;
    u32 off=16+literal(tmp+16,large,65556);GV(u32,tmp+8)=off;
    off+=literal(tmp+off,large,296848);GV(u32,tmp+12)=off;literal(tmp+off,large,58452);
    memcpy(sync_archive,GA(u8,tmp),sync_archive_bytes);memset(GA(u8,tmp),0,sync_archive_bytes);free(large);
    if(!only || only==1) {
    prefix=0x806A0000;GV(u32,0x800BBCD0)=0x801F8000;sync_reads=0;
    GV(s16,g_setupData+4)=10;GV(s16,g_setupData+0x1C)=0;GV(s16,g_setupData+0x34)=-1;
    A0=0x800C3E50;EXPECT_FATAL(n_loader_LoadSync());assert(sync_reads==0 && "sync preflight must include the second first-part before any file read");modmem_sync_end();
    }
    if(!only || only==2) {
    init();modmem_clear();prefix=0x806E0000;GV(u32,0x800BBCD0)=0x801F8000;sync_reads=0;sync_flag_fixture=1;metadata_raw_small=1;
    GV(s16,g_setupData+4)=0;GV(s16,g_setupData+6)=1;GV(s16,g_setupData+0x1C)=0x32;GV(s16,g_setupData+0x34)=-1;
    A0=0x800C3E50;EXPECT_FATAL(n_loader_LoadSync());assert(sync_reads==0 && "sync preflight must preserve cached type across flagged entry");modmem_sync_end();sync_flag_fixture=0;metadata_raw_small=0;
    }
    free(sync_archive);sync_archive=NULL;sync_archive_bytes=0;fixture_packed=2048;
    if(!only || only==3) {
    init();modmem_clear();prefix=0x807FE400;GV(u32,0x800BBCD0)=0x801F8000;sync_raw_fixture=1;sync_order_count=0;
    GV(s16,g_setupData+4)=0x32;GV(s16,g_setupData+0x1C)=0x32;GV(s16,g_setupData+0x34)=-1;
    A0=0x800C3E50;EXPECT_FATAL(n_loader_LoadSync());assert(sync_order_count==0 && "sync preflight must count every raw model reload");modmem_sync_end();sync_raw_fixture=0;
    }
    prefix=0x80200130;
    puts("PASS synchronous preflight accounts for first-part sharing, skipped cached types and repeated raw reads before files");
}
static void sync_staging_flow(void) {
    init();modmem_clear();GV(u32,0x800BBCD0)=0x801F8000;fixture_packed=400u*2048;
    sync_archive_bytes=fixture_packed;sync_archive=calloc(1,sync_archive_bytes);sync_reads=0;
    u32 tmp=0x80400000;u8 texture[16];memset(texture,0xff,sizeof texture);
    u32 a[4]={2,12,16,0xABCD},b[5]={2,12,16,0x55,0x77665544};
    GV(u32,tmp)=3;GV(u32,tmp+4)=16;
    u32 off=16+literal(tmp+16,texture,sizeof texture);GV(u32,tmp+8)=off;
    off+=literal(tmp+off,(u8*)a,sizeof a);GV(u32,tmp+12)=off;literal(tmp+off,(u8*)b,sizeof b);
    memcpy(sync_archive,GA(u8,tmp),128);memset(GA(u8,tmp),0,128);
    GV(s16,g_setupData+4)=0;GV(s16,g_setupData+0x1C)=0;GV(s16,g_setupData+0x34)=-1;
    A0=0x800C3E50;n_loader_LoadSync();
    assert(!modmem_sync_busy() && V0==0xF000 && R[19]==0);
    assert(sync_reads==1 && g_modelCount==1 && g_objectCount==2 && g_loadPtr==prefix+40);
    assert(g_objects==0x801F42A4 && GV(u32,model_addr(0)+0x10)==prefix+32 && GV(u32,model_addr(0)+0x18)==4);
    assert(GV(u32,obj_addr(0)+0x890)==prefix+32 && GV(u32,obj_addr(1)+0x890)==prefix+36);
    assert(GV(u32,prefix+32)==0x77665544 && GV(u32,prefix+36)==0x77665544 && !modmem_stage_source());
    assert(GV(u32,0x80010000)==0 && GV(u32,0x80108000)==0 && !original_sync_calls);
    /* Original sync sharing differs from async for type 9 and prev>=10. */
    const s16 pairs[][2]={{0,1},{0,9},{13,14},{4,5},{10,0}};
    for(u32 pair=0;pair<5;pair++) {
        init();modmem_clear();GV(u32,0x800BBCD0)=0x801F8000;sync_reads=0;
        GV(s16,g_setupData+4)=pairs[pair][0];GV(s16,g_setupData+0x1C)=pairs[pair][1];GV(s16,g_setupData+0x34)=-1;
        A0=0x800C3E50;n_loader_LoadSync();
        assert(sync_reads==2 && g_modelCount==2 && g_objectCount==2 && g_loadPtr==prefix+(pair==4?72:56));
        assert((GV(u32,model_addr(0)+8)==GV(u32,model_addr(1)+8))==(pair!=4));
    }
    init();modmem_clear();GV(u32,0x800BBCD0)=0x801F8000;sync_reads=0;
    for(u32 i=0;i<8;i++)GV(s16,g_setupData+4+i*0x18)=0;
    GV(s16,g_setupData+4+8*0x18)=-1;GV(u32,0x801F8000)=0xBADC0FFE;
    A0=0x800C3E50;EXPECT_FATAL(n_loader_LoadSync());
    assert(GV(u32,0x801F8000)==0xBADC0FFE && sync_reads==0);modmem_sync_end();
    free(sync_archive);sync_archive=NULL;sync_archive_bytes=0;fixture_packed=2048;
    init();modmem_clear();GV(u32,0x800BBCD0)=0x801F8000;sync_raw_fixture=1;sync_order_count=0;
    GV(s16,g_setupData+4)=0x32;GV(s16,g_setupData+0x1C)=0x32;GV(s16,g_setupData+0x34)=-1;
    A0=0x800C3E50;n_loader_LoadSync();
    assert(sync_order_count==6 && sync_order[0]==1 && sync_order[1]==0 && sync_order[2]==2 && sync_order[3]==1);
    assert(g_modelCount==2 && g_loadPtr==prefix+8192 && GV(u32,model_addr(0)+0x18)==2032);sync_raw_fixture=0;
    const s16 packed_types[]={0x1F,0x9A};
    for(u32 i=0;i<2;i++){
        init();modmem_clear();GV(u32,0x800BBCD0)=0x801F8000;sync_packed_fixture=1;sync_order_count=0;
        GV(s16,g_setupData+4)=packed_types[i];GV(s16,g_setupData+0x1C)=-1;
        A0=0x800C3E50;n_loader_LoadSync();
        assert(g_modelCount==1 && g_loadPtr==prefix+2048 && GV(u32,model_addr(0)+8)==prefix+64 && GV(u32,model_addr(0)+12)==prefix+68);
        assert(GV(u32,model_addr(0)+0x14)==prefix+80 && GV(u32,model_addr(0)+0x18)==2048);
        assert(sync_order_count==1 && sync_order[0]==(i?312:400));sync_packed_fixture=0;
    }
    puts("PASS synchronous sharing, seven-slot guard, raw read order and packed boss records");
    puts("PASS synchronous original model path uses disjoint staging, extended heap and contiguous instances");
}
int main(void) {
    init();
    _putenv_s("RT_MOD_ARENA","1");
    sync_staging_flow();sync_budget_guards();
    init();modmem_clear();_putenv_s("RT_MOD_ARENA","0");
    u8 *unchanged_ram=malloc(RAM_SIZE);memcpy(unchanged_ram,g_ram,RAM_SIZE);Cpu unchanged_cpu=g_cpu;
    n_loader_LoadSync();unchanged_cpu.r[2]=0x12345678;
    assert(original_sync_calls==1 && !memcmp(unchanged_ram,g_ram,RAM_SIZE) && !memcmp(&unchanged_cpu,&g_cpu,sizeof g_cpu));
    _putenv_s("RT_MOD_ARENA","1");model_mods=0;n_loader_LoadSync();
    assert(original_sync_calls==2 && !memcmp(unchanged_ram,g_ram,RAM_SIZE));
    free(unchanged_ram);model_mods=1;original_sync_calls=0;
    puts("PASS synchronous OFF and no-model paths delegate original without RAM/CPU edits");
    init();
    fixture_packed=94u*2048; n_loader_Begin();
    g_setupType=0; g_setupPrevType=-1;
    GV(u32,STAGE_FILE_TBL)=0; GV(u32,STAGE_FILE_TBL+4)=94;
    n_loader_RequestModel();
    assert(GV(u32,CDREQ+0x10)>=0x80200000u && "split compressed read needs separate extended ownership");
    puts("PASS split compressed request has extended ownership");
    split_staging_flow(94,0);split_staging_flow(400,0);split_staging_flow(400,0x1E);
    puts("PASS split parts remain disjoint through real LZ decode for OBJ and EV with 94/400 sectors");
    init();n_loader_Begin();u32 lease=modmem_stage_acquire(STAGE_FILE_TBL,0,2048);
    u32 scratch=modmem_scratch(4096,"lease test");assert(scratch+4096<=lease && g_loadPtr==prefix);
    A1=STAGE_FILE_TBL;A2=1;A3=lease;GV(u32,A1+4)=0;GV(u32,A1+8)=1;
    EXPECT_FATAL(n_cd_RequestRead());assert(g_cdState==0);
    modmem_stage_release();
    EXPECT_FATAL(modmem_stage_acquire(STAGE_FILE_TBL,0,6u*1024*1024));
    init();n_loader_Begin();lease=modmem_stage_acquire(STAGE_FILE_TBL,0,2048);
    g_cdState=3;GV(s32,0x80062F18)=1;n_cd_CancelRead();
    EXPECT_FATAL(modmem_stage_source());EXPECT_FATAL(modmem_stage_acquire(STAGE_FILE_TBL,0,2048));
    EXPECT_FATAL(n_loader_Begin());GV(s32,0x80062F18)=0;
    GV(u32,0x80062D74)=0x80050000;GV(u16,0x80062EFC)=2;
    GV(u16,0x80062F02)=GV(u16,0x80062EFE)=0;
    for(u32 i=0;i<3;i++){GV(u16,0x80050000+i*16)=2;GV(u32,0x80050004+i*16)=1;GV(u32,0x8005000C+i*16)=lease;}
    EXPECT_FATAL(modmem_stage_acquire(STAGE_FILE_TBL,0,2048));EXPECT_FATAL(n_loader_Begin());
    GV(u32,0x80062D74)=0;GV(u16,0x80062EFC)=0;
    assert(modmem_stage_acquire(STAGE_FILE_TBL,0,2048)==lease);
    modmem_stage_release();n_loader_Begin();assert(!modmem_stage_source());
    puts("PASS staging read identity, exhaustion, cancellation and begin lifecycle before writes");

    fixture_packed=2048;
    init();
    n_loader_Begin();
    assert(g_loadPtr == prefix && "opt-in model mod must use arena after static prefix");
    puts("PASS loader dynamic begin");
    /* The original heap-end is below the extended arena. */
    A0=16; n_heap_Alloc();
    assert(V0==0 && g_loadPtr==prefix+64 && "heap allocation uses arena limit");
    puts("PASS heap limit");
    A0=UINT32_MAX; u32 unchanged=g_loadPtr; n_heap_Alloc();
    assert(V0==UINT32_MAX && g_loadPtr==unchanged);
    puts("PASS heap overflow preserves cursor");
    /* Actual shadow path must reject before writing the object or RAM. */
    g_loadPtr=0x807ffff0u; A0=0x80045000u;
    GV(s16,A0+0x392)=-1; GV(u32,A0+0x8BC)=0x12345678;
    EXPECT_FATAL(n_shadow_Add());
    assert(GV(u32,0x80045000u+0x8BC)==0x12345678);
    puts("PASS shadow capacity before write");
    /* Real loader copies stay contiguous, shared pointer fields stay shared. */
    init(); n_loader_Begin();
    g_objects=0x801e0000; g_modelCount=1; g_loadPtr=prefix+16;
    u32 rec=model_addr(0); GV(u32,rec+0x10)=prefix; GV(u32,rec+0x18)=16;
    GV(u32,rec+8)=prefix+0x100;
    for(u32 i=0;i<4;i++) GV(u32,prefix+4*i)=0x11110000+i;
    n_loader_CopyModelWork(); n_loader_EnemyRecord();
    assert(g_loadPtr==prefix+32 && g_instance==1);
    assert(!memcmp(GA(u8,prefix),GA(u8,prefix+16),16));
    assert(GV(u32,g_objects+0x890)==prefix+16 && GV(u32,rec+8)==prefix+0x100);
    puts("PASS contiguous copies and shared model pointers");
    for(u32 i=2;i<=4;i++) { g_modelCount=(s32)i; g_loadPtr=prefix+i*32; n_loader_EnemyRecord(); }
    g_objectCount=4; GV(s16,g_setupData+0x1C)=-1;
    n_loader_Reload();
    assert(g_loadPtr==prefix+32 && g_modelCount==1);
    assert(GV(u32,prefix)==0x11110000);
    puts("PASS actual retained-model reload checkpoint");
    /* An oversized model is rejected before invoking the production decoder. */
    init(); n_loader_Begin(); g_setupType=0; g_setupPrevType=-1;
    GV(u32,TEX_BUF+8)=16; GV(u32,TEX_BUF+16)=UINT32_MAX;
    int was_decodes=decodes;
    EXPECT_FATAL(n_loader_UnpackModel()); assert(decodes==was_decodes);
    puts("PASS decode capacity before decoder writes");
    /* Literal-only synthetic LZ pickup, decoded by n_lz_Unpack itself. */
    init(); n_loader_Begin(); g_objects=0x801e0000;
    g_setupEntry=0x80041000; GV(s16,g_setupEntry)=0x5B;
    GV(s32,g_fileList+4)=2; g_listIndex=0; g_cdState=0; g_cdSize=40;
    u8 payload[16]={0}; u32 a=12,b=16;
    memcpy(payload+4,&a,4); memcpy(payload+8,&b,4); memset(payload+12,0xA5,4);
    GV(u32,WORK_BUF)=16;
    u32 out=WORK_BUF+4;
    for(u32 k=0;k<16;k+=8) { GV(u8,out++)=255; for(u32 j=0;j<8;j++) GV(u8,out++)=payload[k+j]; }
    n_loader_ItemFileDone();
    assert(g_loadPtr==prefix+4 && GV(u32,prefix)==0xA5A5A5A5u);
    assert(GV(u32,PICKUPS+8)==prefix && g_instance==1);
    puts("PASS actual pickup decode/copy with original LZ decoder");
    init(); n_loader_Begin(); g_objects=0x801e0000;
    GV(s16,g_itemList)=0x5B; GV(s16,g_itemList+0x18)=-1;
    u32 later=0x800C3E50u; GV(u32,later)=16; out=later+4;
    for(u32 k=0;k<16;k+=8) { GV(u8,out++)=255; for(u32 j=0;j<8;j++) GV(u8,out++)=payload[k+j]; }
    n_enemy_PlaceAll();
    assert(g_loadPtr==prefix+4 && GV(u32,prefix)==0xA5A5A5A5u && GV(u32,PICKUPS+8)==prefix);
    puts("PASS actual later-pickup loader uses arena and original LZ decoder");
    g_loadPtr=0x807fffe0u;
    EXPECT_FATAL(n_loader_ExtraObject()); assert(g_loadPtr==0x807fffe0u);
    puts("PASS extra object preflight before record writes");
    init(); n_loader_Begin();
    A1=STAGE_FILE_TBL; A2=0; A3=prefix; GV(u32,A1)=0; GV(u32,A1+4)=4000;
    EXPECT_FATAL(n_cd_RequestRead()); assert(g_cdState==0);
    puts("PASS async CD model capacity before request publication");
    /* Whole-room rejection happens before the original loader mutates tables. */
    init(); prefix=0x807fff00; GV(s16,g_setupData+4)=0; GV(s16,g_setupData+0x1C)=-1;
    g_loadPtr=0x12345678;
    EXPECT_FATAL(n_loader_Begin()); assert(g_loadPtr==0x12345678);
    prefix=0x80200130;
    puts("PASS room preflight before writes");
    init(); _putenv_s("RT_MOD_ARENA","0"); n_loader_Begin();
    assert(g_loadPtr==HEAP_START); u8 *baseline=malloc(RAM_SIZE); memcpy(baseline,g_ram,RAM_SIZE);
    Cpu cpu_before=g_cpu; int budget_before=g_rt_budget;
    init(); model_mods=0; _putenv_s("RT_MOD_ARENA","1"); n_loader_Begin();
    assert(!memcmp(baseline,g_ram,RAM_SIZE) && !memcmp(&cpu_before,&g_cpu,sizeof g_cpu) && budget_before==g_rt_budget);
    free(baseline);
    puts("PASS no-model-mod RAM/register/poll-budget parity");
    model_mods=1; modmem_clear();
    EXPECT_FATAL(n_loader_RequestModel());
    puts("PASS original-state model load waits for fresh room ownership");
    init();
    for(u32 i=0;i<20;i++) GV(s16,g_setupData+4+i*0x18)=0;
    GV(s16,g_setupData+4+20*0x18)=-1;
    fatal_expected=1;
    if(setjmp(fail)) assert(!"shared model payloads must not be budgeted 20 times");
    n_loader_Begin(); fatal_expected=0;
    assert(g_loadPtr==prefix);
    puts("PASS room preflight counts shared payloads once");
    init(); n_loader_Begin(); g_modelCount=MODMEM_MODELS; g_setupType=0x32; g_setupPrevType=-1;
    EXPECT_FATAL(n_loader_RequestModel()); assert(g_cdState==0);
    puts("PASS original model-record table capacity before read");
    return 0;
}
