/* A room's sounds (TEST overlay): its sound bank and music, and the
 * music that changes with events.
 *
 * Each stage has a table (0x800A42AC) of a sound set per room (-1: none).
 * Changing to a room with another set loads the set's file to 0x80108000
 * and copies its parts to high memory: a header of offsets, then the
 * bank's attributes (VAB header) and one or two sequences (some sets have
 * only one, set 8 none). They're opened with the sound library and the
 * sequences started at full volume.
 *
 * Music events (0x800A4A0C, 16 bytes each, the current one 0x800B3D2E):
 * s16 kind, then values compared with the music position (the sequence's
 * play count), and sound cues (0x8008FBB0) at those points; the kind
 * moves on (+100) as they pass.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"

void n_snd_LoadRoomSet(void);
void n_snd_RoomSet(void);
void n_snd_MusicCue(void);
void n_music_Events(void);
void n_snd_SeqEnd(void);
void n_seq_Rewind(void);

#define SET_BUF          0x80108000u
#define F_HIMEM_RESET    0x8008F670u
#define F_HIMEM_ALLOC    0x8008F684u
#define F_HIMEM_SET      0x8008F69Cu
#define g_setNow         GV(s16, 0x80062E7C)
#define g_setKeep        GV(s16, 0x80062E78)
#define g_vabId          GV(s16, 0x80062E7A)
#define g_seqId          GA(s16, 0x80062E70)
#define g_vabHead        GV(u32, 0x800BBCD4)
#define g_seq1           GV(u32, 0x800BBCC8)
#define g_seq2           GV(u32, 0x800BBCCC)

/* Sound sets with two sequences (0), one (1), none (2: set 8). */
static const u8 set_kind[0x22] = {
    0, 0, 1, 0, 0, 1, 0, 1, 2, 1, 0, 0, 1, 1, 0, 0, 0,
    0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0,
};

/* Words in [a, b) of the set file's header offsets. */
static s32 part_words(u32 a, u32 b) { return (GV(s32, b) - GV(s32, a)) / 4; }

/* Copy n words from *src to dst, advancing *src. */
static void copy_part(u32 *src, u32 dst, s32 n) {
    for (s32 i = 0; i < n; i++, *src += 4, dst += 4) GV(u32, dst) = GV(u32, *src);
}

/* 8008F6A8: the room's sound set: load it unless it's the one already in
 * (then keep its memory). */
void n_snd_LoadRoomSet(void) {
    FRAME(0x38);
    u32 tbl = GV(u32, 0x800A42ACu + (u32)GV(s32, 0x800B4BA0) * 4);
    GV(u32, 0x800AFCB4) = tbl;
    s16 set = GV(s16, tbl + GV(u32, 0x800B3E20) * 4);
    GCALL0(F_HIMEM_RESET);
    if (set == -1) {
        g_seq2 = 0;
        g_seq1 = 0;
        g_vabHead = 0;
        g_setNow = set;
        return;
    }
    if (g_setNow == set && g_setKeep == 0) {
        GCALL1(F_HIMEM_SET, g_seq2 ? g_seq2 : g_seq1);
        return;
    }
    GCALL1(0x80011E24u, 0);
    GCALL1(0x80011E74u, 0);
    GCALL1(0x80011E74u, 1);
    g_seq2 = 0;
    g_seq1 = 0;
    g_vabHead = 0;
    u32 dir = GCALL0(0x80011BB0u);
    GS(u32, sp + 0x10) = SET_BUF;
    GCALL4(F_FILE_LOAD, 0x8006C6C0u, 0x80058510u, dir, (u32)(set + 0xE6));
    s32 n = part_words(SET_BUF + 4, SET_BUF + 8);
    u32 dst = GCALL1(F_HIMEM_ALLOC, (u32)(n * 4));
    g_vabHead = dst;
    int kind = (u32)set < 0x22 ? set_kind[set] : 1;
    u32 src;
    if (kind == 2) {
        n = part_words(SET_BUF + 4, SET_BUF + 8);
        dst = GCALL1(F_HIMEM_ALLOC, (u32)(n * 4));
        g_vabHead = dst;
        src = SET_BUF + 0xC;
    } else if (kind == 0) {
        src = SET_BUF + 0x14;
    } else {
        src = SET_BUF + 0x10;
    }
    copy_part(&src, dst, n);
    if (kind == 0) {
        n = part_words(SET_BUF + 8, SET_BUF + 0xC);
        dst = GCALL1(F_HIMEM_ALLOC, (u32)(n * 4));
        g_seq1 = dst;
        copy_part(&src, dst, n);
        n = part_words(SET_BUF + 0xC, SET_BUF + 0x10);
        dst = GCALL1(F_HIMEM_ALLOC, (u32)(n * 4));
        g_seq2 = dst;
        copy_part(&src, dst, n);
    } else if (kind == 1) {
        n = part_words(SET_BUF + 8, SET_BUF + 0xC);
        dst = GCALL1(F_HIMEM_ALLOC, (u32)(n * 4));
        g_seq1 = dst;
        copy_part(&src, dst, n);
    }
    g_vabId = (s16)GCALL2(0x80024488u, g_vabHead, 7);       /* open the bank's header */
    if (g_vabId < 0) return;
    g_vabId = (s16)GCALL2(0x80024C58u, src, (u32)(s32)g_vabId);   /* and its waveforms */
    GCALL1(0x80024D48u, 1);
    if (kind == 0) {
        g_seqId[0] = (s16)GCALL2(0x8001AD68u, g_seq1, (u32)(s32)g_vabId);
        g_seqId[1] = (s16)GCALL2(0x8001AD68u, g_seq2, (u32)(s32)g_vabId);
    } else if (kind == 1) {
        g_seqId[0] = (s16)GCALL2(0x8001AD68u, g_seq1, (u32)(s32)g_vabId);
    }
    GCALL2(0x8001D358u, 0x7F, 0x7F);
    if (kind == 0) {
        GCALL3(0x8001DDA8u, (u32)(s32)g_seqId[0], 0x7F, 0x7F);
        GCALL3(0x8001DDA8u, (u32)(s32)g_seqId[1], 0x7F, 0x7F);
    } else if (kind == 1) {
        GCALL3(0x8001DDA8u, (u32)(s32)g_seqId[0], 0x7F, 0x7F);
    }
    g_setNow = set;
}

/* 8008FB40 (stage, room): the room's sound set; -2 for no such stage. */
void n_snd_RoomSet(void) {
    if (A0 >= 0x12) { V0 = (u32)-2; return; }
    u32 tbl = GV(u32, 0x800A42ACu + A0 * 4);
    V0 = (u32)(s32)GV(s16, tbl + A1 * 4);
}

/* 8008FBB0 (n): sound cue n of bank 6. */
void n_snd_MusicCue(void) {
    FRAME(0x20);
    GS(u32, sp + 0x10) = 0x10;
    GCALL4(0x80080670u, 6, A0 & 0xFF, 0x7F, 0x7F);
}

/* 80092D98 (n): end sequence n of the set file (its end marker). */
void n_snd_SeqEnd(void) {
    u32 e = SET_BUF + (u32)((s32)(s16)A0 * 4);
    u32 off = GV(u32, e + 8) & ~3u;
    GS(s32, GV(u32, SET_BUF + off + 0x10)) = -1;
    V0 = (u32)-1;
}

/* 8008FBE4: the current music event; 0 when it's finished, else 1. */
void n_music_Events(void) {
    FRAME(0x28);
    u32 ev = 0x800A4A0Cu + (u32)((s32)GV(s16, 0x800B3D2E) * 16);
    u32 snd = GV(u32, 0x80010050) + 0x2D73Cu;
    u32 seq = GV(u32, snd + 0x1D4 + (u32)GV(u16, ev + 0xE) * 4);
    s16 pos = (s16)(GV(u16, seq + 0x16) - GV(u16, seq + 0x18));
    u32 more = 1;
#define CUE(n) GCALL1(0x8008FBB0u, (n))
#define AT(off) (pos == GV(s16, ev + (off)))
#define NEXT(d) (GV(u16, ev) = (u16)(GV(u16, ev) + (d)))
    switch (GV(s16, ev)) {
    case 0:
        if (GV(s16, ev + 2) >= pos) {
            GV(u16, ev + 8) = GV(u16, ev + 6);
        } else {
            if (mips_div(GV(s16, ev + 6), GV(s16, ev + 8)) == 0) {
                GCALL1(0x8001F6A4u, 0x10);
                CUE(0);
                GV(u16, ev + 8) = 0;
            }
            GV(u16, ev + 8) = (u16)(GV(u16, ev + 8) + 1);
        }
        seq = GV(u32, snd + 0x1D4 + (u32)GV(u16, ev + 0xE) * 4);
        if (GV(u16, seq + 0x16) == 0xFFFF) more = 0;
        break;
    case 1:
        if (AT(2)) CUE(0);
        if (AT(0xA)) { CUE(1); more = 0; }
        break;
    case 2:
        if (AT(2)) CUE(0);
        if (AT(6)) CUE(1);
        if (AT(0xA)) { CUE(2); more = 0; }
        break;
    case 3:
        if (AT(2)) CUE(0);
        if (AT(0xA)) { CUE(0); more = 0; }
        break;
    case 4:
        GCALL1(0x8007C4C4u, snd);
        CUE(0);
        NEXT(100);
        break;
    case 5:
        if (AT(2)) { CUE(0); NEXT(100); }
        break;
    case 0x68:
        if (GCALL1(0x80019268u, 0x10000) == 3) {
            GCALL1(0x8007C50Cu, snd);
            CUE(1);
            NEXT(100);
        }
        break;
    case 0xCC:
        if (AT(0xA)) { CUE(2); more = 0; NEXT(-0xC8); }
        break;
    case 0x69:
        if (AT(6)) { CUE(1); NEXT(100); }
        /* fall through */
    case 0xCD:
        if (AT(0xA)) { CUE(2); NEXT(100); }
        /* fall through */
    case 0x131:
        if (AT(0xC)) { more = 0; NEXT(-0x12C); }
        break;
    }
#undef CUE
#undef AT
#undef NEXT
    GV(u32, 0x800BB194) = GV(u32, GV(u32, 0x800B3D30) + 0xC);
    V0 = more;
}

/* 800925C4 (sequence player): rewind its tracks (+0x1D0 count, +0x1D4
 * the tracks; +0x274 each track's data start). */
void n_seq_Rewind(void) {
    u32 obj = A0;
    s32 n = GV(s32, obj + 0x1D0);
    if (n <= 0) { V0 = obj + 0x1D4; return; }
    for (s32 i = 0; i < n; i++) {
        u32 tr = GV(u32, obj + 0x1D4 + (u32)i * 4);
        GV(u32, obj + 0x274 + (u32)i * 4) = tr + 0x18;
        GV(u16, tr + 8) = 0xFFFF;
        GV(u16, tr + 0x16) = GV(u16, tr + 0x18);
        GV(u16, tr + 0xA) = 0xFFFF;
        GV(u8, tr + 0xC) = GV(u8, tr + 0x1A);
        GV(u8, tr + 0xD) = 0x10;
    }
    V0 = 0;
}
