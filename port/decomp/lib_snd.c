/* libsnd (kernel): the sequencer tick.
 *
 *   0x8006466C  1 while a tick runs (no re-entry)
 *   0x80064670  which sequences are open (bit per sequence)
 *   0x80064678  per sequence, its separate-song table (0xB0 bytes each,
 *               +0x98 the pending commands)
 *   0x80064EF8  number of sequences (s16), 0x80064EFA songs per sequence
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libsnd_SeqCalledTbyT(void);
void n_libsnd_SeqPlayTick(void);
void n_libsnd_SeqTick(void);
void n_libsnd_SeqEvent(void);
void n_libsnd_SeqDelta(void);
void n_libsnd_UtPitchBend(void);
void n_libsnd_NotePitch(void);
void n_libsnd_ToneBasePitch(void);
void n_libsnd_Pitch(void);
void n_libsnd_SelectProg(void);
void n_libsnd_SeqSetVol(void);
void n_libsnd_ToneSetVol(void);
void n_libsnd_VabOpenHead(void);
void n_libsnd_ControlChange(void);
void n_libsnd_UtKeyOnV(void);
void n_libsnd_KeyOnBits(void);
void n_libsnd_VoiceFlagBits(void);
void n_libsnd_TickStart(void);
void n_libsnd_SeqOpen(void);
void n_libsnd_SsSeqOpen(void);
void n_libsnd_UtGetProgAtr(void);
void n_libsnd_Nrpn98(void);
void n_libsnd_SetTickMode(void);
void n_libsnd_ResetVoices(void);
void n_libsnd_VoiceOff(void);
void n_libsnd_VoiceKeyOn(void);
void n_libsnd_NoteOn(void);
void n_libsnd_UtKeyOn(void);
void n_libsnd_VoiceSetAttr(void);
void n_libsnd_VagAddr(void);
void n_libsnd_UtGetVagAtr(void);
void n_libsnd_VoiceSetVol(void);
void n_libsnd_NoteHook(void);
void n_libsnd_ByteHook(void);
void n_libsnd_FindTones(void);
void n_libsnd_BendAll(void);
void n_libsnd_NoteOff(void);
void n_libsnd_VoiceClear(void);
void n_libsnd_VoiceSetup(void);
void n_libsnd_VoiceAlloc(void);

/* 80019868 SsSeqCalledTbyT: run one tick: the voice update (80020BF8),
 * then for each song of each open sequence its pending commands (+0x98):
 * 1 play a tick (80019D88, then 0x10 / 0x20 fades 80019AD8, 0x40 / 0x80
 * 8001DED8), 2 pause (80019CE8), 8 replay (8001A658), 4 stop (8001D6B8,
 * clearing the commands). */
/* Callees see (and save) the original's s-registers, so they are kept as
 * it had them: s0 the song's offset, s1 the song (set when it plays), s2
 * and fp the sequence's table entry, s3 song << 16, s4 / s5 / s7 the
 * sequence (s16, << 16, plain), s6 the song. */
void n_libsnd_SeqCalledTbyT(void) {
    FRAME(0x38);
    u32 saved[9] = { R[16], R[17], R[18], R[19], R[20], R[21], R[22], R[23], R[30] };
    for (int i = 0; i < 8; i++) MEM_W32(sp + 0x10 + i * 4, saved[i]);
    MEM_W32(sp + 0x30, R[30]);
    MEM_W32(sp + 0x34, R[31]);
    if (GV(u32, 0x8006466Cu) == 1) return;
    GV(u32, 0x8006466Cu) = 1;
    R[23] = 0;
    gcall(0x80020BF8u, R[4], R[5], R[6], R[7]);
    if ((s16)GV(u16, 0x80064EF8u) > 0) {
        u32 tbl = 0x80064678u;
        for (s32 s = 0;;) {
            R[30] = tbl; R[23] = (u32)s;
            if ((GV(u32, 0x80064670u) & (1u << (s & 31))) && (s16)GV(u16, 0x80064EFAu) > 0) {
                u32 off = 0;
                R[22] = 0; R[18] = tbl; R[21] = (u32)s << 16; R[20] = (u32)(s32)(s16)s;
                for (s32 t = 0;;) {
                    u32 sq = (u32)(s32)(s16)s, tr = (u32)t;
                    R[16] = off; R[19] = (u32)t << 16; R[22] = (u32)t;
#define CMDS (MEM_R32(MEM_R32(tbl) + off + 0x98))
                    if (CMDS & 1) {
                        R[17] = tr;
                        gcall(0x80019D88u, sq, tr, R[6], R[7]);
                        if (CMDS & 0x10) gcall(0x80019AD8u, sq, tr, R[6], R[7]);
                        if (CMDS & 0x20) gcall(0x80019AD8u, sq, tr, R[6], R[7]);
                        if (CMDS & 0x40) gcall(0x8001DED8u, sq, tr, R[6], R[7]);
                        if (CMDS & 0x80) gcall(0x8001DED8u, sq, tr, R[6], R[7]);
                    }
                    if (CMDS & 2) gcall(0x80019CE8u, sq, tr, R[6], R[7]);
                    if (CMDS & 8) gcall(0x8001A658u, sq, tr, R[6], R[7]);
                    if (CMDS & 4) {
                        gcall(0x8001D6B8u, sq, tr, R[6], R[7]);
                        MEM_W32(MEM_R32(tbl) + off + 0x98, 0);
                    }
#undef CMDS
                    t++;
                    off += 0xB0;
                    if (!(t < (s16)GV(u16, 0x80064EFAu))) break;
                    LOOP_POLL(0x80019A80u);
                }
            }
            s++;
            tbl += 4;
            if (!(s < (s16)GV(u16, 0x80064EF8u))) break;
            LOOP_POLL(0x80019A98u);
        }
    }
    GV(u32, 0x8006466Cu) = 0;
    R[16] = saved[0]; R[17] = saved[1]; R[18] = saved[2]; R[19] = saved[3]; R[20] = saved[4];
    R[21] = saved[5]; R[22] = saved[6]; R[23] = saved[7]; R[30] = saved[8];
}

/* ---- reading a song (SMF-like data, 0xB0-byte song records) ----
 *   +0x00 read pointer        +0x10 the loop's start
 *   +0x16 running status      +0x17 its channel
 *   +0x52 s16 wait            +0x54 s16 ticks per step
 *   +0x88 the time so far     +0x90 time to the next event
 *   +0x98 the commands (0x400 looping, 1 playing)
 * The MIDI handlers are hooks: 0x800645D8 note (seq, song, note, vel),
 * 0x800645DC program, 0x800645E0 pitch bend, 0x800645E4 meta event,
 * 0x800645E8 control change. */
#define SONG(seq, song) (MEM_R32(0x80064678u + (u32)(seq) * 4) + (u32)(song) * 0xB0)
#define F_SEQ_END       0x80019EB4u   /* (seq, song, code): the end of the data or loop */

/* Write the caller's s-registers and the return address into the frame at
 * sp+off as the original's prologue saves them (callees can read stale
 * stack: see SeqCalledTbyT). */
static void save_regs(u32 sp, u32 off, int n, u32 ra) {
    for (int i = 0; i < n; i++) MEM_W32(sp + off + (u32)i * 4, R[16 + i]);
    MEM_W32(sp + off + (u32)n * 4, ra);
}

/* 80019D88: play a tick of (seq, song) (80019DB8, the arguments as s16). */
void n_libsnd_SeqPlayTick(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, 0x80019940u);
    gcall(0x80019DB8u, (u32)(s32)(s16)A0, (u32)(s32)(s16)A1, R[6], R[7]);
}

/* 80019DB8: a tick of a song: count down the step (+0x90 by +0x54, the
 * wait +0x52 first); when it runs out, read events (8001A0F8) until one
 * is due later, and carry the remainder. */
void n_libsnd_SeqTick(void) {
    FRAME(0x28);
    u32 sv[4] = { R[16], R[17], R[18], R[19] };
    save_regs(sp, 0x10, 4, 0x80019DA4u);
    u32 seq16 = A0 << 16, song16 = A1 << 16;
    s32 seq = (s32)seq16 >> 16, song = (s32)song16 >> 16;
    u32 p = SONG(seq, song);
    s32 div = (s16)MEM_R16(p + 0x54);
    s32 cnt = (s32)MEM_R32(p + 0x90);
    s32 d = (s32)((u32)cnt - (u32)div);
    if (d > 0) {
        s16 w = (s16)MEM_R16(p + 0x52);
        V0 = (u32)MEM_R16(p + 0x52) - 1;
        if (w > 0) MEM_W16(p + 0x52, (u16)V0);
        else if (w != 0) MEM_W32(p + 0x90, (u32)d);
        else {
            MEM_W16(p + 0x52, MEM_R16(p + 0x54));
            V0 = MEM_R32(p + 0x90) - 1;
            MEM_W32(p + 0x90, V0);
        }
        return;
    }
    if (div < cnt) { V0 = 1; return; }
    u32 acc = (u32)cnt;
    R[16] = acc; R[17] = p; R[18] = song16; R[19] = seq16;
    for (;;) {
        gcall(0x8001A0F8u, (u32)seq, (u32)song, R[6], R[7]);
        u32 t = MEM_R32(p + 0x90);
        if (t == 0) { LOOP_POLL(0x80019E78u); continue; }
        acc += t; R[16] = acc;
        s32 dv = (s16)MEM_R16(p + 0x54);
        if ((s32)acc < dv) { LOOP_POLL(0x80019E8Cu); continue; }
        V0 = acc - (u32)dv;
        MEM_W32(p + 0x90, V0);
        break;
    }
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2]; R[19] = sv[3];
}

/* Call a MIDI hook (seq, song, a2, a3). */
static void hook(u32 at, s32 seq, s32 song, u32 a2, u32 a3) {
    gcall(MEM_R32(at), (u32)seq, (u32)song, a2, a3);
}

/* 8001A0F8 (_SsGetSeqData): read one event of (seq, song) and pass it to
 * its hook: a status byte (channel kept in +0x17, status in +0x16) or
 * data under the running status. Notes read their delta time
 * (8001A4A8) into +0x90 before the hook. Back at the loop's start while
 * looping: 80019EB4 and -1; end of track (meta 0x2F): 80019EB4 and 1;
 * otherwise 0. */
void n_libsnd_SeqEvent(void) {
    FRAME(0x30);
    u32 sv[6] = { R[16], R[17], R[18], R[19], R[20], R[21] };
    save_regs(sp, 0x10, 6, 0x80019E70u);
    s32 seq = (s16)A0, song = (s16)A1;
    u32 tab = 0x80064678u + (u32)seq * 4;
    u32 p = SONG(seq, song);
    u32 ptr = MEM_R32(p);
    u32 b = MEM_R8(ptr);
    ptr++;
    MEM_W32(p, ptr);
    u32 r6 = tab, r7 = ptr;               /* what a hook finds in a2 / a3 when not given */
    R[18] = b; R[19] = p; R[21] = 0;
    u32 ret = 0;
    if ((MEM_R32(SONG(seq, song) + 0x98) & 0x401) == 0x401) {
        u32 loop = MEM_R32(p + 0x10);
        if (ptr == loop + 1) {
            gcall(F_SEQ_END, (u32)seq, (u32)song, MEM_R8(loop + 1), r7);
            ret = (u32)-1;
            goto out;
        }
    }
    u32 status;
    if (b & 0x80) {
        MEM_W8(p + 0x17, (u8)(b & 0xF));
        status = b & 0xF0;
        if (status == 0xF0) status = 0xFF;
        else if (status != 0xC0 && status != 0x90 && status != 0xB0 && status != 0xE0) goto out;
        MEM_W8(p + 0x16, (u8)status);
        r6 = status;
        switch (status) {
        case 0x90: {
            u32 q = MEM_R32(p);
            u32 note = MEM_R8(q); MEM_W32(p, q + 1);
            u32 vel = MEM_R8(q + 1); MEM_W32(p, q + 2);
            R[17] = (u32)seq; R[16] = (u32)song; R[18] = note; R[20] = vel;
            MEM_W32(p + 0x90, gcall(0x8001A4A8u, (u32)seq, (u32)song, r6, r7));
            hook(0x800645D8u, seq, song, note, vel);
            goto out;
        }
        case 0xB0: case 0xC0: {
            u32 q = MEM_R32(p);
            u32 d = MEM_R8(q); MEM_W32(p, q + 1);
            hook(status == 0xB0 ? 0x800645E8u : 0x800645DCu, seq, song, d, r7);
            goto out;
        }
        case 0xE0:
            MEM_W32(p, MEM_R32(p) + 1);
            hook(0x800645E0u, seq, song, r6, r7);
            goto out;
        default: {                        /* 0xFF, a meta event */
            u32 q = MEM_R32(p);
            b = MEM_R8(q); MEM_W32(p, q + 1);
            break;
        }
        }
    } else {
        status = MEM_R8(p + 0x16);
        switch (status) {
        case 0x90: {
            u32 q = MEM_R32(p);
            u32 vel = MEM_R8(q); MEM_W32(p, q + 1);
            R[17] = (u32)seq; R[16] = (u32)song; R[20] = vel;
            MEM_W32(p + 0x90, gcall(0x8001A4A8u, (u32)seq, (u32)song, r6, r7));
            hook(0x800645D8u, seq, song, b, vel);
            goto out;
        }
        case 0xB0: hook(0x800645E8u, seq, song, b, r7); goto out;
        case 0xC0: hook(0x800645DCu, seq, song, b, r7); goto out;
        case 0xE0: hook(0x800645E0u, seq, song, r6, r7); goto out;
        case 0xFF: break;
        default: goto out;
        }
    }
    /* a meta event, its type in b */
    if (b == 0x2F) {
        R[21] = 1; ret = 1;
        gcall(F_SEQ_END, (u32)seq, (u32)song, 0x2F, r7);
    } else {
        hook(0x800645E4u, seq, song, b, r7);
    }
out:
    V0 = ret;
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2]; R[19] = sv[3]; R[20] = sv[4]; R[21] = sv[5];
}

/* 8001A4A8: read a delta time (a variable-length number) of (seq, song):
 * returns it times 10, also added to the song's time (+0x88). 0 leaves
 * the time alone. */
void n_libsnd_SeqDelta(void) {
    u32 p = SONG((s32)(s16)A0, (s32)(s16)A1);
    u32 ptr = MEM_R32(p);
    u32 v = MEM_R8(ptr);
    MEM_W32(p, ptr + 1);
    if (v == 0) { V0 = 0; return; }
    if (v & 0x80) {
        v &= 0x7F;
        for (;;) {
            u32 q = MEM_R32(p);
            u32 c = MEM_R8(q);
            MEM_W32(p, q + 1);
            v = (v << 7) + (c & 0x7F);
            if (!(c & 0x80)) break;
            LOOP_POLL(0x8001A524u);
        }
    }
    V0 = v * 10;
    MEM_W32(p + 0x88, MEM_R32(p + 0x88) + V0);
    R[3] = MEM_R32(p + 0x88);
}

/* 80022DA8 SsUtPitchBend(voice, vab, prog, note, bend): if the voice
 * (0x38-byte records at 0x80064F20: +0 vab, +4 note, +6 tone, +8 prog,
 * -2 its note's pitch base) still plays that, bend it: bend - 64 scaled
 * by the tone's bend range (tone records at *0x80065730, 0x20 bytes: +0xC
 * down, +0xD up, in 1/64 notes), the new pitch (80022084 (note, fine))
 * stored for the voice (0x80065474 + voice * 16) and its update flag 4
 * set (0x80065450 + voice). Returns 1, or 0 when the voice moved on. */
void n_libsnd_UtPitchBend(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    u32 raw = A0;
    s32 v = (s16)A0;
    u32 bend = MEM_R32(sp + 0x28) - 64;
    u32 rec = 0x80064F20u + (u32)v * 0x38;
    V0 = 0;
    if ((s16)GV(u16, rec) != (s16)A1) return;
    if ((s16)GV(u16, rec + 8) != (s16)A2) return;
    if ((s16)GV(u16, rec + 4) != (s16)A3) return;
    u32 tone = (GV(u16, rec + 6) + ((u32)(s32)GV(s8, 0x80065747u) << 4)) & 0xFFFF;
    u32 note = GV(u16, rec - 2);
    s32 b = (s16)bend;
    u32 fine;
    u32 trec = GV(u32, 0x80065730u) + tone * 32;
    if (b > 0) {
        s32 x = b * (s32)GV(u8, trec + 0xD);
        s32 q = x / 63;
        note += (u32)q;
        fine = (u32)(x - q * 63) << 1;
    } else if (b == 0) {
        fine = 0;
    } else {
        s32 x = b * (s32)GV(u8, trec + 0xC);
        s32 q = (x >= 0 ? x : x + 0x3F) >> 6;
        note = note + (u32)q - 1;
        fine = ((u32)(x - (q << 6)) << 1) + 0x7F;
    }
    R[16] = (u32)v;
    GV(u16, 0x8006574Cu + 0xC) = (u16)raw;
    GV(u8, 0x8006574Cu) = GV(u8, rec + 6);
    u32 pitch = gcall(0x80022084u, note & 0xFFFF, fine & 0xFFFF, R[6], R[7]);
    GV(u16, 0x80065474u + ((u32)v << 4)) = (u16)pitch;
    GV(u8, 0x80065450u + (u32)v) |= 4;
    V0 = 1;
    R[16] = s0;
}

/* 80022084 (note, fine): the pitch for a note on the current tone (the
 * VAB's tone records *0x80065730, 0x20 bytes, picked by 0x80065747 * 16 +
 * 0x8006574C: +4 its centre note, +5 its fine tune), by 800220EC. */
void n_libsnd_NotePitch(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    s32 i = (s32)(s16)(u16)((s32)GV(s8, 0x8006574Cu) + ((s32)GV(s8, 0x80065747u) << 4));
    u32 t = GV(u32, 0x80065730u) + (u32)(i << 5);
    V0 = gcall(0x800220ECu, (u32)(s32)(s16)A0, (u32)(s32)(s16)A1, MEM_R8(t + 4), MEM_R8(t + 5)) & 0xFFFF;
}

/* 80022038: the pitch of the note 0x80065742 at fine 0, for the centre
 * 0x80065750 and tune 0x80065751 (at most 0x7F), by 800220EC. */
void n_libsnd_ToneBasePitch(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 tune = GV(u8, 0x80065751u);
    if (!(tune < 0x80)) tune = 0x7F;
    V0 = gcall(0x800220ECu, (u32)(s32)GV(s8, 0x80065742u), 0, GV(u8, 0x80065750u), tune) & 0xFFFF;
}

/* 800220EC (note, fine, centre, tune): the SPU pitch for a note: the
 * semitone (note - centre, with fine + tune in 1/128ths) split into an
 * octave and a step, the step's frequency from the tables 0x8005A094
 * (semitones) x 0x8005A0AC (1/128ths), shifted down by the octave
 * (rounded); 0x3FFF from the octave above the centre on. */
void n_libsnd_Pitch(void) {
    s32 f = (s16)(u16)((A3 & 0xFF) + A1);
    s32 q = (f >= 0 ? f : f + 0x7F) >> 7;
    u32 semi = A0 + (u32)q - (A2 & 0xFF);
    s32 frac = (s16)(u16)((u32)f - ((u32)q << 7));
    u32 sum = semi;
    if (frac < 0) {
        frac = (s16)(u16)(frac + 0x80);
        semi -= 1;
        sum = semi + (u32)((frac >= 0 ? frac : frac + 0x7F) >> 7);
    }
    s32 s = (s16)(u16)sum;
    s32 oct = s / 12;
    s32 step = (s16)(u16)(s - oct * 12);
    s32 sh = oct - 2;
    if (step < 0) { step += 12; sh = oct - 3; }
    u32 a = GV(u16, 0x8005A094u + (u32)((s32)(s16)step * 2));
    u32 b = GV(u16, 0x8005A0ACu + (u32)((s32)(s16)frac * 2));
    u32 p = (u32)((s32)(a * b) >> 16);
    s32 o = (s16)(u16)sh;
    if (o < 0) {
        u32 n = (u32)-o;
        p = (p + (1u << ((n - 1) & 31))) >> (n & 31);
        V0 = p & 0xFFFF;
    } else {
        V0 = 0x3FFF;
    }
}

/* 80023F78 (vab, prog): make a VAB's program current (vab < 16, open:
 * 0x80065760 + vab == 1; prog < 0x80065722): its tables (0x80065658 /
 * 0x80065698 / 0x800656E0 per VAB, into 0x80065724 / 0x8006572C /
 * 0x80065730), the numbers (0x80065741, 0x80065746) and the program's
 * first tone (+8 of its 16-byte record, 0x80065747). 0, else -1. */
void n_libsnd_SelectProg(void) {
    u32 vab = A0, prog = A1;
    V0 = (u32)-1;
    if (!((vab & 0xFFFF) < 0x10)) return;
    s32 v = (s16)vab;
    if (GV(u8, 0x80065760u + (u32)v) != 1) return;
    s32 p = (s16)prog;
    if (!(p < (s16)GV(u16, 0x80065722u))) return;
    u32 i = (u32)v << 2;
    u32 t3 = GV(u32, 0x80065698u + i), progs = GV(u32, 0x80065658u + i), tones = GV(u32, 0x800656E0u + i);
    GV(u8, 0x80065741u) = (u8)vab;
    GV(u8, 0x80065746u) = (u8)prog;
    GV(u32, 0x80065730u) = tones;
    GV(u32, 0x8006572Cu) = t3;
    GV(u32, 0x80065724u) = progs;
    GV(u8, 0x80065747u) = MEM_R8(((u32)p << 4) + progs + 8);
    V0 = 0;
}

/* 80022818: key off the current voice (0x80065758): its state cleared
 * (+0x17, +0x0 / +0x4 of the 0x38-byte record), its bit added to the
 * key-off mask (0x80065638) and taken out of the key-on one (0x80064F00). */
void n_libsnd_VoiceOff(void) {
    u32 v = GV(u16, 0x80065758u);
    u32 lo = v < 0x10 ? 1u << (v & 31) : 0, hi = v < 0x10 ? 0 : 1u << ((v - 0x10) & 31);
    u32 rec = v * 0x38;
    GV(u8, 0x80064F2Du + rec) = 0;
    u32 off_lo = GV(u16, 0x80065638u), off_hi = GV(u16, 0x8006563Au);
    GV(u16, 0x80064F14u + rec) = 0;
    GV(u16, 0x80064F10u + rec) = 0;
    off_lo |= lo;
    GV(u16, 0x80065638u) = (u16)off_lo;
    GV(u16, 0x80064F00u) = (u16)(GV(u16, 0x80064F00u) & ~off_lo);
    off_hi |= hi;
    GV(u16, 0x8006563Au) = (u16)off_hi;
    V0 = GV(u16, 0x80064F02u) & ~off_hi;
    GV(u16, 0x80064F02u) = (u16)V0;
}

/* 8001CC78 (seq, song, note, vel): the sequencer's note event: velocity
 * 0 is a note off (80021DA4), otherwise a note on (80021888, with the
 * velocity and the channel's volume +0x27) unless the channel is muted
 * (+0x80 bit per channel); both get (seq | song << 8, the song's VAB
 * +0x26, the channel's program +0x37, note). */
void n_libsnd_NoteHook(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x18, R[31]);
    u32 seq = A0, song = A1, note = A2, vel = A3 & 0xFF;
    u32 p = SONG((s32)(s16)seq, (s32)(s16)song);
    u32 ch = MEM_R8(p + 0x17);
    u32 c = p + ch;
    u32 vol = MEM_R8(c + 0x27);
    u32 id = (u32)(s32)(s16)(u16)(seq | (song << 8));
    if (vel == 0) {
        gcall(0x80021DA4u, id, (u32)(s32)(s8)MEM_R8(p + 0x26), MEM_R8(c + 0x37), note & 0xFF);
        return;
    }
    if ((((s32)(s16)MEM_R16(p + 0x80)) >> (ch & 31)) & 1) { V0 = 1; return; }
    MEM_W32(sp + 0x10, vel);
    MEM_W32(sp + 0x14, vol);
    gcall(0x80021888u, id, (u32)(s32)(s8)MEM_R8(p + 0x26), MEM_R8(c + 0x37), note & 0xFF);
}

/* 8001C7C8 (seq, song): read one more data byte of the song and pass it
 * to 80022F94 (seq | song << 8, VAB, channel program, byte); then the
 * delta time into +0x90. The original's s-registers. */
void n_libsnd_ByteHook(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x1C, R[31]);
    u32 id = (u32)(s32)(s16)(u16)(A0 | (A1 << 8));
    R[18] = (u32)(s32)(s16)A0; R[17] = (u32)(s32)(s16)A1;
    R[16] = SONG((s32)R[18], (s32)R[17]);
    u32 p = R[16];
    u32 q = MEM_R32(p);
    s32 vab = (s8)MEM_R8(p + 0x26);
    u32 ch = MEM_R8(p + 0x17);
    u32 b = MEM_R8(q);
    MEM_W32(p, q + 1);
    gcall(0x80022F94u, id, (u32)vab, MEM_R8(ch + p + 0x37), b);
    MEM_W32(R[16] + 0x90, gcall(0x8001A4A8u, R[18], R[17], R[6], R[7]));
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 80023908 (tones, priorities): the current program's tones (0x80065740
 * of them) whose key range (+6 .. +7) holds the note 0x80065742: their
 * numbers into tones, their priorities (+0x16) into priorities.
 * Returns how many. */
void n_libsnd_FindTones(void) {
    u32 out = A0, pri = A1;
    u32 n = 0;
    if ((s32)GV(s8, 0x80065740u) > 0) {
        for (u32 i = 0;;) {
            s32 k = ((s32)GV(s8, 0x80065747u) << 4) + (s32)(s8)(u8)i;
            u32 t = (u32)(k << 5) + GV(u32, 0x80065730u);
            s32 note = GV(s8, 0x80065742u);
            if (!(note < (s32)MEM_R8(t + 6)) && !((s32)MEM_R8(t + 7) < note)) {
                u32 j = n & 0xFF;
                n++;
                MEM_W8(pri + j, MEM_R8(t + 0x16));
                MEM_W8(out + j, (u8)i);
            }
            i++;
            if (!((s32)(s8)(u8)i < (s32)GV(s8, 0x80065740u))) break;
            LOOP_POLL(0x800239BCu);
        }
    }
    V0 = n & 0xFF;
}

/* 80022F94 (id, vab, prog, bend): pitch-bend every voice playing that
 * program (SsUtPitchBend each; the id kept in 0x80065754, the program
 * made current). Returns how many were bent. The original's s-registers. */
void n_libsnd_BendAll(void) {
    FRAME(0x38);
    u32 sv[7];
    for (int i = 0; i < 7; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x18 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x34, R[31]);
    R[18] = A0; R[20] = A1 << 16; R[19] = A2 << 16; R[21] = A3;
    gcall(0x80023F78u, (u32)((s32)R[20] >> 16), (u32)((s32)R[19] >> 16), R[6], R[7]);
    R[17] = 0;
    GV(u16, 0x80065754u) = (u16)R[18];
    R[16] = 0;
    if ((s32)GV(s8, 0x80065734u) > 0) {
        R[22] = R[18] << 16;
        R[18] = R[21] & 0xFFFF;
        for (;;) {
            MEM_W32(sp + 0x10, R[18]);
            u32 r = gcall(0x80022DA8u, (u32)(s32)(s16)R[17], (u32)((s32)R[22] >> 16),
                          (u32)((s32)R[20] >> 16), (u32)((s32)R[19] >> 16));
            R[16] += (u32)(s32)(s16)r;
            R[17]++;
            if (!((s32)(s16)R[17] < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x8002303Cu);
        }
    }
    V0 = R[16];
    for (int i = 0; i < 7; i++) R[16 + i] = sv[i];
}

/* 80021DA4 (id, vab, prog, note): release every voice playing that
 * note of that program (not reserved: 0x80059874): a voice still in its
 * attack (+0x10 not 0xFF) is keyed off (VoiceOff), the others handed to
 * 800227D8. Returns how many. The original's s-registers. */
void n_libsnd_NoteOff(void) {
    FRAME(0x30);
    u32 sv[7];
    for (int i = 0; i < 7; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x2C, R[31]);
    MEM_W32(sp + 0x28, sv[6]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x18, sv[2]);
    R[16] = 0; R[17] = 0;
    if ((s32)GV(s8, 0x80065734u) > 0) {
        R[22] = 1; R[21] = A3 & 0xFFFF;
        R[20] = (u32)(s32)(s16)A2; R[19] = (u32)(s32)(s16)A0; R[18] = (u32)(s32)(s16)A1;
        for (;;) {
            u32 v = R[16] & 0xFF;
            u32 rec = v * 0x38;
            if (!(GV(u32, 0x80059874u) & (R[22] << (v & 31)))
                && (u32)(s32)(s16)GV(u16, 0x80064F1Eu + rec) == R[21]
                && (u32)(s32)(s16)GV(u16, 0x80064F24u + rec) == R[20]
                && (u32)(s32)(s16)GV(u16, 0x80064F20u + rec) == R[19]
                && (u32)(s32)(s16)GV(u16, 0x80064F28u + rec) == R[18]) {
                if ((u32)(s32)(s16)GV(u16, 0x80064F10u + rec) != 0xFF) {
                    GV(u16, 0x80065758u) = (u16)v;
                    gcall(0x80022818u, 0, R[5], R[6], R[7]);
                    R[17]++;
                } else {
                    R[17]++;
                    gcall(0x800227D8u, v, R[5], R[6], R[7]);
                }
            }
            R[16]++;
            if (!((s32)(R[16] & 0xFF) < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x80021ED0u);
        }
    }
    V0 = R[17];
    for (int i = 0; i < 7; i++) R[16 + i] = sv[i];
}

/* 800227D8 (voice): forget a voice's note (+0x17 of its record, +0x10
 * and +0x14 of the 0x38-byte records at 0x80064F00). */
void n_libsnd_VoiceClear(void) {
    u32 rec = (A0 & 0xFF) * 0x38;
    GV(u8, 0x80064F2Du + rec) = 0;
    GV(u16, 0x80064F10u + rec) = 0;
    GV(u16, 0x80064F14u + rec) = 0;
    V0 = rec;
}

/* 80020A08: set up the current voice (0x80065758) for the current tone:
 * envelope 0x7FFF, out of the 16 silent-tick masks; its sample address
 * (+0xC / +0xE of the program's 16-byte record by the tone 0x80065756)
 * and ADSR (+0x10, +0x12 of the tone, the release rate plus 0x800656D8,
 * at most 0x1F) into its attributes (0x80065470 + voice * 16), flagged
 * 8 and 0x30 (0x80065450 + voice). */
void n_libsnd_VoiceSetup(void) {
    u32 v16 = GV(u16, 0x80065758u);
    u32 x8 = v16 << 3;
    GV(u16, 0x80064F16u + (u32)((s32)(s16)v16 * 0x38)) = 0x7FFF;
    u32 w = 0x800655F8u;
    for (s32 i = 0;;) {
        i++;
        GV(u32, w) &= ~(1u << ((u32)(s32)(s16)GV(u16, 0x80065758u) & 31));
        if (!(i < 16)) break;
        w += 4;
        LOOP_POLL(0x80020A70u);
    }
    u32 t = GV(u16, 0x80065756u);
    s32 k = (s32)(s16)t - 1;
    k = (k + (s32)((u32)k >> 31)) >> 1;
    u32 pe = ((u32)k << 4) + GV(u32, 0x80065724u);
    u32 addr = (t & 1) ? MEM_R16(pe + 0xC) : MEM_R16(pe + 0xE);
    u32 off = (u32)((s32)(x8 << 16) >> 15);
    GV(u16, 0x80065476u + off) = (u16)addr;
    GV(u8, 0x80065450u + (u32)(s32)(s16)GV(u16, 0x80065758u)) |= 8;
    u32 tone = ((u32)(((s32)GV(s8, 0x80065747u) << 4) + (s32)GV(s8, 0x8006574Cu)) << 5) + GV(u32, 0x80065730u);
    GV(u16, 0x80065478u + off) = MEM_R16(tone + 0x10);
    u32 a2 = MEM_R16(tone + 0x12);
    u32 rr = GV(u16, 0x800656D8u) + (a2 & 0x1F);
    if (!((s32)(s16)rr < 32)) rr = 0x1F;
    GV(u16, 0x8006547Au + off) = (u16)(rr | (a2 & 0xFFE0));
    u32 f = 0x80065450u + (u32)(s32)(s16)GV(u16, 0x80065758u);
    V0 = GV(u8, f) | 0x30;
    GV(u8, f) = (u8)V0;
}

/* 80020798: pick a voice for a new note (of priority 0x8006574F): the
 * first free one (not reserved 0x80059874, no note +0x17, envelope +0x16
 * 0); else the one of lowest priority (+0x2A), on a tie the quietest
 * envelope, then the oldest (+0x12); none: the voice count. Every other
 * voice ages by one, the chosen one starts fresh with this priority.
 * Returns its number (the count when none). */
void n_libsnd_VoiceAlloc(void) {
    u32 found = 0x63, best_env = 0xFFFF, ties = 0, best_age = 0, cand = 0x63;
    u32 pri = (u32)(s32)GV(s8, 0x8006574Fu);
    s32 n = GV(s8, 0x80065734u);
#define REC(v) ((v) * 0x38)
    if (n > 0) {
        u32 res = GV(u32, 0x80059874u);
        for (u32 i = 0;;) {
            u32 v = i & 0xFF, r = REC(v);
            if (!(res & (1u << (v & 31)))) {
                if (GV(s8, 0x80064F2Du + r) == 0 && GV(u16, 0x80064F16u + r) == 0) { found = i; break; }
                s32 p = (s16)GV(u16, 0x80064F2Au + r);
                u32 lim = pri & 0xFFFF;
                if (p < (s32)lim) {
                    pri = GV(u16, 0x80064F2Au + r);
                    cand = i;
                    best_env = GV(u16, 0x80064F16u + r);
                    best_age = GV(u16, 0x80064F12u + r);
                    ties = 1;
                } else if ((u32)p == lim) {
                    u32 be = best_env & 0xFFFF, env = GV(u16, 0x80064F16u + r);
                    ties++;
                    if (env < be) {
                        best_age = GV(u16, 0x80064F12u + r);
                        best_env = env;
                        cand = i;
                    } else if (env == be && (s32)best_age < (s32)(s16)GV(u16, 0x80064F12u + r)) {
                        best_age = GV(u16, 0x80064F12u + r);
                        cand = i;
                    }
                }
            }
            i++;
            if (!((s32)(i & 0xFF) < n)) break;
            LOOP_POLL(0x80020908u);
        }
    }
    if ((found & 0xFF) == 0x63) {
        found = cand;
        if ((ties & 0xFF) == 0) found = GV(u8, 0x80065734u);
    }
    n = GV(s8, 0x80065734u);
    if ((s32)(found & 0xFF) < n) {
        if (n > 0) {
            u32 res = GV(u32, 0x80059874u);
            for (u32 j = 0;;) {
                u32 v = j & 0xFF;
                if (!(res & (1u << (v & 31)))) GV(u16, 0x80064F12u + REC(v)) += 1;
                j++;
                if (!((s32)(j & 0xFF) < n)) break;
                LOOP_POLL(0x800209A8u);
            }
        }
        u32 r = REC(found & 0xFF);
        GV(u16, 0x80064F12u + r) = 0;
        GV(u16, 0x80064F3Au + r) = 0;
        GV(u16, 0x80064F2Eu + r) = 0;
        GV(u16, 0x80064F2Au + r) = (u16)(s16)GV(s8, 0x8006574Fu);
    }
#undef REC
    V0 = found & 0xFF;
}

static inline u32 mulhu32(u32 a, u32 b) { return (u32)(((uint64_t)a * b) >> 32); }
static inline u32 mullo32(u32 a, u32 b) { return (u32)((s32)a * (s64)(s32)b); }
/* The compiler's unsigned division by a constant: magic m, shift k. */
static inline u32 udiv_m(u32 x, u32 m, int k) { u32 h = mulhu32(x, m); return (h + ((x - h) >> 1)) >> k; }

/* 800228D8 (-, pitch): libsnd's key on for the voice in 0x80065758:
 * the volume from the sequence, master and note volumes and the voice's
 * tone (0x80064678 table; voice mode 0x21 skips the tone volume and the
 * squaring), the three pans (0x8006574E, 4B, 45), mono (0x80065720 1:
 * the louder side both), squared to the SPU's curve; the voice's
 * pitch, volumes and dirty bits (0x80065450), the key-on, reverb (by
 * 0x80065752 bit 2) and key-off masks. V0 as the original. */
void n_libsnd_VoiceKeyOn(void) {
    const u32 g = 0x80065758u;
    const u32 pitch = A1;
    u32 a = GV(u8, GV(u32, 0x8006572Cu) + 0x18);
    u32 x = mullo32((u32)(s32)GV(s8, g - 0x14), (a << 14) - a);
    u32 h = (u32)(((s64)(s32)x * (s32)0x82061029) >> 32);
    u32 v = (u32)(((s32)(h + x) >> 13) - ((s32)x >> 31));
    x = mullo32(mullo32(v, (u32)(s32)GV(s8, g - 0xE)), (u32)(s32)GV(s8, g - 0xB));
    u32 r6 = udiv_m(x, 0x040C2051u, 13), r7;
    const u32 vo = (u32)(s32)(s16)GV(u16, g) << 3;
    u32 md = GV(u16, g - 4);
    s32 mode = (s16)md;
    u32 t = (u32)((mode & 0xFF00) >> 8);
    u32 tone = GV(u32, 0x80064678u + ((md & 0xFF) << 2)) + t * 176;
    if (mode == 0x21) r7 = r6;
    else {
        u32 l = mullo32(r6, MEM_R16(tone + 0x58));
        u32 r = mullo32(r6, MEM_R16(tone + 0x5A));
        r7 = udiv_m(l, 0x02040811u, 6);
        r6 = udiv_m(r, 0x02040811u, 6);
    }
    u32 L, Rr;
    u32 p = GV(u8, g - 0xA);
    if (p < 0x40) { Rr = udiv_m(mullo32(r6, p), 0x04104105u, 5); L = r7; }
    else { Rr = r6; L = udiv_m(mullo32(r7, 0x7F - p), 0x04104105u, 5); }
    static const u32 pans[2] = { 0x8006574Bu, 0x80065745u };
    for (int i = 0; i < 2; i++) {
        p = GV(u8, pans[i]);
        if (p < 0x40) Rr = udiv_m(mullo32(Rr, p), 0x04104105u, 5);
        else L = udiv_m(mullo32(L, 0x7F - p), 0x04104105u, 5);
    }
    if ((s16)GV(u16, 0x80065720u) == 1) {
        if (L < Rr) L = Rr; else Rr = L;
    }
    if ((s16)GV(u16, g - 4) != 0x21) {
        L = udiv_m(mullo32(L, L), 0x00040011u, 13);
        Rr = udiv_m(mullo32(Rr, Rr), 0x00040011u, 13);
    }
    const u32 idx = (vo & 0xFFFF) << 1;
    GV(u16, 0x80065474u + idx) = (u16)pitch;
    GV(u16, 0x80065470u + idx) = (u16)L;
    GV(u16, 0x80065472u + idx) = (u16)Rr;
    s32 n = (s16)GV(u16, g);
    GV(u8, 0x80065450u + (u32)n) |= 7;
    n = (s16)GV(u16, g);
    GV(u16, 0x80064F14u + (u32)n * 56) = (u16)pitch;
    n = (s16)GV(u16, g);
    u32 lo, hi;
    if (n < 16) { lo = 1u << (n & 31); hi = 0; }
    else { lo = 0; hi = 1u << ((n - 16) & 31); }
    if (GV(u8, 0x80065752u) & 4) {
        GV(u16, 0x80064F04u) |= (u16)lo;
        GV(u16, 0x80064F06u) |= (u16)hi;
    } else {
        GV(u16, 0x80064F04u) &= (u16)~lo;
        GV(u16, 0x80064F06u) &= (u16)~hi;
    }
    GV(u16, 0x80064F08u) &= (u16)~lo;
    u32 k2 = GV(u16, 0x80064F02u) | hi;
    GV(u16, 0x80064F0Au) &= (u16)~hi;
    u32 k1 = GV(u16, 0x80064F00u) | lo;
    GV(u16, 0x80064F02u) = (u16)k2;
    GV(u16, 0x80064F00u) = (u16)k1;
    GV(u16, 0x80065638u) &= (u16)~k1;
    V0 = GV(u16, 0x8006563Au) & ~k2;
    GV(u16, 0x8006563Au) = (u16)V0;
}

/* 80021888 (id, vab, prog, note, velocity, pan): libsnd's note on. The
 * program made current (SelectProg; -1), its note, velocity (scaled by
 * the program volume table +0x60 at +0x17 unless id is 0x21), pan and
 * program fields into the current-note globals; a note past the tone
 * count: -1. Velocity 0: NoteOff, its count. Else for each tone of the
 * note (FindTones) a voice (VoiceAlloc; none: -1) set up with the note's
 * fields (0x80064F00 + voice * 56), its sample and envelope (VoiceSetup)
 * and keyed on at the tone's pitch (ToneBasePitch, or 80022218 for tone
 * 0xFF): the mask of voices. The original's s-registers. */
void n_libsnd_NoteOn(void) {
    FRAME(0x148);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x120, sv[0]);
    MEM_W32(sp + 0x128, sv[2]);
    MEM_W32(sp + 0x138, sv[6]);
    MEM_W32(sp + 0x12C, sv[3]);
    MEM_W32(sp + 0x13C, sv[7]);
    MEM_W16(sp + 0x110, (u16)A2);
    MEM_W32(sp + 0x124, sv[1]);
    MEM_W32(sp + 0x144, R[31]);
    MEM_W32(sp + 0x140, sfp);
    MEM_W32(sp + 0x134, sv[5]);
    MEM_W32(sp + 0x130, sv[4]);
    const u32 id = A0, vab = A1, note = A3;
    R[16] = id; R[18] = 0; R[22] = id; R[19] = vab; R[23] = note;
    R[17] = (u32)(s32)(s16)id;
    u32 t = (u32)(((s32)R[17] & 0xFF00) >> 8);
    R[20] = MEM_R16(sp + 0x158);
    R[30] = MEM_R16(sp + 0x15C);
    R[21] = GV(u32, 0x80064678u + ((id & 0xFF) << 2)) + t * 176;
    if (gcall(0x80023F78u, (u32)(s32)(s16)vab, (u32)(s32)(s16)A2, (id & 0xFF) << 2, note) != 0) { V0 = (u32)-1; goto out; }
    GV(u16, 0x80065754u) = (u16)id;
    GV(u8, 0x80065742u) = (u8)note;
    GV(u8, 0x80065743u) = 0;
    if ((s32)R[17] == 0x21) GV(u8, 0x80065744u) = (u8)R[20];
    else {
        u32 x = (u32)((s32)R[20] * (s32)(s16)MEM_R16(R[21] + 0x60 + ((u32)MEM_R8(R[21] + 0x17) << 1)));
        u32 h = (u32)(((s64)(s32)x * (s32)0x81020409) >> 32);
        GV(u8, 0x80065744u) = (u8)(((s32)(h + x) >> 6) - ((s32)x >> 31));
    }
    R[16] = 0x80065745u;
    GV(u8, 0x80065745u) = (u8)R[30];
    {
        s32 pr = (s16)MEM_R16(sp + 0x110);
        u32 p = GV(u32, 0x80065724u) + ((u32)pr << 4);
        GV(u8, 0x8006574Au) = MEM_R8(p + 1);
        GV(u8, 0x8006574Bu) = MEM_R8(p + 4);
        GV(u8, 0x80065740u) = MEM_R8(p);
        if (!((s32)GV(s8, 0x80065747u) < (s32)MEM_R16(GV(u32, 0x8006572Cu) + 0x12))) { V0 = (u32)-1; goto out; }
        if (R[20] == 0) {
            R[18] = gcall(0x80021DA4u, (u32)(s32)(s16)R[22], (u32)(s32)(s16)R[19], (u32)pr, R[23] & 0xFFFF);
            V0 = R[18];
            goto out;
        }
    }
    R[19] = gcall(0x80023908u, sp + 0x90, sp + 0x10, R[6], R[7]);
    R[17] = 0;
    if ((R[19] & 0xFF) == 0) { V0 = R[18]; goto out; }
    R[16] = 0x80065756u;
    MEM_W32(sp + 0x118, (u32)(s32)(s16)R[22]);
    for (;;) {
        u32 i = R[17] & 0xFF;
        GV(u16, 0x80065756u) = MEM_R8(sp + 0x10 + i);
        u32 tn = MEM_R8(sp + 0x90 + i);
        u32 k = ((u32)(s32)(s8)tn + ((u32)(s32)GV(s8, 0x80065747u) << 4)) & 0xFFFF;
        GV(u8, 0x8006574Cu) = (u8)tn;
        u32 tp = GV(u32, 0x80065730u) + (k << 5);
        GV(u8, 0x8006574Fu) = MEM_R8(tp);
        GV(u8, 0x8006574Du) = MEM_R8(tp + 2);
        GV(u8, 0x8006574Eu) = MEM_R8(tp + 3);
        GV(u8, 0x80065750u) = MEM_R8(tp + 4);
        GV(u8, 0x80065751u) = MEM_R8(tp + 5);
        GV(u8, 0x80065752u) = MEM_R8(tp + 1);
        u32 v = gcall(0x80020798u, 0, R[5], R[6], R[7]) & 0xFF;
        GV(u16, 0x80065758u) = (u16)v;
        if ((s32)v < (s32)GV(s8, 0x80065734u)) {
#define VR(o) (0x80064F00u + (u32)((s32)(s16)GV(u16, 0x80065758u) * 56) + (o))
            GV(u8, VR(0x2D)) = 1;
            GV(u16, VR(0x12)) = 0;
            GV(u16, VR(0x20)) = (u16)R[22];
            GV(u16, VR(0x28)) = (u16)(s16)GV(s8, 0x80065741u);
            GV(u16, VR(0x22)) = (u16)(s16)GV(s8, 0x80065747u);
            GV(u16, VR(0x24)) = MEM_R16(sp + 0x110);
            if (MEM_R32(sp + 0x118) != 0x21) {
                GV(u16, VR(0x18)) = (u16)R[20];
                GV(u16, VR(0x1C)) = MEM_R8(R[21] + 0x17);
            }
            GV(u8, VR(0x1A)) = (u8)R[30];
            GV(u16, VR(0x46)) = (u16)(s16)GV(s8, 0x80065744u);
            GV(u16, VR(0x26)) = (u16)(s16)GV(s8, 0x8006574Cu);
            GV(u16, VR(0x1E)) = (u16)R[23];
            GV(u16, VR(0x2A)) = (u16)(s16)GV(s8, 0x8006574Fu);
            GV(u16, VR(0x10)) = GV(u16, 0x80065756u);
#undef VR
            gcall(0x80020A08u, (u32)(s32)(s16)GV(u16, 0x80065758u), R[5], R[6], R[7]);
            u32 pitch;
            if ((s16)GV(u16, 0x80065756u) == 0xFF) pitch = gcall(0x80022218u, GV(u8, 0x80065758u), R[5], R[6], R[7]);
            else pitch = gcall(0x80022038u, R[4], R[5], R[6], R[7]);
            gcall(0x800228D8u, R[19] & 0xFF, pitch & 0xFFFF, R[6], R[7]);
            R[18] |= 1u << ((u32)(s32)(s16)GV(u16, 0x80065758u) & 31);
        } else R[18] = (u32)-1;
        R[17]++;
        if (!((R[17] & 0xFF) < (R[19] & 0xFF))) break;
        LOOP_POLL(0x80021D68u);
    }
    V0 = R[18];
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8001EEA8 (vab, prog, tone, note, fine, voll, volr): libsnd's
 * SsUtKeyOn. Under the lock 0x8006466C (held: -1): the program made
 * current (SelectProg), the note, fine, tone and a volume and pan from
 * the two volumes (equal: 0x40; else the louder one and 64 * the
 * quieter / louder, 127 less for the right), the program and tone fields
 * (a tone with no sample +0x16: -1); a voice (VoiceAlloc; none: -1) set
 * up as mode 0x21 (0x80064F00 + voice * 56), VoiceSetup, and keyed on at
 * the note's pitch (NotePitch, 80022218 for tone 0xFF). The voice. The
 * original's s-registers. */
void n_libsnd_UtKeyOn(void) {
    FRAME(0x38);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x10, sv[0]);
    const u32 voll = MEM_R32(sp + 0x4C);
    MEM_W32(sp + 0x14, sv[1]);
    const u32 volr = MEM_R32(sp + 0x50);
    MEM_W32(sp + 0x30, sfp);
    MEM_W32(sp + 0x28, sv[6]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x2C, sv[7]);
    const u32 fine = MEM_R16(sp + 0x48);
    MEM_W32(sp + 0x34, R[31]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x18, sv[2]);
    const u32 vab = A0, prog = A1, tone = A2, note = A3;
    R[16] = voll; R[17] = volr; R[30] = vab; R[22] = prog; R[19] = tone; R[21] = note; R[23] = fine;
    R[18] = voll; R[20] = volr;
    if (GV(u32, 0x8006466Cu) == 1) { V0 = (u32)-1; goto out; }
    GV(u32, 0x8006466Cu) = 1;
    if (gcall(0x80023F78u, (u32)(s32)(s16)vab, (u32)(s32)(s16)prog, R[6], R[7]) != 0) goto fail;
    GV(u16, 0x80065754u) = 0x21;
    {
        s32 l = (s16)voll, r = (s16)volr;
        GV(u8, 0x80065742u) = (u8)note;
        GV(u8, 0x80065743u) = (u8)fine;
        GV(u8, 0x8006574Cu) = (u8)tone;
        if (l == r) {
            GV(u8, 0x80065745u) = 0x40;
            GV(u8, 0x80065744u) = (u8)voll;
        } else if (r < l) {
            u32 q = (u32)mips_div(r << 6, l);
            GV(u8, 0x80065744u) = (u8)voll;
            GV(u8, 0x80065745u) = (u8)q;
        } else {
            u32 q = (u32)mips_div(l << 6, r);
            GV(u8, 0x80065744u) = (u8)volr;
            GV(u8, 0x80065745u) = (u8)(0x7F - q);
        }
    }
    {
        u32 p = GV(u32, 0x80065724u) + (u32)((s32)(prog << 16) >> 12);
        R[16] = 0x8006574Au;
        GV(u8, 0x8006574Au) = MEM_R8(p + 1);
        u32 tn = GV(u8, 0x8006574Cu);
        GV(u8, 0x8006574Bu) = MEM_R8(p + 4);
        u32 kk = (u32)((s32)((((u32)(s32)(s8)tn + ((u32)(s32)GV(s8, 0x80065747u) << 4)) << 16)) >> 11);
        GV(u8, 0x80065740u) = MEM_R8(p);
        u32 t = GV(u32, 0x80065730u) + kk;
        GV(u8, 0x8006574Fu) = MEM_R8(t);
        u32 smp = MEM_R16(t + 0x16);
        GV(u16, 0x80065756u) = (u16)smp;
        GV(u8, 0x8006574Du) = MEM_R8(t + 2);
        GV(u8, 0x8006574Eu) = MEM_R8(t + 3);
        GV(u8, 0x80065750u) = MEM_R8(t + 4);
        GV(u8, 0x80065751u) = MEM_R8(t + 5);
        GV(u8, 0x80065752u) = MEM_R8(t + 1);
        if ((s16)smp == 0) goto fail;
        R[17] = gcall(0x80020798u, (u32)(s32)(s16)smp, 0x80065754u, R[6], R[7]) & 0xFF;
    }
    {
        const u32 v = R[17];
        if (v == (u32)(s32)GV(s8, 0x80065734u)) goto fail;
        const u32 rec = 0x80064F00u + v * 56;
        GV(u16, 0x80065758u) = (u16)v;
        GV(u16, rec + 0x20) = 0x21;
        GV(u16, rec + 0x28) = (u16)vab;
        GV(u16, rec + 0x24) = (u16)prog;
        GV(u16, rec + 0x22) = (u16)(s16)GV(s8, 0x80065747u);
        GV(u16, rec + 0x10) = GV(u16, 0x80065756u);
        GV(u16, rec + 0x1E) = (u16)note;
        GV(u16, rec + 0x26) = (u16)(s16)GV(s8, 0x8006574Cu);
        GV(u8, rec + 0x2D) = 1;
        GV(u16, rec + 0x12) = 0;
        GV(u16, rec + 0x46) = (u16)(s16)GV(s8, 0x80065744u);
        gcall(0x80020A08u, v * 56, R[5], R[6], R[7]);
        u32 pitch;
        if ((s16)GV(u16, 0x80065756u) != 0xFF) pitch = gcall(0x80022084u, note & 0xFFFF, fine, R[6], R[7]);
        else pitch = gcall(0x80022218u, v, R[5], R[6], R[7]);
        gcall(0x800228D8u, 1, pitch & 0xFFFF, R[6], R[7]);
        GV(u32, 0x8006466Cu) = 0;
        V0 = v;
        goto out;
    }
fail:
    GV(u32, 0x8006466Cu) = 0;
    V0 = (u32)-1;
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 800254C0 (voice, attr): a voice's record (0x80064F00 + voice * 56)
 * from a note's attributes (pitch +0, +4, +0xE, pan +0x10, note +6,
 * program fields +0xC / +0xA / +8 / +2), as mode 0x21. */
void n_libsnd_VoiceSetAttr(void) {
    const u32 r = 0x80064F00u + A0 * 56, a = A1;
    GV(u16, r + 0x10) = MEM_R16(a);
    GV(u16, r + 0x14) = MEM_R16(a + 4);
    GV(u16, r + 0x18) = MEM_R16(a + 0xE);
    GV(u8, r + 0x1A) = MEM_R8(a + 0x10);
    u32 note = MEM_R16(a + 6);
    GV(u16, r + 0x20) = 0x21;
    GV(u16, r + 0x1E) = (u16)note;
    GV(u16, r + 0x22) = MEM_R16(a + 0xC);
    GV(u16, r + 0x24) = MEM_R16(a + 0xA);
    GV(u16, r + 0x26) = MEM_R16(a + 8);
    GV(u16, r + 0x28) = MEM_R16(a + 2);
    GV(u16, r + 0x46) = MEM_R16(a + 0xE);
}

/* 8001EDB8 (vab, prog, tone): the SPU address of a tone's sample (its
 * VAG number +0x16 of the tone record: the 16-byte table at 0x80065724,
 * odd numbers +0xC, even +0xE, * 8); a bad program: -1. */
void n_libsnd_VagAddr(void) {
    FRAME(0x18);
    const u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = A2;
    if (gcall(0x80023F78u, (u32)(s32)(s16)A0, (u32)(s32)(s16)A1, R[6], R[7]) == (u32)-1) V0 = (u32)-1;
    else {
        u32 t = (((u32)(s32)GV(s8, 0x80065747u) << 4) + (u32)(s32)(s16)R[16]) << 5;
        u32 vag = MEM_R16(t + GV(u32, 0x80065730u) + 0x16);
        s32 k = (s16)vag - 1;
        k = (k + (s32)((u32)k >> 31)) >> 1;
        u32 e = GV(u32, 0x80065724u) + ((u32)k << 4);
        V0 = (u32)MEM_R16(e + ((vag & 1) ? 0xC : 0xE)) << 3;
    }
    R[16] = s0;
}

/* 8001EB78 SsUtGetVagAtr(vab, prog, tone, out): a tone's attributes
 * (its 0x20-byte record: bytes 0-0xD, halves 0x10-0x16) for an open VAB
 * (0x80065760 + vab 1, else -1). 0. The original's s-registers. */
void n_libsnd_UtGetVagAtr(void) {
    FRAME(0x20);
    const u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x18, R[31]);
    R[17] = A2; R[16] = A3;
    s32 vab = (s16)A0;
    if (GV(u8, 0x80065760u + (u32)vab) != 1) V0 = (u32)-1;
    else {
        gcall(0x80023F78u, (u32)vab, (u32)(s32)(s16)A1, R[6], R[7]);
        u32 off = (u32)((s32)((R[17] + ((u32)(s32)GV(s8, 0x80065747u) << 4)) << 16) >> 11);
        const u32 o = R[16];
        for (u32 i = 0; i < 0xE; i++) MEM_W8(o + i, MEM_R8(GV(u32, 0x80065730u) + off + i));
        u32 t = off + GV(u32, 0x80065730u);
        for (u32 i = 0x10; i < 0x18; i += 2) MEM_W16(o + i, MEM_R16(t + i));
        V0 = 0;
    }
    R[16] = s0; R[17] = s1;
}

/* 80025264 (voice, attr): the voice's volumes, pitch and ADSR words
 * from attr (+0, +2, +4, +8, +0xA, +0xC) by the mask attr+6 (0: all)
 * into its shadow registers (0x80065470 + voice * 16), each flagged
 * dirty (0x80065450 + voice). */
void n_libsnd_VoiceSetVol(void) {
    const u32 v = A0, a = A1;
    const int all = (u32)(s32)(s16)MEM_R16(a + 6) < 1;
    const u32 off = (u32)((s32)(v << 19) >> 15);
    const u32 fl = 0x80065450u + v;
#define ON(b) ((MEM_R16(a + 6) & (b)) || all)
    if (ON(1)) { GV(u16, 0x80065470u + off) = MEM_R16(a) & 0x7FFF; GV(u8, fl) |= 1; }
    if (ON(2)) { GV(u16, 0x80065472u + off) = MEM_R16(a + 2) & 0x7FFF; GV(u8, fl) |= 2; }
    if (ON(4)) { GV(u16, 0x80065478u + off) = MEM_R16(a + 0xA); GV(u8, fl) |= 0x10; }
    if (ON(8)) { GV(u16, 0x8006547Au + off) = MEM_R16(a + 0xC); GV(u8, fl) |= 0x20; }
    if (ON(0x20)) { GV(u16, 0x80065474u + off) = MEM_R16(a + 4); GV(u8, fl) |= 4; }
    if (ON(0x10)) { GV(u16, 0x80065476u + off) = MEM_R16(a + 8); GV(u8, fl) |= 8; }
#undef ON
}

/* SeqSetVol helpers: the pan of a voice's volume pair by p (0-127):
 * below 64 the right side scaled by p / 63, else the left by (127 - p)
 * / 63 (the original's two divide forms kept: unsigned on one side,
 * signed on the other). */
#define SND_PAN_U(l, r, p) do { u32 p_ = (p); \
        if (p_ < 0x40) (r) = ((r) * p_) / 63u; \
        else (l) = ((l) * (0x7Fu - p_)) / 63u; } while (0)
#define SND_PAN_S(l, r, p) do { u32 p_ = (p); \
        if (p_ < 0x40) (r) = (((r) & 0xFFFF) * p_) / 63u; \
        else (l) = (u32)((s32)(((l) & 0xFFFF) * (0x7Fu - p_)) / 63); } while (0)

/* 800231F8 (access, left, right): SsSeqSetVol. The sequence's volume
 * (+0x58/+0x5A of its 176-byte record, from the table at 0x80064678 by
 * the access number's low byte, sequence = high byte), each capped at
 * 127, then every voice playing that sequence (not reserved: 0x80059874;
 * +0x20 the access number, +0x28 the VAB, matching the sequence's +0x26)
 * gets its SPU volume recomputed: the channel's volume (+0x60 by the
 * voice's channel +0x1C) times its velocity (+0x18), the master volume
 * (0x8006572C +0x18), the program's and the tone's volume, the
 * sequence's, then the program, tone and voice pans; mono (0x80065720)
 * takes the louder side for both. Squared / 16383 into the pair at
 * 0x80065470 + voice * 16, with bits 0-1 of 0x80065450 + voice set
 * (update both). Makes each matching program current (SelectProg). */
void n_libsnd_SeqSetVol(void) {
    FRAME(0x40);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    for (int i = 0; i < 8; i++) MEM_W32(sp + 0x18 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x38, sfp);
    MEM_W32(sp + 0x3C, R[31]);
    u32 acc = A0;
    u32 seq = GV(u32, 0x80064678u + ((acc & 0xFF) << 2)) + (u32)((s32)(acc & 0xFF00) >> 8) * 176;
    MEM_W16(seq + 0x58, (u16)A1);
    if (!(MEM_R16(seq + 0x58) < 0x7F)) MEM_W16(seq + 0x58, 0x7F);
    MEM_W16(seq + 0x5A, (u16)A2);
    if (!(MEM_R16(seq + 0x5A) < 0x7F)) MEM_W16(seq + 0x5A, 0x7F);
    R[17] = seq; R[22] = acc; R[18] = 0;
    if ((s32)GV(s8, 0x80065734u) > 0) {
        R[21] = 0x02040811u; R[23] = 0x7F; R[19] = 0x82082083u; R[20] = 0x80020009u;
        R[30] = 0x80065470u;
        for (;;) {
            s32 i = (s16)R[18];
            u32 rec = 0x80060000u + (u32)i * 56;
            if (!(GV(u32, 0x80059874u) & (1u << (i & 31)))
                && (s32)GV(s16, rec + 0x4F20) == (s32)(s16)acc
                && (s32)GV(s16, rec + 0x4F28) == (s32)(s8)MEM_R8(seq + 0x26)) {
                R[16] = (u32)i * 56;
                gcall(0x80023F78u, (u32)(s32)GV(s16, rec + 0x4F28), (u32)(s32)GV(s16, rec + 0x4F22), R[6], R[7]);
                u32 ch = (u32)(s32)GV(s16, rec + 0x4F1C);
                u32 v = (u32)(s32)GV(s16, rec + 0x4F18) * (u32)(s32)(s16)MEM_R16(seq + 0x60 + (ch << 1));
                s32 a = (s32)v / 127;
                u32 m = (u32)MEM_R8(GV(u32, 0x8006572Cu) + 0x18) * ((u32)a * 16383u);
                s32 b = (s32)m / 16129;
                u32 tone = GV(u32, 0x80065724u) + ((u32)(s32)GV(s16, rec + 0x4F24) << 4);
                u32 prog = ((((u32)(s32)GV(s16, rec + 0x4F22) << 4) + (u32)(s32)GV(s16, rec + 0x4F26)) << 5)
                           + GV(u32, 0x80065730u);
                u32 t = ((u32)b * MEM_R8(tone + 1)) * MEM_R8(prog + 2);
                u32 x = t / 16129u;
                u32 l = (x * MEM_R16(seq + 0x58)) / 127u;
                u32 r = (x * MEM_R16(seq + 0x5A)) / 127u;
                SND_PAN_U(l, r, MEM_R8(prog + 3));
                SND_PAN_S(l, r, MEM_R8(tone + 4));
                SND_PAN_S(l, r, MEM_R8(rec + 0x4F1A));
                if (GV(s16, 0x80065720u) == 1) {
                    if (!((l & 0xFFFF) < (r & 0xFFFF))) r = l; else l = r;
                }
                u32 l2 = (l & 0xFFFF) * (l & 0xFFFF), r2 = (r & 0xFFFF) * (r & 0xFFFF);
                GV(u16, 0x80065470u + ((u32)i << 4)) = (u16)((s32)l2 / 16383);
                GV(u16, 0x80065472u + ((u32)i << 4)) = (u16)((s32)r2 / 16383);
                GV(u8, 0x80065450u + (u32)i) |= 3;
            }
            R[18]++;
            if (!((s32)(s16)R[18] < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x80023724u);
        }
    }
    V0 = (u32)(s32)(s16)acc;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 800239D8 (id, vab, tone, volume, [pan]): set the volume of the voices
 * playing a sequence's tone and recompute their SPU volumes (as
 * SeqSetVol, with this volume and pan instead of the voice's own; 0 taken
 * as 1). The program is made current (SelectProg) and the id kept in
 * 0x80065754; the sequence's current channel volume (+0x60 by +0x17) is
 * set to 1 where it was 0 and differs. Returns how many voices. */
void n_libsnd_ToneSetVol(void) {
    FRAME(0x40);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x1C, sv[1]);
    MEM_W32(sp + 0x34, sv[7]);
    MEM_W32(sp + 0x2C, sv[5]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x38, R[31]);
    MEM_W32(sp + 0x30, sv[6]);
    MEM_W32(sp + 0x28, sv[4]);
    const u32 id = A0, vol0 = A3;
    const s32 sid = (s16)id, vab = (s16)A1, tone = (s16)A2;
    const u32 seq = GV(u32, 0x80064678u + ((id & 0xFF) << 2)) + (u32)((s32)((u32)sid & 0xFF00) >> 8) * 176;
    u32 pan = MEM_R16(sp + 0x50);
    R[16] = id; R[17] = vol0; R[18] = (u32)vab; R[19] = (u32)tone; R[20] = seq; R[21] = (u32)sid; R[22] = pan; R[23] = 0;
    gcall(0x80023F78u, (u32)vab, (u32)tone, R[6], R[7]);
    GV(u16, 0x80065754u) = (u16)id;
    if (pan == 0) pan = 1;
    u32 vol = vol0 & 0xFFFF;
    if (vol == 0) vol = 1;
    u32 count = 0;
    if (0 < (s32)GV(s8, 0x80065734u)) {
        for (u32 iv = 0;;) {
            s32 i = (s16)iv;
            u32 rec = 0x80060000u + (u32)i * 56;
            if (!(GV(u32, 0x80059874u) & (1u << (i & 31)))
                && (s32)GV(s16, rec + 0x4F20) == sid
                && (s32)GV(s16, rec + 0x4F24) == tone
                && (s32)GV(s16, rec + 0x4F28) == vab) {
                u32 cp = seq + ((u32)MEM_R8(seq + 0x17) << 1) + 0x60;
                s32 cv = (s16)MEM_R16(cp);
                if ((u32)cv != vol && cv == 0) MEM_W16(cp, 1);
                s32 a = (s32)((u32)(s32)GV(s16, rec + 0x4F18) * vol) / 127;
                s32 b = (s32)((u32)a * (MEM_R8(GV(u32, 0x8006572Cu) + 0x18) * 16383u)) / 16129;
                u32 prog = ((((u32)(s32)GV(s16, rec + 0x4F22) << 4) + (u32)(s32)GV(s16, rec + 0x4F26)) << 5)
                           + GV(u32, 0x80065730u);
                u32 t = ((u32)b * MEM_R8(GV(u32, 0x80065724u) + ((u32)tone << 4) + 1)) * MEM_R8(prog + 2);
                u32 x = t / 16129u;
                u32 l = (x * MEM_R16(seq + 0x58)) / 127u;
                u32 r = (x * MEM_R16(seq + 0x5A)) / 127u;
                u32 p = MEM_R8(prog + 3);
                if (p < 0x40) r = (r * p) / 63u; else l = (l * (0x7Fu - p)) / 63u;
                p = MEM_R8(GV(u32, 0x80065724u) + ((u32)(s32)GV(s16, rec + 0x4F24) << 4) + 4);
                if (p < 0x40) r = (r * p) / 63u; else l = (l * (0x7Fu - p)) / 63u;
                p = pan & 0xFF;
                if (p < 0x40) r = (r * p) / 63u; else l = (l * (0x7Fu - p)) / 63u;
                if (GV(s16, 0x80065720u) == 1) {
                    if (l < r) l = r; else r = l;
                }
                GV(u16, 0x80065470u + ((u32)i << 4)) = (u16)((l * l) / 16383u);
                GV(u16, 0x80065472u + ((u32)i << 4)) = (u16)((r * r) / 16383u);
                GV(u8, 0x80065450u + (u32)i) |= 3;
                count++;
            }
            iv++;
            if (!((s32)(s16)iv < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x80023F40u);
        }
    }
    V0 = count;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
}

/* 80024868 (header, id, alloc, mode): open a VAB header (SsVabOpenHead):
 * a free slot of 0x80065760 (id -1: the first; else that one), counted in
 * 0x800657B8; checked ('VAB', at most 0x80065722 programs: 128 from
 * version 5 of 'pVAB', else 64); the program table (0x80065658 by slot),
 * each program's index among the non-empty ones (+8), the tone table
 * (0x800656E0), the VAG sizes (<< 2, or << 3 from version 5) summed and
 * rounded to 64 bytes for SPU memory from alloc(size, mode, id)
 * (0x800657C0; failing past 512 KB), each VAG's address / 8 into the
 * program records (+0xC/+0xE), the total into 0x80065778, slot state 2.
 * 80021870 / 80021848 the library's busy check and lock. Returns the id
 * or -1. */
void n_libsnd_VabOpenHead(void) {
    FRAME(0x430);
    u32 sv[7];
    for (int i = 0; i < 7; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x410 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x42C, R[31]);
    const u32 hdr = A0, a1 = A1, alloc = A2, mode = A3;
    R[19] = hdr; R[16] = a1; R[22] = alloc; R[21] = mode; R[18] = 0x10;
    u32 ret = 0xFFFFFFFFu;
    if (gcall(0x80021870u, R[4], R[5], R[6], R[7]) == 1) goto out;
    R[17] = 1;
    gcall(0x80021848u, 1, R[5], R[6], R[7]);
    {
        s32 id = 16;
        const s32 want = (s16)a1;
        if (want < 16) {
            if (want == -1) {
                for (s32 k = 0;;) {
                    if (GV(u8, 0x80065760u + (u32)k) == 0) {
                        GV(u8, 0x80065760u + (u32)k) = 1;
                        GV(u16, 0x800657B8u) += 1;
                        id = k;
                        break;
                    }
                    k++;
                    if (!(k < 16)) break;
                    LOOP_POLL(0x800248F4u);
                }
            } else if (GV(u8, 0x80065760u + (u32)want) == 0) {
                GV(u8, 0x80065760u + (u32)want) = 1;
                GV(u16, 0x800657B8u) += 1;
                id = want;
            }
        }
        R[18] = (u32)id;
        if (!(id < 16)) { gcall(0x80021848u, 0, R[5], R[6], R[7]); goto out; }
        GV(u32, 0x80065698u + ((u32)id << 2)) = hdr;
        const u32 magic = MEM_R32(hdr);
        GV(u32, 0x80065738u) = 0;
        u32 progs = hdr + 0x20;
        int bad = (magic >> 8) != 0x564142u;
        if (!bad) {
            GV(u16, 0x80065722u) = ((magic & 0xFF) == 0x70 && !((s32)MEM_R32(hdr + 4) < 5)) ? 0x80 : 0x40;
            if ((s32)GV(s16, 0x80065722u) < (s32)MEM_R16(hdr + 0x12)) bad = 1;
        }
        if (bad) {
            GV(u8, 0x80065760u + (u32)id) = 0;
            gcall(0x80021848u, 0, R[5], R[6], R[7]);
            GV(u16, 0x800657B8u) -= 1;
            goto out;
        }
        GV(u32, 0x80065658u + ((u32)id << 2)) = progs;
        R[19] = progs;
        const s32 maxp = GV(s16, 0x80065722u);
        u32 tones = progs + ((u32)maxp << 4);
        if (maxp > 0) {
            u32 k16 = 0;
            for (s32 k = 0;;) {
                u32 p = progs + ((u32)k << 4);
                MEM_W32(p + 8, k16);
                if (MEM_R8(p) != 0) k16++;
                k++;
                if (!(k < maxp)) break;
                LOOP_POLL(0x80024A78u);
            }
        }
        GV(u32, 0x800656E0u + ((u32)id << 2)) = tones;
        const u32 nvag = MEM_R8(hdr + 0x16);
        R[20] = nvag;
        u32 vt = tones + ((u32)MEM_R16(hdr + 0x12) << 9);
        u32 sizes[256];
        u32 total = 0;
        for (s32 k = 0;;) {
            if (!((s32)nvag < k)) {
                u32 s = MEM_R16(vt);
                sizes[k] = (s32)MEM_R32(hdr + 4) < 5 ? s << 2 : s << 3;
                MEM_W32(sp + 0x10 + ((u32)k << 2), sizes[k]);
                total += sizes[k];
            }
            vt += 2;
            k++;
            if (!(k < 256)) break;
            LOOP_POLL(0x80024AF4u);
        }
        total = (total + 0x3F) & 0xFFFFFFC0u;
        R[17] = (u32)id;
        const u32 addr = gcall(alloc, total, mode, (u32)id, R[7]);
        if (addr == 0xFFFFFFFFu) goto out;
        if (0x80000u < addr + total) {
            GV(u8, 0x80065760u + (u32)id) = 0;
            gcall(0x80021848u, 0, R[5], R[6], R[7]);
            GV(u16, 0x800657B8u) -= 1;
            goto out;
        }
        GV(u32, 0x800657C0u + ((u32)id << 2)) = addr;
        u32 sum = 0;
        for (s32 k = 0;;) {
            sum += MEM_R32(sp + 0x10 + ((u32)k << 2));
            MEM_W16(progs + ((u32)(k / 2) << 4) + ((k & 1) ? 0xE : 0xC), (u16)((addr + sum) >> 3));
            k++;
            if ((s32)nvag < k) break;
            LOOP_POLL(0x80024BFCu);
        }
        GV(u32, 0x80065778u + ((u32)id << 2)) = sum;
        GV(u8, 0x80065760u + (u32)id) = 2;
        ret = (u32)id;
    }
out:
    V0 = ret;
    for (int i = 0; i < 7; i++) R[16 + i] = sv[i];
}

/* 8001C878 (sep, seq, controller): a MIDI control change: the value byte
 * read from the sequence (+0 its read pointer); bank select (0) into
 * +0x26, the others through the handler table at 0x800645F0 (data entry
 * 6, volume 7, pan 10, expression 11, damper 64, depth 91 (+0x24),
 * NRPN 98/99 (+0x14/+0x18), RPN 100/101: (sep, seq, value); reset 121: (sep, seq)) which read on
 * themselves; else (and after bank select) the next delta time
 * (SeqDelta) into +0x90. */
void n_libsnd_ControlChange(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 sv16 = R[16];
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1;
    const u32 seq = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    R[16] = seq;
    const u32 p = MEM_R32(seq);
    const u32 v = MEM_R8(p);
    MEM_W32(seq, p + 1);
    u32 h = 0;
    switch (A2 & 0xFF) {
    case 0x06: h = 0x800645F0u; break;
    case 0x07: h = 0x800645F4u; break;
    case 0x0A: h = 0x800645F8u; break;
    case 0x0B: h = 0x800645FCu; break;
    case 0x40: h = 0x80064600u; break;
    case 0x5B: h = 0x80064614u; break;
    case 0x62: h = 0x80064604u; break;
    case 0x63: h = 0x80064608u; break;
    case 0x64: h = 0x8006460Cu; break;
    case 0x65: h = 0x80064610u; break;
    case 0x79: gcall(GV(u32, 0x80064618u), a, b, 0x79, R[7]); goto out;
    case 0x00: MEM_W8(seq + 0x26, (u8)v); break;
    }
    if (h) gcall(GV(u32, h), a, b, v, R[7]);
    else MEM_W32(seq + 0x90, gcall(0x8001A4A8u, a, b, R[6], R[7]));
out:
    R[16] = sv16;
}

/* 8001F348 (voice, vab, prog, tone, [note, fine, voll, volr]): SsUtKeyOnV:
 * key a note on on a given voice (< 24), under the library's busy flag
 * (0x8006466C; -1 if set). The program made current (SelectProg); the
 * note, fine pitch, tone, velocity (the louder side) and pan (64 for
 * equal sides) into the current-note globals (0x80065740..), the tone's
 * attributes (0x80065724 by prog, 0x80065730 by tone + program * 16; -1
 * for an empty one), the voice's record (0x80064F00 + voice * 56) filled
 * (id 0x21), VoiceSetup, then a noise voice (pitch 0xFF: 80022218) or
 * the pitch for the note and a key on. Returns the voice or -1. */
void n_libsnd_UtKeyOnV(void) {
    FRAME(0x40);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    for (int i = 0; i < 8; i++) MEM_W32(sp + 0x18 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x38, sfp);
    MEM_W32(sp + 0x3C, R[31]);
    const u32 voice = A0, prog = A2, tone = A3;
    const u32 voll = MEM_R32(sp + 0x58), volr = MEM_R32(sp + 0x5C);
    const u32 note = MEM_R16(sp + 0x50), fine = MEM_R16(sp + 0x54);
    MEM_W16(sp + 0x10, (u16)A1);
    R[16] = voll; R[17] = volr; R[18] = voice; R[19] = voll; R[20] = tone;
    R[21] = note; R[22] = volr; R[23] = prog; R[30] = fine;
    u32 ret = 0xFFFFFFFFu;
    if (GV(u32, 0x8006466Cu) == 1) goto out;
    GV(u32, 0x8006466Cu) = 1;
    if (!((voice & 0xFFFF) < 0x18)
        || gcall(0x80023F78u, (u32)(s32)(s16)A1, (u32)(s32)(s16)prog, R[6], R[7]) != 0) goto fail;
    GV(u16, 0x80065754u) = 0x21;
    GV(u8, 0x80065742u) = (u8)note;
    GV(u8, 0x80065743u) = (u8)fine;
    GV(u8, 0x8006574Cu) = (u8)tone;
    {
        const s32 l = (s16)voll, r = (s16)volr;
        if (l == r) { GV(u8, 0x80065745u) = 0x40; GV(u8, 0x80065744u) = (u8)voll; }
        else if (r < l) {
            s32 q = mips_div((s32)((u32)r << 6), l);
            GV(u8, 0x80065744u) = (u8)voll;
            GV(u8, 0x80065745u) = (u8)q;
        } else {
            s32 q = mips_div((s32)((u32)l << 6), r);
            GV(u8, 0x80065744u) = (u8)volr;
            GV(u8, 0x80065745u) = (u8)(0x7F - q);
        }
    }
    {
        const u32 t = GV(u32, 0x80065724u) + (u32)((s32)(prog << 16) >> 12);
        R[16] = 0x8006574Au;
        GV(u8, 0x8006574Au) = MEM_R8(t + 1);
        GV(u8, 0x8006574Bu) = MEM_R8(t + 4);
        GV(u8, 0x80065740u) = MEM_R8(t);
        const u32 idx = (u32)((s32)GV(s8, 0x8006574Cu) + ((s32)GV(s8, 0x80065747u) << 4));
        const u32 p = (u32)((s32)(idx << 16) >> 11) + GV(u32, 0x80065730u);
        GV(u8, 0x8006574Fu) = MEM_R8(p);
        const u32 pitch = MEM_R16(p + 0x16);
        GV(u16, 0x80065756u) = (u16)pitch;
        GV(u8, 0x8006574Du) = MEM_R8(p + 2);
        GV(u8, 0x8006574Eu) = MEM_R8(p + 3);
        GV(u8, 0x80065750u) = MEM_R8(p + 4);
        GV(u8, 0x80065751u) = MEM_R8(p + 5);
        GV(u8, 0x80065752u) = MEM_R8(p + 1);
        if (pitch == 0) goto fail;
    }
    {
        const u32 rec = 0x80060000u + (u32)(s32)(s16)voice * 56;
        GV(u16, 0x80065758u) = (u16)voice;
        GV(u16, rec + 0x4F20) = 0x21;
        GV(u16, rec + 0x4F28) = MEM_R16(sp + 0x10);
        GV(u16, rec + 0x4F24) = (u16)prog;
        GV(u16, rec + 0x4F22) = (u16)(s32)GV(s8, 0x80065747u);
        GV(u16, rec + 0x4F10) = GV(u16, 0x80065756u);
        GV(u16, rec + 0x4F1E) = (u16)note;
        GV(u8, rec + 0x4F2D) = 1;
        GV(u16, rec + 0x4F12) = 0;
        GV(u16, rec + 0x4F26) = (u16)(s32)GV(s8, 0x8006574Cu);
        GV(u16, rec + 0x4F46) = (u16)(s32)GV(s8, 0x80065744u);
    }
    gcall(0x80020A08u, R[4], R[5], R[6], R[7]);
    if (GV(s16, 0x80065756u) == 0xFF) gcall(0x80022218u, voice & 0xFF, R[5], R[6], R[7]);
    else {
        u32 pt = gcall(0x80022084u, note, fine, R[6], R[7]);
        gcall(0x800228D8u, 1, pt & 0xFFFF, R[6], R[7]);
    }
    GV(u32, 0x8006466Cu) = 0;
    ret = (u32)(s32)(s16)voice;
    goto out;
fail:
    GV(u32, 0x8006466Cu) = 0;
out:
    V0 = ret;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 800250C8 (mask): for each voice in the mask (of 0x80065734), its key-on
 * bit set (0x80064F00/02) and key-off bit cleared (0x80065638/3A). */
void n_libsnd_KeyOnBits(void) {
    const u32 m = A0;
    const s32 n = GV(s8, 0x80065734u);
    if (n <= 0) return;
    for (u32 iv = 0;;) {
        const s32 i = (s8)iv;
        const u32 bit = 1u << (i & 31);
        if (m & bit) {
            if (i < 16) {
                GV(u16, 0x80064F00u) |= (u16)bit;
                GV(u16, 0x80065638u) &= (u16)~bit;
            } else {
                const u32 b2 = 1u << ((i - 16) & 31);
                GV(u16, 0x80064F02u) |= (u16)b2;
                GV(u16, 0x8006563Au) &= (u16)~b2;
            }
        }
        iv++;
        if (!((s32)(s8)iv < n)) break;
        LOOP_POLL(0x80025184u);
    }
}

/* 80025194 (mask, on): each of the 24 voices in the mask gets its bit
 * in 0x80064F04/06 set or cleared by the same bit of on. */
void n_libsnd_VoiceFlagBits(void) {
    const u32 m = A0, on = A1;
    for (u32 iv = 0;;) {
        const s32 i = (s8)iv;
        const u32 bit = 1u << (i & 31);
        if (m & bit) {
            const u32 a = i < 16 ? 0x80064F04u : 0x80064F06u;
            const u32 b = i < 16 ? bit : 1u << ((i - 16) & 31);
            if (on & bit) GV(u16, a) |= (u16)b; else GV(u16, a) &= (u16)~b;
        }
        iv++;
        if (!((s32)(s8)iv < 24)) break;
        LOOP_POLL(0x80025254u);
    }
}

/* 8001D3A8 (arg): libsnd's tick start (after a 999-step delay): by the
 * tick mode (0x80059CA4) the root counter 2 target (0x44E8 for mode 2,
 * 0x89D0 for 3, 1 at counter 3 for mode 5 with arg, else 0x204CC0 / n
 * below 70 ticks (counting 0x80059CB5) or 0x409980 / n); mode 0 only
 * marks the callback slot (0x80059CB6) 0x7F, a running mode-5 vsync tick
 * (0x80059CB4) re-hooks 0x80059CAC (80025898); else the counter set up
 * (80039C20, 80039AE8) and the callback hooked (80025838): the library's
 * tick 8001D618 (the old one kept in 0x80059CB0), 8001D664, or the
 * user's 0x80059CAC. In a critical section; nothing if already set
 * (0x80059CA8). */
void n_libsnd_TickStart(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x10, sv[0]);
    const u32 arg = A0;
    for (s32 r = 998; r >= 0; r--) LOOP_POLL(0x8001D3C4u);
    u32 ctr = 0xF2000002u, tgt = 0x44E8;
    const u32 st = 0x80059CB4u;
    const s32 mode = (s32)GV(u32, 0x80059CA4u);
    GV(u8, st + 2) = 6;
    GV(u8, st) = 0;
    GV(u8, st + 1) = 0;
    GV(u32, st - 4) = 0;
    switch (mode) {
    case 2: break;
    case 0: GV(u8, st + 2) = 0x7F; goto out;
    case 5:
        GV(u8, st + 2) = 0;
        if (arg == 0) GV(u8, st) = 1;
        else { ctr = 0xF2000003u; tgt = 1; }
        break;
    case 3: tgt = 0x89D0; break;
    default:
        if (GV(u32, 0x80059CA8u) != 0) goto out;
        {
            const s32 n = (s32)GV(u32, 0x80059CA4u);
            if (n < 70) {
                tgt = (u32)mips_div(0x204CC0, n);
                GV(u8, 0x80059CB5u) += 1;
            } else tgt = (u32)mips_div(0x409980, n);
        }
        break;
    }
    R[16] = st; R[17] = tgt; R[18] = ctr;
    gcall(0x80039A08u, R[4], R[5], R[6], R[7]);
    if (GV(s8, st) != 0) gcall(0x80025898u, GV(u32, st - 8), R[5], R[6], R[7]);
    else {
        gcall(0x80039C20u, ctr, R[5], R[6], R[7]);
        gcall(0x80039AE8u, ctr, tgt & 0xFFFF, 0x1000, R[7]);
        if (GV(s8, st + 2) == 0) {
            u32 old = gcall(0x80025838u, 0, 0, R[6], R[7]);
            const u32 a0 = (u32)(s32)GV(s8, st + 2);
            GV(u32, st - 4) = old;
            gcall(0x80025838u, a0, 0x8001D618u, R[6], R[7]);
        } else {
            const u32 a1 = GV(s8, st + 1) != 0 ? 0x8001D664u : GV(u32, st - 8);
            gcall(0x80025838u, (u32)(s32)GV(s8, st + 2), a1, R[6], R[7]);
        }
    }
    gcall(0x80039A18u, R[4], R[5], R[6], R[7]);
out:
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* R3000 divu: x / 0 gives all ones, remainder x. */
static inline u32 snd_divu(u32 x, u32 y) { return y ? x / y : 0xFFFFFFFFu; }
static inline u32 snd_remu(u32 x, u32 y) { return y ? x % y : x; }

/* 8001CDC8 (id, vab, data): set up a SEQ for playing: the sequence record
 * (0x80064678 by id) reset (channels: program = channel, pan 64, volume
 * 127), the header checked ('S'/'p', version 1; else a message and -1, or
 * 0 for no SEQ), the resolution (+0x50), the tempo (60000000 / the
 * microseconds per beat, rounded: +0x8C, +0x94), the rhythm (+0x24/25),
 * the first delta (SeqDelta) and the loop / start pointers; then the
 * ticks per frame from the tick rate (0x80064674): a whole number per
 * frame (+0x52) or frames per tick (+0x54, rounded; +0x52 -1), copied to
 * +0x56. */
void n_libsnd_SeqOpen(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 sv16 = R[16];
    const u32 id = A0, vab = A1, data = A2;
    const u32 rec = GV(u32, 0x80064678u + (u32)((s32)(id << 16) >> 14));
    R[16] = rec;
    u32 ret = 0;
    MEM_W8(rec + 0x26, (u8)vab);
    MEM_W16(rec + 0x50, 0);
    static const u8 z8[] = { 0x18, 0x19, 0x1E, 0x1A, 0x1B, 0x1F, 0x17 };
    for (unsigned k = 0; k < sizeof z8; k++) MEM_W8(rec + z8[k], 0);
    MEM_W32(rec + 0x84, 0); MEM_W32(rec + 0x88, 0); MEM_W32(rec + 0x8C, 0);
    MEM_W16(rec + 0x56, 0);
    MEM_W8(rec + 0x21, 0);
    MEM_W8(rec + 0x20, 1);
    MEM_W8(rec + 0x14, 0);
    MEM_W32(rec + 0x90, 0);
    static const u8 z8b[] = { 0x1C, 0x1D, 0x15, 0x16 };
    for (unsigned k = 0; k < sizeof z8b; k++) MEM_W8(rec + z8b[k], 0);
    MEM_W16(rec + 0x80, 0);
    MEM_W8(rec + 0x24, 0);
    MEM_W8(rec + 0x25, 0);
    for (u32 k = 0;;) {
        MEM_W8(rec + k + 0x37, (u8)k);
        MEM_W8(rec + k + 0x27, 0x40);
        MEM_W16(rec + 0x60 + k * 2, 0x7F);
        k++;
        if (!(k < 16)) break;
        LOOP_POLL(0x8001CE7Cu);
    }
    MEM_W16(rec + 0x52, 1);
    MEM_W32(rec, data);
    const u32 c = MEM_R8(data);
    if (c != 0x53 && c != 0x70) {
        gcall(0x800398C8u, 0x80010268u, R[5], R[6], R[7]);
        goto out;
    }
    MEM_W32(rec, data + 7);
    {
        const u32 ver = MEM_R8(data + 7);
        MEM_W32(rec, data + 8);
        if (ver != 1) {
            gcall(0x800398C8u, 0x80010250u, R[5], R[6], R[7]);
            ret = 0xFFFFFFFFu;
            goto out;
        }
    }
    {
        const u32 hi = MEM_R8(data + 8);
        MEM_W32(rec, data + 9);
        const u32 lo = MEM_R8(data + 9);
        MEM_W32(rec, data + 0xA);
        MEM_W16(rec + 0x50, (u16)(lo | (hi << 8)));
        const u32 b0 = MEM_R8(data + 0xA);
        MEM_W32(rec, data + 0xB);
        const u32 b1 = MEM_R8(data + 0xB);
        MEM_W32(rec, data + 0xC);
        const u32 b2 = MEM_R8(data + 0xC);
        const u32 us = b2 | (b0 << 16) | (b1 << 8);
        const s32 q = mips_div(60000000, (s32)us);
        const s32 rem = us ? 60000000 % (s32)us : 60000000;
        MEM_W32(rec, data + 0xD);
        MEM_W32(rec + 0x8C, us);
        MEM_W32(rec + 0x8C, (s32)(us >> 1) < rem ? (u32)q + 1 : (u32)q);
    }
    {
        const u32 p = MEM_R32(rec);
        MEM_W32(rec + 0x94, MEM_R32(rec + 0x8C));
        MEM_W8(rec + 0x24, MEM_R8(p));
        MEM_W32(rec, p + 1);
        MEM_W8(rec + 0x25, MEM_R8(p + 1));
        MEM_W32(rec, p + 2);
    }
    {
        const u32 d = gcall(0x8001A4A8u, (u32)(s32)(s16)id, 0, R[6], R[7]);
        const u32 prod = (u32)(s32)(s16)MEM_R16(rec + 0x50) * MEM_R32(rec + 0x8C);
        const u32 cur = MEM_R32(rec);
        MEM_W32(rec + 8, cur);
        MEM_W32(rec + 0x84, d);
        MEM_W32(rec + 0x90, d);
        MEM_W32(rec + 0x10, 0);
        MEM_W32(rec + 0xC, cur);
        MEM_W32(rec + 4, cur);
        const u32 vs = GV(u32, 0x80064674u);
        const u32 v15 = vs * 15, v60 = v15 * 4;
        if (prod * 10 < v60) {
            const u32 q2 = snd_divu(vs * 600, prod);
            MEM_W16(rec + 0x52, (u16)q2);
            MEM_W16(rec + 0x54, (u16)q2);
        } else {
            const u32 n = (u32)(s32)(s16)MEM_R16(rec + 0x50) * MEM_R32(rec + 0x8C) * 10;
            const u32 q3 = snd_divu(n, v60), r3 = snd_remu(n, v60);
            MEM_W16(rec + 0x52, 0xFFFF);
            MEM_W16(rec + 0x54, (u16)q3);
            if (v15 * 2 < r3) MEM_W16(rec + 0x54, (u16)(q3 + 1));
        }
        MEM_W16(rec + 0x56, MEM_R16(rec + 0x54));
    }
out:
    V0 = ret;
    R[16] = sv16;
}

/* 8001AD68 (data, vab): SsSeqOpen: the first free sequence slot (bit
 * clear in 0x80064670; all taken: a message and -1) taken and the SEQ set
 * up there (SeqOpen), the MIDI event handler table (0x800645D8) filled.
 * Returns the slot, or -1 if SeqOpen failed. */
void n_libsnd_SsSeqOpen(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 sv16 = R[16];
    const u32 data = A0, vab = A1;
    const u32 used = GV(u32, 0x80064670u);
    u32 ret = 0xFFFFFFFFu;
    if (used == 0xFFFFFFFFu) {
        gcall(0x800398C8u, 0x80010220u, R[5], R[6], R[7]);
        goto out;
    }
    {
        u32 slot = 0;
        for (u32 k = 0;;) {
            if (!(used & (1u << (k & 31)))) { LOOP_POLL(0x8001ADB8u); slot = k; break; }
            k++;
            if (!((s32)k < 32)) break;
            LOOP_POLL(0x8001ADC8u);
        }
        const s32 s = (s16)slot;
        R[16] = (u32)s;
        GV(u32, 0x80064670u) |= 1u << (s & 31);
        const u32 r = gcall(0x8001CDC8u, (u32)s, (u32)(s32)(s16)vab, data, R[7]);
        static const u32 tbl[37] = {
            0x8001CC78u, 0x8001CD58u, 0x8001C7C8u, 0x8001CAA8u, 0x8001C878u, 0x8001AFF8u,
            0x8001B078u, 0x8001B3F8u, 0x8001B4C8u, 0x8001B598u, 0x8001B688u, 0x8001B7C8u,
            0x8001B8C8u, 0x8001BA08u, 0x8001BA78u, 0x8001B738u, 0x8001BAE8u, 0x8001BBA8u,
            0x8001BC38u, 0x8001BCF8u, 0x8001BD88u, 0x8001BE18u, 0x8001BFC8u, 0x8001C088u,
            0x8001C138u, 0x8001C1E8u, 0x8001C298u, 0x8001C358u, 0x8001C408u, 0x8001C4B8u,
            0x8001C598u, 0x8001C638u, 0x8001C6D8u, 0x8001C708u, 0x8001C738u, 0x8001C768u,
            0x8001C798u };
        for (u32 k = 0; k < 37; k++) GV(u32, 0x800645D8u + k * 4) = tbl[k];
        ret = (s16)r == -1 ? 0xFFFFFFFFu : (u32)s;
    }
out:
    V0 = ret;
    R[16] = sv16;
}

/* 8001EA68 (vab, prog, attr): SsUtGetProgAtr: a program's attributes
 * (tones, master volume, priority, mode, pan, attr word +6) copied out;
 * -1 unless the VAB is open (0x80065760 state 1). */
void n_libsnd_UtGetProgAtr(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16], s17v = R[17];
    const s32 vab = (s16)A0, prog = (s16)A1;
    const u32 at = A2;
    u32 ret = 0xFFFFFFFFu;
    if (GV(u8, 0x80065760u + (u32)vab) == 1) {
        R[17] = at; R[16] = (u32)prog;
        gcall(0x80023F78u, (u32)vab, (u32)prog, R[6], R[7]);
        const u32 off = (u32)prog << 4;
        for (u32 k = 0; k < 5; k++) MEM_W8(at + k, MEM_R8(GV(u32, 0x80065724u) + off + k));
        MEM_W16(at + 6, MEM_R16(GV(u32, 0x80065724u) + off + 6));
        ret = 0;
    }
    V0 = ret;
    R[16] = s16v; R[17] = s17v;
}

/* 8001B8C8 (sep, seq, value): NRPN LSB (98): 20 marks the loop start
 * (+8 the read pointer; +0x1C set), 30 the loop end (+0x1D the count:
 * 0 ends looping (+0x15), 127 loops forever (no delay), else counted
 * down and jumped back), anything else counts as data (+0x1F). The value
 * kept in +0x1B; the next delta time into +0x90. */
void n_libsnd_Nrpn98(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 sv16 = R[16];
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1, v = A2;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    R[16] = rec;
#define DELTA() gcall(0x8001A4A8u, a, b, R[6], R[7])
    switch (v & 0xFF) {
    case 0x14: {
        MEM_W8(rec + 0x1B, (u8)v);
        MEM_W8(rec + 0x1C, 1);
        u32 d = DELTA();
        u32 p = MEM_R32(rec);
        MEM_W32(rec + 0x90, d);
        MEM_W32(rec + 8, p);
        break;
    }
    case 0x1E: {
        const u32 c = MEM_R8(rec + 0x1D);
        MEM_W8(rec + 0x1B, (u8)v);
        if (c == 0) {
            MEM_W8(rec + 0x15, 0);
            MEM_W32(rec + 0x90, DELTA());
        } else if (c < 0x7F) {
            MEM_W8(rec + 0x1D, (u8)(c - 1));
            u32 d = DELTA();
            MEM_W32(rec + 0x90, d);
            if (MEM_R8(rec + 0x1D) == 0) MEM_W8(rec + 0x15, 0);
            else MEM_W32(rec, MEM_R32(rec + 8));
        } else {
            DELTA();
            MEM_W32(rec + 0x90, 0);
            MEM_W32(rec, MEM_R32(rec + 8));
        }
        break;
    }
    default:
        MEM_W8(rec + 0x1B, (u8)v);
        MEM_W8(rec + 0x1F, (u8)(MEM_R8(rec + 0x1F) + 1));
        MEM_W32(rec + 0x90, DELTA());
        break;
    }
#undef DELTA
    R[16] = sv16;
}

/* 8001DBC8 (mode): SsSetTickMode: bit 12 for a manual tick (0x80059CA8),
 * the mode kept (0x80059CA4); the ticks per second (0x80064674) by mode:
 * 6 and up the mode itself, 0 / 5 (by vsync) 50 on PAL else 60, 1 60
 * (mode 5 on NTSC, 60 kept as the mode on PAL), 2 240, 3 120, 4 50 (mode
 * 5 on PAL). */
void n_libsnd_SetTickMode(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 sv16 = R[16];
    const u32 mode = A0;
    R[16] = mode;
    const u32 vm = gcall(0x8002629Cu, R[4], R[5], R[6], R[7]);
    if (mode & 0x1000) { GV(u32, 0x80059CA8u) = 1; GV(u32, 0x80059CA4u) = mode & 0xFFF; }
    else { GV(u32, 0x80059CA8u) = 0; GV(u32, 0x80059CA4u) = mode; }
    const u32 m = GV(u32, 0x80059CA4u);
#define TPS GV(u32, 0x80064674u)
    if (!((s32)m < 6)) TPS = m;
    else if (!(m < 6)) TPS = 0x3C;
    else switch (m) {
    case 0: case 5: TPS = vm == 1 ? 0x32 : 0x3C; break;
    case 1: TPS = 0x3C; GV(u32, 0x80059CA4u) = vm != 0 ? 0x3C : 5; break;
    case 2: TPS = 0xF0; break;
    case 3: TPS = 0x78; break;
    case 4: TPS = 0x32; GV(u32, 0x80059CA4u) = vm == 1 ? 5 : 0x32; break;
    }
#undef TPS
    R[16] = sv16;
}

/* 8001E118: reset every voice not reserved (0x80059874): its record
 * (0x80064F00 + voice * 56) marked free (+0x20 / +0x26 0xFF, priority
 * 0x18), volume 0, pitch 0x1000, start 0x1000 and ADSR 0x80FF / 0x4000
 * set (SpuSetVoiceAttr, mask 0x60093, from the stack), then keyed off. */
void n_libsnd_ResetVoices(void) {
    FRAME(0x68);
    u32 sv[4];
    for (int i = 0; i < 4; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x50, sv[0]);
    MEM_W32(sp + 0x14, 0x60093);
    MEM_W16(sp + 0x24, 0x1000);
    MEM_W32(sp + 0x2C, 0x1000);
    MEM_W16(sp + 0x4A, 0x80FF);
    MEM_W32(sp + 0x60, R[31]);
    MEM_W32(sp + 0x5C, sv[3]);
    MEM_W32(sp + 0x58, sv[2]);
    MEM_W32(sp + 0x54, sv[1]);
    MEM_W16(sp + 0x18, 0);
    MEM_W16(sp + 0x1A, 0);
    MEM_W16(sp + 0x4C, 0x4000);
    if ((s32)GV(s8, 0x80065734u) > 0) {
        R[19] = 1; R[18] = 0x18; R[17] = 0xFF;
        for (u32 iv = 0;;) {
            const s32 i = (s16)iv;
            const u32 bit = 1u << (i & 31);
            R[16] = iv;
            if (!(GV(u32, 0x80059874u) & bit)) {
                const u32 rec = 0x80060000u + (u32)i * 56;
                GV(u16, rec + 0x4F12) = 0x18;
                GV(u16, rec + 0x4F16) = 0;
                GV(u16, rec + 0x4F20) = 0xFF;
                GV(u16, rec + 0x4F22) = 0;
                GV(u16, rec + 0x4F24) = 0;
                GV(u16, rec + 0x4F26) = 0xFF;
                GV(u16, rec + 0x4F46) = 0;
                MEM_W32(sp + 0x10, bit);
                gcall(0x8001E268u, sp + 0x10, R[5], R[6], R[7]);
                GV(u16, 0x80065758u) = (u16)iv;
                gcall(0x80022818u, 1, R[5], R[6], R[7]);
            }
            iv++;
            if (!((s32)(s16)iv < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x8001E238u);
        }
    }
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}
