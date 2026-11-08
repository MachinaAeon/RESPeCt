/* Game flow (TEST overlay): starting TEST (its state, the attract demo's
 * choice of stage), entering a room, sampling the controller every frame
 * (live or from the demo recording), the work heap, the room scripts'
 * branches, loading a stage's textures, the hit pause, rumble.
 *
 * Game types (0x80062DF4): 0 new game, 1 continuing (restore the
 * checkpoint), 2 a loaded game, 3 the attract demo (stage picked at
 * random: 0x800B3D1C = 0, 8 or 9, its input from a recording).
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "libgs.h"
#include "poll.h"
#include "modmem.h"

void n_test_InitState(void);
void n_room_Start(void);
void n_demo_Start(void);
void n_input_SampleFrame(void);
void n_heap_Top(void);
void n_heap_Alloc(void);
void n_stage_LoadTextures(void);
void n_roomscript_Branch(void);
void n_shot_HitPause(void);
void n_pad_Rumble(void);
void n_test_DisplayReset(void);
void n_test_Main(void);

#define GAME_TYPE      GV(u8, 0x80062DF4)
#define GAME_DEMO      3
#define DEMO_STAGE     GV(s32, 0x800B3D1C)    /* 0, 8 or 9 */
#define START_STAGE    GV(s32, 0x80062DB8)
#define START_ROOM     GV(s32, 0x80062D9C)
#define STAGE          GV(s32, 0x800B4BA0)
#define ROOM           GV(s32, 0x800B3E20)
#define PREV_ROOM      GV(s32, 0x800B3F08)
#define HEAP_TOP       GV(u32, 0x800B4D78)
#define HEAP_END       GV(u32, 0x800BBCD0)

#define g_buttonsHi    GV(u32, 0x800B4B98)
#define g_inputAxesW   GV(u32, 0x800A8840)
#define g_inputAxesPrev GV(u32, 0x800B3DF4)
#define D_800B3CE0     GV(u32, 0x800B3CE0)   /* last frame's g_buttonsHi */
#define INPUT_MODE     GV(s32, 0x800B3CE4)   /* 0 live, 1 demo playback, else none */
#define g_demoInput    GV(u32, 0x800AF574)
#define g_demoFrame    GV(s32, 0x800B3D08)
#define g_demoLength   GV(s32, 0x800B3CC8)
#define g_demoRealButtons GV(u32, 0x800B3E04)

/* 8006DE48: TEST starts: the default start (stage 7 room 4, or the demo's
 * pick), and every per-game variable reset. */
void n_test_InitState(void) {
    FRAME(0x18);
    GV(s32, 0x800A866C) = -1;                  /* came from: no stage */
    START_STAGE = 7;
    START_ROOM = 4;
    if (GAME_TYPE == GAME_DEMO) {
        GV(u32, 0x80062DF8) = 0;
        GCALL0(0x8007F028u);
        u32 r = GCALL0(F_RAND) & 0xFF;
        DEMO_STAGE = r < 0x55 ? 9 : r < 0xA9 ? 8 : 0;
        if (GAME_TYPE == GAME_DEMO) {
            switch (DEMO_STAGE) {
            case 0: START_STAGE = 0; START_ROOM = 2; break;
            case 8: START_STAGE = 8; START_ROOM = 1; break;
            case 9: START_STAGE = 9; START_ROOM = GAME_TYPE; break;
            }
        }
    }
    GV(u32, 0x800A8614) = 0;
    GV(u32, 0x800B3D44) = 0;
    GV(s32, 0x800B3D4C) = -1;
    GV(u32, 0x800B3CB8) = 0;
    D_800A8610 = 1;
    ENEMY_COUNT = 0;
    GV(u32, 0x800BAA30) = 0;
    BUF_INDEX = 0;
    g_buttons = 0;
    g_buttonsHi = 0;
    GV(u32, 0x80060D3C) = 0x578;
    GV(u16, 0x80060D40) = 0x1E;
    GV(u16, 0x80060D42) = 0x1E;
    PREV_ROOM = 0;
    GV(u16, 0x800B3D2C) = 0;                   /* input lock */
    GV(s16, 0x800B3D34) = -1;
    STAGE = START_STAGE;
    ROOM = START_ROOM;
    if (GAME_TYPE == GAME_DEMO) {
        switch (DEMO_STAGE) {
        case 0: GV(u16, 0x800B3D34) = 6; break;
        case 8: GV(u16, 0x800B3D34) = 4; break;
        case 9: GV(u16, 0x800B3D34) = 7; break;
        }
    }
    GV(u8, 0x800A55A0) = 0;
    GV(u16, 0x800A55A2) = 0;
    GV(u32, 0x800A8670) = 0;
    GV(u32, 0x800A85B8) = 0;
    /* the player-side object template */
    copy_words(0x800B3FB8u, 0x800A89C0u, 0x318 / 4);
    GV(u8, 0x800B3FB8 + 0x2B) = 2;
    GV(s32, 0x800B3FB8 + 0x314) = -1;
    GV(s16, 0x800BB024) = -1;
    GV(s16, 0x800BB020) = -1;
    GV(u16, 0x800BB026) = 0;
    GV(u16, 0x800BB022) = 0;
    GV(s32, 0x800BBB68) = -1;
    GV(u16, 0x800B3CC2) = 0;                   /* no fog */
    GV(u32, 0x800AF564) = 0xFA0;
    GV(u32, 0x800A8848) = 0x4E20;
    GV(u8, 0x800B3D0C) = 0;
    GV(u8, 0x800B3D0D) = 0;
    GV(u8, 0x800B3D0E) = 0;
    GV(u8, 0x800B3D0F) = 0;
    GV(u16, 0x800AF56A) = 0;
    GV(u16, 0x800B4D6C) = 0;
    GV(u16, 0x800B4D6A) = 0;
    GV(u16, 0x800B4D68) = 0;
    GV(u16, 0x800B3D9C) = 0;
    GV(u16, 0x800B4B9C) = 0;
    GV(u8, 0x800B4BE0) = 0;
    D_800AFCB0 = 0;
    GV(u32, 0x800AB51C) = 0;
    for (int i = 11; i >= 0; i--) GV(u32, 0x800A8850u + (u32)i * 4) = 0;
    for (u32 i = 0; i < 0x94; i += 4) GV(u32, 0x80062F60u + i) = 0;   /* story flags */
    g_buttons = 0;
    g_buttonsPrev = 0;
    g_buttonsHi = 0;
    D_800B3CE0 = 0;
    GCALL1(0x8003DF84u, 4);
    for (u32 i = 0; i < 5; i++) GV(u32, 0x80063448u + i * 4) = 0;    /* this stage's record */
    g_fireCooldown = 0;
    V0 = 0x80060000u;
}

/* 8006E294: a room starts: remember the previous one and load it. */
void n_room_Start(void) {
    FRAME(0x18);
    D_800A8610 = 1;
    u32 prev = PREV_ROOM, room = ROOM;
    u32 kind = GV(u32, 0x800A89B0);
    GV(u32, 0x800B3CB8) = prev;
    PREV_ROOM = room;
    if (kind) GCALL0(0x80090DC4u);
    else GCALL0(0x8009233Cu);
    GCALL2(0x80076784u, 0, 0);
}

/* 800713F4: the attract demo: load its recording (files 0x1B-0x1D) into
 * the heap and start playing it back. */
void n_demo_Start(void) {
    FRAME(0x20);
    if (GAME_TYPE == GAME_DEMO) {
        GCALL0(0x800735A4u);                       /* heap_Top */
        u32 dest = V0;
        g_demoInput = dest;
        u32 words = R[16];                         /* (no recording: the caller's s0) */
        u32 file = 0;
        switch (DEMO_STAGE) {
        case 0: file = 0x1B; break;
        case 8: file = 0x1C; break;
        case 9: file = 0x1D; break;
        }
        if (file) {
            GS(u32, sp + 0x10) = dest;
            words = GCALL4(F_FILE_LOAD, 0x8006C130u, 0x80058558u, 0x800A261Cu, file);
        }
        GCALL1(0x800735B4u, words);                /* heap_Alloc */
        INPUT_MODE = 1;
        u32 p = g_demoInput;
        u32 len = GV(u32, p);
        g_demoInput = p + 4;
        g_demoFrame = 0;
        g_demoLength = (s32)len;
    }
}

/* 80071584 (input_SampleFrame): this frame's buttons and stick: from
 * the pad, or in the demo from the recording (8 bytes a frame), ending
 * the demo when it runs out (0x80062DFC = 5) or Start is pressed (1). */
void n_input_SampleFrame(void) {
    FRAME(0x20);
    s32 mode = INPUT_MODE;
    if (mode == 0) {
        g_buttonsPrev = g_buttons;
        D_800B3CE0 = g_buttonsHi;
        u32 raw = GCALL1(0x80013A5Cu, 0);          /* pad_GetButtons */
        u32 v = GCALL1(0x8001189Cu, raw);          /* input_MapButtons */
        g_buttons = (s32)(v & 0xFFFF);
        g_buttonsHi = v >> 16;
        g_inputAxesPrev = g_inputAxesW;
        g_inputAxesW = GCALL1(F_PAD_GET_AXES, 0);
    } else if (mode == 1) {
        if (g_inputLocked != 0) {
            g_buttonsHi = 0;
            g_buttons = 0;
        } else {
            GV(u32, 0x800AF578) = g_demoRealButtons;
            u32 raw = GCALL1(0x80013A5Cu, 0);
            u32 v = GCALL1(0x8001189Cu, raw);
            g_demoRealButtons = v & 0xFFFF;
            g_buttonsPrev = g_buttons;
            u32 axes = g_inputAxesW;
            s32 f = g_demoFrame;
            u32 rec = g_demoInput + (u32)f * 8;
            g_inputAxesPrev = axes;
            g_demoFrame = f + 1;
            g_buttons = GV(s32, rec);
            g_inputAxesW = GV(u32, rec + 4);
            int end = 0;
            if (f + 1 >= g_demoLength) {
                GV(u8, 0x80062DFC) = 5;              /* ran out */
                end = 1;
            } else if (GCALL1(0x80013B90u, 0) != 0x200000 && (g_demoRealButtons & 0x800)) {
                GV(u8, 0x80062DFC) = 1;              /* Start pressed */
                end = 1;
            }
            if (end) {
                GCALL1(F_DRAW_SYNC, 0);
                GCALL0(0x80072F74u);
                INPUT_MODE = 0;
                GCALL1(F_SET_DISP_MASK, 0);
                GCALL1(0x8001306Cu, 1);
            }
        }
    }
}

/* 800735A4: the heap's next free address. */
void n_heap_Top(void) { V0 = HEAP_TOP; }

/* 800735B4 (words): take words * 4 bytes of the heap; -1 if that runs
 * past its end (0x800BBCD0), else 0. */
void n_heap_Alloc(void) {
    if (modmem_active()) {
        u32 next;
        if (!modmem_heap_take(HEAP_TOP, (uint64_t)A0 * 4, &next)) { V0 = 0xFFFFFFFFu; return; }
        HEAP_TOP = next;
        V0 = 0;
        return;
    }
    u32 top = HEAP_TOP + (A0 << 2);
    HEAP_TOP = top;
    V0 = top < HEAP_END ? 0 : 0xFFFFFFFFu;
}

/* 800735F4: load the textures of the stage's model list (0x800BBB88:
 * 0x18-byte entries after a 4-byte header, ended by 0xF000): each TIM
 * into the packet area and up to VRAM. Ids 0x22/0x23 use 0x10's;
 * a repeated id isn't loaded again. */
void n_stage_LoadTextures(void) {
    FRAME(0x38);
    u32 ent = GV(u32, 0x800BBB88) + 4;
    s32 id = GV(s16, ent);
    s32 prev = -1;
    while ((id & 0xF000) != 0xF000) {
        if (id == 0x22) id = 0x10;
        if (id == 0x23) id = 0x10;
        if (id >= 0x32) {
            GS(u32, sp + 0x10) = PACKET_AREA;
            GCALL4(F_FILE_LOAD, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, (u32)((id - 0x32) * 3 + 1));
        } else if (id != prev) {
            GS(u32, sp + 0x10) = PACKET_AREA;
            if (id == 0x1E)
                GCALL4(F_FILE_LOAD, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, 0x197);
            else if (id < 0x1E || id >= 0x22)
                GCALL4(F_FILE_LOAD, 0x8006C230u, 0x80058530u, 0x800A0CD8u, (u32)(id + 0x32));
            else
                GCALL4(F_FILE_LOAD, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, (u32)(id + 0x174));
        }
        GCALL1(0x8006F370u, PACKET_AREA);         /* tim_Load */
        ent += 0x18;
        prev = id;
        id = GV(s16, ent);
    }
    GCALL0(0x8008E9C0u);
}

/* Room script blocks (at 0x800BBB88): a header (u16 flags: bits 8-9 the
 * number of variants - 1, 0x400 "go on to the next block", 0x800
 * "random", 0xC00 "by stored choice"; byte 2 a story flag or choice
 * slot) and 0x18-byte entries ended by an 0xF000 entry. Move p to the
 * end entry of the block it is in. */
static u32 skip_block(u32 p) {
    if ((GV(s16, p) & 0xF000) == 0xF000) return p;
    do p += 0x18; while ((GV(s16, p) & 0xF000) != 0xF000);
    return p;
}

/* 80073F88: pick the variant of the current room script block: by a
 * stored 2-bit choice (0x800B4D88), by a story flag, or at random with
 * odds by difficulty (0x80062DF8; tables 0x800A0FEC.. in percent). */
void n_roomscript_Branch(void) {
    FRAME(0x30);
    u32 odds = R[19];                          /* (for 4 variants: the caller's s3) */
    for (;;) {
        u32 blk = GV(u32, 0x800BBB88);
        u16 hdr = GV(u16, blk);
        s32 nvar = ((hdr & 0x300) >> 8) + 1 & 3;
        u32 p = blk + 4;
        if ((hdr & 0xC00) == 0xC00) {
            if (nvar == 0) break;
            s32 v = GV(s16, blk + 2) & 0xFF00;
            s32 slot = v >> 8;
            u32 sh = (u32)(v >> 7) & 0xF;
            u32 w = GV(u32, 0x800B4D88u + (u32)((slot >> 4) << 2));
            nvar = (s32)(w & (3u << sh)) >> sh;
            if (nvar == 0) break;
            for (s32 i = 0; i < nvar; i++) p = skip_block(p);
            GV(u32, 0x800BBB88) = p;
            break;
        }
        if (!(hdr & 0x800)) {
            if (GCALL2(F_AREA_CHECK, 0x80062FC0u, GV(u8, blk + 2))) {
                for (s32 i = 0; i < nvar; i++) p = skip_block(p);
                GV(u32, 0x800BBB88) = p;
            } else if (GV(u16, blk) & 0x400) {
                break;
            }
        }
        if (GV(u16, blk) & 0x400) continue;
        u32 r = GCALL0(F_RANDOM_BYTE) & 0xFF;
        if (GV(u32, 0x80062DF8) == 0) {
            if (nvar == 1) odds = 0x800A1004u;
            else if (nvar == 2) odds = 0x800A0FECu;
        } else {
            if (nvar == 1) odds = 0x800A100Cu;
            else if (nvar == 2) odds = 0x800A0FF8u;
        }
        s32 k = 0;
        for (; k < nvar; k++, odds += 4) {
            s32 pct = GV(s32, odds) * 51;
            if (!(pct / 20 < (s32)r)) break;           /* percent of 255 */
        }
        if (k == 0) break;
        for (; k > 0; k--) {
            p = skip_block(p);
            p += 4;
        }
        GV(u32, 0x800BBB88) = p - 4;
        break;
    }
}

/* 8006F5D8: when a shot first hits: wait two frames, then reset the
 * display position (clamped to the VRAM). */
void n_shot_HitPause(void) {
    FRAME(0x38);
    GCALL1(F_VSYNC, 2);
    u32 env = sp + 0x18;
    GS(u32, sp + 0x10) = 0xF0;
    GCALL4(F_SET_DEF_DISP_ENV, env, 0, 0, 0x140);
    s16 x = GS(s16, env), y = GS(s16, env + 2);
    if (x < -0x400) x = -0x400;
    else if (x >= 0x2C0) x = 0x2BF;
    if (y < -0x200) y = -0x200;
    else if (y >= 0x110) y = 0x10F;
    GS(s16, env) = x;
    GS(s16, env + 2) = y;
    GCALL1(F_VSYNC, 0);
    GCALL1(F_PUT_DISP_ENV, env);
}

/* 800731BC (left, right, left time, right time): rumble the two
 * DualShock motors (not in the demo). */
void n_pad_Rumble(void) {
    u32 left = A0, right = A1, tl = A2, tr = A3;
    FRAME(0x20);
    V0 = 3;
    if (GAME_TYPE != GAME_DEMO) {
        if (left & 0xFF) GCALL4(0x800135A8u, 0, 0, tl & 0xFFFF, left & 0xFF);
        if (right & 0xFF) GCALL4(0x800135A8u, 0, 1, tr & 0xFFFF, right & 0xFF);
    }
}

/* 80072F74: TEST's display reset (the demo ending, a stage change): two
 * draw areas and display set-ups (0x800A84D0/E0, 0x800A8470/50), then
 * twelve times three frames of black (in the demo, the music faded out
 * over them by 16 a step while it plays, then stopped); the display off
 * and the frame callbacks set back to TEST's (0x8006F1E4, 0x8006F1C0).
 * Its frame waits can make save states (gcall_ss: the s registers as the
 * original has them). */
void n_test_DisplayReset(void) {
    FRAME(0x40);
    u32 in_s[8];
    for (int i = 0; i < 8; i++) { in_s[i] = R[16 + i]; GS(u32, sp + 0x18 + 4 * i) = R[16 + i]; }
    GS(u32, sp + 0x38) = R[31];
    if (GAME_TYPE == GAME_DEMO) {
        if (gcall_ra(0x80072FB4u, 0x80019268u, 0x10000, A1, A2, A3) != 0) {
            const s32 v = (s32)GV(s16, 0x80062DB6u) * 127;
            R[21] = (u32)((s32)(((s64)v * 0x51EB851FLL) >> 37) - (v >> 31));
        } else R[21] = 0;
    }
    R[18] = 0; R[23] = 0x100; R[22] = 0x7B; R[20] = 0x100;
    R[17] = 0x800A84E0u; R[19] = 0x800A84D0u;
    for (;;) {
        R[16] = R[18] << 8;
        const u32 v = gcall_ra(0x80073024u, 0x80038188u, 2, 2, R[16], 0);
        gcall_ra(0x80073038u, 0x80038388u, R[19], 1, 0, v & 0xFFFF);
        gcall_ra(0x80073040u, 0x80038328u, R[17], A1, A2, A3);
        const u32 t = R[17];
        GV(u16, t + 0x10) = (u16)R[20];
        R[20] -= 0x80;
        GV(u16, t + 8) = (u16)R[16];
        GV(u16, t + 0xA) = 0;
        GV(u8, t + 0xC) = 0;
        GV(u8, t + 0xD) = 0;
        GV(u16, t + 0x12) = (u16)R[23];
        GV(u8, t + 4) = 0x7B;
        GV(u8, t + 5) = 0x7B;
        GV(u8, t + 6) = 0x7B;
        R[17] += 0x14;
        R[18]++;
        R[19] += 8;
        if (!((s32)R[18] < 2)) break;
        LOOP_POLL(0x80073074u);
    }
    R[17] = 0x800A8470u;
    R[18] = 0xF0;
    GS(u32, sp + 0x10) = 0xF0;
    gcall_ra(0x800730A0u, 0x80038088u, R[17], 0, 0, 0x140);
    R[16] = 0x800A8450u;
    GS(u32, sp + 0x10) = 0xF0;
    gcall_ra(0x800730C0u, 0x80038148u, R[16], 0, 0, 0x140);
    gcall_ss(0x800730C8u, 0x800255F8u, 0, A1, A2, A3);
    gcall_ra(0x800730D0u, 0x80035BA4u, R[16], A1, A2, A3);
    gcall_ra(0x800730D8u, 0x800359D8u, R[17], A1, A2, A3);
    R[18] = 0;
    R[17] = 0x800A84D0u;
    R[16] = 0x800A84E0u;
    for (;;) {
        if (GAME_TYPE == GAME_DEMO && R[21] != 0) {
            const u32 a = (u32)(s32)(s16)R[21];
            gcall_ra(0x80073118u, 0x800206B4u, 0x10, a, a, A3);
            R[21] -= 0x10;
            if ((s32)R[21] <= 0) {
                R[21] = 0;
                gcall_ra(0x80073130u, 0x8001F6A4u, 0x10, A1, A2, A3);
            }
        }
        gcall_ss(0x80073138u, 0x800255F8u, 3, A1, A2, A3);
        gcall_ra(0x80073140u, 0x8003590Cu, R[17], A1, A2, A3);
        gcall_ra(0x80073148u, 0x8003590Cu, R[16], A1, A2, A3);
        gcall_ra(0x80073150u, 0x8003590Cu, R[17] + 8, A1, A2, A3);
        gcall_ra(0x80073158u, 0x8003590Cu, R[16] + 0x14, A1, A2, A3);
        R[18]++;
        if (!((s32)R[18] < 12)) break;
        LOOP_POLL(0x80073160u);
    }
    gcall_ss(0x80073170u, 0x800255F8u, 0, A1, A2, A3);
    gcall_ra(0x80073178u, 0x800352DCu, 0, A1, A2, A3);
    gcall_ra(0x80073184u, 0x80011558u, 0x8006F1E4u, A1, A2, A3);
    gcall_ra(0x80073190u, 0x80011564u, 0x8006F1C0u, A1, A2, A3);
    for (int i = 0; i < 8; i++) R[16 + i] = in_s[i];
}

/* 8006CAA0: TEST's thread (it never returns). Set up the game (TEST's
 * state, the checkpoint, the HUD, sounds), then for each stage: load it
 * (events, models, the stage, its sounds and textures) and for each room:
 * enter it (the room's set-up), then every frame: the status screen
 * (0x800B3D20: CONPAU_E's thread, by a mode switch) or the file screen
 * (0x800B3D38: the map/file overlay at 0x80010050 + 0xEA60) when asked,
 * the frame's work (the player, the enemies, the scripts, the camera,
 * the effects, the HUD), the room change test, the drawing, the
 * controller and a vertical blank (mode switch), until the room (0x800B3E18)
 * or the stage (0x800BB19C >= 6) changes.
 *
 * It runs the whole game in the mode's thread: every call is gcall_ss
 * with the s registers and $fp as the original keeps them (s1 0x80010000,
 * s2 1, s3 s5 fp 0x80060000, s4 s6 0x800B0000, s7 0x80058558, s0 by the
 * place, t0 0x800B0000 at the loop's top) and its loops poll with
 * SAVE_POLL, so save states are made in it as in the translation. */
#define TM(ra, f) gcall_ss((ra), (f), A0, A1, A2, A3)
void n_test_Main(void) {
    FRAME(0x40);
    GS(u32, sp + 0x3C) = R[31];
    GS(u32, sp + 0x38) = R[30];
    for (int i = 7; i >= 0; i--) GS(u32, sp + 0x18 + 4 * i) = R[16 + i];
    TM(0x8006CAD0u, 0x8006F4B0u);
    TM(0x8006CAD8u, 0x8006DE48u);
    TM(0x8006CAE0u, 0x8006DD5Cu);
    GV(u32, 0x800B608Cu) = GV(u32, 0x80062DF8u) == 1 ? 8 : 4;
    TM(0x8006CB08u, 0x80071CF4u);
    g_buttons = 0;
    TM(0x8006CB14u, 0x8008F1F0u);
    gcall_ss(0x8006CB20u, 0x8008F3BCu, 0, 0x78, A2, A3);
    gcall_ss(0x8006CB30u, 0x8007CC74u, 0x800C3E50u, 0, A2, A3);
    TM(0x8006CB38u, 0x80092DD0u);
    TM(0x8006CB40u, 0x8009493Cu);
    TM(0x8006CB48u, 0x80098AECu);
    gcall_ss(0x8006CB54u, 0x8008F3BCu, 1, 0x3C, A2, A3);
    gcall_ss(0x8006CB5Cu, 0x80011994u, 1, A1, A2, A3);
    gcall_ss(0x8006CB68u, 0x80011D24u, 0, 0, A2, A3);
    TM(0x8006CB70u, 0x8008F0E0u);
    gcall_ss(0x8006CB7Cu, 0x8008F3BCu, 2, 0x78, A2, A3);
    for (;;) {                                                /* a stage */
        R[22] = 0x800B0000u;
        TM(0x8006CB84u, 0x8008E324u);
        R[30] = 0x80060000u;
        TM(0x8006CB8Cu, 0x8008F6A8u);
        R[18] = 1;
        TM(0x8006CB94u, 0x8008DAD0u);
        TM(0x8006CB9Cu, 0x80077F8Cu);
        gcall_ss(0x8006CBA8u, 0x8007428Cu, 0x800C3E50u, A1, A2, A3);
        TM(0x8006CBB0u, 0x800713F4u);
        TM(0x8006CBB8u, 0x80092248u);
        gcall_ss(0x8006CBC4u, 0x8008F3BCu, 3, 0xB4, A2, A3);
        TM(0x8006CBCCu, 0x80072DC8u);
        TM(0x8006CBD4u, 0x8006F490u);
        gcall_ss(0x8006CBE0u, 0x80090B90u, 0, 8, A2, A3);
        R[21] = 0x80060000u;
        GV(u8, 0x80062DE5u) = 0;
        GV(u32, 0x800B3D50u) = 0;
        g_reticleX = 0xA0;
        g_reticleY = 0x78;
        GV(u8, 0x80062DECu) = 1;
        GV(u32, 0x80062E10u) = 0;
        for (;;) {                                            /* a room */
            GV(u32, 0x800A8954u) = 0;
            TM(0x8006CC2Cu, 0x8007CD04u);
            R[16] = 0x80060000u;
            TM(0x8006CC34u, 0x8008D2E4u);
            TM(0x8006CC3Cu, 0x80082E04u);
            TM(0x8006CC44u, 0x8006E294u);
            TM(0x8006CC4Cu, 0x8006DB40u);
            TM(0x8006CC54u, 0x8006F914u);
            TM(0x8006CC5Cu, 0x80073758u);
            TM(0x8006CC64u, 0x8009453Cu);
            TM(0x8006CC6Cu, 0x80082F3Cu);
            TM(0x8006CC74u, 0x8006DC24u);
            if (GV(u8, 0x80062DE5u) == 0) {
                TM(0x8006CC8Cu, 0x8008E4DCu);
                GV(u8, 0x80062DE5u) = 1;
            }
            gcall_ss(0x8006CC9Cu, 0x800352DCu, 0, A1, A2, A3);
            gcall_ss(0x8006CCB0u, 0x8006F40Cu, 0, 0, 0x140, 0x200);
            gcall_ss(0x8006CCB8u, 0x800255F8u, 0, A1, A2, A3);
            GV(u32, 0x800B3E18u) = 0;
            GV(u8, 0x80062E1Cu) = 2;
            gcall_ss(0x8006CCD0u, 0x800352DCu, 1, A1, A2, A3);
            if (GV(u32, 0x800B3E18u) == 0) {
                R[17] = 0x80010000u; R[19] = 0x80060000u; R[20] = 0x800B0000u; R[23] = 0x80058558u;
                R[8] = 0x800B0000u;
                for (;;) {                                    /* a frame */
                    if (GV(u32, 0x800B3D20u) != 0) {          /* the status screen */
                        gcall_ss(0x8006CD0Cu, 0x80035374u, 0, A1, A2, A3);
                        TM(0x8006CD14u, 0x8006D068u);
                        GV(u32, 0x80062DF0u) = 1;
                        GV(u8, 0x80062DECu) = 0;
                        gcall_ss(0x8006CD24u, 0x8001306Cu, 1, A1, A2, A3);
                        gcall_ss(0x8006CD40u, 0x800116A4u, 0x8006C11Cu, 0x8005851Cu, 0x13, GV(u32, 0x80010050u));
                        gcall_ss(0x8006CD54u, 0x800805F4u, 0, 2, 0x7F, 0x7F);
                        gcall_ss(0x8006CD60u, 0x80011FF8u, (u32)(s32)GV(s16, 0x80062E78u), 0x30, A2, A3);
                        gcall_ss(0x8006CD68u, 0x800808F4u, 0x30, A1, A2, A3);
                        gcall_ss(0x8006CD78u, 0x8001304Cu, 1, 0x800CB7A4u, A2, A3);
                        gcall_ss(0x8006CD80u, 0x8001306Cu, 1, A1, A2, A3);
                        TM(0x8006CD88u, 0x800735F4u);
                        gcall_ss(0x8006CD94u, 0x80011FF8u, (u32)(s32)GV(s16, 0x80062E78u), 0x7F, A2, A3);
                        gcall_ss(0x8006CD9Cu, 0x800808F4u, 0x7F, A1, A2, A3);
                        R[8] = 0x800B0000u;
                        GV(u32, 0x800A8838u) = 4;
                        GV(u8, 0x80062DECu) = 1;
                        GV(u32, 0x800B3D20u) = 0;
                        GV(u32, 0x800B3D38u) = 0;
                        g_playerFrozen = 0;
                    }
                    if (GV(u32, 0x800B3D38u) != 0) {          /* the file screen */
                        gcall_ss(0x8006CDD8u, 0x80035374u, 0, A1, A2, A3);
                        R[16] = 0xEA60;
                        TM(0x8006CDE0u, 0x8006D068u);
                        GV(u32, 0x80062DF0u) = 1;
                        GV(u8, 0x80062DECu) = 0;
                        gcall_ss(0x8006CDF0u, 0x8001306Cu, 1, A1, A2, A3);
                        GS(u32, sp + 0x10) = GV(u32, 0x80010050u) + 0xEA60;
                        gcall_ss(0x8006CE1Cu, 0x800115C0u, 0x8006C130u, 0x80058558u, 0x800A261Cu, 0x14);
                        gcall_ss(0x8006CE28u, 0x80016F78u, GV(u32, 0x80010050u) + 0xEA60, GV(u32, 0x80010050u), A2, A3);
                        gcall_ss(0x8006CE34u, 0x80011FF8u, (u32)(s32)GV(s16, 0x80062E78u), 0x30, A2, A3);
                        gcall_ss(0x8006CE3Cu, 0x800808F4u, 0x30, A1, A2, A3);
                        gcall_ss(0x8006CE4Cu, 0x8001304Cu, 1, 0x800C3F58u, A2, A3);
                        gcall_ss(0x8006CE54u, 0x8001306Cu, 1, A1, A2, A3);
                        TM(0x8006CE5Cu, 0x800735F4u);
                        gcall_ss(0x8006CE68u, 0x80011FF8u, (u32)(s32)GV(s16, 0x80062E78u), 0x7F, A2, A3);
                        gcall_ss(0x8006CE70u, 0x800808F4u, 0x7F, A1, A2, A3);
                        const u32 k = GV(u32, 0x800A866Cu);
                        GV(u8, 0x80062DECu) = 1;
                        GV(u32, 0x800B3D38u) = 0;
                        if (k == 2 && GV(u32, 0x800B3F08u) == 0xE) {
                            gcall_ss(0x8006CEB4u, 0x800116A4u, 0x8006C148u, 0x80058558u + 0x1C, 0x11, GV(u32, 0x80010050u));
                            gcall_ss(0x8006CEC0u, 0x800927D8u, GV(u32, 0x80010050u), 0xFFFFFFFFu, A2, A3);
                        }
                        g_playerFrozen = 0;
                        GV(u8, 0x800B3DF7u) = GV(u8, 0x800A8843u);
                        GV(u8, 0x800B3DF6u) = GV(u8, 0x800A8842u);
                    }
                    TM(0x8006CEF0u, 0x8009016Cu);
                    if (GV(u32, 0x800A8610u) != 0) TM(0x8006CF0Cu, 0x80078580u);
                    TM(0x8006CF14u, 0x800705D8u);
                    TM(0x8006CF1Cu, 0x8009358Cu);
                    TM(0x8006CF24u, 0x8008D628u);
                    TM(0x8006CF2Cu, 0x800800A0u);
                    TM(0x8006CF34u, 0x8008EB18u);
                    TM(0x8006CF3Cu, 0x80082F88u);
                    g_fireRequest = 0;
                    gcall_ss(0x8006CF50u, 0x80076B80u, 0x800B3E40u, A1, A2, A3);
                    TM(0x8006CF58u, 0x8006D068u);
                    TM(0x8006CF60u, 0x80070FA0u);
                    if (g_inputLocked == 0) TM(0x8006CF7Cu, 0x8007F16Cu);
                    if (TM(0x8006CF84u, 0x8008480Cu) != 0) break;
                    TM(0x8006CF94u, 0x80085F10u);
                    if (GV(u32, 0x800BA9F4u) != 0) TM(0x8006CFB0u, 0x8007D7FCu);
                    else gcall_ss(0x8006CFC4u, 0x8007E064u, GV(u8, 0x80062D1Cu), A1, A2, A3);
                    if (GV(u32, 0x800B3E18u) == 0) TM(0x8006CFDCu, 0x8006D4D0u);
                    TM(0x8006CFE4u, 0x8007EF50u);
                    gcall_ss(0x8006CFECu, 0x80035374u, 0, A1, A2, A3);
                    GV(u32, 0x80062DF0u) = 1;
                    TM(0x8006CFF4u, 0x80071584u);
                    GV(u32, 0x800C3A94u) = 0;
                    GV(u32, 0x800C3A90u) += 1;
                    {
                        const u32 c = GV(u32, 0x80063448u + 0xC);
                        if (c != 0xFFFFFFFFu) GV(u32, 0x80063448u + 0xC) = c + 1;
                    }
                    gcall_ss(0x8006D030u, 0x8001306Cu, 1, A1, A2, A3);
                    R[8] = 0x800B0000u;
                    if (GV(u32, 0x800B3E18u) != 0) break;
                    SAVE_POLL(0x8006D038u);
                }
            }
            gcall_ss(0x8006D048u, 0x800352DCu, 0, A1, A2, A3);
            if (!(GV(s32, 0x800BB19Cu) < 6)) break;
            R[2] = 0x800A0000u;                       /* the loop's top stores through it */
            SAVE_POLL(0x8006D058u);
        }
        GV(u32, 0x800BB19Cu) = 0;
        SAVE_POLL(0x8006D060u);
    }
}
