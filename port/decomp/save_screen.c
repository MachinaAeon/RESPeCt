/* The save screen (TITLE overlay, entered after dying or at a payphone)
 * and the memory card code it and LOAD GAME use.
 *
 * Its steps (save_Frame calls one per frame by SV_STEP, through the
 * table at 0x80080528): ask whether to save, wait for a card, check it
 * (format, free blocks, our saves), choose a slot (15 shown five at a
 * time), confirm overwriting, write; messages on the way. Each step's
 * own counters: SV_SUB (its phase), SV_WAIT.
 *
 * Card access goes through the BIOS: events for the card's I/O end,
 * error, timeout and new card (8 events, both software and hardware),
 * _card_info / _card_clear / _card_load, the file functions. Busy waits
 * poll like the originals' loops, so the card's interrupts come at the
 * same moments.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "title.h"

#define SV_STEP        GV(s32, 0x80082534)
#define SV_SUB         GV(s32, 0x80082538)
#define SV_WAIT        GV(s32, 0x8008253C)
#define SV_PORT        GV(s32, 0x80082558)   /* the card chosen (0, 1) */
#define SV_CARD        0x80082550u           /* per card: 2 present, else its _card_info result */
#define SV_RETRY       0x80082548u           /* per card: same result how many times */
#define SV_SLOT        GV(s32, 0x80082560)   /* the cursor, 0..15 (15: back) */
#define SV_TOP         GV(s32, 0x80082564)   /* first slot shown */
#define SV_SAVE_SLOT   GV(s32, 0x80082568)
#define SV_HOLD        GV(s32, 0x8008256C)   /* key repeat */
#define SV_RATE        GV(s32, 0x80082570)
#define SV_FLAGS       GV(u32, 0x80082574)   /* 1 the card, 0x80000000 checking */
#define SV_MSG         GV(u32, 0x8008257C)
#define SV_FREE        GV(s32, 0x80082580)
#define SV_OURS        GV(s32, 0x80082584)
#define SV_SLOTS       0x80082590u           /* per slot: 0 free, 1 can't, 2+ used (file number + 2), 0xFE other */
#define SV_INFO        0x800825B0u           /* per slot: u16 +0 clears, +0x1E %, +0x3C time; u8 +0x5A flag */
#define SV_HELD        GV(u32, 0x800825A0)
#define SV_OVERWRITE   GV(u32, 0x800825A4)
#define SV_SPRITES     GV(u32, 0x80082620)
#define SV_STEP_FNS    0x80080528u
#define CARD_DEV       0x80080520u           /* "bu00:", "bu10:" */
#define DIRENT         0x80084628u           /* directory entries, 0x28 bytes */
#define FILE_NAME      0x8006C230u           /* "BASLUS-01087..." */
#define FILE_BUF       0x80082628u           /* the file: header 0x200, data 0x1E00 */

#define EV_SW          0x80082510u           /* 4 software card events: ioe, error, timeout, new card */
#define EV_HW          0x80082520u           /* 4 hardware ones */
#define EV_TEST        0x80082530u

#define F_OPEN_EVENT    0x80039948u
#define F_CLOSE_EVENT   0x80039958u
#define F_TEST_EVENT    0x80039978u
#define F_ENABLE_EVENT  0x80039988u
#define F_ENTER_CRIT    0x80039A08u
#define F_EXIT_CRIT     0x80039A18u
#define F_CARD_INFO     0x80038F98u
#define F_CARD_LOAD     0x80038FA8u
#define F_CARD_WAIT     0x80038FC8u
#define F_CARD_CLEAR    0x80038FD8u
#define F_STRCAT        0x80039828u
#define F_STRNCMP       0x80039838u
#define F_STRCPY        0x80039848u
#define F_OPEN          0x80039A38u
#define F_LSEEK         0x80039A48u
#define F_WRITE         0x80039A68u
#define F_CLOSE         0x80039A78u
#define F_NEXT_FILE     0x80039A88u
#define F_FIRST_FILE    0x80039C58u
#define F_CARD_PRESENT  0x8007E594u
#define F_SLOT_INFO     0x8007B178u   /* (port, file number, out) */
#define F_CLEARS        0x8007EB10u
#define F_SV_TEXT       0x8007D70Cu
#define F_SV_PRESSED    0x8007D590u   /* (pad, button) */
#define F_SV_SLOTS_USED 0x8007EE20u
#define F_SV_WRITE_FILE 0x8007DE6Cu
#define F_SV_CARD_STATE 0x8007AE08u

/* 8007AC54: open and enable the eight card events. */
void n_card_OpenEvents(void) {
    FRAME(0x30);
    static const u32 spec[4] = {4, 0x8000, 0x100, 0x2000};
    GCALL0(F_ENTER_CRIT);
    for (int i = 0; i < 4; i++) GV(u32, EV_SW + i * 4) = GCALL4(F_OPEN_EVENT, 0xF4000001u, spec[i], 0x2000, 0);
    for (int i = 0; i < 4; i++) GV(u32, EV_HW + i * 4) = GCALL4(F_OPEN_EVENT, 0xF0000011u, spec[i], 0x2000, 0);
    for (int i = 0; i < 8; i++) GCALL1(F_ENABLE_EVENT, GV(u32, EV_SW + i * 4));
    GCALL0(F_EXIT_CRIT);
}

/* Wait for one of four events (base: EV_SW or EV_HW): which (0..3);
 * `pc` the loop branch (taken while none came), `back` the branches
 * taken to the loop's end (one per event 0..2: the original jumps back
 * up to it). */
static u32 card_wait(u32 base, u32 pc, const u32 back[3]) {
    for (;;) {
        for (u32 k = 0; k < 3; k++) {
            if (GCALL1(F_TEST_EVENT, GV(u32, base + k * 4)) == 1) {
                LOOP_POLL(back[k]);
                return k;
            }
        }
        if (GCALL1(F_TEST_EVENT, GV(u32, base + 12)) == 1) return 3;
        LOOP_POLL(pc);
    }
}

static void test_events(u32 base) {
    for (u32 k = 0; k < 4; k++) GCALL1(F_TEST_EVENT, GV(u32, base + k * 4));
}

static void close_events(void) {
    for (u32 k = 0; k < 8; k++) GCALL1(F_CLOSE_EVENT, GV(u32, EV_SW + k * 4));
}

/* 8007AE08: the state of the card in `port`: 0 ready, 1 error, 2 none,
 * 3 new (unformatted after clearing), -1 couldn't ask. */
void n_card_State(void) {
    FRAME(0x28);
    u32 chan = A0 << 4;
    GCALL0(0x8007AC54u);
    test_events(EV_SW);
    if (!GCALL1(F_CARD_INFO, chan)) {
        close_events();
        V0 = (u32)-1;
        return;
    }
    static const u32 back1[3] = {0x8007B0DC, 0x8007B0D4, 0x8007B0CC};
    static const u32 back2[3] = {0x8007B0C4, 0x8007B0BC, 0x8007B0B4};
    static const u32 back3[3] = {0x8007B0AC, 0x8007B0A4, 0x8007B09C};
    u32 r1 = card_wait(EV_SW, 0x8007AEC0, back1);
    if (r1 == 3) {                                    /* a new card: clear its flag */
        test_events(EV_HW);
        GCALL1(F_CARD_CLEAR, chan);
        r1 = card_wait(EV_HW, 0x8007AF64, back2);
    }
    test_events(EV_SW);
    GCALL1(F_CARD_LOAD, chan);
    u32 r2 = card_wait(EV_SW, 0x8007AFFC, back3);
    close_events();
    V0 = r1 ? r1 : r2;
}

/* Count the files of the card whose path is at `path` (the directory
 * entries go to DIRENT). The original's loop branch is at `pc`. */
static s32 count_files(u32 path, u32 pc) {
    u32 d = DIRENT;
    s32 n = 0;
    if (GCALL2(F_FIRST_FILE, path, d) == d) {
        n = 1;
        for (;;) {
            d += 0x28;
            u32 r = GCALL1(F_NEXT_FILE, d);
            n++;
            if (r != d) break;
            LOOP_POLL(pc);
        }
        n--;
    }
    return (s16)n;
}

/* 8007B340: is there a save of ours on either card (for LOAD GAME)? */
void n_load_FindSave(void) {
    FRAME(0xB8);
    for (u32 port = 0; port < 2; port++) {
        if (!(u16)GCALL1(F_SV_CARD_STATE, port)) {
            GCALL2(F_STRCPY, sp + 0x10, GV(u32, CARD_DEV + port * 4));
            GCALL2(F_STRCAT, sp + 0x10, 0x8006C22Cu);
            s32 n = count_files(sp + 0x10, 0x8007B3D8);
            if (n > 0) {
                u32 d = DIRENT;
                for (s32 i = 0; i < n; i++, d += 0x28) {
                    if (GCALL3(F_STRNCMP, FILE_NAME, d, 0xC) == 0) { V0 = 1; return; }
                    if (i + 1 < n) LOOP_POLL(0x8007B424);
                }
            }
        }
        if (port + 1 < 2) LOOP_POLL(0x8007B434);
    }
    V0 = 0;
}

/* 8007B46C: the save screen's frame: while not checking a card, watch
 * the chosen card (three misses in a row: it was taken out, start over
 * with a message); then run the step. */
void n_save_Frame(void) {
    FRAME(0x20);
    s32 fl = (s32)SV_FLAGS;
    if (fl < 0 && SV_STEP != 3) {
        if (GCALL1(F_CARD_PRESENT, (u32)(fl & 1) << 4)) {
            u32 f = SV_FLAGS, c = f & 1;
            u32 cnt = 0x80082578u + c * 2;
            u16 n = (u16)(GV(u16, cnt) + 1);
            GV(u16, cnt) = n;
            if (n >= 3) {
                if (GV(u32, SV_CARD + c * 4) == 2) {
                    SV_FLAGS = f & 0x7FFFFFFFu;
                    SV_SUB = 0;
                    SV_MSG = 0x80080030u;
                    SV_STEP = 4;
                    SV_SPRITES = 0;
                    GV(u16, cnt) = 0;
                }
                GV(u32, SV_CARD + (SV_FLAGS & 1) * 4) = 2;
            }
        } else {
            u32 c = SV_FLAGS & 1;
            GV(u32, SV_CARD + c * 4) = 0;
            GV(u16, 0x80082578u + c * 2) = 0;
        }
        GV(u16, 0x80082578u + ((SV_FLAGS ^ 1) & 1) * 2) = 0;
    } else {
        GV(u16, 0x8008257A) = 0;
        GV(u16, 0x80082578) = 0;
    }
    GCALL0(GV(u32, SV_STEP_FNS + (u32)SV_STEP * 4));
}

/* title_Pressed for the save screen's steps (as the title's menus) */
static int pressed(u32 mask) {
    if (!((u16)GCALL3(0x8006EC1Cu, (u32)(s32)GV(s16, 0x800805CC), T_PAD2, mask))) return 0;
    return !((u16)GCALL3(0x8006EC1Cu, (u32)(s32)GV(s16, 0x80080E48), T_PAD2_PREV, mask));
}

static int held(u32 mask) {
    return (u16)GCALL3(0x8006EC1Cu, (u32)(s32)GV(s16, 0x800805CC), T_PAD2, mask) != 0;
}

/* save_Pressed (8007D590) on this frame's pad and not last frame's */
static int sv_pressed(u32 mask) {
    if (!((u16)GCALL2(F_SV_PRESSED, (u32)(s32)GV(s16, 0x800805CC), mask))) return 0;
    return !((u16)GCALL2(F_SV_PRESSED, (u32)(s32)GV(s16, 0x80080E48), mask));
}

static int sv_held(u32 mask) {
    return (u16)GCALL2(F_SV_PRESSED, (u32)(s32)GV(s16, 0x800805CC), mask) != 0;
}

static u32 take_sprite(u32 image, u16 flags) {
    s32 k = (s32)GCALL0(F_SPRITE_ALLOC);
    if (k == -1) return 0;
    u32 s = SPR(k);
    SP_U16(s, 0x20) |= flags;
    GCALL2(F_SPRITE_SET_IMAGE, s, image);
    return s;
}

/* 8007B5F8: "Save?" yes / no: set up (third frame), fade in, then up /
 * down (0x800 / 0x40) move the cursor, fire chooses. */
void n_save_Ask(void) {
    FRAME(0x28);
    if (SV_SUB == 0) {
        s32 n = SV_WAIT + 1;
        SV_WAIT = n;
        if (n != 3) return;
        GCALL1(F_T_LOAD_PICS, 5);
        GCALL2(F_T_LOAD_IMAGES, 6, 3);
        GCALL2(F_T_LOAD_IMAGES, 0xA, 1);
        GS(u16, sp + 0x10) = 0x17E;
        GS(u16, sp + 0x12) = 0x1E3;
        GS(u16, sp + 0x14) = 2;
        GS(u16, sp + 0x16) = 4;
        GCALL4(F_CLEAR_IMAGE, sp + 0x10, 0, 0, 0);
        GCALL4(F_TEXT_SET, 0x8007FF9Cu, 0, 0x30, 0xA8);
        SV_PORT = 0;
        GCALL3(F_TEXT_CURSOR, 0, 0, 0);
        SV_HELD = 1;
        SV_SUB++;
        u32 s = take_sprite(0x8007F220u, 6);
        if (s) {
            SP_U16(s, 0x18) = 5;
            SP_U16(s, 0x14) = 0xFF;
            SP_U16(s, 8) = 0xFF;
            SP_U16(s, 0x1E) = 0x20;
            SP_U16(s, 0x1C) = 0x20;
            SP_U16(s, 0x1A) = 0x20;
            SP_U16(s, 0x10) = 0;
            SP_U16(s, 0x12) = 0;
            GV(u32, s + 0x34) = 0x8007EB54u;
        }
        s = take_sprite(0x8007F220u, 6);
        if (s) {
            SP_U16(s, 0x14) = 0x40;
            SP_U16(s, 8) = 0x40;
            SP_U16(s, 0x10) = 0xFF0;
            SP_U16(s, 0x18) = 5;
            SP_U16(s, 0x1E) = 0x20;
            SP_U16(s, 0x1C) = 0x20;
            SP_U16(s, 0x1A) = 0x20;
            u16 t = SP_U16(s, 2);
            SP_U16(s, 0x12) = 0;
            GV(u32, s + 0x34) = 0x8007EB54u;
            SP_U16(s, 2) = (u16)(t + 2);
        }
        T_FADE = 0;
        return;
    }
    s32 f = T_FADE;
    if (f < 0x80) {
        f += 8;
        T_FADE = f;
        if (f >= 0x81) T_FADE = 0x80;
        return;
    }
    if (pressed(0x7FF)) goto no;
    if (pressed(0x20)) {
        if (SV_PORT != 0) goto no;
        GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
        SV_STEP = 1;
        SV_SUB = 0;
        goto end;
    }
    if (pressed(0x800) || pressed(0x40)) {
        GCALL4(F_T_SOUND, 0, 1, 0x7F, 0x7F);
        u32 v = SV_PORT ^ 1;
        SV_PORT = v;
        GCALL3(F_TEXT_CURSOR, v, 0, 0);
    }
    goto end;
no:
    GCALL4(F_T_SOUND, 0, 3, 0x7F, 0x7F);
    SV_PORT = 1;
    SV_STEP = 0xD;
    SV_SUB = 0;
end:
    if (!held(0x800) || !held(0x40)) SV_HELD = 0;
}

/* 8007B9F8: look at both cards: one present: go on with it (step 3),
 * none: step 2; both: ask which (step 4 with a message). */
void n_save_FindCard(void) {
    FRAME(0x18);
    GCALL0(0x8007EB5Cu);
    if (GV(s32, SV_CARD) == 2) {
        if (GV(s32, SV_CARD + 4) == 2) {
            SV_MSG = 0x80080030u;
            SV_FLAGS = 0;
            SV_STEP = 4;
            goto out;
        }
        SV_PORT = 1;
        SV_FLAGS |= 1;
    } else {
        SV_PORT = 0;
        SV_FLAGS &= ~1u;
    }
    if (GV(s32, SV_CARD) == 2 || GV(s32, SV_CARD + 4) == 2) SV_STEP = 3;
    else SV_STEP = 2;
out:
    SV_SUB = 0;
}

/* 8007BF1C: check the chosen card (asking until the same answer comes
 * five times): no card / error / unformatted: a message; else read its
 * directory: free blocks, our saves (their info into SV_INFO), which
 * slots can take a new save; then the slot list (step 8). */
void n_save_CheckCard(void) {
    FRAME(0xB8);
    if (SV_SUB == 0) {
        SV_FLAGS = (SV_FLAGS & ~1u);
        SV_FLAGS = SV_FLAGS | (u32)SV_PORT;
        u32 c = (u32)SV_PORT * 4;
        GV(u32, SV_RETRY + c) = 0;
        GV(u32, SV_CARD + c) = (u32)-1;
        GCALL4(F_TEXT_SET, 0x8007FFF4u, 0, 0x30, 0xA8);
        SV_WAIT = 0;
        SV_SUB++;
        return;
    }
    s32 n = SV_WAIT + 1;
    SV_WAIT = n;
    if (n < 2) return;
    s32 st = (s16)GCALL1(F_SV_CARD_STATE, (u32)SV_PORT);
    if (st == -1) return;
    u32 c = (u32)SV_PORT * 4;
    if (st != GV(s32, SV_CARD + c)) {
        GV(u32, SV_RETRY + c) = 0;
        GV(s32, SV_CARD + c) = st;
    }
    c = (u32)SV_PORT * 4;
    s32 tries = GV(s32, SV_RETRY + c) + 1;
    GV(s32, SV_RETRY + c) = tries;
    if (tries < 5) return;
    GV(u32, 0x80082540) = 0xF;
    SV_SUB = 0;
    if (st == 1) {
        SV_MSG = 0x800800F0u;
        SV_STEP = 4;
        goto checking;
    }
    if (st >= 2) {
        if (st == 2) {
            SV_MSG = 0x80080030u;
            SV_STEP = 4;
        } else if (st == 3) {
            SV_STEP = 6;
            goto checking;
        }
        return;
    }
    if (st != 0) return;
    {
        GCALL2(F_STRCPY, sp + 0x10, GV(u32, CARD_DEV + c));
        GCALL2(F_STRCAT, sp + 0x10, 0x8006C22Cu);
        s32 files = count_files(sp + 0x10, 0x8007C10C);
        s32 used = 0;
        u32 d = DIRENT;
        for (s32 i = 0; i < files; i++, d += 0x28) {
            s32 size = GV(s32, d + 0x18);
            s32 b = (s32)((u32)size + 0x1FFF);
            if (b < 0) b = (s32)((u32)size + 0x3FFE);
            used += b >> 13;
            if (i + 1 < files) LOOP_POLL(0x8007C15C);
        }
        SV_FREE = 0xF - used;
        s32 ours = 0;
        d = DIRENT;
        for (s32 i = 0; i < files; i++, d += 0x28) {
            if (GCALL3(F_STRNCMP, FILE_NAME, d, 0xC) == 0) ours++;
            if (i + 1 < files) LOOP_POLL(0x8007C1B0);
        }
        SV_OURS = ours;
        if (SV_FREE + ours <= 0) {
            SV_MSG = 0x80080224u;
            SV_STEP = 4;
            goto checking;
        }
        GCALL1(F_SV_SLOTS_USED, (u32)files);
        u32 port = (u32)SV_PORT;
        d = DIRENT;
        for (s32 i = 0; i < files; i++, d += 0x28) {
            if (GCALL3(F_STRNCMP, FILE_NAME, d, 0xC) == 0) {
                u32 num = (u32)((s32)(GV(u8, d + 0xD) - 0x30) * 10 - 0x30 + GV(u8, d + 0xE));
                if (GCALL3(F_SLOT_INFO, port, num, sp + 0x10) == 2) {
                    GV(u8, SV_SLOTS + num) = 0xFE;
                } else {
                    u32 e = SV_INFO + num * 2;
                    GV(u16, e) = GS(u8, sp + 0x11);
                    u32 v = GCALL2(F_CLEARS, GS(u32, sp + 0x18), GS(u32, sp + 0x14));
                    GV(u16, e + 0x1E) = (u16)v;
                    GV(u16, e + 0x3C) = GS(u16, sp + 0x1C);
                    GV(u8, SV_INFO + num + 0x5A) = GS(u8, sp + 0x13);
                }
            }
            if (i + 1 < files) LOOP_POLL(0x8007C2B8);
        }
        /* mark free slots up to the free blocks, the rest unusable */
        s32 k = 0, i = 0;
        int lt;
        if (SV_FREE > 0) {
            for (;;) {
                i = 0;
                for (;;) {
                    u32 a = SV_SLOTS + (u32)i;
                    if (GV(u8, a) == 0xFF) {
                        GV(u8, a) = 0;
                        k++;
                        if (k >= SV_FREE) { lt = i < 0xF; goto rest; }
                    }
                    i++;
                    if (i >= 0xF) break;
                    LOOP_POLL(0x8007C320);
                }
                if (k >= SV_FREE) break;
                LOOP_POLL(0x8007C334);
            }
        }
        lt = i < 0xF;
    rest:
        if (lt) {
            for (;;) {
                u32 a = SV_SLOTS + (u32)i;
                if (GV(u8, a) == 0xFF) GV(u8, a) = 1;
                i++;
                if (i >= 0xF) break;
                LOOP_POLL(0x8007C374);
            }
        }
        SV_SLOT = 0;
        SV_TOP = 0xF;
        SV_STEP = 8;
        SV_SUB = 0;
    }
checking:
    SV_FLAGS |= 0x80000000u;
}

static void slot_back(void) {
    GCALL4(F_T_SOUND, 0, 3, 0x7F, 0x7F);
    GV(s32, SV_CARD) = GCALL1(F_CARD_PRESENT, 0) ? 2 : 0;
    GV(s32, SV_CARD + 4) = GCALL1(F_CARD_PRESENT, 0x10) ? 2 : 0;
    if (GV(s32, SV_CARD) == 2 || GV(s32, SV_CARD + 4) == 2) SV_STEP = 5;
    else SV_STEP = 2;
    u32 f = SV_FLAGS;
    SV_FLAGS = 0;
    SV_SUB = 0;
    SV_SPRITES = 0;
    SV_PORT = (s32)(f & 1);
}

/* 8007C9BC: the slot list: up / down (held: repeating) move the cursor,
 * the list scrolling with it; fire on a used slot: overwrite? (step 9);
 * on a free one: write (step 0xA); on "back" (15), or back: look at the
 * cards again. */
void n_save_SlotSelect(void) {
    FRAME(0x28);
    if (SV_SUB == 0) {
        SV_MSG = 0x80080350u;
        GCALL0(F_SV_TEXT);
        GV(u16, TEXT_POS) = 0x10;
        GV(u16, TEXT_POS + 2) = 0x1C;
        T_LINE_H = 0xF;
        SV_HOLD = 0;
        SV_RATE = 8;
        SV_HELD = 1;
        for (s32 i = 0; i < 2; i++) {                 /* the arrows (with a mouse) */
            s32 k = (s32)GCALL0(F_SPRITE_ALLOC);
            if (k != -1) {
                u32 s = SPR(k);
                SP_U16(s, 0x20) |= 6;
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1F8u);
                u16 cy = SP_U16(s, 0xE);
                SP_U16(s, 0x10) = 0xE0;
                SP_U16(s, 0xE) = (u16)(cy + 1);
                u16 u = SP_U16(s, 4);
                if (i == 0) { SP_U16(s, 0x12) = 0x180; u += 8; }
                else { SP_U16(s, 0x12) = 0xC20; u += 0x10; }
                SP_U16(s, 4) = u;
                u16 v = SP_U16(s, 6);
                SP_U16(s, 0x18) = 1;
                SP_U16(s, 0x14) = 9;
                SP_U16(s, 8) = 9;
                SP_U16(s, 0x16) = 8;
                SP_U16(s, 0xA) = 8;
                GV(u32, s + 0x34) = 0x8007EBBCu;
                SP_U16(s, 6) = (u16)(v + 0xD0);
            }
            if (i + 1 < 2) LOOP_POLL(0x8007CB04);
        }
        SV_SPRITES = 1;
        SV_RATE = 8;
        SV_SUB++;
    }
    if (sv_pressed(0x7FF)) {
    back:
        slot_back();
        goto end;
    }
    if (sv_held(0x800) || sv_held(0x40)) {
        s32 h = SV_HOLD;
        if (h != 0) { SV_HOLD = h - 1; goto end; }
        SV_HOLD = SV_RATE;
        if (SV_RATE >= 2 || (T_FRAMES & 1)) GCALL4(F_T_SOUND, 0, 1, 0x7F, 0x7F);
        if (sv_held(0x800)) {
            s32 v = SV_SLOT - 1;
            SV_SLOT = v;
            if (v < 0) SV_SLOT = 0xF;
            s32 cur = SV_SLOT, d = cur - SV_TOP;
            if (d < 0) d += 0x10;
            if (d >= 5) SV_TOP = cur;
        } else {
            s32 v = SV_SLOT + 1;
            SV_SLOT = v;
            if (v >= 0x10) SV_SLOT = 0;
            s32 cur = SV_SLOT, d = cur - SV_TOP;
            if (d < 0) d += 0x10;
            if (d >= 5) {
                SV_TOP = cur - 4;
                if (cur - 4 < 0) SV_TOP = cur + 0xC;
            }
        }
        GCALL0(F_SV_TEXT);
        goto end;
    }
    SV_HOLD = 0;
    SV_RATE = 8;
    if (sv_pressed(0x20)) {
        s32 cur = SV_SLOT;
        if (cur == 0xF) { LOOP_POLL(0x8007CDDC); goto back; }
        GV(s32, 0x8008255C) = cur;
        u8 b = GV(u8, SV_SLOTS + (u32)cur);
        if (b >= 2) {
            GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
            SV_STEP = 9;
            SV_OVERWRITE = 1;
            SV_SAVE_SLOT = SV_SLOT;
        } else if (b == 0) {
            GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
            SV_STEP = 0xA;
            s32 v = SV_SLOT;
            SV_OVERWRITE = 0;
            SV_SAVE_SLOT = v;
        } else {
            GCALL4(F_T_SOUND, 0, 4, 0x7F, 0x7F);
            SV_STEP = 0xC;
        }
        SV_SUB = 0;
    }
end:
    if (!sv_held(0x800) || !sv_held(0x40)) SV_HELD = 0;
}

/* 8007D108: write the save (after a frame with the "saving" message);
 * failed: a message (step 4); done: the slot's info updated (step 0xB). */
void n_save_Write(void) {
    FRAME(0x20);
    if (SV_SUB == 0) {
        SV_MSG = 0x800802B4u;
        GCALL0(F_SV_TEXT);
        SV_WAIT = 0;
        SV_SUB++;
        return;
    }
    s32 n = SV_WAIT + 1;
    SV_WAIT = n;
    if (n < 2) return;
    GS(u32, sp + 0x10) = 0;
    if (GCALL4(F_SV_WRITE_FILE, SV_FLAGS & 1, (u32)SV_SAVE_SLOT, 0, SV_OVERWRITE)) {
        SV_MSG = 0x800802ECu;
        SV_STEP = 4;
        SV_SPRITES = 0;
        SV_FLAGS = SV_FLAGS & 1;
    } else {
        SV_STEP = 0xB;
        GV(u8, SV_SLOTS + (u32)SV_SLOT) = (u8)(GV(u8, 0x80082560) + 2);
        GV(u16, SV_INFO + (u32)SV_SLOT * 2) = GV(u16, 0x8008261C);
        s32 total = GV(s32, 0x80063498), done = GV(s32, 0x8006349C), pct;
        if (total == 0) pct = 0;
        else {
            pct = mips_div((s32)((u32)done * 100), total);
            if (pct >= 0x65) pct = 0x64;
        }
        s32 slot = SV_SLOT;
        u32 e = SV_INFO + (u32)slot * 2;
        GV(u16, e + 0x1E) = (u16)pct;
        GV(u16, e + 0x3C) = GV(u16, 0x800634A0);
        GV(u8, SV_INFO + (u32)slot + 0x5A) = GV(u8, 0x80062DF8);
        SV_FLAGS = SV_FLAGS & 1;
    }
    SV_SUB = 0;
}

/* 8007D2C8: "Overwrite?" yes / no. */
void n_save_Confirm(void) {
    FRAME(0x18);
    if (SV_SUB == 0) {
        SV_MSG = 0x80080318u;
        GCALL0(F_SV_TEXT);
        SV_PORT = 0;
        GCALL3(F_TEXT_CURSOR, 0, 0, 1);
        SV_HELD = 1;
        SV_SUB++;
        return;
    }
    if (sv_pressed(0x7FF)) goto no;
    if (sv_pressed(0x800) || sv_pressed(0x40)) {
        GCALL4(F_T_SOUND, 0, 1, 0x7F, 0x7F);
        u32 v = (u32)SV_PORT ^ 1;
        SV_PORT = (s32)v;
        GCALL3(F_TEXT_CURSOR, v, 0, 1);
        goto end;
    }
    if (sv_pressed(0x20)) {
        if (SV_PORT != 0) goto no;
        GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
        SV_STEP = 0xD;
    }
    goto end;
no:
    GCALL4(F_T_SOUND, 0, 3, 0x7F, 0x7F);
    SV_STEP = 8;
    SV_SUB = 0;
end:
    if (!sv_held(0x800) || !sv_held(0x40)) SV_HELD = 0;
}

/* 8007D590: save_Pressed(pad, button): as title_Pressed, for the save
 * screen's steps (up/down: 0x800 up, 0x40 down; 2 and 8 the yes/no ones). */
void n_save_Pressed(void) {
    u32 pad = A0;
    s32 m = (s16)A1;
    u32 t = g_padTypeBit;
    if (t == 0x200000u) {
        if (m == 0x40) V0 = pad & 0x40;
        else if (m < 0x41) V0 = m == 0x20 ? pad & 0x20 : 0;
        else if (m == 0x7FF) V0 = (pad & 0x840) == 0x840;
        else if (m == 0x800) { LOOP_POLL(0x8007D6D8); V0 = (u16)(pad & 0x800); }
        else V0 = 0;
        return;
    }
    if ((s32)t > 0x200000 ? t != 0x400000u : t != 0x80000u) { V0 = 0; return; }
    if (m == 0x40) {
        s32 st = SV_STEP;
        if (st == 2) { LOOP_POLL(0x8007D660); V0 = (u16)(pad & 0x4000); }
        else if (st != 8) { LOOP_POLL(0x8007D67C); V0 = (u16)(pad & 0x2000); }
        else { LOOP_POLL(0x8007D674); V0 = (u16)(pad & 0x4000); }
        return;
    }
    if (m < 0x41) { V0 = m == 0x20 ? pad & 0xA0 : 0; return; }
    if (m == 0x7FF) { V0 = pad & 0x40; return; }
    if (m != 0x800) { V0 = 0; return; }
    s32 st = SV_STEP;
    if (st == 2 || st == 8) V0 = (u16)(pad & 0x1000);
    else V0 = (u32)(s32)(s16)(pad & 0xFFFF8000u);
}

/* Copy a 0xFF-terminated string from `src` to `dst` without its end;
 * the loop branch (a backward jump) at pc. Returns the new dst. */
static u32 copy_string(u32 dst, u32 src, u32 pc) {
    for (;;) {
        u8 c = GV(u8, src);
        src++;
        if (c == 0xFF) return dst;
        GV(u8, dst) = c;
        dst++;
        LOOP_POLL(pc);
    }
}

/* 8007D70C: build the slot list's text in window 0: the title (which
 * card), five slots from SV_TOP (number; "back", a message for free /
 * unusable / other slots, or the save's clears as pips, completion and
 * time), the cursor on SV_SLOT; then the step's message (SV_MSG). */
void n_save_BuildText(void) {
    u32 a = TEXT_BUF;
    GV(u8, a) = 0xF8;
    a++;
    GV(u8, a) = 0xF;
    u32 src = 0x800804F0u;
    u8 c = GV(u8, src);
    a++;
    if (c != 0xFF) {
        for (;;) {
            GV(u8, a) = GV(u8, src);
            src++;
            c = GV(u8, src);
            a++;
            if (c == 0xFF) break;
            LOOP_POLL(0x8007D758);
        }
    }
    if (SV_FLAGS & 1) GV(u8, a - 1) = (u8)(GV(u8, a - 1) + 1);
    static const u8 nl[] = {0xFE, 0xF7, 8, 0xF8, 0x1E};
    for (u32 k = 0; k < sizeof nl; k++) GV(u8, a++) = nl[k];
    s32 slot = SV_TOP;
    for (s32 row = 0;; row++, slot++) {
        if (slot >= 0x10) slot = 0;
        GV(u8, a++) = 0xF9;
        GV(u8, a++) = slot == SV_SLOT ? 0x80 : 0x20;
        GV(u8, a++) = 0xFC;
        if (slot == 0xF || (GV(u8, SV_SLOTS + (u32)slot) >= 2 && GV(u8, SV_INFO + (u32)slot + 0x5A) != 0))
            GV(u8, a) = 1;
        else
            GV(u8, a) = 0;
        a++;
        if (slot == 0xF) {                            /* "back" */
            static const u8 back[] = {0xFA, 0x2E, 0, 0x21, 0, 0x34, 0, 0x25, 0, 0x30, 0xFE};
            for (u32 k = 0; k < sizeof back; k++) GV(u8, a++) = back[k];
            goto next;
        }
        {
            s32 d;
            GV(u8, a++) = 0;
            if (slot >= 9) { GV(u8, a++) = 0xD; d = slot - 0xA; }
            else { GV(u8, a++) = 0xC; d = slot; }
            GV(u8, a++) = 0;
            GV(u8, a++) = (u8)(d + 0xD);
            GV(u8, a++) = 0xFA;
            GV(u8, a++) = 0x14;
        }
        {
            u8 st = GV(u8, SV_SLOTS + (u32)slot);
            s32 kind = st == 0xFE ? -2 : st;
            if (kind < 2) {                           /* a message */
                u32 s = kind == -2 ? GV(u32, 0x800804C0u + 8) : GV(u32, 0x800804C0u + (u32)kind * 4);
                u8 ch = GV(u8, s);
                s++;
                GV(u8, a) = ch;
                a++;
                if (ch != 0xFF) {
                    for (;;) {
                        ch = GV(u8, s);
                        s++;
                        GV(u8, a) = ch;
                        a++;
                        if (ch == 0xFF) break;
                        LOOP_POLL(0x8007D9C8);
                    }
                }
                GV(u8, a - 1) = 0xFE;
                continue_row:
                if (row + 1 >= 5) break;
                LOOP_POLL(0x8007DD48);
                continue;
            }
        }
        {
            GV(u8, a++) = 0xF7;
            GV(u8, a++) = 0xFA;
            GV(u8, a++) = 0xF8;
            GV(u8, a++) = 0xE;
            a = copy_string(a, 0x800804CCu, 0x8007DA20);
            s32 pips = GV(s16, SV_INFO + (u32)slot * 2);
            if (pips >= 9) pips = 8;
            s32 i = 0;
            if (pips > 0) {
                for (;;) {
                    GV(u8, a++) = 1;
                    GV(u8, a++) = 0;
                    i++;
                    if (i >= pips) break;
                    LOOP_POLL(0x8007DA64);
                }
            }
            while (i < 8) {
                LOOP_POLL(0x8007DA8C);
                GV(u8, a++) = 1;
                GV(u8, a++) = 1;
                i++;
            }
            GV(u8, a++) = 0xFE;
            GV(u8, a++) = 0xFA;
            GV(u8, a++) = 0x30;
            a = copy_string(a, 0x800804D8u, 0x8007DAC8);
            s32 pct = (s16)GV(u16, SV_INFO + (u32)slot * 2 + 0x1E);
            GV(u8, a++) = 0;
            if (pct >= 0x64) {
                s32 q = pct / 100;
                GV(u8, a++) = (u8)(q + 0xC);
                pct -= q * 100;
            } else {
                GV(u8, a++) = 0;
            }
            s32 q = pct / 10;
            GV(u8, a++) = 0;
            if (q == 0) {
                if (GV(u8, a - 2) != 0) GV(u8, a++) = 0xC;
                else GV(u8, a++) = 0;
            } else {
                GV(u8, a++) = (u8)(q + 0xC);
                pct -= q * 10;
            }
            static const u8 pc_[] = {0, 0, 0, 0xFF, 0, 0};
            GV(u8, a++) = pc_[0];
            GV(u8, a++) = (u8)(pct + 0xC);
            for (u32 k = 2; k < sizeof pc_; k++) GV(u8, a++) = pc_[k];
            a = copy_string(a, 0x800804E4u, 0x8007DBFC);
            s32 tm = GV(s16, SV_INFO + (u32)slot * 2 + 0x3C);
            if (tm >= 0x3E8) tm = 0x3E7;
            GV(u8, a++) = 0;
            if (tm >= 0x64) {
                s32 h = tm / 100;
                GV(u8, a++) = (u8)(h + 0xC);
                tm -= h * 100;
            } else {
                GV(u8, a++) = 0;
            }
            GV(u8, a++) = 0;
            if (GV(u8, a - 2) == 0 && tm / 10 == 0) {
                GV(u8, a++) = 0;
            } else {
                s32 t = tm / 10;
                GV(u8, a++) = (u8)(t + 0xC);
                tm -= t * 10;
            }
            GV(u8, a++) = 0;
            GV(u8, a++) = (u8)(tm + 0xC);
            static const u8 tail[] = {0xFE, 0xF7, 8, 0xF8, 0x1E};
            for (u32 k = 0; k < sizeof tail; k++) GV(u8, a++) = tail[k];
        }
    next:
        goto continue_row;
    }
    static const u8 foot[] = {0xF9, 0x80, 0xF8, 0xF, 0xF7, 0xFA, 0xFA, 8, 0xFC, 1, 0, 0xB, 0xFC, 0, 0xF6, 0xFA, 0x1A};
    for (u32 k = 0; k < sizeof foot; k++) GV(u8, a++) = foot[k];
    u32 m = SV_MSG;
    for (;;) {
        u8 ch = GV(u8, m);
        m++;
        GV(u8, a) = ch;
        a++;
        if (ch == 0xFF) break;
        if (ch != 0xFE) { LOOP_POLL(0x8007DE40); continue; }
        GV(u8, a++) = 0xFA;
        GV(u8, a++) = 0x30;
        LOOP_POLL(0x8007DE54);
    }
    T_LINE_H = 0xF;
    V0 = 0xF;
}

/* Copy n bytes (a loop of one byte a pass, its branch at pc). */
static u32 copy_bytes(u32 dst, u32 src, u32 n, u32 pc) {
    for (u32 i = 0; i < n; i++) {
        GV(u8, dst + i) = GV(u8, src + i);
        if (i + 1 < n) LOOP_POLL(pc);
    }
    return dst + n;
}

static u32 zero_bytes(u32 dst, u32 n, u32 pc) {
    for (u32 i = 0; i < n; i++) {
        GV(u8, dst + i) = 0;
        if (i + 1 < n) LOOP_POLL(pc);
    }
    return dst + n;
}

/* 8007DE6C: write save file `slot` (BASLUS-01087xx) to the card in
 * `port`: a 0x200-byte header (title, icon) and 0x1E00 bytes of data
 * (progress, settings, story flags, items), each with a byte checksum;
 * created first unless overwriting. Five tries at each step. 0: done,
 * 1: failed. */
void n_save_WriteFile(void) {
    FRAME(0x48);
    u32 port = A0;
    s32 slot = (s32)A1;
    u32 overwrite = A3;
    u32 path = sp + 0x10;
    GCALL2(F_STRCPY, path, GV(u32, CARD_DEV + port * 4));
    GCALL2(F_STRCAT, path, FILE_NAME);
    GCALL2(F_STRCAT, path, 0x8006C240u);
    s32 q = slot / 10;
    GS(u8, sp + 0x24) = 0;
    GS(u8, sp + 0x22) = (u8)(q + 0x30);
    GS(u8, sp + 0x23) = (u8)(slot - q * 10 + 0x30);
    s32 fd;
    if (!overwrite) {
        fd = (s32)GCALL2(F_OPEN, path, 0x10200);
        if (fd == -1) { V0 = 1; return; }
        GCALL1(F_CLOSE, (u32)fd);
    }
    /* the header */
    u32 b = FILE_BUF;
    GV(u8, b++) = 'S';
    GV(u8, b++) = 'C';
    GV(u8, b++) = 0x11;
    GV(u8, b++) = 1;
    copy_bytes(b, GV(u32, 0x800803BCu), 0x32, 0x8007DFC0);
    s32 n1 = SV_SAVE_SLOT + 1;
    GV(u8, b + 0x2F) = (u8)(GV(u8, b + 0x2F) + n1 / 10);
    s32 n2 = SV_SAVE_SLOT + 1;
    GV(u8, b + 0x31) = (u8)(GV(u8, b + 0x31) + (n2 - n2 / 10 * 10));
    b += 0x32;
    b = zero_bytes(b, 14, 0x8007E048);
    b = zero_bytes(b, 28, 0x8007E05C);
    b = copy_bytes(b, 0x800803D4u, 0x20, 0x8007E084);
    copy_bytes(b, 0x80080400u, 0x80, 0x8007E0B0);
    /* the data: progress and settings */
    u32 base = 0x80082828u;
    GV(u8, base) = 0xE;
    b = base + 1;
    GV(u8, 0x80062DB0) = 1;
    u32 story = GV(u32, 0x800634F0);
    if (story & 0x20000000u) GV(u8, 0x80062DB0) = 2;
    if (story & 0x10000000u) GV(u8, 0x80062DB0)++;
    if (story & 0x08000000u) GV(u8, 0x80062DB0)++;
    if (story & 0x04000000u) GV(u8, 0x80062DB0)++;
    if (story & 0x02000000u) GV(u8, 0x80062DB0)++;
    if ((story & 0x00800000u) || GV(u8, 0x80062D3C)) GV(u8, 0x80062DB0)++;
    if (GV(u32, 0x800634F0) & 0x400000u) GV(u8, 0x80062DB0)++;
    GV(u32, 0x8008261C) = GV(u8, 0x80062DB0);
    GV(u8, b++) = GV(u8, 0x8008261C);
    GV(u8, b++) = GV(u8, 0x80062D3C);
    GV(u8, b++) = GV(u8, 0x80062DF8);
    for (u32 k = 0; k < 5; k++) GV(u32, b + k * 4) = GV(u32, 0x80063498 + k * 4);
    GV(u32, b + 0x14) = GV(u32, 0x80080610);
    GV(u32, b + 0x18) = GV(u32, 0x80062DD4);
    GV(u32, b + 0x1C) = GV(u32, 0x80080618);
    GV(u32, b + 0x20) = GV(u32, 0x80062E20);
    GV(u32, b + 0x24) = GV(u32, 0x80062DB4);
    GV(u32, b + 0x28) = GV(u32, 0x80062DA4);
    b = copy_bytes(b + 0x2C, 0x80063770u, 0x3D, 0x8007E2CC);
    GV(u8, b) = GV(u8, 0x80062E30);
    b++;
    GV(u8, b) = GV(u8, 0x80062DE4);
    b++;
    while (b < base + 0x7F) {
        LOOP_POLL(0x8007E304);
        GV(u8, b) = 0;
        b++;
    }
    u32 sum = 0;
    for (u32 i = 0; i < 0x7F; i++) {
        sum += GV(u8, base + i);
        if (i + 1 < 0x7F) LOOP_POLL(0x8007E328);
    }
    GV(u8, b) = (u8)sum;
    /* story flags, items */
    u32 data = b + 1;
    for (u32 o = 0;; o += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GV(u32, data + o + j) = GV(u32, 0x800634B0u + o + j);
        if (o + 0x10 == 0x90) break;
        LOOP_POLL(0x8007E374);
    }
    GV(u32, data + 0x90) = GV(u32, 0x800634B0u + 0x90);
    b = copy_bytes(data + 0x94, 0x80063548u, 0x222, 0x8007E3AC);
    u32 end = data + 0x1D7F;
    if (b < end) {
        for (;;) {
            GV(u8, b) = 0;
            b++;
            if (b >= end) break;
            LOOP_POLL(0x8007E3D0);
        }
    }
    sum = 0;
    for (u32 i = 0; i < 0x1D7F; i++) {
        sum += GV(u8, data + i);
        if (i + 1 < 0x1D7F) LOOP_POLL(0x8007E3EC);
    }
    GV(u8, b) = (u8)sum;
    /* write the header ... */
    s32 tries = 0, n;
    for (;;) {
        for (n = 0; n < 5; n++) {
            fd = (s32)GCALL2(F_OPEN, path, 2);
            if (fd != -1) break;
            if (n + 1 < 5) LOOP_POLL(0x8007E424);
        }
        if (n == 5) { V0 = 1; return; }
        if (GCALL3(F_WRITE, (u32)fd, FILE_BUF, 0x200) == 0x200) {
            LOOP_POLL(0x8007E44C);
            GCALL1(F_CLOSE, (u32)fd);
            break;
        }
        GCALL1(F_CLOSE, (u32)fd);
        tries++;
        if (tries >= 5) break;
        LOOP_POLL(0x8007E464);
    }
    /* ... then the data after it */
    for (tries = 0;;) {
        for (n = 0; n < 5; n++) {
            fd = (s32)GCALL2(F_OPEN, path, 2);
            if (fd != -1) break;
            if (n + 1 < 5) LOOP_POLL(0x8007E4A0);
        }
        if (n != 5) {
            for (n = 0; n < 5; n++) {
                if ((s32)GCALL3(F_LSEEK, (u32)fd, 0x200, 0) != -1) break;
                if (n + 1 < 5) LOOP_POLL(0x8007E4D0);
            }
            if (n != 5 && GCALL3(F_WRITE, (u32)fd, 0x80082828u, 0x1E00) == 0x1E00) {
                LOOP_POLL(0x8007E4F0);
                GCALL1(F_CLOSE, (u32)fd);
                V0 = 0;
                return;
            }
            GCALL1(F_CLOSE, (u32)fd);
        }
        tries++;
        if (tries >= 5) break;
        LOOP_POLL(0x8007E508);
    }
    V0 = 1;
}

/* 8007E538: the save screen's state, reset. */
void n_save_Reset(void) {
    T_LINE_H = 0xF;
    SV_STEP = 0;
    SV_WAIT = 0;
    SV_SUB = 0;
    SV_FLAGS = 0;
    SV_SPRITES = 0;
}

/* 8007E594: is a card in `chan` (port << 4)? */
void n_card_Present(void) {
    FRAME(0x20);
    u32 chan = A0;
    GCALL0(F_ENTER_CRIT);
    u32 ev = GCALL4(F_OPEN_EVENT, 0xF4000001u, 0x100, 0x2000, 0);
    GV(u32, EV_TEST) = ev;
    GCALL1(F_ENABLE_EVENT, ev);
    GCALL0(F_EXIT_CRIT);
    chan &= 0xFF;
    GCALL1(F_TEST_EVENT, GV(u32, EV_TEST));
    GCALL1(F_CARD_INFO, chan);
    GCALL1(F_CARD_WAIT, chan >> 4);
    u32 r = GCALL1(F_TEST_EVENT, GV(u32, EV_TEST)) == 1;
    GCALL1(F_CLOSE_EVENT, GV(u32, EV_TEST));
    V0 = r;
}

/* 8007EB54: an update that does nothing. */
void n_save_NoUpdate(void) {
}

/* 8007EB5C: look at both card slots (2: a card). */
void n_save_CheckCards(void) {
    FRAME(0x18);
    GV(s32, SV_CARD) = GCALL1(F_CARD_PRESENT, 0) ? 2 : 0;
    u32 r = GCALL1(F_CARD_PRESENT, 0x10);
    GV(s32, SV_CARD + 4) = r ? 2 : 0;
    V0 = r ? 2 : 0x80080000u;
}

/* 8007EE20: mark the slots our `count` directory entries use. */
void n_save_SlotsUsed(void) {
    FRAME(0x28);
    s32 count = (s32)A0;
    for (s32 i = 0xE; i >= 0; i--) {
        GV(u8, SV_SLOTS + (u32)i) = 0xFF;
        if (i > 0) LOOP_POLL(0x8007EE58);
    }
    u32 d = DIRENT;
    for (s32 i = 0; i < count; i++, d += 0x28) {
        if (GCALL3(F_STRNCMP, FILE_NAME, d, 0xC) == 0) {
            u32 num = (u32)((s32)(GV(u8, d + 0xD) - 0x30) * 10 - 0x30 + GV(u8, d + 0xE));
            GV(u8, SV_SLOTS + num) = (u8)(num + 2);
        }
        if (i + 1 < count) LOOP_POLL(0x8007EECC);
    }
    V0 = count > 0 ? 0 : SV_SLOTS - 1;
}
