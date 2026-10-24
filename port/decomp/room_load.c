/* Room data: the room object pool and the background loader (TEST
 * overlay).
 *
 * Room objects (doors, props with models) live in a pool of 14 records of
 * 0x318 bytes at 0x800A89C0: +0x28 u8 part count (the first bytes are
 * per-part flags), +0x2A u8 kind, +0x2B u8 1 free / 2 used, +0x170 scale,
 * +0x180 matrix, +0x314 s32. A room's object file (loaded at 0x80108000)
 * is: s32 count, s32 offset of a table of s16 entries (bit 15: a model set
 * only, else an object whose kind is the low byte), then the offsets of
 * the model blocks.
 *
 * The background loader (state 0x800A0CC0, handlers at 0x800A5BD8: 0 idle,
 * 1 start, 2 wait) streams the next room's object file while the current
 * room plays. The file index comes from the stage (0x800B4BA0) and room
 * (0x800B3E20) being loaded, with a few rooms swapping files by game
 * progress (0x8008F64C flag checks).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "game_stack.h"

void n_room_ObjectFile(void);
void n_room_LoadObjects(void);
void n_room_SpawnObjects(void);
void n_room_FreeObjects(void);
void n_room_AllocObject(void);
void n_room_InitModels(void);
void n_room_NoObjectFile(void);
void n_room_StreamIdle(void);
void n_room_StreamStart(void);
void n_room_StreamWait(void);
void n_room_StreamStep(void);

#define OBJECTS         0x800A89C0u
#define OBJECT_SIZE     0x318
#define OBJECT_COUNT    14
#define OBJ_FILE        0x80108000u           /* where the object file goes */
#define g_loadRoom      GV(s32, 0x800B3E20)
#define g_loadStage     GV(s32, 0x800B4BA0)
#define g_altRoom       GV(s32, 0x800A8614)   /* secondary destination code */
#define g_fileDir       GV(u32, 0x800BD018)   /* the stage's file: directory, name, sector table */
#define g_fileName      GV(u32, 0x800BD01C)
#define g_fileSectors   GV(u32, 0x800BD020)
#define g_objFileEnd    GV(u32, 0x800B3D54)
#define g_streamState   GV(s32, 0x800A0CC0)
#define g_streamFailed  GV(s8, 0x800B3CC0)    /* no room to stream: load at the change */
#define F_LOAD_FILE     0x800115C0u
#define F_MODEL_INIT    0x8006F370u

/* 80092164: which object file the room being loaded uses: the room
 * number, or a variant in stage 2 (rooms 13/14 after flag 0x70, room 15
 * after flag 0x3C: +4) and stage 6 (14 unless a secondary code is set). */
void n_room_ObjectFile(void) {
    FRAME(0x18);
    u32 file = g_altRoom ? GV(u16, 0x800A8614) : GV(u16, 0x800B3E20);
    if (g_loadStage == 2) {
        if (GCALL2(F_AREA_CHECK, 0x80062FC0u, 0x70) && (u32)(g_loadRoom - 0xD) < 2) file += 4;
        if (GCALL2(F_AREA_CHECK, 0x80062FA0u, 0x3C) && g_loadRoom == 0xF) file += 4;
    }
    if (g_loadStage == 6 && g_altRoom == 0) file = 0xE;
    V0 = (u32)(s32)(s16)file;
}

/* 8009293C: rooms with no object file of their own: stage 5, stage 1
 * rooms 0-1, stage 6 with a secondary code. */
void n_room_NoObjectFile(void) {
    if (g_loadStage == 5 || (g_loadStage == 1 && g_loadRoom < 2)) { V0 = 1; return; }
    V0 = g_loadStage == 6 && g_altRoom != 0;
}

/* 80092248: load the room's object file now (when it couldn't be
 * streamed): pick the stage's file from the tables at 0x800A5B00/48/90. */
void n_room_LoadObjects(void) {
    FRAME(0x30);
    u32 k = (u32)g_loadStage * 4;
    g_fileDir = GV(u32, 0x800A5B00u + k);
    g_fileName = GV(u32, 0x800A5B48u + k);
    g_fileSectors = GV(u32, 0x800A5B90u + k);
    n_room_ObjectFile();
    s32 file = (s16)V0;
    n_room_NoObjectFile();
    if (!(V0 & 0xFFFF)) {
        GS(u32, sp + 0x10) = OBJ_FILE;
        GCALL4(F_LOAD_FILE, g_fileDir, g_fileName, g_fileSectors, (u32)file);
        u32 s = g_fileSectors + (u32)file * 4;
        g_objFileEnd = ((GV(u32, s + 4) - GV(u32, s)) << 11) + OBJ_FILE;
    }
}

/* 8009233C: create the room's objects from its object file. */
void n_room_SpawnObjects(void) {
    FRAME(0x38);
    if (g_streamFailed == 1) n_room_LoadObjects();
    s32 count = GV(s32, OBJ_FILE);
    u32 ent = GV(u32, OBJ_FILE + 4) + OBJ_FILE;
    u32 blk = OBJ_FILE;                                  /* +8: block offsets */
    for (s32 i = 0; i < count - 1; i++, blk += 4) {
        if (GV(s16, ent) & 0x8000) {                     /* models only */
            u32 b = OBJ_FILE + GV(u32, blk + 8);
            g_objFileEnd = b;
            GCALL2(0x800927D8u, b, (u32)-1);
            continue;
        }
        n_room_AllocObject();
        u32 o = OBJECTS + (u32)((s32)(s16)V0 * OBJECT_SIZE);
        for (u32 j = 0; j < OBJECT_SIZE; j += 4) GV(u32, o + j) = GV(u32, 0x800B3FB8u + j);
        GCALL3(0x8006E304u, 0, OBJ_FILE + GV(u32, blk + 8), o);
        GV(u32, o + 0x170) = 0x1000;
        GV(u32, o + 0x174) = 0x1000;
        GV(u32, o + 0x178) = 0x1000;
        GCALL2(F_OBJ_MATRIX, o + 0x180, o + 0x170);
        GV(u8, o + 0x2A) = GV(u8, ent);
        ent += 2;
        for (s32 j = 0; j < (s32)GV(u8, o + 0x28); j++) GV(u8, o + (u32)j) = 1;
    }
}

/* 8009250C: free every room object and reset the loader. */
void n_room_FreeObjects(void) {
    for (u32 i = 0; i < OBJECT_COUNT; i++) {
        u32 o = OBJECTS + i * OBJECT_SIZE;
        GV(u8, o + 0x2B) = 1;
        GV(s32, o + 0x314) = -1;
    }
    GV(u16, 0x800B3D3C) = 0;
    GV(u8, 0x800BBD10) = 0;
    GV(u8, 0x800B3CC0) = 0;
    GV(u16, 0x800B3CC2) = 0;
}

/* 8009256C: take a free room object; its index, or -1. */
void n_room_AllocObject(void) {
    for (s32 i = 0; i < OBJECT_COUNT; i++) {
        u32 o = OBJECTS + (u32)i * OBJECT_SIZE;
        if (GV(u8, o + 0x2B) & 1) {
            GV(u8, o + 0x2B) = 2;
            V0 = (u32)i;
            return;
        }
    }
    V0 = (u32)-1;
}

/* 800927D8 (block, index): set up the models of a model block (s32
 * count, then their offsets): all of them (-1), or the one given. */
void n_room_InitModels(void) {
    FRAME(0x28);
    u32 blk = A0;
    s32 k = (s16)A1;
    s32 n = GV(s32, blk);
    if (k == -1) {
        for (s32 i = 0; i < n; i++) GCALL1(F_MODEL_INIT, blk + GV(u32, blk + 4 + (u32)i * 4));
    } else if (!(n < k)) {
        GCALL1(F_MODEL_INIT, blk + GV(u32, blk + 4 + (u32)k * 4));
    }
}

/* 800929A4: loader idle. */
void n_room_StreamIdle(void) {}

/* 800929AC: loader start: if the next room's object file fits between
 * the current room's data (its size at 0x800BBCD8) and 0x80150000, start
 * reading it (0x80076B14, asynchronous) and wait; otherwise give up and
 * load it at the room change. */
void n_room_StreamStart(void) {
    FRAME(0x28);
    u32 k = (u32)g_loadStage * 4;
    g_fileName = GV(u32, 0x800A5B48u + k);
    g_fileSectors = GV(u32, 0x800A5B90u + k);
    n_room_ObjectFile();
    u32 file = V0;
    n_room_NoObjectFile();
    if (V0 & 0xFFFF) {
        g_streamState = 0;
    } else {
        s32 fi = (s16)file;
        u32 s = g_fileSectors + (u32)fi * 4;
        u32 end = GV(u32, 0x800BBCD8) + ((GV(u32, s + 4) - GV(u32, s)) << 11) + OBJ_FILE;
        if (0x80150000u < end) {
            g_streamState = 0;
            GV(u8, 0x800B3CC0) = 1;
        } else {
            if (GCALL4(0x80076B14u, g_fileName, g_fileSectors, (u32)fi, OBJ_FILE))
                g_objFileEnd = GV(u32, 0x800B3E58) + OBJ_FILE;
            g_streamState = 2;
        }
    }
}

/* 80092AD0: loader wait: done when the read (0x800B3E40) has finished. */
void n_room_StreamWait(void) {
    if (GV(u32, 0x800B3E40) == 0) g_streamState = 0;
}

/* 80092CB0: run the loader's current state. */
void n_room_StreamStep(void) {
    FRAME(0x18);
    u32 fn = GV(u32, 0x800A5BD8u + (u32)g_streamState * 4);
    V0 = fn;
    switch (fn) {                                         /* the usual handlers, directly */
    case 0x800929A4u: n_room_StreamIdle(); break;
    case 0x800929ACu: n_room_StreamStart(); break;
    case 0x80092AD0u: n_room_StreamWait(); break;
    default: jalr_call(fn); break;
    }
}
