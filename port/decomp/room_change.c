/* Changing rooms (TEST overlay): the step machine that runs every frame
 * and, when a door is taken, fades out, loads the next room, places the
 * player and fades back in.
 *
 * The request lives at 0x800B3D28:
 *   +4  s16 step (0 = idle; 1-14 below), +6 s16 room file index,
 *   +8  door list (0x18-byte entries: s16 id, s16 room code (negative:
 *       -(stage << 8 | room)), ..., +8 player placement), +C s16 the door
 *       taken, +E s16 number of doors.
 * 0x800B3D30 (= +8) points at the entry being entered. A negative room
 * code holds the destination: room in the low byte, stage + 1 in the
 * high byte; it goes to 0x800B3E20 / 0x800B4BA0 (the room and stage being
 * loaded; the current ones are 0x800B3F08 / 0x800A866C).
 *
 * Steps: 1 fade out; 2 wait for it, blank the display; 3 load the room;
 * 4-5 settle; 5 start the fade in; 6 set up the room's models while it
 * fades; 7-8 room entry events; 9 room init; 10 wait for the room script;
 * 11 wait for the CD; 12 wait for the streamed data; 13 the final fade;
 * 14 wait for it, the player can act again.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "game_stack.h"
#include "poll.h"

void n_room_ChangeStep(void);
void n_room_DecodeDestination(void);
void n_room_EntryEvents(void);
void n_room_MusicChanges(void);
void n_room_Load(void);
void n_room_PlacePlayer(void);
void n_room_StreamStep(void);
void n_op_Door(void);

#define REQ             0x800B3D28u
#define ROOM_STEP       GV(s16, REQ + 4)
#define REQ_FILE        GV(s16, REQ + 6)
#define REQ_DOORS       GV(u32, REQ + 8)
#define REQ_DOOR        GV(s16, REQ + 0xC)
#define REQ_NDOORS      GV(s16, REQ + 0xE)
#define g_doorEntry     GV(u32, 0x800B3D30)
#define g_loadRoom      GV(s32, 0x800B3E20)
#define g_loadStage     GV(s32, 0x800B4BA0)
#define g_curStage      GV(s32, 0x800A866C)
#define g_curRoom       GV(s32, 0x800B3F08)
#define g_fadeState     GV(u16, 0x800A55A2)   /* screen_fade.c */
#define g_stageData     GV(u32, 0x80010050)
#define ROOM_RECORD_OFF 0x2D73C               /* the room record in the stage data */
#define g_streamState   GV(s32, 0x800A0CC0)   /* room_load.c */
#define g_scriptBusy    GV(s32, 0x800A0CBC)

#define F_FADE_IN       0x80090B90u
#define F_FADE_OUT      0x80090BF0u
#define F_SET_DISP_MASK 0x800352DCu
#define F_INIT_MODELS   0x800927D8u
#define F_LOAD_FILE     0x800115C0u           /* (dir, name, table, index, dest on stack) */

/* 8008FF94: decode the entry's destination into the room / stage to load
 * (0x800B3E20 / 0x800B4BA0) and its secondary code (+2) into 0x800A8614 /
 * 0x800B3D44. */
void n_room_DecodeDestination(void) {
    s16 v = GV(s16, g_doorEntry + 6);
    if (v < 0) {
        u32 n = (u32)-(u32)GV(u16, g_doorEntry + 6);
        g_loadRoom = (s32)(n & 0xFF);
        g_loadStage = (s32)((n >> 8) & 0xFF) - 1;
    } else {
        g_loadRoom = v;
    }
    v = GV(s16, g_doorEntry + 2);
    if (v < 0) {
        u32 n = (((u32)-(u32)GV(u16, g_doorEntry + 2)) >> 8) & 0xFF;
        GV(u32, 0x800A8614) = n;
        GV(u32, 0x800B3D44) = n;
    } else {
        GV(u32, 0x800A8614) = 0;
        GV(u32, 0x800B3D44) = 0;
    }
}

/* 80090028: entering the room: rebuild the bullet row for handguns
 * (unless 0x800AFCB0 bit 2), then look the stage/room pair up in the
 * event table at 0x800A545C ({from stage, from room, to stage, to room},
 * ended by a negative stage) and run the transition event on a match.
 * Returns 1 if one ran. */
void n_room_EntryEvents(void) {
    FRAME(0x18);
    if (!(GV(u16, 0x800AFCB0) & 4) && GV(s16, g_currentWeaponAddr) < 4) {
        GCALL0(0x80095CDCu);                               /* free the bullet row */
        u32 w = g_currentWeaponAddr;
        GV(u16, w + 2) = GV(u16, 0x800A09C8u + (u32)(s32)GV(s16, w) * 8 + 4);   /* full magazine */
        GCALL3(0x80095B90u, 0x62, 0xD4, (s32)GV(s16, g_currentWeaponAddr + 2));
    }
    for (u32 e = 0x800A545Cu; GV(s32, e) >= 0; e += 16) {
        if (GV(s32, e) != g_curStage || GV(s32, e + 4) != g_curRoom ||
            GV(s32, e + 8) != g_loadStage)
            continue;
        if (GV(s32, e + 12) == g_loadRoom) {
            GCALL0(0x800717E8u);
            V0 = 1;
            return;
        }
    }
    V0 = 0;
}

/* 80090474: does the room change need the music changed? 1 unless the
 * door taken leads to a room whose music group (0x8008FB78) is the
 * current one's and the playing track (0x80062E7C) is the destination's
 * (0x8008FB40). */
void n_room_MusicChanges(void) {
    FRAME(0x20);
    u32 r = 1;
    if (REQ_DOOR != -1) {
        int found = 0;
        s32 n = REQ_NDOORS;
        u32 d = REQ_DOORS;
        for (s32 i = 0; i < n; i++, d += 0x18) {
            s16 code = GV(s16, d + 2);
            if (code < 0 ? REQ_DOOR == (s32)(((u32)-(s32)code) & 0xFF) : REQ_DOOR == code) {
                found = 1;
                break;
            }
        }
        if (found) {
            u32 e = g_doorEntry;
            u32 room = GV(u16, e + 6), stage;
            if (GV(s16, e + 6) < 0) {
                u32 c = (u32)-(u32)room;
                room = c & 0xFF;
                stage = ((c >> 8) & 0xFF) - 1;
            } else {
                stage = GV(u16, 0x800A866C);
            }
            if (GV(s16, 0x80062E78) != 0) {
                r = 1;
            } else {
                s32 st = (s16)stage, rm = (s16)room;
                s32 track = GV(s16, 0x80062E7C);
                if ((s32)GCALL2(0x8008FB40u, st, rm) == track) {
                    u32 a = GCALL2(0x8008FB78u, g_curStage, g_curRoom);
                    u32 b = GCALL2(0x8008FB78u, st, rm);
                    r = a != b;
                }
            }
        }
    }
    V0 = r;
}

/* 800905D0: load the next room: its file (into the top of the heap,
 * below 0x80150000), a fresh room record copied from the template at
 * 0x800B42D0, its models (file index + 1, at 0x80150000), then init. */
void n_room_Load(void) {
    FRAME(0x30);
    GCALL1(0x800255F8u, 0);
    GCALL0(0x800809B4u);
    u32 music = 0;
    n_room_MusicChanges();
    if (V0) music = GCALL2(0x80011EC4u, (s32)GV(s16, 0x80062E78), 0x800);   /* fade the music out */
    GCALL0(0x8009250Cu);                                   /* free the room objects */
    GV(u32, 0x800A8610) = 1;
    GV(u32, 0x800AF530) = 0;
    s32 idx = REQ_FILE * 2;
    u32 tbl = 0x800A42F4u;                                 /* sector table */
    u32 size = (GV(u32, tbl + (u32)(idx + 1) * 4) - GV(u32, tbl + (u32)idx * 4)) << 11;
    u32 dest = 0x80150000u - size;
    u32 room = g_stageData + ROOM_RECORD_OFF;
    GV(u32, 0x800BBCD8) = size;
    GS(u32, sp + 0x10) = dest;
    GCALL4(F_LOAD_FILE, 0x8006C934u, 0x8005857Cu, tbl, (u32)idx);
    for (u32 i = 0; i < 0x8C4; i += 4) GV(u32, room + i) = GV(u32, 0x800B42D0u + i);
    GV(u32, room + 0x890) = dest;
    GCALL3(0x8006E304u, 0, dest, room);
    GV(u32, room + 0x170) = 0x1000;                        /* scale 1.0 */
    GV(u32, room + 0x174) = 0x1000;
    GV(u32, room + 0x178) = 0x1000;
    GCALL2(F_OBJ_MATRIX, room + 0x180, room + 0x170);
    GV(u8, room + 0x2A) = 2;
    GV(u8, room + 0x29) = 1;
    for (s32 i = 0; i < (s32)GV(u8, room + 0x28); i++) GV(u8, room + (u32)i) = 1;
    GV(u16, room + 0x85C) = 0;
    s32 n = GV(s32, room + 0x1D0);                         /* parts */
    for (s32 i = 0; i < n; i++) {
        u32 p = GV(u32, room + 0x1D4 + (u32)i * 4);
        GV(u32, room + 0x274 + (u32)i * 4) = p + 0x18;
        GV(u16, p + 8) = 0xFFFF;
        GV(u16, p + 0x16) = GV(u16, p + 0x18);
        GV(u16, p + 0xA) = 0xFFFF;
        GV(u8, p + 0xC) = GV(u8, p + 0x1A);
        GV(u8, p + 0xD) = 0;
    }
    GS(u32, sp + 0x10) = 0x80150000u;
    GCALL4(F_LOAD_FILE, 0x8006C934u, 0x8005857Cu, tbl, (u32)(idx + 1));
    GCALL2(F_INIT_MODELS, 0x80150000u, (u32)-1);
    GV(u8, 0x80062E1C) = 0;
    GCALL0(0x8006D180u);
    GV(u32, 0x800AB524) = 1;
    GV(u32, 0x800B6084) = 0;
    if (music) {
        GCALL1(0x80011E74u, 0);
        GCALL1(0x80011E74u, 1);
        GCALL1(0x80011E24u, 0);
    }
}

/* 800908DC: place the player at the door taken: at the entry's explicit
 * position and rotation (+8 a pointer), or 80 units in front of the
 * matching door (type 4) in the room's door list (0x800AB52C, 0x24-byte
 * entries: s16 type, s16 id, s32 x, y, z, ..., +0x12 s16 facing),
 * turned to face away from it. */
void n_room_PlacePlayer(void) {
    FRAME(0x38);
    s32 n = REQ_NDOORS;
    u32 d = REQ_DOORS;
    u32 i = 0;
    int found = 0;
    if (n > 0) {
        s16 want = REQ_DOOR;
        for (;;) {
            if (GV(s16, d) == want) { found = 1; break; }
            i++;
            if ((s16)i >= n) break;
            d += 0x18;
        }
    }
    if (found) {
        u32 door = GV(u32, 0x800AB52C);
        u32 pos = GV(u32, d + 8);
        if ((s32)pos < 0) {                                /* explicit placement */
            GV(u32, g_player + 0x18) = GV(u32, pos);
            GV(u32, g_player + 0x1C) = GV(u32, pos + 4);
            GV(u32, g_player + 0x20) = GV(u32, pos + 8);
            GV(u16, g_player + 0x44) = GV(u16, pos + 0xC);
            GV(u16, g_player + 0x46) = GV(u16, pos + 0x10);
            GV(u16, g_player + 0x48) = GV(u16, pos + 0x14);
        } else {
            u32 c = i;
            if ((s16)c > 0) {
                s32 want = (s16)c;
                for (;;) {
                    if (GV(s16, door) == 4) {
                        c--;
                        if (GV(s16, door + 2) == want) break;
                    }
                    int more = (s32)(c << 16) > 0;
                    door += 0x24;
                    if (!more) break;
                }
            }
            GS(u16, sp + 0x10) = 0;
            GV(u16, g_player + 0x44) = 0;
            s32 a = GV(s16, door + 0x12) + 0x800;
            s16 yaw = (s16)(a % 4096);                     /* turn around */
            GS(s16, sp + 0x12) = yaw;
            GV(s16, g_player + 0x46) = yaw;
            GS(u16, sp + 0x14) = 0;
            GV(u16, g_player + 0x48) = 0;
            GCALL3(F_ROT_STEP, sp + 0x10, sp + 0x18, (u32)-0x50);
            GV(u32, g_player + 0x18) = GS(u32, sp + 0x18) * 20 + GV(u32, door + 4);
            GV(u32, g_player + 0x1C) = GS(u32, sp + 0x1C) * 20 + GV(u32, door + 8);
            GV(u32, g_player + 0x20) = GS(u32, sp + 0x20) * 20 + GV(u32, door + 0xC);
        }
        GV(u16, 0x800A865C) = 0;                           /* camera pitch level */
    }
    GV(u16, 0x800B4D6A) = GV(u16, 0x800A85DC);
}

/* 8009016C: the room change step machine, every frame (after the player
 * update 0x8007672C and the background loader). */
void n_room_ChangeStep(void) {
    FRAME(0x20);
    u32 room = g_stageData + ROOM_RECORD_OFF;
    GCALL0(0x8007672Cu);
    n_room_StreamStep();
    switch (ROOM_STEP) {
    case 1:
        GCALL2(F_FADE_OUT, 0, 0x10);
        ROOM_STEP = 2;
        break;
    case 2:
        if (GV(s16, 0x800A55A2) == 4) {                    /* faded out */
            ROOM_STEP = 3;
            GCALL1(F_SET_DISP_MASK, 0);
        }
        break;
    case 3:
        n_room_Load();
        ROOM_STEP = 4;
        break;
    case 4:
        ROOM_STEP = 5;
        break;
    case 5:
        ROOM_STEP = 6;
        GCALL2(F_FADE_IN, 0, 0x10);
        GCALL1(F_SET_DISP_MASK, 1);
        break;
    case 6:
        GCALL2(F_INIT_MODELS, 0x80150000u, (u32)-1);
        if (!(g_fadeState & 4)) ROOM_STEP = 7;
        break;
    case 7:
        ROOM_STEP = 8;
        break;
    case 8:
        n_room_DecodeDestination();
        n_room_EntryEvents();
        GCALL0(0x8008F6A8u);
        ROOM_STEP = 9;
        break;
    case 9: {
        GCALL0(0x8008DAD0u);
        GCALL1(0x80080F38u, (s32)REQ_FILE);
        ROOM_STEP = 10;
        s32 n = GV(s16, room + 0x1D0);
        g_scriptBusy = 1;
        for (s32 i = 0; i < n; i = (s16)(i + 1))
            GV(u8, GV(u32, room + 0x1D4 + (u32)i * 4) + 0xD) = 0x10;
        break;
    }
    case 10:
        if (!(GCALL0(0x8008FBE4u) & 0xFFFF)) ROOM_STEP = 11;
        break;
    case 11:
        if (GCALL1(0x80019268u, 0x10000) == 3) {
            GCALL1(0x8001F6A4u, 0x10);
            ROOM_STEP = 12;
        }
        break;
    case 12:
        if (g_scriptBusy == 0 && g_streamState == 0) {
            ROOM_STEP = 13;
            GV(u32, 0x800B3E18) = 1;
        }
        break;
    case 13: {
        u32 speed = 0x80;
        if (g_curStage == 0x10 && g_loadRoom == 0xA &&
            GCALL2(F_AREA_CHECK, 0x80062FC0u, 0x6B) == 0)
            speed = 0x10;
        GCALL2(F_FADE_IN, 0, speed);
        ROOM_STEP = 14;
        break;
    }
    case 14:
        if (!(g_fadeState & 4)) {
            ROOM_STEP = 0;
            if (GV(u32, 0x800BBB60) == 0) GV(u8, 0x80062DEC) = 1;   /* the player can act */
        }
        break;
    default:
        break;
    }
}

/* ---- the room script's door op ---- */

#define g_canAct        GV(u8, 0x80062DEC)
#define g_msgState      GV(s32, 0x800BA9F4)
#define g_msgOverMenu   GV(s32, 0x800BA9F8)
#define g_scriptThreads GV(s32, 0x800BBB58)   /* running script threads */
#define EVENT_FLAGS     0x80062FE0u
#define MSG_BUF         0x800A88C8u
#define ITEM_NAMES      0x800A6D5Cu           /* text per item */
#define F_FLAG_TEST     0x8008F64Cu           /* (flags, bit) */
#define F_FLAG_SET      0x8008F5F8u
#define F_DOOR_SOUND    0x80087AF0u           /* (door, which) */
#define F_FIND_KEY      0x80087A20u           /* (door flags): key item or -1 */
#define F_MSG_ADD_ENTRY 0x8007CE4Cu
#define F_MSG_SHOW      0x8007D6E0u

/* Copy a 0xFFFF-ended text to dst (the loop branch at pc); returns the
 * end of the copy, *n the halfwords copied. */
static u32 copy_text(u32 dst, u32 src, s32 *n, u32 pc) {
    u16 c = GV(u16, src);
    *n = 0;
    if (c == 0xFFFF) return dst;
    for (;;) {
        src += 2;
        GV(u16, dst) = c;
        dst += 2;
        c = GV(u16, src);
        (*n)++;
        if (c == 0xFFFF) return dst;
        LOOP_POLL(pc);
    }
}

/* 80087C28 (thread): script op: a door (thread +0x1C: the trigger, +2 the
 * door's index in the room's door list 0x800B3D30, 0x18 bytes each; +0x10
 * flags: bit 31 locked until event flag (low 24 bits) is set, either by
 * a key item (0x3F000000 says which) or from the other side (bit 30)).
 * Shows "locked" / "used the key" messages, then requests the room change
 * (0x800B3D28) and ends the thread. */
void n_op_Door(void) {
    FRAME(0x30);
    u32 th = A0;
    u32 door = g_doorEntry + (u32)(s32)GV(s16, GV(u32, th + 0x1C) + 2) * 0x18;
    s16 step = GV(s16, th + 4);
    if ((u32)(s32)step >= 7) { V0 = 1; return; }
    LOOP_POLL(0x80087C88);
    switch (step) {
    case 0: {
        s32 fl = GV(s32, door + 0x10);
        g_canAct = 0;
        if (fl < 0) {
            if (GCALL2(F_FLAG_TEST, EVENT_FLAGS, (u32)fl & 0xFFFFFF) != 0) goto open;
            if (GV(u32, door + 0x10) & 0x40000000) {           /* locked from the other side */
                GCALL2(F_DOOR_SOUND, door, 0);
                GS(u32, sp + 0x10) = 2;
                GS(u32, sp + 0x14) = 0;
                GS(u32, sp + 0x18) = 0;
                GCALL4(F_MSG_ADD_ENTRY, 0x800A2E84u, (u32)-0x80, 0x30, 0x10);
                g_msgState = 1;
                g_msgOverMenu = 0;
                GV(u16, th + 4) = 1;
                V0 = 1;
                return;
            }
            s32 key = (s32)GCALL1(F_FIND_KEY, GV(u32, door + 0x10));
            if (key < 0) {                                     /* no key: locked */
                GCALL2(F_DOOR_SOUND, door, 0);
                GCALL2(F_MSG_SHOW, 0x800A2ECCu, 0);
                GV(u16, th + 4) = 2;
                V0 = 1;
                return;
            }
            /* "used <key>" */
            s32 n;
            u32 d = copy_text(MSG_BUF, 0x800A2EF4u, &n, 0x80087D8C);
            GV(u16, d) = 0xFFFE; d += 2;
            GV(u16, d) = 0xFC01; d += 2;
            d = copy_text(d, GV(u32, ITEM_NAMES + (u32)key * 4), &n, 0x80087DE8);
            GV(u16, d) = 0x79; d += 2;
            GV(u16, d) = 0xFC00; d += 2;
            GV(u16, d) = 0xFE3C; d += 2;
            GV(u16, d) = 0xFE00; d += 2;
            GV(u16, d) = 0xFE20; d += 2;
            GV(u16, d) = 0xFF01;
            GV(u16, d + 2) = 0xFE00;
            GCALL2(F_DOOR_SOUND, door, 1);
            s32 x = n >= 16 ? (s32)(s16)(u16)((0u - ((u32)(n / 2) << 19)) >> 16) : -0x38;
            GS(u32, sp + 0x10) = 2;
            GS(u32, sp + 0x14) = 0;
            GS(u32, sp + 0x18) = 0;
            GCALL4(F_MSG_ADD_ENTRY, MSG_BUF, (u32)x, 0x30, 0x10);
            g_msgState = 1;
            g_msgOverMenu = 0;
            GV(u16, th + 4) = 3;
            V0 = 1;
            return;
        }
        if (GCALL2(F_FLAG_TEST, EVENT_FLAGS, (u32)fl & 0xFFFFFF) != 0) goto open;
        if (GV(u32, door + 0x10) & 0x40000000) {               /* unlocks from this side */
            GCALL2(F_DOOR_SOUND, door, 1);
            GCALL2(F_MSG_SHOW, 0x800A2F14u, 0);
            GV(u16, th + 4) = 3;
            V0 = 1;
            return;
        }
        goto open;
    }
    case 1:
    case 2:
        if (g_msgState == 0) GV(u16, th + 4) = 6;
        V0 = 1;
        return;
    case 3:
        if (g_msgState == 0) GV(u16, th + 4) = 5;
        V0 = 1;
        return;
    case 4:
        V0 = 1;
        return;
    case 5:
        goto open;
    default:                                                   /* 6: not opened */
        g_canAct = 1;
        GV(u16, th + 8) = (u16)(GV(u16, th + 8) & 0xFFFE);
        g_scriptThreads = g_scriptThreads - 1;
        V0 = 0;
        return;
    }
open:
    for (s32 i = 4;; i--) {                                    /* clear the alert slots */
        GV(s16, 0x800B3EA8u + (u32)i * 12) = -1;
        if (i - 1 < 0) break;
        LOOP_POLL(0x80087F20);
    }
    {
        u32 f = GV(u32, door + 0x10) & 0xFFFFFF;
        if (f != 0) GCALL2(F_FLAG_SET, EVENT_FLAGS, f);
    }
    ROOM_STEP = 1;
    GV(u16, REQ + 6) = GV(u16, door + 4);
    u16 c = GV(u16, door + 2);
    GV(u16, REQ + 0xC) = c;
    if ((s16)c < 0) GV(u16, REQ + 0xC) = (u16)((0u - c) & 0xFF);
    REQ_DOORS = door;
    GV(u16, th + 8) = (u16)(GV(u16, th + 8) & 0xFFFE);
    g_scriptThreads = g_scriptThreads - 1;
    V0 = 0;
}
