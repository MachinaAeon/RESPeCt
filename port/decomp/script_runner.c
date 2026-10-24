/* Event scripts (TEST overlay): the threads that run a room's scripts,
 * starting them, and the room's event data.
 *
 * Ten script threads of 0x58 bytes at 0x800BB7E0: +0 the script pointer
 * (pc), +4 s16 the current instruction's state (opcodes that take more
 * than one frame count with it), +6 s16, +8 u16 flags (bit 0 running),
 * +0x1C an argument (the trigger that started it), +0x54 nonzero while
 * the script can be skipped. Every frame (0x8008D628, with the stack in
 * the scratchpad) each running thread executes instructions: an opcode
 * (s16 at pc) indexes the handler table at 0x800A2B8C; a handler gets the
 * thread and returns 0 (done for this frame: the next thread), 1 (wait:
 * the same instruction next frame) or 2 (go on with the next one).
 * Skipping (START, or fire / run with flag 0xB9 set) jumps a skippable
 * script to its skip label (opcode 0x5A, found by 0x80088350).
 *
 * A room's event file (0x8008DAD0) holds: zone tables (0x800B60C8), the
 * enemy list (0x800BBB78), the room objects (0x800AB52C), and the script
 * table (0x800BBB6C: script n at offset +4 + n * 4).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "game_stack.h"
#include "libgs.h"

void n_script_ResetThread(void);
void n_script_RunThreads(void);
void n_script_Init(void);
void n_script_Start(void);
void n_script_StartAt(void);
void n_script_StartPc(void);
void n_script_AllocThread(void);
void n_script_Frame(void);
void n_script_TriggerEnemy(void);
void n_script_TriggerFlag(void);
void n_script_TriggerZone(void);
void n_room_LoadEvents(void);

#define THREADS          0x800BB7E0u
#define THREAD_SIZE      0x58u
#define THREAD_COUNT     10
#define OPCODES          0x800A2B8Cu   /* handler per opcode */
#define g_scriptTable    GV(u32, 0x800BBB6C)
#define g_threadsStarted GV(s32, 0x800BBB58)
#define g_scriptsOff     GV(s32, 0x800BBB50)
#define g_scriptsInit    GV(s32, 0x800BBB68)   /* negative: (re)initialise */
#define g_eventLock      GV(s32, 0x800BBB60)   /* set: the player can't act */
#define FLAGS_ROOM       0x80062FA0u
#define FLAGS_GAME       0x80062FC0u
#define F_FLAG_TEST      0x8008F64Cu
#define F_FLAG_SET       0x8008F5F8u
#define F_HIMEM_ALLOC    0x8008F684u
#define F_SKIP_TO        0x80088350u   /* (pc, opcode): instructions to it */
#define F_OBJ_MATRIX     0x8006F69Cu
#define F_ENEMY_UNMARK   0x8007CEB8u
#define F_ROOM_ZONES     0x80079A18u
#define F_OVERLAY_INIT   0x8008C980u

static inline u32 thread_addr(s32 i) { return THREADS + (u32)i * THREAD_SIZE; }

static void reset_thread(u32 t) {
    GV(u32, t) = 0;
    GV(u16, t + 4) = 0;
    GV(u16, t + 8) = 0;
    GV(s16, t + 6) = -1;
}

/* 8008D024 (thread): an idle thread. */
void n_script_ResetThread(void) {
    reset_thread(A0);
    V0 = (u32)-1;
}

/* 8008D588 (first): a free thread from index first on, made running; its
 * index, or -1. */
void n_script_AllocThread(void) {
    FRAME(0x20);
    for (u32 n = A0;; n++) {
        if (n >= THREAD_COUNT) { V0 = (u32)-1; return; }
        u32 t = THREADS + n * THREAD_SIZE;
        if (GV(u16, t + 8) & 1) continue;
        A0 = t;
        n_script_ResetThread();
        GV(u16, t + 8) = 1;
        GV(u32, 0x800A8670) = 0;
        g_threadsStarted++;
        V0 = n;
        return;
    }
}

/* 8008D390 (first, script): start script number `script` of the room's
 * table in a free thread; 1, or 0 if none is free. */
void n_script_Start(void) {
    FRAME(0x18);
    u32 script = A1;
    n_script_AllocThread();
    s32 i = (s32)V0;
    if (i < 0) { V0 = 0; return; }
    u32 t = thread_addr(i);
    u32 base = g_scriptTable;
    GV(u32, t + 0x1C) = 0;
    s32 off = GV(s16, base + script * 4 + 4);
    off = ((off + (off < 0)) >> 1) << 1;          /* even, rounded toward zero */
    GV(u32, t) = base + (u32)off;
    V0 = 1;
}

/* 8008D51C (first, pc, arg): start the script at pc; 1, or 0. */
void n_script_StartAt(void) {
    FRAME(0x20);
    u32 pc = A1, arg = A2;
    n_script_AllocThread();
    s32 i = (s32)V0;
    if (i < 0) { V0 = 0; return; }
    u32 t = thread_addr(i);
    GV(u32, t) = pc;
    GV(u32, t + 0x1C) = arg;
    V0 = 1;
}

/* 8008D4BC (first, pc): start the script at pc (no argument); 1, or 0. */
void n_script_StartPc(void) {
    FRAME(0x18);
    u32 pc = A1;
    n_script_AllocThread();
    s32 i = (s32)V0;
    if (i < 0) { V0 = 0; return; }
    u32 t = thread_addr(i);
    GV(u32, t) = pc;
    GV(u32, t + 0x1C) = 0;
    V0 = 1;
}

/* 8008D2E4: (re)initialise the script system when asked (0x800BBB68 < 0). */
void n_script_Init(void) {
    FRAME(0x20);
    if (g_scriptsInit >= 0) return;
    g_threadsStarted = 0;
    g_scriptsOff = 0;
    for (int i = 0; i < THREAD_COUNT; i++) {
        A0 = thread_addr(i);
        n_script_ResetThread();
    }
    GV(u32, 0x800A0CB0) = 0;
    GV(u32, 0x800A0CB4) = 0;
    GCALL0(F_OVERLAY_INIT);
    for (int i = 15; i >= 0; i--) GV(s32, 0x800BB420 + i * 4) = -1;
    GV(u32, 0x800BB770) = 0;
    g_eventLock = 0;
}

/* Was the button pressed this frame? */
static inline int pressed(u32 b) { return (g_buttons & b) && !(g_buttonsPrev & b); }

/* 8008D03C: run the script threads for a frame. */
void n_script_RunThreads(void) {
    FRAME(0x28);
    GV(u32, 0x800BA9E0) = 0;
    if (g_eventLock) D_80062DEC = 0;
    int skip = 0;
    u32 pad = g_padTypeBit;
    if (pad == 0x200000) {                                 /* light gun */
        if (pressed(0x800) && pressed(0x40)) skip = 1;
        if (GCALL2(F_FLAG_TEST, FLAGS_GAME, 0xB9) && pressed(0x20)) skip = 1;
    } else if (pad == 0x400000 || pad == 0x80000) {
        if (pressed(0x800)) skip = 1;
        if (GCALL2(F_FLAG_TEST, FLAGS_GAME, 0xB9) && (pressed(0x20) || pressed(0x40))) skip = 1;
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        u32 t = thread_addr(i);
        if (!(GV(u16, t + 8) & 1) || !skip || !GV(u32, t + 0x54)) continue;
        u32 n = GCALL2(F_SKIP_TO, GV(u32, t), 0x5A);
        GV(u32, t + 0x54) = 0;
        GV(u16, t + 4) = 0;
        GV(u32, t) += n * 2;
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        u32 t = thread_addr(i);
        if (!(GV(u16, t + 8) & 1)) continue;
        for (;;) {
            u32 op = GV(u32, OPCODES + (u32)((s32)GV(s16, GV(u32, t)) * 4));
            A0 = t;
            s32 r = (s32)jalr_call(op);
            if (r != 0 && r != 2) break;                   /* wait */
            GV(u16, t + 4) = 0;
            if (r == 0) break;                             /* done for this frame */
        }
    }
    GCALL2(F_OBJ_MATRIX, 0x800BB4D0u, 0);
    GCALL2(F_OBJ_MATRIX, 0x800BB520u, 0);
}

/* 8008D628: once a frame, run the scripts (unless switched off) with the
 * stack in the scratchpad. */
void n_script_Frame(void) {
    FRAME(0x18);
    if (g_scriptsOff) return;
    u32 saved = GCALL0(F_GET_SP);
    GCALL1(F_SET_SP, SCRATCH_STACK);
    n_script_RunThreads();
    GCALL1(F_SET_SP, saved);
}

/* 8008D93C (trigger): an enemy trigger (entries of 0x800BBC18 with the
 * trigger's enemy id): start its script (0x800A3194) unless its room flag
 * is set. 1 if the enemy is in the room. */
void n_script_TriggerEnemy(void) {
    FRAME(0x20);
    u32 trig = A0;
    if (GCALL2(F_FLAG_TEST, FLAGS_ROOM, (u32)(s32)GV(s16, trig + 2))) { V0 = 0; return; }
    const u32 s = 0x800A3194u;
    for (s32 i = 0; i < 4; i++) {
        u32 e = 0x800BBC18u + (u32)i * 0x14;
        if (GV(s16, trig + 2) != GV(s16, e + 2)) continue;
        GV(u16, s + 0xE) = GV(u16, e);
        GV(u16, s + 0x12) = 0;
        GV(u16, s + 0x10) = GV(u16, e + 0xC);
        GV(u16, s + 0x14) = GV(u16, e + 2);
        GCALL1(F_ENEMY_UNMARK, (u32)(i + 1));
        A0 = 0; A1 = s; A2 = e;
        n_script_StartAt();
        if (V0) GV(u16, trig + 0x1E) = 0;
        V0 = 1;
        return;
    }
    V0 = 0;
}

/* 8008DA00 (trigger): start script +2 if the game flag +0x20 allows it
 * (bit 0x4000: run when it's set, else when it's clear; negative: always). */
void n_script_TriggerFlag(void) {
    FRAME(0x18);
    u32 trig = A0;
    s16 f = GV(s16, trig + 0x20);
    if (f >= 0) {
        if (f & 0x4000) {
            if (!GCALL2(F_FLAG_TEST, FLAGS_GAME, (u32)(f & 0x3FFF))) { V0 = 0; return; }
        } else {
            if (GCALL2(F_FLAG_TEST, FLAGS_GAME, (u32)(s32)f)) { V0 = 0; return; }
        }
    }
    A0 = 0; A1 = (u32)(s32)GV(s16, trig + 2);
    n_script_Start();
}

/* 8008DA74 (trigger): start the zone script (0x800A31AC) for it, unless
 * its game flag +0x22 (when positive) is clear. */
void n_script_TriggerZone(void) {
    FRAME(0x18);
    u32 trig = A0;
    s32 f = GV(s16, trig + 0x22);
    if (f > 0 && !GCALL2(F_FLAG_TEST, FLAGS_GAME, (u32)f)) { V0 = 0; return; }
    A0 = 0; A1 = 0x800A31ACu; A2 = trig;
    n_script_StartAt();
}

/* 8008DAD0: load the room's event file (stage 0x800B4BA0, room
 * 0x800B3E20; 30 rooms a stage) into high memory and point at its parts. */
void n_room_LoadEvents(void) {
    FRAME(0x28);
    const u32 tbl = 0x800A32F0u;
    u32 n = (u32)GV(s32, 0x800B4BA0) * 30 + GV(u32, 0x800B3E20);
    u32 size = (GV(u32, tbl + (n + 1) * 4) - GV(u32, tbl + n * 4)) << 11;
    u32 buf = GCALL1(F_HIMEM_ALLOC, size);
    GV(u32, 0x800BBB90) = buf;
    GS(u32, sp + 0x10) = buf;
    GCALL4(F_FILE_LOAD, 0x8006C678u, 0x80058550u, tbl, n);
    GV(u32, 0x800A883C) = buf + GV(u32, buf + 0x04);
    GV(u32, 0x800BBB88) = buf + GV(u32, buf + 0x08);
    GV(u32, 0x800BBB78) = buf + GV(u32, buf + 0x0C);
    GV(u32, 0x800AB52C) = buf + GV(u32, buf + 0x10);
    GV(u32, 0x800BBB7C) = buf + GV(u32, buf + 0x14);
    g_scriptTable = buf + GV(u32, buf + 0x18);
    GCALL0(F_ROOM_ZONES);
    /* zone tables: counts, then 16-byte and two 20-byte entry lists */
    u32 d = GV(u32, 0x800A883C);
    const u32 z = 0x800B60C8u;
    GV(u8, z) = GV(u8, d);
    GV(u8, z + 8) = (u8)GV(s16, d + 2);
    GV(u8, z + 0x10) = GV(u8, d + 4);
    s16 k = GV(s16, d + 6);
    u32 p = d + 8;
    GV(u32, z + 4) = p;
    p += (u32)GV(u8, z) * 16;
    GV(u32, z + 0xC) = p;
    p += (u32)GV(u8, z + 8) * 20;
    GV(u32, z + 0x14) = p;
    GV(u8, z + 0x18) = (u8)k;
    GV(u32, z + 0x1C) = p + (u32)GV(u8, z + 0x10) * 20;
    if (D_80062DF4 == 3) {
        GCALL2(F_FLAG_SET, 0x80062F60u, 0x74);
        GCALL2(F_FLAG_SET, 0x80062F60u, 0x75);
    }
}
