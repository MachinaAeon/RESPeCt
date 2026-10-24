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
