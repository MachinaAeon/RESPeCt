/* Sound (KERNEL, 0x80011BB0-0x80012DD8): the game's layer over Sony's
 * libsnd / libspu: sound banks, the music, positional sound effects.
 *
 * Sound banks: 7 slots of 12 bytes at 0x800637C0 (+0 the buffer the bank
 * is copied to, +4 its size in words, +8 (s16) the VAB id or -1, +0xA the
 * caller's tag). Music: two sequences (ids at 0x80062E70/72, -1 none)
 * with separate data (0x80062E7A); a fade (0x80062E60..68: left and
 * right volume, the sequence, the fade step in 1/256, its increase).
 * The master volume is 0x80062DB4 (music) and 0x80062DB6 (effects), in
 * percent.
 *
 * Positional effects use voices in pairs (the pair's first voice at
 * 0x80062D48 + 2 * pair): 0x80012524 starts a sound on a pair with
 * its left and right volumes from the listener's panning tables (four
 * 16-entry tables of u16, pointers at 0x80062EA0..AC, indexed by the
 * direction's bits 8-11, interpolated by its low byte), 0x80012AA4 moves
 * it, and the vertical-blank callback (sound_UpdatePairs) frees pairs
 * whose voices finished. 0x80062D4E: a bit per playing pair.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). See kernel.h for the frame conventions. */
#include "kernel.h"

#define K_BANKS        0x800637C0u          /* 7 x 12 bytes */
#define K_SEQ_IDS      0x80062E70u          /* s16 x 2: music sequences */
#define K_SEQ_DATA     0x80062E7Au          /* s16 x 2: their data ids */
#define k_musicVol     GV(s16, 0x80062DB4)  /* percent */
#define k_sfxVol       GV(s16, 0x80062DB6)  /* percent */
#define k_fadeL        GV(u16, 0x80062E60)
#define k_fadeR        GV(u16, 0x80062E62)
#define k_fadeSeq      GV(u16, 0x80062E64)
#define k_fadeStep     GV(u16, 0x80062E66)
#define k_fadeInc      GV(u16, 0x80062E68)
#define K_PAIR_VOICE   0x80062D48u          /* s16 per pair: its first voice */
#define k_pairsOn      GV(u16, 0x80062D4E)  /* a bit per playing pair */
#define k_pairNext     GV(u32, 0x80062D50)  /* the pair the callback checks next */
#define K_PAIR_KEYS    0x8005960Cu          /* u32 per pair: its voices' key bits */
#define K_PAN_TABLES   0x80062EA0u          /* 4 pointers: panning tables */
#define K_PAN_DIFF     0x80062E88u          /* s16 x 4: work (0x800122C4) */
#define K_PAN_OUT      0x80062E80u          /* s16 x 4: work (0x800122C4) */
#define K_MOVE_DIFF    0x80062E98u          /* s16 x 4: work (0x80012AA4) */
#define K_MOVE_OUT     0x80062E90u          /* s16 x 4: work (0x80012AA4) */

#define K_SPU_SET_KEY      0x800183D8u      /* SpuSetKey (on, voice bits) */
#define K_SPU_VOICE_STATUS 0x80019268u      /* (voice bit): envelope state */
#define K_SPU_VOICE_VOL    0x800197E8u      /* (voice, left, right) */
#define K_SEQ_VOL          0x8001DDA8u      /* (seq, left, right) */
#define K_SEQ_STOP         0x8001D838u
#define K_SEQ_PLAY         0x8001D118u      /* (seq, mode, count) */
#define K_SEQ_CLOSE        0x8001A834u
#define K_SEQ_GET_VOL      0x8001DEA0u      /* (seq, 0, &left, &right) */
#define K_VAB_CLOSE        0x80024058u

/* 80011BB0: the address of the sound file names (0x80058D2C). */
void n_sound_FileNames(void) { V0 = 0x80058D2Cu; }

/* 80011BBC (slot, tag, file data): a sound bank from loaded file data
 * (+4/+8: start and end of the VAB header; the body follows): copy the
 * header to the slot's buffer, open it (0x80024488), and when that
 * worked transfer the body (0x80024C58, its result in 0x80062E5C) and
 * wait for the transfer (0x80024D48). */
void n_sound_OpenBank(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x1C) = R[31];
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x14) = in_s1;
    u32 slot = A0;
    u32 src = A2 + 4;
    s32 bytes = (s32)(GV(u32, src + 4) - GV(u32, src));
    u32 tag = A1;
    R[18] = tag;
    u32 dst = GV(u32, K_BANKS + slot * 12);
    if (bytes < 0) bytes += 3;
    src += 8;
    s32 words = bytes >> 2, i = 0;
    if (words > 0) {
        for (;;) {
            i++;
            GV(u32, dst + (u32)(i - 1) * 4) = GV(u32, src);
            src += 4;
            if (!(i < words)) break;
            LOOP_POLL(0x80011C38u);
        }
    }
    R[16] = src;
    u32 b = K_BANKS + slot * 12;
    R[17] = b;
    GV(u32, b + 4) = (u32)i;
    u32 id = JAL(0x80011C68u, 0x80024488u, dst, (s32)(s16)slot, (u32)i, slot);
    GV(u16, b + 8) = (u16)id;
    if ((s16)id >= 0) {
        u32 r = JAL(0x80011C84u, 0x80024C58u, src, (s32)(s16)id, A2, A3);
        GV(s32, 0x80062E5C) = (s16)r;
        JAL(0x80011C98u, 0x80024D48u, 1, A1, A2, A3);
    } else {
        V0 = id << 16;
    }
    GV(u16, b + 0xA) = (u16)tag;
    RESTORE_S;
}

/* 80011D48 (keep): stop all sound: close the 7 banks, both sequences
 * and their data, the voices (0x8001A888); the sound library too
 * (0x8001D198) unless keep. */
void n_sound_StopAll(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x10) = in_s0;
    u32 keep = A0;
    R[17] = keep;
    JAL(0x80011D64u, 0x8001E118u, 0, A1, A2, A3);
    for (u32 i = 0;; i++) {
        R[16] = i;
        JAL(0x80011D70u, 0x80011DCCu, i, A1, A2, A3);
        if (i + 1 >= 7) break;
        LOOP_POLL(0x80011D78u);
    }
    R[16] = 7;
    JAL(0x80011D88u, 0x80011E24u, 0, A1, A2, A3);
    JAL(0x80011D90u, 0x80011E74u, 0, A1, A2, A3);
    JAL(0x80011D98u, 0x80011E24u, 1, A1, A2, A3);
    JAL(0x80011DA0u, 0x80011E74u, 1, A1, A2, A3);
    JAL(0x80011DA8u, 0x8001A888u, A0, A1, A2, A3);
    if (keep == 0) JAL(0x80011DB8u, 0x8001D198u, A0, A1, A2, A3);
    RESTORE_S;
}

/* Close an id (s16 at addr, -1: none) with fn, then mark it closed. */
static void close_id(u32 addr, u32 fn, u32 ra, u32 v0_none) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x14) = in_s1;
    R[16] = addr;
    R[17] = 0xFFFFFFFFu;
    s32 id = GV(s16, addr);
    if (id != -1) {
        JAL(ra, fn, id, A1, A2, A3);
        GV(u16, addr) = 0xFFFF;
    } else {
        A0 = (u32)id;
        V0 = v0_none;
    }
    RESTORE_S;
}

/* 80011DCC (slot): close a sound bank. */
void n_sound_CloseBank(void) {
    close_id(K_BANKS + A0 * 12 + 8, K_VAB_CLOSE, 0x80011E0Cu, A0 * 12);
}

/* 80011E24 (n): close a sequence's data (opened as a VAB). */
void n_sound_CloseSeqData(void) {
    close_id(K_SEQ_DATA + (u32)((s32)(s16)A0 << 1), K_VAB_CLOSE, 0x80011E5Cu, K_SEQ_DATA);
}

/* 80011E74 (n): close a sequence. */
void n_sound_CloseSeq(void) {
    close_id(K_SEQ_IDS + (u32)((s32)(s16)A0 << 1), K_SEQ_CLOSE, 0x80011EACu, K_SEQ_IDS);
}

/* 80011EC4 (n, increase): start fading sequence n out: its current
 * volumes into the fade, step 0. 1 when it plays, else 0. */
void n_sound_FadeStart(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    s32 n = (s16)A0;
    if (GV(s16, K_SEQ_IDS + (u32)n * 2) == -1) { V0 = 0; return; }
    k_fadeSeq = (u16)A0;
    k_fadeInc = (u16)A1;
    k_fadeStep = 0;
    JAL(0x80011F10u, K_SEQ_GET_VOL, n, 0, 0x80062E60u, 0x80062E62u);
    V0 = 1;
}

/* 80011F24: per frame (from the frame callback): the fade: both volumes
 * down by the step's high byte (to 0), the step up by the increase
 * (back to 0 once it reaches 0x100), then the sequence's volume. */
void n_sound_FadeStep(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if ((s16)k_fadeL == 0 && (s16)k_fadeR == 0) { V0 = 0; return; }
    u32 l = k_fadeL;
    u32 step = k_fadeStep;
    s32 d = (s32)(step << 16) >> 24;
    k_fadeL = (u16)(l - d);
    if ((s16)k_fadeL < 0) k_fadeL = 0;
    k_fadeR = (u16)(k_fadeR - d);
    if ((s16)k_fadeR < 0) k_fadeR = 0;
    if ((s16)step >= 0x100) k_fadeStep = 0;
    else k_fadeStep = (u16)(step + k_fadeInc);
    s32 seq = GV(s16, K_SEQ_IDS + (u32)((s32)(s16)k_fadeSeq << 1));
    JAL(0x80011FD8u, K_SEQ_VOL, seq, (s32)(s16)k_fadeL, (s32)(s16)k_fadeR, A3);
}

/* 80011FF8 (n, volume): a sequence's volume (percent of the music
 * volume), both sides. */
void n_sound_SeqVolume(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (GV(s16, K_SEQ_DATA) < 0) { V0 = (u32)(s32)GV(s16, K_SEQ_DATA); return; }
    if (A0 != 0 && A0 != 1) { V0 = 1; return; }
    s32 v = mul32((s32)A1, k_musicVol);
    s32 seq = GV(s16, K_SEQ_IDS + A0 * 2);
    s32 vol = (s16)div100(v);
    JAL(0x8001208Cu, K_SEQ_VOL, seq, vol, vol, A3);
}

/* 8001209C (n): play sequence n (0/1 at full music volume) once. */
void n_sound_SeqPlay(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 n = A0;
    R[16] = n;
    if (GV(s16, K_SEQ_DATA) < 0) { V0 = (u32)(s32)GV(s16, K_SEQ_DATA); RESTORE_S; return; }
    if (n == 0 || n == 1) {
        s32 v = mul32(k_musicVol, 127);
        s32 seq = GV(s16, K_SEQ_IDS + n * 2);
        s32 vol = (s16)div100(v);
        u32 hi = (u32)(((s64)v * 0x51EB851F) >> 32);     /* $a3: the mfhi */
        JAL(0x80012130u, K_SEQ_VOL, seq, vol, vol, hi);
    }
    JAL(0x8001214Cu, K_SEQ_PLAY, (s32)GV(s16, K_SEQ_IDS + n * 2), 1, 0, A3);
    RESTORE_S;
}

/* 8001215C (n): stop sequence n. */
void n_sound_SeqStop(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (GV(s16, K_SEQ_DATA) < 0) { V0 = (u32)(s32)GV(s16, K_SEQ_DATA); return; }
    u32 a = K_SEQ_IDS + A0 * 2;
    s32 seq = GV(s16, a);
    if (seq < 0) { A0 = (u32)seq; V0 = a; return; }
    JAL(0x80012190u, K_SEQ_STOP, seq, A1, A2, A3);
}

/* 800121A0 (four panning tables): key off the effect voices (bits
 * 18-23), then set the listener's panning tables. */
void n_sound_SetListener(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x20) = R[31];
    u32 t0 = A0, t1 = A1, t2 = A2, t3 = A3;
    R[16] = t0; R[17] = t1; R[18] = t2; R[19] = t3;
    JAL(0x800121D4u, K_SPU_SET_KEY, 0, 0xFC0000u, A2, A3);
    JAL(0x800121E8u, 0x800122ACu, t0, t1, t2, t3);
    RESTORE_S;
}

/* 80012204: per frame (vertical-blank callback): one pair a frame (of
 * three): when it plays and both its voices are off (state 3), key it
 * off and free it. */
void n_sound_UpdatePairs(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (((s32)(s16)k_pairsOn >> (k_pairNext & 31)) & 1) {
        u32 st = JAL(0x8001222Cu, 0x80012D6Cu, (s32)GV(s16, 0x80062D50), A1, A2, A3);
        if ((st & 0xFF) == 3 && ((st & 0xFFFF) >> 8) == 3) {
            JAL(0x80012258u, 0x80012A48u, (s32)GV(s16, 0x80062D50), A1, A2, A3);
            k_pairsOn = (u16)(k_pairsOn & ~(1u << (k_pairNext & 31)));
        }
    }
    u32 n = k_pairNext + 1;
    u32 q = n / 3;
    k_pairNext = n - q * 3;
    V0 = q * 3;
}

/* 800122AC (t0, t1, t2, t3): the listener's panning tables. */
void n_sound_SetPanTables(void) {
    GV(u32, K_PAN_TABLES + 0) = A0;
    GV(u32, K_PAN_TABLES + 4) = A1;
    GV(u32, K_PAN_TABLES + 8) = A2;
    GV(u32, K_PAN_TABLES + 0xC) = A3;
}

/* Interpolate the four panning tables at direction dir (bits 8-11 the
 * entry, low byte the fraction) into diff (s16 x 4): next entry minus
 * this one, times the fraction / 256. Returns the entry offset. The loop
 * branch is at loop_pc. */
static u32 pan_diff(u32 dir, u32 diff, u32 loop_pc) {
    u32 e = (dir >> 8) & 0xF;
    u32 nx = ((e + 1) & 0xF) * 2, cur = e * 2;
    for (u32 t = 0; t < 4; t++) {
        u32 tab = GV(u32, K_PAN_TABLES + t * 4);
        GV(u16, diff + t * 2) = (u16)(GV(u16, tab + nx) - GV(u16, tab + cur));
    }
    u32 frac = dir & 0xFF;
    for (u32 i = 0;; i++) {
        u32 a = diff + (u32)((s32)(s16)i << 1);
        GV(u16, a) = (u16)(mul32(GV(s16, a), (s32)frac) >> 8);
        if (!((s16)(i + 1) < 4)) break;
        LOOP_POLL(loop_pc);
    }
    return cur;
}

/* 800122C4 (direction, distance volume, out s16 x 4): an effect's four
 * volumes (left/right of the pair's two voices) for a direction: the
 * panning tables interpolated (into 0x80062E80), scaled by the volume /
 * 256 and the effects volume (percent). */
void n_sound_PanVolumes(void) {
    u32 out = A2, vol = A1;
    u32 cur = pan_diff(A0, K_PAN_DIFF, 0x80012398u);
    s32 v = (s16)vol;
    s32 r[4];
    for (u32 t = 0; t < 4; t++) {
        u16 x = (u16)(GV(u16, GV(u32, K_PAN_TABLES + t * 4) + cur) + GV(u16, K_PAN_DIFF + t * 2));
        GV(u16, K_PAN_OUT + t * 2) = x;
    }
    for (u32 t = 0; t < 4; t++) {
        s32 p = mul32(mul32(GV(s16, K_PAN_OUT + t * 2), v) >> 8, k_sfxVol);
        r[t] = p;
        GS(u16, out + t * 2) = (u16)div100(p);
    }
    A1 = (u32)(r[0] >> 31);
    A2 = GV(u32, K_PAN_TABLES + 0xC) + cur;
    A3 = (u32)v;
    V0 = (u32)(r[3] >> 31);
}

/* 80012524 (pair, direction, distance volume, sound): start an effect on
 * a pair of voices. The sound (s16 x 5): bank, program, tone (the
 * pair's two tones are tone and tone + 1), note, pitch-bend... Builds two
 * voice attribute blocks on the stack (left voice at sp+0x50, right at
 * sp+0x68) from the tones' attributes (0x8001EB78 into sp+0x30 and
 * sp+0x10) and two volume blocks (sp+0x80, sp+0x90), then, holding the
 * voice lock (0x80024D78 / 0x80024DA4), sets both voices up, keys them
 * on (with reverb for tones marked 4) and marks the pair playing.
 * Returns the pair, or -2 when the lock was taken. */
void n_sound_PlayPair(void) {
    FRAME(0xE8);
    SAVE_S;
    GS(u32, sp + 0xD4) = in_s5;
    GS(u32, sp + 0xD8) = in_s6;
    GS(u32, sp + 0xE4) = R[31];
    GS(u32, sp + 0xE0) = in_fp;
    GS(u32, sp + 0xDC) = in_s7;
    GS(u32, sp + 0xD0) = in_s4;
    GS(u32, sp + 0xCC) = in_s3;
    GS(u32, sp + 0xC8) = in_s2;
    GS(u32, sp + 0xC4) = in_s1;
    GS(u32, sp + 0xC0) = in_s0;
    u32 a1 = A1, a2 = A2, snd = A3;
    R[21] = a1; R[22] = a2;
    GS(u16, sp + 0xA8) = (u16)A0;
    s32 bank = GS(s16, snd + 0), prog = GS(s16, snd + 2);
    u32 tone = GS(u16, snd + 4), note = GS(u16, snd + 6), bend = GS(u16, snd + 8);
    u32 bank_u = GS(u16, snd + 0);
    R[19] = (u32)bank; R[20] = (u32)prog; R[30] = tone;
    GS(u16, sp + 0xB8) = (u16)note;
    R[18] = bend; R[23] = bank_u;
    u32 prog_u = GS(u16, snd + 2);
    GS(u16, sp + 0xB0) = (u16)prog_u;
    u32 v = JAL(0x8001258Cu, 0x80025460u, bank, prog, A2, prog_u);
    s32 t0s = (s16)tone, t1s = (s16)(tone + 1);
    R[16] = (u32)t0s;
    GS(u16, sp + 0xB2) = (u16)v;
    JAL(0x800125ACu, 0x8001EB78u, bank, prog, t0s, sp + 0x30);
    R[17] = (u32)t1s;
    JAL(0x800125CCu, 0x8001EB78u, bank, prog, t1s, sp + 0x10);
    GS(u16, sp + 0x86) = 0x3F;
    GS(u16, sp + 0x96) = 0x3F;
    v = JAL(0x800125E8u, 0x8001EDB8u, bank, prog, t0s, A3);
    s32 bend_s = (s16)bend;
    R[18] = (u32)bend_s;
    s32 note_s = (s16)GS(u16, sp + 0xB8);
    GS(u16, sp + 0x88) = (u16)(v >> 3);
    R[16] = (u32)note_s;
    GS(u16, sp + 0x8A) = GS(u16, sp + 0x40);
    GS(u16, sp + 0x8C) = GS(u16, sp + 0x42);
    v = JAL(0x80012628u, 0x800220ECu, note_s, bend_s, GS(u8, sp + 0x34), GS(u8, sp + 0x35));
    R[21] = a1 << 16; R[22] = a2 << 16;
    GS(u16, sp + 0x84) = (u16)v;
    JAL(0x80012644u, 0x800122C4u, (s32)(s16)a1, (s32)(s16)a2, sp + 0xA0, A3);
    s32 p = mul32(GS(s16, sp + 0xA0) + GS(s16, sp + 0xA4), k_sfxVol);
    R[21] = 0x51EB851Fu;
    R[17] = 1;
    GS(u16, sp + 0x82) = 1;
    GS(u16, sp + 0x80) = (u16)div100(p);
    v = JAL(0x80012698u, 0x8001EDB8u, bank, prog, t1s, A3);
    GS(u16, sp + 0x98) = (u16)(v >> 3);
    GS(u16, sp + 0x9A) = GS(u16, sp + 0x20);
    GS(u16, sp + 0x9C) = GS(u16, sp + 0x22);
    v = JAL(0x800126C4u, 0x800220ECu, note_s, bend_s, GS(u8, sp + 0x14), GS(u8, sp + 0x15));
    s32 sfx = k_sfxVol;
    p = mul32(GS(s16, sp + 0xA2) + GS(s16, sp + 0xA6), sfx);
    GS(u16, sp + 0x54) = GS(u16, sp + 0x84);
    GS(u16, sp + 0x50) = GS(u16, sp + 0x46);
    GS(u16, sp + 0x94) = (u16)v;
    GS(u16, sp + 0x90) = 1;
    GS(u16, sp + 0x52) = (u16)bank_u;
    GS(u16, sp + 0x56) = GS(u16, sp + 0xB8);
    GS(u16, sp + 0x92) = (u16)div100(p);
    R[19] = 0;
    /* left voice: volume max(left, 1) as the effects volume; the
     * pan from the left/right ratio (64 the middle) */
    s32 l0 = GS(s16, sp + 0x80), l1 = GS(s16, sp + 0x82);
    u32 m = (l1 < l0) ? GS(u16, sp + 0x80) : GS(u16, sp + 0x82);
    GS(u16, sp + 0x5E) = (u16)div100(mul32((s16)m, sfx));
    s32 pan;
    if (l1 < l0) pan = mips_div(l1 << 6, l0);
    else pan = mips_div((0x3FFF - l0) << 6, l1);
    s32 pan_l = pan;
    s32 r0 = GS(s16, sp + 0x92), r1 = GS(s16, sp + 0x90);
    u32 rm = GS(u16, sp + 0x92);
    GS(u16, sp + 0x5A) = GS(u16, sp + 0xB0);
    GS(u16, sp + 0x68) = GS(u16, sp + 0x26);
    GS(u16, sp + 0x60) = (u16)pan;
    GS(u16, sp + 0x58) = (u16)tone;
    GS(u16, sp + 0x6A) = (u16)bank_u;
    GS(u16, sp + 0x5C) = GS(u16, sp + 0xB2);
    GS(u16, sp + 0x6C) = GS(u16, sp + 0x94);
    GS(u16, sp + 0x6E) = GS(u16, sp + 0xB8);
    if (r0 < r1) rm = GS(u16, sp + 0x90);
    GS(u16, sp + 0x76) = (u16)rm;
    GS(u16, sp + 0x76) = (u16)div100(mul32((s16)rm, k_sfxVol));
    if (r0 < r1) pan = mips_div(r0 << 6, r1);
    else pan = mips_div((0x3FFF - r1) << 6, r0);
    GS(u16, sp + 0x78) = (u16)pan;
    GS(u16, sp + 0x72) = GS(u16, sp + 0xB0);
    GS(u16, sp + 0x70) = (u16)(tone + 1);
    GS(u16, sp + 0x74) = GS(u16, sp + 0xB2);
    v = JAL(0x8001291Cu, 0x80024D78u, r0, rm, r1, pan_l);
    if ((s8)v == -1) { V0 = 0xFFFFFFFEu; RESTORE_S; return; }
    s32 pair = (s16)GS(u16, sp + 0xA8);
    R[17] = (u32)pair;
    u32 pv = K_PAIR_VOICE + (u32)pair * 2;
    R[16] = pv;
    JAL(0x80012958u, 0x800254C0u, (s32)GV(s16, pv), sp + 0x50, A2, A3);
    JAL(0x80012968u, 0x800254C0u, GV(s16, pv) + 1, sp + 0x68, A2, A3);
    JAL(0x80012974u, 0x80025264u, (s32)GV(s16, pv), sp + 0x80, A2, A3);
    JAL(0x80012984u, 0x80025264u, GV(s16, pv) + 1, sp + 0x90, A2, A3);
    u32 keys = K_PAIR_KEYS + (u32)pair * 4;
    R[18] = keys;
    JAL(0x800129A0u, 0x800250C8u, GV(u32, keys), A1, A2, A3);
    u32 rev = 0;
    if (GS(u8, sp + 0x11) & 4) rev |= 1u << ((GV(s16, pv) + 1) & 31);
    if (GS(u8, sp + 0x31) & 4) rev |= 1u << (GV(s16, pv) & 31);
    R[19] = rev;
    JAL(0x800129F8u, 0x80025194u, GV(u32, keys), rev, A2, A3);
    JAL(0x80012A00u, 0x80024DA4u, A0, A1, A2, A3);
    k_pairsOn = (u16)(k_pairsOn | (1u << (pair & 31)));
    V0 = (u32)pair;
    RESTORE_S;
}

/* 80012A48 (pair): key a pair's voices off and mark it free. Returns
 * the pair. */
void n_sound_StopPair(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    s32 pair = (s16)A0;
    R[16] = (u32)pair;
    JAL(0x80012A78u, K_SPU_SET_KEY, 0, GV(u32, K_PAIR_KEYS + (u32)pair * 4), A2, A3);
    k_pairsOn = (u16)(k_pairsOn & ~(1u << (pair & 31)));
    V0 = (u32)pair;
    RESTORE_S;
}

/* 80012AA4 (pair, direction, distance volume): move a playing effect:
 * the pair's volumes again for a new direction (0x80062E90 as
 * 0x800122C4 does), each voice taking its own side only. Returns the
 * pair. */
void n_sound_MovePair(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    u32 pair = A0, vol = A2;
    R[17] = pair;
    u32 cur = pan_diff(A1, K_MOVE_DIFF, 0x80012B88u);
    s32 v = (s16)vol;
    s32 r[4];
    for (u32 t = 0; t < 4; t++) {
        u16 x = (u16)(GV(u16, GV(u32, K_PAN_TABLES + t * 4) + cur) + GV(u16, K_MOVE_DIFF + t * 2));
        GV(u16, K_MOVE_OUT + t * 2) = x;
        r[t] = mul32(mul32((s16)x, v) >> 8, k_sfxVol);
    }
    s32 d[4];
    for (u32 t = 0; t < 4; t++) {
        d[t] = div100(r[t]);
        GV(u16, K_MOVE_OUT + t * 2) = (u16)d[t];
    }
    s32 p = (s16)pair;
    R[17] = (u32)p;
    u32 pv = K_PAIR_VOICE + ((u32)p << 1);
    R[16] = pv;
    s32 left = (s16)(d[0] + d[2]);
    JAL(0x80012D30u, K_SPU_VOICE_VOL, (s32)GV(s16, pv), left, 1, (u32)(r[2] >> 31));
    s32 right = (s16)(GV(u16, K_MOVE_OUT + 2) + GV(u16, K_MOVE_OUT + 6));
    JAL(0x80012D54u, K_SPU_VOICE_VOL, GV(s16, pv) + 1, 1, right, A3);
    V0 = (u32)p;
    RESTORE_S;
}

/* 80012D6C (pair): both voices' states, the first in the low byte. */
void n_sound_PairStatus(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x14) = in_s1;
    u32 pv = K_PAIR_VOICE + (u32)((s32)(s16)A0 << 1);
    R[16] = pv;
    R[17] = 1;
    u32 a = JAL(0x80012D9Cu, K_SPU_VOICE_STATUS, 1u << (GV(s16, pv) & 31), A1, A2, A3);
    s32 vn = GV(s16, pv);
    R[16] = a;
    u32 b = JAL(0x80012DB0u, K_SPU_VOICE_STATUS, 1u << ((vn + 1) & 31), A1, A2, A3);
    V0 = (a | (b << 8)) & 0xFFFF;
    RESTORE_S;
}

/* 80012DD0: the playing pairs' bits. */
void n_sound_PairsOn(void) { V0 = (u32)(s32)GV(s16, 0x80062D4E); }
