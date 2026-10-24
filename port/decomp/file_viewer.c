/* The status screen's file viewer (CONPAU_E): the list of files the
 * player has found (their numbers at 0x800B3E68, 0xFF ends; the count at
 * 0x800CF98C), their 3D models turning in the middle of the screen, and a
 * file's pages read one at a time.
 *
 * It runs as the callback of the command menu's panel in mode 3 (the
 * screen opened to read a file just picked up: 800C741C adds it to the
 * list and 0x800CF9C0 is its number + 1; 0 when browsing the list), a
 * state machine on ST_OBJ_MODE (0x800CF8C0):
 *   0 start, 1 the list (blinking cursor), 2 the models turn in,
 *   4 shrink them, 5 wait, 6 pick a file (its name shown), 7 open it,
 *   8 reading (the page panels below run), 9 the closing text (direct),
 *   10 wait for a button after it, 11 the models back, 12/13 turn out.
 * A file's pages: file 0x28 + 3 * file (+1, +2: more pages), its page
 * counts at 0x800CF454 (3 bytes per file: total, then where the second
 * and third files start), decoded into 0x800D28B0; the page shown is
 * 0x800CF990 of 0x800CF994.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). These run from panel callbacks, never while the
 * screen's thread is switched away, but keep the original's frames ($ra
 * and the s registers where its prologue stores them) all the same. */
#include "status.h"

void n_files_Start(void);
void n_files_Screen(void);
void n_files_Open(void);
void n_files_LoadPage(void);
void n_files_PageTurn(void);
void n_files_PageLayout(void);
void n_files_HidePanel(void);
void n_files_Arrow(void);
void n_files_PageMark(void);
void n_files_Register(void);

#define FV_MODE        GV(u32, 0x800CF8C0)   /* state (= ST_OBJ_MODE) */
#define FV_DIRECT      GV(u32, 0x800CF9C0)   /* file read directly + 1, 0 browsing */
#define FV_DELAY       GV(u32, 0x800CF9F4)   /* cursor blink delay */
#define FV_BLINK       GV(u32, 0x800CF9C4)
#define FV_WAIT        GV(s16, 0x800CF9BC)   /* input repeat delay */
#define FV_WAIT_U      GV(u16, 0x800CF9BC)
#define FV_ROW         GV(s16, 0x800CF988)
#define FV_CURSOR      GV(s16, 0x800CF98A)   /* 1.. count, 0 / count + 1 the ends */
#define FV_CURSOR_U    GV(u16, 0x800CF98A)
#define FV_COUNT       GV(s16, 0x800CF98C)
#define FV_PAGE        GV(u32, 0x800CF990)
#define FV_PAGES       GV(u32, 0x800CF994)
#define FV_TIM         0x800CF9D8u           /* the page image's TIM info: +8 w, +0xA h, +0x10 x, +0x12 y */
#define FV_PAGE_W      GV(u16, 0x800CF9C8)
#define FV_PAGE_H      GV(u16, 0x800CF9CA)
#define FILE_LIST      0x800B3E68u
#define FILE_PAGES     0x800CF454u
#define FILE_NAMES     0x800CF420u           /* name text per file */
#define FILE_BUF       0x800DB0B0u
#define IMAGE_BUF      0x800D28B0u
#define TEXT_BUF       0x800EFD70u
#define OBJ_ANGLE      GV(s32, 0x800CF840)   /* the models' turn (objects + 0x78) */
#define OBJ_MODELS     0x800CF87Cu           /* the models' animation records */
#define OBJ_MODEL(i)   GV(u32, OBJ_MODELS + (u32)(i) * 4)
#define F_DECODE       0x80016F78u           /* (packed, dest) */
#define F_LOAD_TIM     0x8006F370u           /* (tim): into VRAM */
#define F_TIM_INFO     0x8002E048u
#define F_SHOW_TEXT    0x8007CE4Cu           /* (text, x, y, w, [h, ...]) */
#define F_FILES_CLOSE  0x800C650Cu
#define F_FILES_MODELS 0x800C6228u

/* The file under the list cursor, or the one read directly. */
static u32 current_file(void) {
    u32 f = FV_DIRECT;
    if (f == 0) return GV(u8, FILE_LIST + (u32)(s32)FV_CURSOR - 1);
    return f - 1;
}

/* 800C54B8: panel callback in mode 3: the file viewer takes over the
 * panel (and runs a frame); for a file read directly, the list cursor
 * goes to it (rows of 10). */
void n_files_Start(void) {
    FRAME(0x18);
    SAVE_RA(0x10);
    GV(u32, g_curPanel + 0x3C) = 0x800C5598u;
    V0 = 0x800C5598u;
    jalr_ra(0x800C54D8u, 0x800C5598u);
    u32 f = FV_DIRECT;
    if (f == 0) { sound(0x800C5588u, 2); return; }
    FV_MODE = 7;
    u32 i = 0;
    for (;;) {
        if (GV(u8, FILE_LIST + i) == f - 1) break;
        i++;
        if (!((s32)i < 60)) break;
        LOOP_POLL(0x800C5520);
    }
    s32 q = (s32)i / 10;
    GV(u16, 0x800CF988) = (u16)(2 - q);
    V0 = i - (u32)q * 10 + 1;
    GV(u16, 0x800CF98A) = (u16)V0;
}

/* The models' records: a turn of n frames from their rest pose. */
static void model_turn(u32 m, u16 dv, u8 step, u16 frames) {
    GV(u16, m + 0x16) = dv;
    GV(u8, m + 0xC) = GV(u8, m + 0x1A);
    GV(u16, m + 0x12) = 0;
    GV(u16, m + 0x10) = 0;
    GV(u16, m + 0xA) = frames;
    GV(u8, m + 0xD) = step;
}

/* Close the viewer (800C650C) with the cancel click. */
static void close_viewer(u32 ra) {
    JAL(ra, F_FILES_CLOSE, A0, A1, A2, A3);
    sound(0x800C6040u, 3);
}

/* 800C5598: panel callback: the file viewer (see above). Cancel (mouse),
 * or a keyboard with nothing read directly, closes it. */
void n_files_Screen(void) {
    FRAME(0x38);
    SAVE_SREG(0, 0x28);
    SAVE_RA(0x30);
    SAVE_SREG(1, 0x2C);
    u32 p = g_curPanel;
    GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 0x800;
    if (ST_CANCEL != 0 || (g_padTypeBit == 0x8000 && FV_DIRECT == 0)) {
        JAL(0x800C55F8u, F_FILES_CLOSE, A0, A1, A2, A3);
        g_panelMode = 0x40;
        sound(0x800C5618u, 3);
        ST_SOUND_ON = 0;
        ST_SOUND = 0;
        JAL(0x800C562Cu, F_SOUND_STOP, A0, A1, A2, A3);
        ST_CANCEL = 0;
        return;
    }
    u32 mode = FV_MODE;
    if (mode >= 14) return;
    LOOP_POLL(0x800C565C);
    switch (mode) {
    case 0:
        FV_MODE = 1;
        FV_DELAY = 0;
        FV_WAIT_U = 0;
        GV(u16, 0x800CF9BE) = 0xC;
        FV_BLINK = 0;
        return;

    case 1: {   /* the list: the cursor blinks while up/down is held */
        if (BTN(0x800C56A8u, 0x800) || BTN(0x800C56C0u, 0x40)) {
            u32 d = FV_DELAY;
            if (d != 0) { FV_DELAY = d - 1; return; }
            u32 b = FV_BLINK ^ 1;
            u32 r = sp + 0x20;
            GS(u16, r + 4) = 0x10;
            u16 x = GV(u16, 0x800CF338 + 0xC), y = GV(u16, 0x800CF338 + 0xE);
            GS(u16, r + 6) = 3;
            FV_BLINK = b;
            GS(u16, r) = x;
            GS(u16, r + 2) = y;
            JAL(0x800C5740u, F_LOAD_IMAGE, r, 0x800CF8C8u + b * 0x60, b, x);
            FV_DELAY = 8;
        } else {
            FV_DELAY = 0;
        }
        if (BTN(0x800C5760u, 0x7FF) && !BTN_PREV(0x800C5778u, 0x7FF)) {
            close_viewer(0x800C578Cu);
            return;
        }
        if (!BTN(0x800C57A4u, 0x20)) return;
        if (BTN_PREV(0x800C57BCu, 0x20)) return;
        if (FV_BLINK != 0) { close_viewer(0x800C578Cu); return; }
        if (FV_COUNT == 0) return;
        FV_MODE = 2;
        GV(u16, 0x800CF988) = 0;
        GV(u16, 0x800CF98A) = 1;
        return;
    }

    case 2: {   /* the models turn in */
        s32 a = OBJ_ANGLE + 0x1E;
        OBJ_ANGLE = a;
        if (a >= 0x1E) {
            OBJ_ANGLE = 0x1E;
            FV_MODE = 4;
        }
        GV(u32, 0x800CF7C8 + 0x60) = 0;
        return;
    }

    case 4: {   /* the models to their rest pose, shown */
        ST_OBJ_ON = 1;
        if (GV(s32, 0x800CF878) > 0) {
            for (u32 i = 0;;) {
                model_turn(OBJ_MODEL(i), GV(u16, OBJ_MODEL(i) + 0x18), 0x10, 0x1F);
                i++;
                if (!((s32)i < GV(s32, 0x800CF878))) break;
                LOOP_POLL(0x800C58D4);
            }
        }
        GV(u8, 0x800CF7CD) = 1;
        FV_MODE = 5;
        GV(u16, 0x800CF9BA) = 0;
        GV(u16, 0x800CF9B8) = 0;
        FV_WAIT_U = 0;
        sound(0x800C5920u, 6);
        ST_SOUND_ON = 0;
        ST_SOUND = 0;
        JAL(0x800C5934u, F_FILES_MODELS, A0, A1, A2, A3);
        return;
    }

    case 5:
        if (GV(u16, OBJ_MODEL(1) + 0xA) != 0) return;
        FV_MODE = 6;
        return;

    case 6: {   /* pick a file */
        if (FV_WAIT != 0) FV_WAIT_U = (u16)(FV_WAIT_U - 1);
        if (GV(u16, OBJ_MODEL(2) + 0xA) != 0) return;
        if (GV(u16, OBJ_MODEL(0) + 0xA) != 0) return;
        if (BTN(0x800C59C0u, 0x7FF) && !BTN_PREV(0x800C59D8u, 0x7FF)) goto back;
        if (BTN(0x800C5A04u, 0x20) && !BTN_PREV(0x800C5A1Cu, 0x20)) {
            s16 c = FV_CURSOR;
            if (c == 0 || c == FV_COUNT + 1) goto back;
            FV_MODE = 7;
            JAL(0x800C5A5Cu, F_SOUND_STOP, A0, A1, A2, A3);
            sound(0x800C6040u, 2);
            return;
        }
        if (GV(u16, OBJ_MODEL(2) + 0xA) != 0) return;
        if (GV(u16, OBJ_MODEL(0) + 0xA) != 0) return;
        {
            s16 c = FV_CURSOR;
            if (c != 0 && c != FV_COUNT + 1) {
                /* its name */
                u32 t = GV(u32, FILE_NAMES + (u32)GV(u8, FILE_LIST + (u32)(s32)c - 1) * 4);
                u32 d = TEXT_BUF;
                if (GV(s16, t) != -1) {
                    for (;;) {
                        u16 ch = GV(u16, t);
                        t += 2;
                        GV(u16, d) = ch;
                        d += 2;
                        if (GV(s16, t) == -1) break;
                        LOOP_POLL(0x800C5B10);
                    }
                }
                GV(u16, d) = 0xFFFF;
                JAL(0x800C5B24u, F_SOUND_STOP, A0, A1, A2, A3);
                GS(u32, sp + 0x10) = 4;
                GS(u32, sp + 0x14) = 0;
                GS(u32, sp + 0x18) = 0;
                JAL(0x800C5B4Cu, F_SHOW_TEXT, TEXT_BUF, (u32)-0x3C, 0x2A, 0xC);
                GV(u32, 0x800BA9F4) = 1;
                GV(u32, 0x800BA9F8) = 1;
                ST_SOUND_ON = (u32)-1;
                ST_SOUND = (u32)-1;
            }
        }
        if (BTN(0x800C5B8Cu, 0x40)) {           /* next file */
            if (FV_WAIT != 0) return;
            s16 c = FV_CURSOR;
            u16 cu = FV_CURSOR_U;
            if (c == 0 || c == FV_COUNT) {
                FV_CURSOR_U = (u16)(cu + 1);
            } else if (c != FV_COUNT + 1) {
                u32 m = OBJ_MODEL(2);
                model_turn(m, (u16)(GV(u16, m + 0x18) + 1), 0x10, 0x14);
                FV_CURSOR_U = (u16)(FV_CURSOR_U + 1);
                sound(0x800C5D30u, 6);
            }
            FV_WAIT_U = 8;
            return;
        }
        if (!BTN(0x800C5C54u, 0x800)) { FV_WAIT_U = 0; return; }
        if (FV_WAIT != 0) return;                /* previous file */
        {
            s16 c = FV_CURSOR;
            u16 cu = FV_CURSOR_U;
            if (c == 1 || c == FV_COUNT + 1) {
                FV_CURSOR_U = (u16)(cu - 1);
            } else if (c != 0) {
                u32 m = OBJ_MODEL(2);
                GV(u16, m + 0x16) = (u16)(GV(u16, m + 0x18) + 4);
                GV(u16, m + 0x14) = GV(u16, m + 0x16);
                GV(u8, m + 0xC) = GV(u8, m + 0x1A);
                GV(u16, m + 0x12) = 0;
                GV(u16, m + 0x10) = 0;
                GV(u16, m + 0xA) = 0x14;
                GV(u8, m + 0xD) = 0xF0;
                FV_CURSOR_U = (u16)(FV_CURSOR_U - 1);
                sound(0x800C5D30u, 6);
            }
        }
        FV_WAIT_U = 8;
        return;
    back:
        FV_MODE = 0xB;
        sound(0x800C6040u, 3);
        return;
    }

    case 7:     /* open the file */
        g_panelMode = 0x800;
        ST_OBJ_ON = 0;
        JAL(0x800C5D60u, 0x800C6594u, A0, A1, A2, A3);
        FV_MODE = 8;
        FV_WAIT_U = 8;
        GV(u32, 0x800CF9CC) = 0;
        return;

    case 9: {   /* the text after a file read directly: a header, then the
                 * file's name (lines of up to 0x2A characters), boxed to
                 * the longest line */
        u32 s = 0x800CF380u, d = TEXT_BUF;
        if (GV(s16, s) != -1) {
            for (;;) {
                u16 ch = GV(u16, s);
                s += 2;
                GV(u16, d) = ch;
                d += 2;
                if (GV(s16, s) == -1) break;
                LOOP_POLL(0x800C5DB4);
            }
        }
        GV(u16, d) = 0xFFFE;
        d += 2;
        GV(u16, d) = 0xFC01;
        d += 2;
        u32 t = GV(u32, FILE_NAMES + (FV_DIRECT - 1) * 4);
        s32 longest = 0;
        if (GV(s16, t) != -1) {
            u32 len = 0;
            s16 c = GV(s16, t);
            for (;;) {
                if (c != -1) {
                    for (;;) {
                        if (c == -2) t += 2;
                        u16 ch = GV(u16, t);
                        t += 2;
                        len++;
                        GV(u16, d) = ch;
                        d += 2;
                        if (!((s16)len < 0x2A)) break;
                        LOOP_POLL(0x800C5E50);
                        c = GV(s16, t);
                        if (c == -1) break;
                    }
                }
                if (longest < (s16)len) longest = (s16)len;
                c = GV(s16, t);
                len = 0;
                if (c == -1) break;
                LOOP_POLL(0x800C5E78);
            }
        }
        GV(u16, d) = 0x79;
        GV(u16, d + 2) = 0xFFFF;
        if (longest < 0xE) longest = 0xE;
        GS(u32, sp + 0x10) = 5;
        GS(u32, sp + 0x14) = 0;
        GS(u32, sp + 0x18) = 0;
        JAL(0x800C5ECCu, F_SHOW_TEXT, TEXT_BUF, (u32)((s32)(0u - ((u32)longest << 18)) >> 16), 0x30, 0x1A);
        GV(u32, 0x800BA9F8) = 1;
        GV(u32, 0x800BA9F4) = 1;
        FV_MODE = 0xA;
        FV_DELAY = 0;
        return;
    }

    case 10: {  /* a button: back to the list, or (read directly, the text
                 * done) close the screen */
        if ((BTN(0x800C5F00u, 0x20) && !BTN_PREV(0x800C5F18u, 0x20)) ||
            (BTN(0x800C5F30u, 0x800) && !BTN_PREV(0x800C5F48u, 0x800)) ||
            (BTN(0x800C5F60u, 0x40) && !BTN_PREV(0x800C5F78u, 0x40))) {
            if (FV_DIRECT == 0) goto to_list;
        } else {
            if (FV_DIRECT == 0) goto to_list;
            if (GV(u32, 0x800BA9F4) != 0) { FV_DELAY = 0; return; }
            if (FV_DIRECT == 0) goto to_list;
        }
        ST_STATE = 3;
        JAL(0x800C5FC8u, F_SOUND_STOP, A0, A1, A2, A3);
        g_hudFrame = 0;
        GV(u16, g_curPanel + 0x1C) = 1;
        sound(0x800C6040u, 3);
        return;
    to_list:
        FV_MODE = 6;
        g_panelMode = 0x40;
        ST_OBJ_ON = 1;
        FV_WAIT_U = 8;
        ST_SOUND_ON = 0;
        ST_SOUND = 0;
        GV(u32, 0x800CF9CC) = 1;
        sound(0x800C6040u, 3);
        return;
    }

    case 11: {  /* the models turn back */
        for (u32 i = 0;;) {
            s32 k = (s16)i;
            if (k == 2) {
                u32 m = OBJ_MODEL(2);
                GV(u16, m + 0x16) = (u16)(GV(u16, m + 0x18) + 1);
            }
            u32 m = OBJ_MODEL(k);
            if (k == 1) GV(u16, m + 0x16) = (u16)(GV(u16, OBJ_MODEL(1) + 0x18) + 4);
            else GV(u16, m + 0x16) = (u16)(GV(u16, OBJ_MODEL(k) + 0x18) + 2);
            m = OBJ_MODEL(k);
            GV(u16, m + 0x14) = GV(u16, m + 0x16);
            GV(u8, OBJ_MODEL(k) + 0xC) = GV(u8, OBJ_MODEL(k) + 0x1A);
            i++;
            GV(u16, OBJ_MODEL(k) + 0x12) = 0;
            GV(u16, OBJ_MODEL(k) + 0x10) = 0;
            GV(u16, OBJ_MODEL(k) + 0xA) = 0x1E;
            GV(u8, OBJ_MODEL(k) + 0xD) = 0xF0;
            if (!((s16)i < 3)) break;
            LOOP_POLL(0x800C6144);
        }
        GV(u8, 0x800CF7CD) = 0;
        FV_MODE = 0xC;
        sound(0x800C6174u, 6);
        JAL(0x800C617Cu, F_SOUND_STOP, A0, A1, A2, A3);
        return;
    }

    case 12:
        if (GV(u16, OBJ_MODEL(0) + 0xA) != 0) return;
        GS(u16, sp + 0x24) = 0x10;
        GS(u16, sp + 0x26) = 1;
        FV_WAIT_U = 0;
        FV_MODE = 0xD;
        GS(u16, sp + 0x20) = GV(u16, 0x800CF338 + 0xC);
        GS(u16, sp + 0x22) = GV(u16, 0x800CF338 + 0xE);
        return;

    case 13: {  /* the models turn out; back to the list */
        s32 a = OBJ_ANGLE - 0x1E;
        OBJ_ANGLE = a;
        if (a < -0xF9) {
            OBJ_ANGLE = -0xFA;
            FV_MODE = 1;
        }
        GV(u32, 0x800CF7C8 + 0x60) = 0;
        return;
    }

    default:    /* 3, 8 */
        return;
    }
}

/* 800C6594: open the file: load its pages, the first one, and the panels:
 * the file's cover (800C7174), the page (800C6C40), the two arrows
 * (800C71B8) and the page mark (800C7334). */
void n_files_Open(void) {
    FRAME(0x38);
    SAVE_RA(0x30);
    SAVE_SREG(5, 0x2C);
    SAVE_SREG(4, 0x28);
    SAVE_SREG(3, 0x24);
    SAVE_SREG(2, 0x20);
    SAVE_SREG(1, 0x1C);
    SAVE_SREG(0, 0x18);
    u32 s4 = R[20], s5 = R[21];          /* the original's registers when a panel isn't made */
    u32 f = current_file();
    u32 f3 = f * 3;
    GS(u32, sp + 0x10) = FILE_BUF;
    JAL(0x800C6618u, F_FILE_LOAD, 0x800C3E9Cu, 0x80058564u, 0x800CEDA0u, f3 + 0x28);
    JAL(0x800C6630u, F_DECODE, GV(u32, FILE_BUF + 4) + FILE_BUF, IMAGE_BUF, A2, A3);
    JAL(0x800C6638u, F_LOAD_TIM, IMAGE_BUF, A1, A2, A3);
    FV_PAGE = 1;
    FV_PAGES = GV(u8, FILE_PAGES + f3);
    JAL(0x800C6668u, 0x800C6A08u, 1, 0, A2, A3);

    u32 n = JAL(0x800C6670u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {             /* the cover */
        s4 = n;
        u32 p = pnl_s((s16)n);
        g_curPanel = p;
        GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 0x804;
        u32 id = GV(u8, FILE_LIST + (u32)(s32)FV_CURSOR - 1);
        u32 spr = 0x800CF47Cu + (u32)GV(u8, 0x800CF4B8u + id) * 20;
        JAL(0x800C66F8u, F_SET_SPRITE, p, spr, A2, A3);
        p = g_curPanel;
        GV(u16, p + 0x10) = (u16)(0u - (((u32)GV(u16, p + 8) >> 1) << 4));
        u16 y;
        if (id == 0xC) y = (u16)-0xB00;
        else if ((s32)id < 2) y = (u16)(0u - (((u32)GV(u16, p + 0xA) >> 1) << 4));
        else y = (u16)-0xD80;
        GV(u16, p + 0x12) = y;
        p = g_curPanel;
        GV(u16, p + 0x18) = 4;
        GV(u32, p + 0x3C) = 0x800C7174u;
    }
    n = JAL(0x800C6770u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {             /* the page */
        u32 p = pnl_s((s16)n);
        s5 = n;
        g_curPanel = p;
        GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 0x804;
        JAL(0x800C67C0u, F_SET_SPRITE, p, 0x800CF36Cu, A2, A3);
        p = g_curPanel;
        GV(u16, p + 0xC) = GV(u16, FV_TIM + 0x10);
        GV(u16, p + 0xE) = GV(u16, FV_TIM + 0x12);
        GV(u16, p + 8) = FV_PAGE_W;
        GV(u16, p + 0xA) = FV_PAGE_H;
        GV(u16, p + 0x10) = (u16)(0u - ((u32)div_pow2((s16)FV_PAGE_W, 1) << 4));
        u16 h = FV_PAGE_H;
        GV(u16, p + 0x18) = 3;
        GV(u32, p + 0x3C) = 0x800C6C40u;
        GV(u16, p + 0x22) = 0;
        GV(u16, p + 0x28) = (u16)s4;
        GV(u16, p + 0x12) = (u16)(0u - ((u32)div_pow2((s16)h, 1) << 4));
    }
    for (u32 i = 0;;) {             /* the arrows */
        n = JAL(0x800C6884u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)n != -1) {
            u32 p = pnl_s((s16)n);
            g_curPanel = p;
            JAL(0x800C68B0u, F_SET_SPRITE, p, 0x800A6050u, A2, A3);
            p = g_curPanel;
            s32 k = (s16)i;
            GV(u16, p + 0x14) = 0x10;
            GV(u16, p + 8) = 0x10;
            GV(u16, p + 0x16) = 8;
            GV(u16, p + 0xA) = 8;
            GV(u16, p + 0x10) = (u16)((u32)k * 0x1140 - 0x940);
            GV(u16, p + 0x12) = (u16)-0x180;
            u16 u4 = GV(u16, p + 4), v6 = GV(u16, p + 6);
            GV(u16, p + 0x24) = (u16)s5;
            GV(u32, p + 0x3C) = 0x800C71B8u;
            GV(u16, p + 0x18) = 0;
            u16 u = (u16)(u4 + (u32)k * 24 + 0x38);
            GV(u16, p + 4) = u;
            GV(u16, p + 6) = (u16)(v6 + 0x60);
            GV(u16, p + 0x22) = u;
            GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 0x800;
        }
        i++;
        if (!((s16)i < 2)) break;
        LOOP_POLL(0x800C6950);
    }
    n = JAL(0x800C6960u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {             /* the page mark */
        u32 p = pnl_s((s16)n);
        g_curPanel = p;
        JAL(0x800C6998u, F_SET_SPRITE, p, 0x800A5E5Cu, A2, A3);
        p = g_curPanel;
        u16 e = GV(u16, p + 0xE), w = GV(u16, p + 0x14);
        GV(u32, p + 0x3C) = 0x800C7334u;
        GV(u16, p + 0x12) = (u16)-0x180;
        u16 h = GV(u16, p + 0x16);
        GV(u16, p + 0x24) = (u16)s5;
        GV(u16, p + 0x18) = 0;
        GV(u16, p + 0x14) = (u16)(w >> 1);
        GV(u16, p + 0x22) = e;
        GV(u16, p + 0x16) = (u16)(h >> 1);
        GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 0x808;
    }
}

/* 800C6A08 (page, dir): show a page: clear the page image, load the part
 * of the file that has it when it isn't the one loaded (dir: turning
 * back), decode the page, its size, into VRAM. */
void n_files_LoadPage(void) {
    FRAME(0x38);
    SAVE_SREG(3, 0x2C);
    SAVE_RA(0x30);
    SAVE_SREG(2, 0x28);
    SAVE_SREG(1, 0x24);
    SAVE_SREG(0, 0x20);
    u32 page = A0, dir = A1;
    u32 r = sp + 0x18;
    s32 w = GV(s16, 0x800CF36C + 8);
    GS(u16, r) = GV(u16, 0x800CF36C + 4);
    GS(u16, r + 2) = GV(u16, 0x800CF36C + 6);
    GS(u16, r + 4) = (u16)div_pow2(w, 2);
    GS(u16, r + 6) = GV(u16, 0x800CF36C + 0xA);
    JAL(0x800C6A70u, F_CLEAR_IMAGE, r, 0, 0, 0);
    u32 f = current_file();
    if (dir != 0 || page != 1) {
        u32 f3 = f * 3, k = 0;
        for (;;) {
            if (page - 1 == (u32)GV(u8, FILE_PAGES + f3 + k + 1) - dir) break;
            k++;
            if (!((s32)k < 2)) break;
            LOOP_POLL(0x800C6AF8);
        }
        if (k != 2) {
            GS(u32, sp + 0x10) = FILE_BUF;
            JAL(0x800C6B3Cu, F_FILE_LOAD, 0x800C3E9Cu, 0x80058564u, 0x800CEDA0u, f3 + k - dir + 0x29);
            JAL(0x800C6B54u, F_DECODE, GV(u32, FILE_BUF + 4) + FILE_BUF, IMAGE_BUF, A2, A3);
            JAL(0x800C6B5Cu, F_LOAD_TIM, IMAGE_BUF, A1, A2, A3);
        }
    }
    /* the page within the part loaded */
    u32 f3 = f * 3;
    for (s32 k = 2;;) {
        u32 start = GV(u8, FILE_PAGES + f3 + (u32)k);
        k--;
        if (!((s32)(page - 1) < (s32)start)) { page -= start; break; }
        if (k <= 0) break;
        LOOP_POLL(0x800C6B90);
    }
    JAL(0x800C6BC0u, F_DECODE, GV(u32, FILE_BUF + page * 4 + 4) + FILE_BUF, IMAGE_BUF, A2, A3);
    JAL(0x800C6BD8u, F_TIM_INFO, IMAGE_BUF + 4, FV_TIM, A2, A3);
    FV_PAGE_W = (u16)((u32)GV(u16, FV_TIM + 8) << 2);
    FV_PAGE_H = GV(u16, FV_TIM + 0xA);
    JAL(0x800C6BFCu, F_LOAD_TIM, IMAGE_BUF, A1, A2, A3);
    GS(u16, r + 4) = 0x10;
    GS(u16, r + 6) = 1;
    GS(u16, r) = GV(u16, FV_TIM + 0x10);
    GS(u16, r + 2) = GV(u16, FV_TIM + 0x12);
    JAL(0x800C6C24u, F_DRAW_SYNC, 0, A1, A2, A3);
}

/* Page turned: the page's state (+0x22) to `to`, unless at that end. */
static void page_click(s32 old) {
    if (old != 0 && old != (s32)FV_PAGES + 1) sound(0x800C6F08u, 6);
    GV(u16, g_curPanel + 0x26) = 0;
}

/* 800C6C40: panel callback: the page (states in +0x22: 0 input, 1 slide
 * out left, 2 slide in from the right, 3 out right, 4 in from the left;
 * +0x26 counts frames off screen); up/down turn pages, cancel (or a mouse's
 * two buttons, or OK past the ends) ends reading (state 9 or 10). Freed
 * when not reading. */
void n_files_PageTurn(void) {
    FRAME(0x20);
    SAVE_RA(0x1C);
    SAVE_SREG(2, 0x18);
    SAVE_SREG(1, 0x14);
    SAVE_SREG(0, 0x10);
    u32 old = R[18];                     /* the original's s2 when cancelled */
    if (FV_MODE - 7 >= 3 || (s16)g_panelMode != 0x800) {
        GV(u16, g_curPanel + 0x1C) = 1;
        return;
    }
    if (ST_CANCEL != 0) goto done;
    {
        u32 p = g_curPanel;
        GV(u16, p + 0xC) = GV(u16, FV_TIM + 0x10);
        GV(u16, p + 0xE) = GV(u16, FV_TIM + 0x12);
        s32 st = GV(s16, p + 0x22);
        if ((u32)st >= 5) return;
        LOOP_POLL(0x800C6CE8);
        switch (st) {
        case 0:
            if (FV_WAIT != 0) FV_WAIT_U = (u16)(FV_WAIT_U - 1);
            old = FV_PAGE;
            if (FV_WAIT == 0) {
                if (BTN(0x800C6D28u, 0x40)) {
                    u32 pg = FV_PAGE;
                    if ((s32)pg < (s32)(FV_PAGES + 1)) { FV_PAGE = pg + 1; FV_WAIT_U = 8; }
                }
                if (BTN(0x800C6D68u, 0x800)) {
                    u32 pg = FV_PAGE;
                    if (!((s32)pg < 2)) { FV_PAGE = pg - 1; FV_WAIT_U = 8; }
                }
            }
            if (BTN(0x800C6DACu, 0x7FF) && !BTN_PREV(0x800C6DC4u, 0x7FF)) goto done;
            if (BTN(0x800C6DDCu, 0x4FF) && !BTN_PREV(0x800C6DF4u, 0x4FF)) goto done;
            if (BTN(0x800C6E0Cu, 0x20) && !BTN_PREV(0x800C6E24u, 0x20)) {
                u32 pg = FV_PAGE;
                if (pg == 0 || pg == FV_PAGES + 1) goto done;
            }
            goto turn;
        case 1:
            if (GV(s16, p + 0x10) < -0x19FF) {
                u16 c = (u16)(GV(u16, p + 0x26) + 1);
                GV(u16, p + 0x26) = c;
                if ((s16)c != 3) return;
                JAL(0x800C6F84u, 0x800C6A08u, FV_PAGE, 0, A2, A3);
                p = g_curPanel;
                u16 s = GV(u16, p + 0x22);
                GV(u16, p + 0x10) = 0x1A00;
                GV(u16, p + 0x22) = (u16)(s + 1);
                JAL(0x800C7064u, 0x800C70BCu, A0, A1, A2, A3);
                return;
            } else {
                u16 x = (u16)(GV(u16, p + 0x10) - 0x400);
                GV(u16, p + 0x10) = x;
                if ((s16)x < -0x19FF) GV(u16, p + 0x10) = (u16)-0x1A00;
                return;
            }
        case 2: {
            u16 x = (u16)(GV(u16, p + 0x10) - 0x400);
            GV(u16, p + 0x10) = x;
            if ((s16)x < -0x7FF) {
                GV(u16, p + 0x22) = 0;
                GV(u16, p + 0x10) = (u16)-0x800;
            }
            return;
        }
        case 3:
            if (!(GV(s16, p + 0x10) < 0x1A00)) {
                u16 c = (u16)(GV(u16, p + 0x26) + 1);
                GV(u16, p + 0x26) = c;
                if ((s16)c != 3) return;
                JAL(0x800C7040u, 0x800C6A08u, FV_PAGE, 1, A2, A3);
                p = g_curPanel;
                GV(u16, p + 0x10) = (u16)-0x1A00;
                u16 s = GV(u16, p + 0x22);
                GV(u16, p + 0x24) = 8;
                GV(u16, p + 0x22) = (u16)(s + 1);
                JAL(0x800C7064u, 0x800C70BCu, A0, A1, A2, A3);
                return;
            } else {
                u16 x = (u16)(GV(u16, p + 0x10) + 0x400);
                GV(u16, p + 0x10) = x;
                if (!((s16)x < 0x1A00)) GV(u16, p + 0x10) = 0x1A00;
                return;
            }
        case 4: {
            u16 x = (u16)(GV(u16, p + 0x10) + 0x400);
            GV(u16, p + 0x10) = x;
            if (!((s16)x < -0x800)) {
                GV(u16, p + 0x22) = 0;
                GV(u16, p + 0x10) = (u16)-0x800;
            }
            return;
        }
        }
    }
done:
    FV_MODE = FV_DIRECT != 0 ? 9 : 0xA;
turn: {
        u32 pg = FV_PAGE;
        if (pg == old) return;
        if (pg == 0) return;
        u32 n = FV_PAGES;
        if (pg == n + 1) return;
        if ((s32)old < (s32)pg) {
            if (pg != 1) GV(u16, g_curPanel + 0x22) = 1;
        } else {
            if (pg != n) GV(u16, g_curPanel + 0x22) = 3;
        }
        page_click((s32)old);
    }
}

/* 800C70BC: file 4's pages 5 and 6 are drawn from other places in VRAM. */
void n_files_PageLayout(void) {
    if (current_file() != 4) { V0 = 4; return; }
    u32 pg = FV_PAGE;
    if (pg - 5 >= 2) { V0 = 0; return; }
    u32 p = g_curPanel;
    u16 y;
    if (pg == 6) {
        GV(u16, p) = 1;
        GV(u16, FV_TIM + 0x10) = 0x140;
        GV(u16, p + 0xC) = 0x140;
        y = 0x1F9;
    } else {
        GV(u16, p) = 0;
        GV(u16, FV_TIM + 0x10) = 0x150;
        GV(u16, p + 0xC) = 0x150;
        y = 0x1E9;
    }
    GV(u16, FV_TIM + 0x12) = y;
    GV(u16, p + 0xE) = y;
    V0 = FV_TIM;
}

/* 800C7174: panel callback: freed when not reading (states 7 to 9). */
void n_files_HidePanel(void) {
    if (FV_MODE - 7 < 3 && (s16)g_panelMode == 0x800) { V0 = 0x800; return; }
    GV(u16, g_curPanel + 0x1C) = 1;
    V0 = 1;
}

/* 800C71B8: panel callback: a page arrow (left, or right when x > 0),
 * hidden at that end or while the page moves (+0x24 the page panel), for
 * the pad type; it blinks. Freed when not reading (7, 8). */
void n_files_Arrow(void) {
    if (FV_MODE - 7 >= 2 || (s16)g_panelMode != 0x800) {
        GV(u16, g_curPanel + 0x1C) = 1;
        V0 = 1;
        return;
    }
    u32 p = g_curPanel;
    u16 fl = (u16)(GV(u16, p + 0x1C) | 4);
    GV(u16, p + 0x1C) = fl;
    if (GV(s16, p + 0x10) > 0) {
        u32 pg = pnl_s(GV(s16, p + 0x24));
        if (!((s32)FV_PAGE < (s32)FV_PAGES) && GV(s16, pg + 0x22) == 0) GV(u16, p + 0x1C) = fl & 0xFFFB;
    } else {
        if ((s32)FV_PAGE < 2) GV(u16, p + 0x1C) = fl & 0xFFFB;
    }
    p = g_curPanel;
    s16 x = GV(s16, p + 0x10);
    if (g_padTypeBit == 0x200000) {
        GV(u16, p + 8) = 0x10;
        if (x > 0) { GV(u16, p + 0x22) = 0x50; GV(u16, p + 4) = 0x50; GV(u16, p + 0x10) = 0x880; }
    } else {
        GV(u16, p + 8) = 8;
        if (x > 0) { GV(u16, p + 0x22) = 0x58; GV(u16, p + 4) = 0x58; GV(u16, p + 0x10) = 0x900; }
    }
    p = g_curPanel;
    u16 c = (u16)(GV(u16, p + 0x26) + 1);
    GV(u16, p + 0x26) = c;
    if (c & 8) V0 = (u32)GV(u16, p + 0x22) + 0x30;
    else V0 = GV(u16, p + 0x22);
    GV(u16, p + 4) = (u16)V0;
}

/* 800C7334: panel callback: the mark on the last page (shown while the
 * page is still). Freed when not reading (7, 8). */
void n_files_PageMark(void) {
    s32 pm;
    if (FV_MODE - 7 >= 2 || (pm = (s16)g_panelMode) != 0x800) {
        GV(u16, g_curPanel + 0x1C) = 1;
        V0 = 1;
        return;
    }
    u32 p = g_curPanel;
    u32 pg = pnl_s(GV(s16, p + 0x24));
    u32 n = FV_PAGES, cur = FV_PAGE;
    if ((s32)(n - 1) < (s32)cur && GV(s16, pg + 0x22) == 0) {
        GV(u16, p + 0x10) = (u16)pm;
        GV(u16, p + 0x1C) = GV(u16, p + 0x1C) | 4;
        V0 = GV(u16, p + 0x22);
        if (cur == n) V0++;
        GV(u16, p + 0xE) = (u16)V0;
        return;
    }
    p = g_curPanel;
    V0 = GV(u16, p + 0x1C) & 0xFFFB;
    GV(u16, p + 0x1C) = (u16)V0;
}

/* 800C741C (item): a file picked up: add it to the list (table at
 * 0x800CF5D8: item, file pairs, 0xFF ends) if it isn't there; its number
 * + 1, or 1 for an item that isn't a file. */
void n_files_Register(void) {
    u32 e = 0x800CF5D8u;
    for (;;) {
        u32 v = GV(u8, e);
        if (v == 0xFF) { V0 = 1; return; }
        if (v == A0) break;
        e += 2;
        LOOP_POLL(0x800C7448);
    }
    u32 p = FILE_LIST;
    for (;;) {
        u32 v = GV(u8, p);
        if (v == 0xFF) {
            u8 f = GV(u8, e + 1);
            GV(u8, p + 1) = 0xFF;
            GV(u8, p) = f;
            V0 = (u32)GV(u8, e + 1) + 1;
            return;
        }
        if (v == GV(u8, e + 1)) { V0 = v + 1; return; }
        p++;
        LOOP_POLL(0x800C7478);
    }
}
