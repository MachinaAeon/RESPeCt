/* Picking up an item (TEST overlay): the room script's pick-up op, which
 * shows the item's model turning and growing in front of the player, the
 * "... got X" message, and puts the item where it belongs.
 *
 * The room's item models are listed at 0x800BBC18: 4 entries of 0x14
 * bytes (s16 id (-1 none), s16 item number, ..., +4 the model object
 * (address)). The script thread keeps the entry being picked up in +0x1C
 * (an address; anything not negative means the item has no model here).
 *
 * The item table (0x800A09C8, 8 bytes per item: u32 amount (u16 for the
 * other difficulty), u16 value, u8 kind flags at +6) decides where an item
 * goes (see inventory.c for the lists):
 *   1     list B (0x800B3DA0)
 *   0x10  carried, with its amount and value
 *   0x20  carried, adding to a box of the same item (or of its ammo, for
 *         items 13-15) if there is one: counts kept within 0-255
 *   8     a file to read: loads and shows it (the CD read takes frames)
 *   4     list C, with its value
 *   else  list C
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

void n_pickup_PlaceModel(void);
void n_op_PickUp(void);
void n_pickup_Step(void);

#define PICKUP_MODELS   0x800BBC18u   /* 4 x 0x14 bytes */
#define ITEM_MODEL      0x800B56F8u   /* the shown model (an object record) */
#define ITEM_MODEL_SRC  0x800B42D0u   /* its template */
#define ITEM_TABLE      0x800A09C8u   /* 8 bytes per item */
#define g_difficulty    GV(s32, 0x80062DF8)   /* 1: double amounts */
#define g_pickupActive  GV(s32, 0x800BBB64)   /* 0 while showing, -1 done */
#define g_msgState      GV(s32, 0x800BA9F4)
#define g_msgOverMenu   GV(s32, 0x800BA9F8)
#define g_dataBuf       GV(u32, 0x80010050)   /* the stage's data buffer */

#define F_ROT_STEP      0x8006F7A8u   /* (rot, &step, speed) */
#define F_DISTANCE      0x8007BDDCu   /* (a, b): 3D distance */
#define F_QS_TARGET     0x80078D38u
#define F_PICKUP_STEP   0x80086E9Cu
#define F_FLAG_SET      0x8008F5F8u   /* (flags, bit) */
#define F_MSG_CLOSE     0x8007CF08u
#define F_PICKUP_END    0x80078EB4u
#define F_MSG_ADD_ENTRY 0x8007CE4Cu
#define F_SND_CUE       0x800805F4u
#define F_FILE_LOAD     0x800115C0u
#define F_FILE_LOAD2    0x800116A4u

/* 80078DDC: copy the item model's template and place it 30 steps in
 * front of the player, at height -0x44C. */
void n_pickup_PlaceModel(void) {
    FRAME(0x30);
    copy_record_polled(ITEM_MODEL, ITEM_MODEL_SRC, 0x80078E28);
    GCALL3(F_ROT_STEP, PLAYER_ROT_ADDR, sp + 0x10, (u32)-0x64);
    GV(s32, ITEM_MODEL + 0x1BC) = -0x44C;
    GV(s32, ITEM_MODEL + 0x19C) = -0x44C;
    s32 x = (s32)((u32)PLAYER_POS[0] + (u32)GS(s32, sp + 0x10) * 30);
    GV(s32, ITEM_MODEL + 0x1B8) = x;
    GV(s32, ITEM_MODEL + 0x198) = x;
    s32 z = (s32)((u32)PLAYER_POS[2] + (u32)GS(s32, sp + 0x18) * 30);
    GV(s32, ITEM_MODEL + 0x1C0) = z;
    GV(s32, ITEM_MODEL + 0x1A0) = z;
}

/* 80086BF8 (thread): script op: pick up an item (pc[1] the item, pc[3]
 * flags: 1 no quick-search target, 2 don't end with 80078EB4; pc[4] the
 * model entry's item number). Steps: 0 find the model, 1 wait, 2-3 show
 * it (pickup_Step) and set its event flag, 4 a short wait (thread +0xC),
 * 5 wait, 6 close the message and go on. */
void n_op_PickUp(void) {
    FRAME(0x20);
    u32 th = A0;
    s16 step = GV(s16, th + 4);
    if ((u32)(s32)step >= 7) { V0 = 1; return; }
    LOOP_POLL(0x80086C30);
    switch (step) {
    case 0: {
        GCALL0(0x80078DDCu);
        g_pickupActive = 0;
        u32 en = PICKUP_MODELS;
        for (s32 i = 0;; i++, en += 0x14) {
            if (GV(s16, en) != -1 && GV(s16, en + 2) == GV(s16, GV(u32, th) + 8)) {
                LOOP_POLL(0x80086C80);
                GV(u32, th + 0x1C) = en;
                break;
            }
            if (i + 1 >= 4) break;
            LOOP_POLL(0x80086C90);
        }
        u32 p = GV(u32, th + 0x1C);
        if ((s32)p < 0 && !(GV(u16, GV(u32, th) + 6) & 1)) {
            GV(s32, GV(u32, p + 4) + 0x868) = (s32)GCALL2(F_DISTANCE, 0x800A85D8u, GV(u32, p + 4) + 0x1B8);
            p = GV(u32, th + 0x1C);
            s32 n = GV(s32, 0x800AF530);
            u32 o = GV(u32, p + 4);
            GS(u32, sp + 0x10) = 1;
            GCALL4(F_QS_TARGET, o, n, GV(u32, GV(u32, p + 4) + 0x868), 1);
        }
        goto wait4;
    }
    case 1:
    case 5:
        if (GV(s16, th + 0xC) == 0) GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
        else GV(u16, th + 0xC) = (u16)(GV(u16, th + 0xC) - 1);
        V0 = 1;
        return;
    case 2:
        GV(u16, th + 0xA) = 0;
        GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
        /* fall through */
    case 3: {
        if (GCALL1(F_PICKUP_STEP, th) == 0) { V0 = 1; return; }
        u32 p = GV(u32, th + 0x1C);
        s32 flag;
        if ((s32)p < 0) {
            GV(u8, GV(u32, p + 4) + 0x29) = 0;
            flag = GV(s16, GV(u32, th + 0x1C) + 2);
        } else {
            u32 en = PICKUP_MODELS;
            for (s32 i = 0;; i++, en += 0x14) {
                if (GV(s16, en) != -1 && GV(s16, en + 2) == GV(s16, GV(u32, th) + 8))
                    GV(u8, GV(u32, en + 4) + 0x29) = 0;
                if (i + 1 >= 4) break;
                LOOP_POLL(0x80086DD4);
            }
            flag = GV(s16, GV(u32, th) + 8);
        }
        if (flag != 0) GCALL2(F_FLAG_SET, 0x80062FA0u, flag);
        g_pickupActive = -1;
        GV(u32, GV(u32, th + 0x1C) + 4) = 0;
        D_800AFCB0 = (u16)(D_800AFCB0 & 0xFFF7);
        GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
        V0 = 1;
        return;
    }
    case 4:
        goto wait4;
    default: /* 6 */
        GCALL0(F_MSG_CLOSE);
        if (!(GV(u16, GV(u32, th) + 6) & 2)) GCALL0(F_PICKUP_END);
        GV(u32, th) = GV(u32, th) + 0xA;
        V0 = 0;
        return;
    }
wait4:
    GV(u16, th + 0xC) = 4;
    GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
    V0 = 1;
}

/* Item ids (minus 1) whose model turns about another axis: 0x1C4 cleared,
 * 0x1C4 = 0x400, or 0x1C8 = 0x400 (the original's jump table). */
static int model_pose(u32 k) {
    switch (k) {
    case 0: case 2: case 3: case 46:
        return 0;
    case 32: case 35: case 36: case 37: case 43: case 53:
    case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70:
    case 72: case 73: case 74: case 75:
        return 1;
    case 40: case 47:
        return 2;
    }
    return -1;
}

/* Add to a count as the game does: double amount (u32 * 2) on difficulty 1,
 * else the u16 amount; then kept within 0-255. */
static u16 item_amount(s16 id) {
    if (g_difficulty == 1) return (u16)(GV(u32, ITEM_TABLE + (u32)(s32)id * 8) * 2);
    return GV(u16, ITEM_TABLE + (u32)(s32)id * 8);
}
static u16 clamp255(u32 a) {
    s16 v = GV(s16, a);
    if (v < 0) return 0;
    if (v >= 0x100) return 0xFF;
    return (u16)v;
}

/* Append an entry to list (count at +off) in entries. */
static u32 list_append(u32 list, u32 off, u32 entries) {
    return entries + (u32)GV(u8, list + (u32)(s32)GV(s16, list + off)) * 8;
}

/* 80086E9C (thread): the pick-up's show, by sub-step (thread +0xA): 0
 * set up the model and the message, 1 grow the model (until scale 0x1000),
 * 2 when the message is closed, the sound and the item goes into its list
 * (returns 1: done; 0 otherwise). */
void n_pickup_Step(void) {
    FRAME(0x38);
    u32 th = A0;
    s16 sub = GV(s16, th + 0xA);
    if (sub == 1) {
        u32 p = GV(u32, th + 0x1C);
        if ((s32)p >= 0) {
            GV(u16, th + 0xA) = (u16)(GV(u16, th + 0xA) + 1);
            V0 = 0;
            return;
        }
        u32 o = GV(u32, p + 4);
        GV(u16, o + 0x1C6) = (u16)(GV(u16, o + 0x1C6) + 0x14);
        o = GV(u32, GV(u32, th + 0x1C) + 4);
        if (GV(s32, o + 0x170) < 0x1000) {
            GV(s32, o + 0x170) += 0x180;
            o = GV(u32, GV(u32, th + 0x1C) + 4);
            GV(s32, o + 0x174) += 0x180;
            o = GV(u32, GV(u32, th + 0x1C) + 4);
            GV(s32, o + 0x178) += 0x180;
        } else {
            GV(u16, th + 0xA) = (u16)(GV(u16, th + 0xA) + 1);
        }
        V0 = 0;
        return;
    }
    if (sub == 0) {
        u32 p = GV(u32, th + 0x1C);
        GV(u8, GV(u32, p + 4) + 0x29) = 1;
        p = GV(u32, th + 0x1C);
        GV(u32, p + 0x10) &= ~2u;
        p = GV(u32, th + 0x1C);
        if ((s32)p < 0) {
            u32 o = GV(u32, p + 4);
            GV(u32, o + 0x1CC) = CAMERA_OBJ;
            GV(u32, GV(u32, GV(u32, th + 0x1C) + 4) + 0x198) = 0;
            GV(u32, GV(u32, GV(u32, th + 0x1C) + 4) + 0x19C) = 0;
            GV(s32, GV(u32, GV(u32, th + 0x1C) + 4) + 0x1A0) = -0x5DC;
            o = GV(u32, GV(u32, th + 0x1C) + 4);
            GV(s32, o + 0x178) = 0x100;
            GV(s32, o + 0x174) = 0x100;
            GV(s32, o + 0x170) = 0x100;
            o = GV(u32, GV(u32, th + 0x1C) + 4);
            GV(u32, o + 0x320) |= 0x2000;
            /* per item placement overrides: {id, y, z}, id -1 ends */
            u32 r = 0x800A2D30u;
            if (GV(s32, r) != -1) {
                for (;;) {
                    if (GV(s32, r) == GV(s16, GV(u32, th) + 2)) {
                        GV(u32, GV(u32, GV(u32, th + 0x1C) + 4) + 0x19C) = GV(u32, r + 4);
                        GV(u32, GV(u32, GV(u32, th + 0x1C) + 4) + 0x1A0) = GV(u32, r + 8);
                    }
                    r += 0xC;
                    if (GV(s32, r) == -1) break;
                    LOOP_POLL(0x8008703C);
                }
            }
            u32 k = (u32)(s32)(s16)(u16)(GV(u16, GV(u32, th) + 2) - 1);
            if (k < 0x4C) {
                LOOP_POLL(0x80087080);
                int pose = model_pose(k);
                o = GV(u32, GV(u32, th + 0x1C) + 4);
                if (pose == 0) GV(u16, o + 0x1C4) = 0;
                else if (pose == 1) GV(u16, o + 0x1C4) = 0x400;
                else if (pose == 2) GV(u16, o + 0x1C8) = 0x400;
            }
        }
        /* the message: prefix text, new line, colour 1, the item's name,
         * then 0x79, colour 0, wait 0x10 frames, 0xFF01, end */
        u32 src = GV(u32, 0x800A6F18);
        u32 dst = 0x800A88C8u;
        u16 c = GV(u16, src);
        if (c != 0xFFFF) {
            for (;;) {
                src += 2;
                GV(u16, dst) = c;
                c = GV(u16, src);
                dst += 2;
                if (c == 0xFFFF) break;
                LOOP_POLL(0x800870FC);
            }
        }
        GV(u16, dst) = 0xFFFE; dst += 2;
        GV(u16, dst) = 0xFC01; dst += 2;
        src = GV(u32, 0x800A6D5Cu + (u32)(s32)GV(s16, GV(u32, th) + 2) * 4);
        s32 n = 0;
        c = GV(u16, src);
        if (c != 0xFFFF) {
            for (;;) {
                src += 2;
                GV(u16, dst) = c;
                dst += 2;
                c = GV(u16, src);
                n++;
                if (c == 0xFFFF) break;
                LOOP_POLL(0x80087160);
            }
        }
        GV(u16, dst) = 0x79; dst += 2;
        GV(u16, dst) = 0xFC00; dst += 2;
        GV(u16, dst) = 0xFE10; dst += 2;
        GV(u16, dst) = 0xFF01;
        GV(u16, dst + 2) = 0xFE00;
        s32 x = n >= 15 ? (s32)(s16)(u16)((0u - ((u32)(n / 2) << 19)) >> 16) : -0x38;
        GS(u32, sp + 0x10) = 2;
        GS(u32, sp + 0x14) = 0;
        GS(u32, sp + 0x18) = 0;
        GCALL4(F_MSG_ADD_ENTRY, 0x800A88C8u, (u32)x, 0x30, 0x10);
        g_msgState = 1;
        u16 s = GV(u16, th + 0xA);
        g_msgOverMenu = 0;
        GV(u16, th + 0xA) = (u16)(s + 1);
        V0 = 0;
        return;
    }
    if (sub != 2) { V0 = 0; return; }

    /* sub-step 2 */
    u32 p = GV(u32, th + 0x1C);
    if ((s32)p < 0) {
        u32 o = GV(u32, p + 4);
        GV(u16, o + 0x1C6) = (u16)(GV(u16, o + 0x1C6) + 0x14);
    }
    if (g_msgState != 0) { V0 = 0; return; }
    u32 pc = GV(u32, th);
    s16 item = GV(s16, pc + 2);
    u8 kind = GV(u8, ITEM_TABLE + (u32)(s32)item * 8 + 6);
    u16 id = GV(u16, pc + 2);
    GCALL4(F_SND_CUE, 0, (kind & 8) ? 2 : 3, 0x7F, 0x7F);
    u32 list, rec;
    if (kind & 1) {
        list = 0x800B3DA0u;
        rec = list_append(list, 0x50, 0x800AF580u);
        GV(u16, rec) = id;
        GV(u16, rec + 2) = 0;
        GV(u32, rec + 4) = 0;
        GV(u16, list + 0x50) = (u16)(GV(u16, list + 0x50) + 1);
        V0 = 1;
        return;
    }
    if (kind & 0x10) {
        list = 0x800A8880u;
        rec = list_append(list, 0x40, 0x800AF800u);
        u32 it = ITEM_TABLE + (u32)(s32)(s16)id * 8;
        GV(u16, rec) = id;
        GV(u16, rec + 2) = GV(u16, it + 4);
        GV(u32, rec + 4) = GV(u32, it);
        GV(u16, list + 0x40) = (u16)(GV(u16, list + 0x40) + 1);
        V0 = 1;
        return;
    }
    if (kind & 0x20) {
        list = 0x800A8880u;
        /* a box of the same item: add to it */
        if (GV(s16, list + 0x40) > 0) {
            for (s32 i = 0;; i++) {
                u32 r = 0x800AF800u + (u32)GV(u8, list + (u32)i) * 8;
                s16 rid = GV(s16, r);
                if (rid == (s16)id) {
                    if (GV(s16, r + 2) < 0) { V0 = 1; return; }      /* unlimited */
                    GV(u16, r + 2) = (u16)(GV(u16, r + 2) + item_amount(rid));
                    r = 0x800AF800u + (u32)GV(u8, list + (u32)i) * 8;
                    GV(u16, r + 2) = clamp255(r + 2);
                    V0 = 1;
                    return;
                }
                if (!(i + 1 < GV(s16, list + 0x40))) break;
                LOOP_POLL(0x800874AC);
            }
        }
        /* ammo 13-15: add to a weapon (item 8) loaded with it */
        if ((u16)(id - 0xD) < 3 && GV(s16, list + 0x40) > 0) {
            for (s32 i = 0;; i++) {
                u32 r = 0x800AF800u + (u32)GV(u8, list + (u32)i) * 8;
                if (GV(s16, r) == 8 && GV(s16, r + 4) == (s16)id) {
                    if (GV(s16, r + 6) < 0) { V0 = 1; return; }
                    GV(u16, r + 6) = (u16)(GV(u16, r + 6) + item_amount(GV(s16, r + 4)));
                    if (GV(s16, r + 2) == 0) {
                        u16 a = GV(u16, r + 2);
                        GV(u16, r + 2) = (u16)(a + 1);
                        GV(u16, r + 6) = (u16)(GV(u16, r + 6) - 1);
                    }
                    GV(u16, r + 6) = clamp255(r + 6);
                    V0 = 1;
                    return;
                }
                if (!(i + 1 < GV(s16, list + 0x40))) break;
                LOOP_POLL(0x800875C8);
            }
        }
        rec = list_append(list, 0x40, 0x800AF800u);
        GV(u16, rec) = id;
        if (g_difficulty == 1) GV(u16, rec + 2) = (u16)(GV(s16, ITEM_TABLE + (u32)(s32)(s16)id * 8) * 2);
        else GV(u16, rec + 2) = GV(u16, ITEM_TABLE + (u32)(s32)(s16)id * 8);
        GV(u32, rec + 4) = 0;
        GV(u16, list + 0x40) = (u16)(GV(u16, list + 0x40) + 1);
        V0 = 1;
        return;
    }
    if (kind & 8) {                                   /* a file: read it */
        GV(u16, 0x800B3D04) = id;
        g_buttonsPrev = 0;
        g_buttons = 0;
        GV(u32, 0x800B3D38) = 3;
        GS(u32, sp + 0x10) = g_dataBuf + 0xEA60;
        GCALL4(F_FILE_LOAD, 0x8006C4E4u, 0x80058558u, 0x800A261Cu, 0x14);
        u32 buf = g_dataBuf;
        GCALL2(0x80016F78u, buf + 0xEA60, buf);
        GCALL2(0x8001304Cu, 1, 0x800C3F58u);
        GCALL1(0x8001306Cu, 1);
        GCALL0(0x800735F4u);
        D_80062DEC = 1;
        GV(u32, 0x800B3D38) = 0;
        if (D_800A866C != 2) { V0 = 1; return; }
        if (D_800B3F08 != 0xE) { V0 = 1; return; }
        GCALL4(F_FILE_LOAD2, 0x8006C4FCu, 0x80058558u + 0x1C, 0x11, g_dataBuf);
        GCALL2(0x800927D8u, g_dataBuf, (u32)-1);
        V0 = 1;
        return;
    }
    list = 0x800B4BE8u;
    rec = list_append(list, 0x50, 0x800AFA00u);
    GV(u16, rec) = id;
    GV(u16, rec + 2) = 0;
    GV(u32, rec + 4) = (kind & 4) ? GV(u32, ITEM_TABLE + (u32)(s32)(s16)id * 8) : 0;
    GV(u16, list + 0x50) = (u16)(GV(u16, list + 0x50) + 1);
    V0 = 1;
}
