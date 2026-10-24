/* Animation tracks and coordinates (TEST overlay).
 *
 * An animation context (a0 of anim_UpdateTracks) holds:
 *   +0x00  header: u16 at +2 the number of tracks, tracks from +4 (each
 *          track's size in words at track +4)
 *   +0x14  index i of the "current" slot: slot = ctx + 0x14 + 4 i holds
 *          {track, key A data, key B data, next key data (or 0)}
 *   +0x18  handler table (handler k at +4 + 4k)
 *   +0x1C  key table: one word per key; a negative word is a control code
 *   +0x20  key data (word offsets from the key table's low half)
 * A track record:
 *   +0x08 u16 next key (0xFFFF none)   +0x0A u16 frames left (0xFFFF: run)
 *   +0x0C u8  loop/label id             +0x0D s8  speed (< 0: backwards)
 *   +0x10 s16 time within the key pair  +0x12 u16 the pair's length
 *   +0x14 u16 key A                     +0x16 u16 key B
 *   +0x1B u8  set while a change waits for the track (cleared here)
 * Key words: byte 2 is the length (in 16ths), byte 3 & 0x7F the handler;
 * control codes (bit 31) are jumps: 0x80 with a label (bits 16-22, 0 =
 * any) to match the track's +0x0C and a new label (bits 23-29), or 0xC0
 * an end marker.
 *
 * Coordinates and animation records can be in the scratchpad (some code
 * keeps a coordinate at 0x1F800200), so all accesses here go through GS.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_anim_NextKey(void);
void n_anim_UpdateTracks(void);
void n_anim_InterpolateCoord(void);
void n_coord_Update(void);
void n_coord_RotToStep(void);

#define IDENTITY_MATRIX   0x80065B90u   /* MATRIX (8 words): identity, no translation */
#define g_coordHook       GS(u32, 0x800BAA08)  /* called on each interpolated coordinate */
#define F_PRINTF          0x800398C8u
#define F_EXIT            0x800397E8u
#define F_ROT_MATRIX      0x8002B128u   /* (rotation, matrix) */
#define F_ROT_MATRIX_YXZ  0x8002AE98u   /* (rotation, matrix) */
#define F_SCALE_MATRIX    0x8002A638u   /* (matrix, scale vector) */
#define F_SET_ROT_MATRIX  0x8002A778u
#define F_SET_TRANS_MATRIX 0x8002A7A8u
#define F_ROT_TRANS       0x8002A958u   /* (short vector, long vector out, flag out) */
#define F_ANIM_NEXT_KEY   0x8007E264u

#define TR_NEXT   0x08
#define TR_LEFT   0x0A
#define TR_LABEL  0x0C
#define TR_SPEED  0x0D
#define TR_TIME   0x10
#define TR_LEN    0x12
#define TR_KEY_A  0x14
#define TR_KEY_B  0x16
#define TR_WAIT   0x1B

#define TIME_TO_START 0x7000   /* time markers: restart forwards / backwards */
#define TIME_TO_END   0x6000

/* 8007E264 (key, keys, track): from key, follow control codes (in the
 * track's direction) to a real key. Returns its index, or 0xFFFF at an
 * end marker for the track's label. */
void n_anim_NextKey(void) {
    FRAME(0x18);
    u32 k = A0, keys = A1, t = A2;
    s32 dir = GS(s8, t + TR_SPEED) >= 0 ? 1 : -1;
    u32 w = GS(u32, keys + (k & 0xFFFF) * 4);
    if ((s32)w >= 0) { V0 = k & 0xFFFF; return; }
    do {
        u32 kind = w & 0xC0000000u;
        u32 label = (w & 0x7F0000) >> 16;
        u32 arg = (w & 0x3F800000) >> 23;
        if (kind == 0x80000000u) {                   /* jump */
            if (label == 0 || GS(u8, t + TR_LABEL) == label) {
                if (label != 0 || arg != 0) GS(u8, t + TR_LABEL) = (u8)arg;
                k = w;
            } else {
                k += (u32)dir;
            }
        } else if (kind == 0xC0000000u) {            /* end */
            if (arg != 1) {
                k += (u32)dir;
            } else {
                if (label == 0) { V0 = 0xFFFF; return; }
                if (GS(u8, t + TR_LABEL) == label) { V0 = 0xFFFF; return; }
                k += (u32)dir;
            }
        } else {                                     /* can't happen */
            GCALL3(F_PRINTF, 0x8006C398, 0x8006C3C0, 0x59);
            GCALL1(F_EXIT, 1);
            k += (u32)dir;
        }
        w = GS(u32, keys + (k & 0xFFFF) * 4);
    } while (w & 0x80000000u);
    V0 = k & 0xFFFF;
}

static u32 next_key(u32 k, u32 keys, u32 t) {
    A0 = k & 0xFFFF; A1 = keys; A2 = t;
    n_anim_NextKey();
    return V0;
}

/* 8007E38C (context): advance every track of an animation: count down its
 * frames, step to the next key pair when the time runs out (forwards or
 * backwards), and call the key's handler on each pair (a handler that
 * returns 1 restarts the pair). Returns the end of the track list. */
void n_anim_UpdateTracks(void) {
    FRAME(0x40);
    u32 ctx = A0;
    u32 hdr = GS(u32, ctx);
    u32 handlers = GS(u32, ctx + 0x18);
    GS(u32, sp + 0x10) = handlers;
    u32 keys = GS(u32, ctx + 0x1C);
    u32 data = GS(u32, ctx + 0x20);
    u32 n = GS(u16, hdr + 2);
    u32 t = hdr + 4;
    for (u32 i = 0; i < n; i++, t += (u32)GS(u16, t + 4) * 4) {
        u16 left = GS(u16, t + TR_LEFT);
        if (left == 0) continue;
        if (left != 0xFFFF) GS(u16, t + TR_LEFT) = (u16)(left - 1);
        s32 back = GS(s8, t + TR_SPEED) >= 0 ? 0 : -1;
        if (GS(s16, t + TR_TIME) == TIME_TO_START) { back = 0; GS(s16, t + TR_TIME) = 0; }
        else if (GS(s16, t + TR_TIME) == TIME_TO_END) { back = -1; GS(s16, t + TR_TIME) = 0; }
        for (;;) {
            if (GS(s16, t + TR_TIME) == 0 || GS(u16, t + TR_LEN) == 0) {
                /* on to the next key pair */
                GS(u8, t + TR_WAIT) = 0;
                if (back == 0) {
                    u32 k = GS(u16, t + TR_KEY_B);
                    if ((s32)GS(u32, keys + k * 4) < 0) {
                        u32 r = next_key(k + 1, keys, t);
                        GS(u16, t + TR_KEY_B) = (u16)r;
                        if ((r & 0xFFFF) == 0xFFFF) { GS(u16, t + TR_LEFT) = 0; break; }
                    }
                    u16 b = GS(u16, t + TR_KEY_B);
                    GS(u16, t + TR_KEY_A) = b;
                    u32 r = next_key((u32)b + 1, keys, t);
                    GS(u16, t + TR_KEY_B) = (u16)r;
                    if ((r & 0xFFFF) == 0xFFFF) { GS(u16, t + TR_LEFT) = 0; break; }
                    u16 len = (u16)(GS(u8, keys + (u32)GS(u16, t + TR_KEY_B) * 4 + 2) << 4);
                    u16 time = GS(u16, t + TR_TIME);
                    GS(u16, t + TR_LEN) = len;
                    GS(u16, t + TR_TIME) = (u16)(len - time);
                } else {
                    u32 k = GS(u16, t + TR_KEY_A);
                    if ((s32)GS(u32, keys + k * 4) < 0) {
                        u32 r = next_key(k - 1, keys, t);
                        GS(u16, t + TR_KEY_A) = (u16)r;
                        if ((r & 0xFFFF) == 0xFFFF) { GS(u16, t + TR_LEFT) = 0; break; }
                    }
                    u16 a = GS(u16, t + TR_KEY_A);
                    GS(u16, t + TR_KEY_B) = a;
                    u32 r = next_key((u32)a - 1, keys, t);
                    GS(u16, t + TR_KEY_A) = (u16)r;
                    if ((r & 0xFFFF) == 0xFFFF) { GS(u16, t + TR_LEFT) = 0; break; }
                    GS(u16, t + TR_LEN) = (u16)(GS(u8, keys + (u32)GS(u16, t + TR_KEY_B) * 4 + 2) << 4);
                }
            }
            /* the slot for the handler: this track and its key data */
            u32 kb = keys + (u32)GS(u16, t + TR_KEY_B) * 4;
            u32 handler = GS(u32, GS(u32, sp + 0x10) + (u32)(GS(u8, kb + 3) & 0x7F) * 4 + 4);
            u32 slot = ctx + (u32)GS(s32, ctx + 0x14) * 4 + 0x14;
            GS(u32, slot) = t;
            GS(u32, slot + 4) = data + (u32)GS(u16, keys + (u32)GS(u16, t + TR_KEY_A) * 4) * 4;
            GS(u32, slot + 8) = data + (u32)GS(u16, keys + (u32)GS(u16, t + TR_KEY_B) * 4) * 4;
            u16 nx = GS(u16, t + TR_NEXT);
            GS(u32, slot + 0xC) = nx == 0xFFFF ? 0 : data + (u32)GS(u16, keys + (u32)nx * 4) * 4;
            if (GS(u16, t + TR_KEY_A) == GS(u16, t + TR_KEY_B)) break;
            /* (a1-a3 as the original leaves them; handlers use a0) */
            if (gcall(handler, ctx, handler, t, GS(u32, sp + 0x10)) == 1) { GS(s16, t + TR_TIME) = 0; continue; }
            /* advance the time */
            u16 time = (u16)(GS(u16, t + TR_TIME) - (u32)(s32)GS(s8, t + TR_SPEED));
            GS(u16, t + TR_TIME) = time;
            if (back == 0) {
                if ((s16)time < 0) GS(s16, t + TR_TIME) = TIME_TO_START;
            } else if (!(GS(s16, t + TR_TIME) < (s32)GS(u16, t + TR_LEN))) {
                GS(s16, t + TR_TIME) = TIME_TO_END;
            }
            break;
        }
    }
    hdr = GS(u32, ctx);
    V0 = hdr + (u32)GS(u16, hdr) * 4;
}

/* 8007E724 (context): key handler: interpolate the current slot's track
 * between its two keys into the coordinate the track drives (track word 0:
 * coordinate list index << 24 | coordinate): translation and rotation
 * (key data: s32 x, y, z, s16 rx, ry, rz at +0xC), then its matrix from
 * the rotation (and the hook 0x800BAA08 first). The result also goes to
 * the slot's next-key record when there is one. Returns 1 if the pair has
 * no length. */
void n_anim_InterpolateCoord(void) {
    FRAME(0x50);
    u32 ctx = A0;
    u32 slot = ctx + (u32)GS(s32, ctx + 0x14) * 4 + 0x14;
    u32 t = GS(u32, slot);
    if (GS(u16, t + TR_LEN) == 0) { V0 = 1; return; }
    u32 w = GS(u32, t);
    u32 index = w & 0xFFFFFF;
    u32 c = GS(u32, ctx + (w >> 24) * 4 + 0x14) + index * 4;
    s32 time = GS(s16, t + TR_TIME);
    s32 len = GS(u16, t + TR_LEN);
    for (u32 k = 0; k < 8; k++) GS(u32, c + 4 + k * 4) = GS(u32, IDENTITY_MATRIX + k * 4);
    s32 rest = len - time;
    u32 ka = GS(u32, slot + 4), kb = GS(u32, slot + 8);
    for (u32 k = 0; k < 3; k++)
        GS(s32, c + 0x18 + k * 4) = mips_div((s32)((u32)GS(s32, ka + k * 4) * (u32)time
                                                  + (u32)GS(s32, kb + k * 4) * (u32)rest), len);
    for (u32 k = 0; k < 3; k++) {
        s32 r = mips_div((s32)((u32)(s32)GS(s16, ka + 0xC + k * 2) * (u32)time
                               + (u32)(s32)GS(s16, kb + 0xC + k * 2) * (u32)rest), len);
        GS(s16, c + 0x44 + k * 2) = (s16)r;
        GS(s32, sp + 0x30 + k * 4) = (s16)r;
    }
    if (g_coordHook != 0) GCALL2(g_coordHook, c, index);
    GCALL2(F_ROT_MATRIX, c + 0x44, sp + 0x10);
    /* coordinate matrix = its matrix x the rotation's (column by column) */
    u32 m = c + 4;
    gte_write_ctrl(0, MEM_R32(m + 0x0));
    gte_write_ctrl(1, MEM_R32(m + 0x4));
    gte_write_ctrl(2, MEM_R32(m + 0x8));
    gte_write_ctrl(3, MEM_R32(m + 0xC));
    gte_write_ctrl(4, MEM_R32(m + 0x10));
    for (u32 col = 0; col < 3; col++) {
        u32 v = sp + 0x10 + col * 2;
        gte_write_data(9, GS(u16, v + 0x0));
        gte_write_data(10, GS(u16, v + 0x6));
        gte_write_data(11, GS(u16, v + 0xC));
        gte_command(0x049E012u);                  /* mvmva: rotation x IR */
        if (col == 2) GS(u32, c) = 0;             /* the coordinate changed */
        u32 x = gte_read_data(9), y = gte_read_data(10), z = gte_read_data(11);
        GS(u16, m + col * 2 + 0x0) = (u16)x;
        GS(u16, m + col * 2 + 0x6) = (u16)y;
        GS(u16, m + col * 2 + 0xC) = (u16)z;
    }
    u32 out = GS(u32, slot + 0xC);
    if (out == 0) { V0 = 0; return; }
    GS(s32, GS(u32, slot + 0xC) + 0) = GS(s32, c + 0x18);
    GS(s32, GS(u32, slot + 0xC) + 4) = GS(s32, c + 0x1C);
    GS(s32, GS(u32, slot + 0xC) + 8) = GS(s32, c + 0x20);
    GS(u16, GS(u32, slot + 0xC) + 0xC) = GS(u16, sp + 0x30);
    GS(u16, GS(u32, slot + 0xC) + 0xE) = GS(u16, sp + 0x34);
    GS(u16, GS(u32, slot + 0xC) + 0x10) = GS(u16, sp + 0x38);
    V0 = 0;
}

/* 80082760 (coordinate, scale): a GsCOORDINATE2-like record's matrix from
 * its rotation (+0x44) and position (+0x18), scaled (scale vector, or 1.0
 * if none); clears its flag word. */
void n_coord_Update(void) {
    FRAME(0x50);
    u32 c = A0, scale = A1;
    if (scale == 0) {
        scale = sp + 0x30;
        GS(s32, scale) = 0x1000;
        GS(s32, scale + 4) = 0x1000;
        GS(s32, scale + 8) = 0x1000;
    }
    const u32 m = sp + 0x10;
    for (u32 k = 0; k < 8; k++) GS(u32, m + k * 4) = GS(u32, IDENTITY_MATRIX + k * 4);
    GS(u32, m + 0x14) = GS(u32, c + 0x18);
    GS(u32, m + 0x18) = GS(u32, c + 0x1C);
    GS(u32, m + 0x1C) = GS(u32, c + 0x20);
    GCALL2(F_ROT_MATRIX_YXZ, c + 0x44, m);
    u32 r = GCALL2(F_SCALE_MATRIX, m, scale);
    for (u32 k = 0; k < 8; k++) GS(u32, c + 4 + k * 4) = GS(u32, m + k * 4);
    GS(u32, c) = 0;
    V0 = r;
}

/* 8008286C (rotation, out, speed): the step for moving speed units along
 * the facing of rotation: (0, 0, speed) rotated, in 8-bit fixed point,
 * rounded to the nearest unit. */
void n_coord_RotToStep(void) {
    FRAME(0x50);
    u32 rot = A0, out = A1;
    u32 speed = A2;
    const u32 m = sp + 0x10, v = sp + 0x30;
    for (u32 k = 0; k < 8; k++) GS(u32, m + k * 4) = GS(u32, IDENTITY_MATRIX + k * 4);
    GS(u16, v + 0) = 0;
    GS(u16, v + 2) = 0;
    GS(u16, v + 4) = (u16)(speed << 8);
    GCALL2(F_ROT_MATRIX_YXZ, rot, m);
    GCALL1(F_SET_ROT_MATRIX, m);
    GCALL1(F_SET_TRANS_MATRIX, m);
    GCALL3(F_ROT_TRANS, v, out, sp + 0x38);
    /* the fractions, then the whole units */
    GS(u16, v + 0) = (u16)(MEM_R8(out + 0) >> 4);
    GS(u16, v + 2) = (u16)(MEM_R8(out + 4) >> 4);
    GS(u16, v + 4) = (u16)(MEM_R8(out + 8) >> 4);
    s32 x = (s32)MEM_R32(out + 0), z = (s32)MEM_R32(out + 8);
    MEM_W32(out + 0, (u32)(x >> 8));
    MEM_W32(out + 8, (u32)(z >> 8));
    MEM_W32(out + 4, (u32)((s32)MEM_R32(out + 4) >> 8));
    V0 = 1;
    for (u32 k = 0; k < 3; k++) {
        if (GS(s16, v + k * 2) < 8) { V0 = 1; continue; }
        u32 nv = MEM_R32(out + k * 4) + 1;
        MEM_W32(out + k * 4, nv);
        V0 = nv;
    }
}
