/* libspu (kernel, 0x800183D8-0x800255F4): the per-voice bit registers,
 * written for every sound the game starts or stops.
 *
 *   0x80059754  pointer to the SPU registers (0x1F801C00; key on at
 *               +0x188, key off at +0x18C, each a low and a high half)
 *   0x80059820  bit 0: changes are kept in shadow copies (0x800645B8 for
 *               the keys, 0x80064430 for the other registers) and written
 *               at the next update; 0x800597EC says which (bit 0 the keys,
 *               bit n the register pair at 0x1F801C00 + 0x18C + 4n)
 *   0x800597E8  the voices keyed on since the last update (shadowed)
 *   0x800597C0  the voices keyed on (direct)
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libspu_SetKey(void);
void n_libspu_SetVoiceBits(void);
void n_libspu_GetEnvelope(void);
void n_libspu_GetVoiceBits(void);
void n_libspu_SetNoiseVoice(void);
void n_libspu_GetNoiseVoice(void);
void n_libspu_SetReverbVoice(void);
void n_libspu_GetReverbVoice(void);
void n_libspu_VmFlush(void);
void n_libspu_SetVoiceVol(void);
void n_libspu_KeyStatus(void);
void n_libspu_SetAddr(void);
void n_libspu_SetVoiceAttr(void);
void n_libspu_Transfer(void);
void n_libspu_SetCommonAttr(void);
void n_libspu_GcSPU(void);
void n_libspu_Malloc(void);
void n_libspu_SetReverbModeParam(void);
void n_libspu_SetReverbRegs(void);

#define SPU       GV(u32, 0x80059754u)
#define DEFERRED  (GV(u32, 0x80059820u) & 1)
#define DIRTY     GV(u32, 0x800597ECu)
#define KEYS      0x800645B8u
#define SHADOW    0x80064430u

/* 800183D8 SpuSetKey (on, voices): key the voices (24 bits) on (1) or
 * off (0). */
void n_libspu_SetKey(void) {
    u32 on = A0, bits = A1 & 0xFFFFFF, hi = bits >> 16;
    if (on == 0) {
        if (DEFERRED) {
            MEM_W16(KEYS + 4, bits);
            MEM_W16(KEYS + 6, hi);
            DIRTY |= 1;
            GV(u32, 0x800597E8u) &= ~bits;
            if (MEM_R16(KEYS) & bits) MEM_W16(KEYS, MEM_R16(KEYS) & ~bits);
            if (MEM_R16(KEYS + 2) & hi) MEM_W16(KEYS + 2, MEM_R16(KEYS + 2) & ~hi);
        } else {
            u32 spu = SPU;
            MEM_W16(spu + 0x18C, bits);
            MEM_W16(spu + 0x18E, hi);
            GV(u32, 0x800597C0u) &= ~bits;
        }
    } else if (on == 1) {
        if (DEFERRED) {
            MEM_W16(KEYS, bits);
            MEM_W16(KEYS + 2, hi);
            DIRTY |= 1;
            GV(u32, 0x800597E8u) |= bits;
            if (MEM_R16(KEYS + 4) & bits) MEM_W16(KEYS + 4, MEM_R16(KEYS + 4) & ~bits);
            if (MEM_R16(KEYS + 6) & hi) MEM_W16(KEYS + 6, MEM_R16(KEYS + 6) & ~hi);
        } else {
            u32 v = GV(u32, 0x800597C0u) | bits;
            u32 spu = SPU;
            MEM_W16(spu + 0x188, bits);
            MEM_W16(spu + 0x18A, hi);
            GV(u32, 0x800597C0u) = v;
        }
    }
}

/* 800210F8 (mode, voices, lo, hi): a per-voice bit register pair (lo and
 * hi as halfword indexes from 0x1F801C00, hi holding voices 16-23): set
 * the voices' bits (mode 1), clear them (0) or set the whole register
 * (8). Returns the register's value (24 bits) with the change. */
void n_libspu_SetVoiceBits(void) {
    u32 mode = A0, bits = A1, lo = A2 * 2, hi = A3 * 2;
    u32 base = DEFERRED ? SHADOW : SPU;
    u32 cur = ((MEM_R16(base + hi) & 0xFF) << 16) | MEM_R16(base + lo);
    u32 bhi = (bits >> 16) & 0xFF;
    if (mode == 1) {
        int d = DEFERRED;
        base = d ? SHADOW : SPU;
        MEM_W16(base + lo, MEM_R16(base + lo) | bits);
        MEM_W16(base + hi, MEM_R16(base + hi) | bhi);
        if (d) DIRTY |= 1u << (((s32)(A2 - 0xC6) >> 1) & 31);
        cur |= bits & 0xFFFFFF;
    } else if (mode == 0) {
        int d = DEFERRED;
        base = d ? SHADOW : SPU;
        MEM_W16(base + lo, MEM_R16(base + lo) & ~bits);
        MEM_W16(base + hi, MEM_R16(base + hi) & ~bhi);
        if (d) DIRTY |= 1u << (((s32)(A2 - 0xC6) >> 1) & 31);
        cur &= ~(bits & 0xFFFFFF);
    } else if (mode == 8) {
        int d = DEFERRED;
        base = d ? SHADOW : SPU;
        MEM_W16(base + lo, bits);
        MEM_W16(base + hi, bhi);
        if (d) DIRTY |= 1u << (((s32)(A2 - 0xC6) >> 1) & 31);
        cur = bits & 0xFFFFFF;
    }
    V0 = cur & 0xFFFFFF;
}

/* 80021478 (voice, out): a voice's current envelope volume (0x1F801C0C
 * + voice * 16) into *out. */
void n_libspu_GetEnvelope(void) {
    V0 = MEM_R16(SPU + A0 * 16 + 0xC);
    MEM_W16(A1, V0);
}

/* 800213E8 (lo, hi): a per-voice bit register pair's 24 bits, read from
 * the SPU (halfword indexes). */
void n_libspu_GetVoiceBits(void) {
    u32 h = MEM_R16(SPU + A1 * 2), l = MEM_R16(SPU + A0 * 2);
    V0 = l | ((h & 0xFF) << 16);
}

/* The voice bit registers (SPU register index pairs): noise mode 0xCA/0xCB,
 * reverb 0xCC/0xCD. */
static void voice_bits(u32 f, u32 a0, u32 a1, u32 a2, u32 a3) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(f, a0, a1, a2, a3);
}

/* 800210C8 SpuSetNoiseVoice (on/off, voice bits). */
void n_libspu_SetNoiseVoice(void) { voice_bits(0x800210F8u, A0, A1, 0xCA, 0xCB); }
/* 800213B8 SpuGetNoiseVoice: the voices in noise mode. */
void n_libspu_GetNoiseVoice(void) { voice_bits(0x800213E8u, 0xCA, 0xCB, R[6], R[7]); }
/* 80021418 SpuSetReverbVoice (on/off, voice bits). */
void n_libspu_SetReverbVoice(void) { voice_bits(0x800210F8u, A0, A1, 0xCC, 0xCD); }
/* 80021448 SpuGetReverbVoice: the voices with reverb. */
void n_libspu_GetReverbVoice(void) { voice_bits(0x800213E8u, 0xCC, 0xCD, R[6], R[7]); }

/* ---- the voice manager (libsnd's SsVm on libspu): 24 voices, 0x38-byte
 * records at 0x80064F16 (envelope at +0, +0x17 noise mode (2 on), +0x18
 * and +0x24 nonzero when their hooks are due), per-voice changes at
 * 0x80065450 (bit 1 volume, 4 pitch, 8 address, 0x10 ADSR) with their
 * values at 0x80065470 (0x10 bytes each) ---- */
#define VM_VOICES     ((s32)GV(s8, 0x80065734u))   /* voices in use */
#define VM_TICK       GV(u32, 0x800655F4u)         /* 0-15 */
#define VM_SILENT     0x800655F8u                  /* per tick, voices whose envelope ended */

/* 80020BF8 (SsVm flush, every sequencer tick): read every voice's
 * envelope and mark the silent ones for this tick; voices silent for
 * all of the last 15 ticks leave noise mode (unless 0x80065770); run the
 * voices' due hooks (0x800655F0, 0x80065468 (voice)); send the changed
 * voice attributes (8001E268 SpuSetVoiceAttr, a SpuVoiceAttr on the
 * stack); key off 0x80065638 and on 0x80064F00 (24-bit masks); set reverb
 * (0x80064F04) and noise (0x80064F08) for the voices in use; clear the
 * masks. Works in the original's s-registers (callees and their frames
 * see them as it had them). */
void n_libspu_VmFlush(void) {
    FRAME(0x78);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x50 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x70, R[31]);
    u32 t = (VM_TICK + 1) & 0xF;
    VM_TICK = t;
    GV(u32, VM_SILENT + t * 4) = 0;
    R[16] = 0;
    if (VM_VOICES > 0) {
        R[20] = VM_SILENT; R[19] = 1; R[18] = 0x80064F16u; R[17] = 0;
        for (;;) {
            gcall(0x80021478u, R[16], R[18], R[6], R[7]);       /* SpuGetVoiceEnvelope */
            if (GV(u16, 0x80064F16u + R[17]) == 0)
                GV(u32, VM_SILENT + VM_TICK * 4) |= 1u << (R[16] & 31);
            R[18] += 0x38;
            R[16]++;
            R[17] += 0x38;
            if (!((s32)R[16] < VM_VOICES)) break;
            LOOP_POLL(0x80020CD0u);
        }
    }
    R[16] = 0;
    if (GV(s8, 0x80065770u) == 0) {
        R[18] = 0xFFFFFFFFu;
        u32 p = VM_SILENT;
        for (;;) {
            u32 w = GV(u32, p);
            R[16]++;
            R[18] &= w;
            if (!((s32)R[16] < 15)) break;
            p += 4;
            LOOP_POLL(0x80020D08u);
        }
        R[16] = 0;
        if (VM_VOICES > 0) {
            R[19] = 1; R[20] = 2; R[17] = 0x80064F2Du;
            for (;;) {
                u32 bit = 1u << (R[16] & 31);
                if (R[18] & bit) {
                    if (GV(s8, R[17]) == 2) {
                        u32 bits = (s32)R[16] < 16 ? (u32)(s32)(s16)bit
                                                   : ((1u << ((R[16] - 16) & 31)) & 0xFF) << 16;
                        gcall(0x800210C8u, 0, bits, R[6], R[7]);   /* SpuSetNoiseVoice off */
                    }
                    GV(u8, R[17]) = 0;
                }
                R[16]++;
                R[17] += 0x38;
                if (!((s32)R[16] < VM_VOICES)) break;
                LOOP_POLL(0x80020D98u);
            }
            R[16] = 0;
        }
    }
    GV(u16, 0x80064F00u) &= ~GV(u16, 0x80065638u);
    GV(u16, 0x80064F02u) &= ~GV(u16, 0x8006563Au);
    R[17] = 0;
    for (;;) {
        if ((s16)GV(u16, 0x80064F2Eu + R[17])) gcall(GV(u32, 0x800655F0u), R[16], R[5], R[6], R[7]);
        if ((s16)GV(u16, 0x80064F3Au + R[17])) gcall(GV(u32, 0x80065468u), R[16], R[5], R[6], R[7]);
        R[16]++;
        R[17] += 0x38;
        if (!((s32)R[16] < 24)) break;
        LOOP_POLL(0x80020E48u);
    }
    /* the attributes: 0x10 mask, 0x14 what is set, 0x18/0x1A volume,
     * 0x24 pitch, 0x2C address, 0x4A/0x4C ADSR */
    const u32 attr = sp + 0x10;
    R[16] = 0; R[17] = 0x80065450u;
    u32 v = 0x80065470u;
    R[23] = v + 0xA; R[22] = v + 8; R[21] = v + 6; R[20] = v + 4; R[19] = v + 2; R[18] = v;
    for (;;) {
        MEM_W32(attr + 4, 0);
        MEM_W32(attr, 1u << (R[16] & 31));
        if (GV(u8, R[17]) & 1) {
            MEM_W32(attr + 4, 3);
            MEM_W16(attr + 8, GV(u16, R[18]));
            MEM_W16(attr + 0xA, GV(u16, R[19]));
        }
        if (GV(u8, R[17]) & 4) {
            MEM_W32(attr + 4, MEM_R32(attr + 4) | 0x10);
            MEM_W16(attr + 0x14, GV(u16, R[20]));
        }
        if (GV(u8, R[17]) & 8) {
            MEM_W32(attr + 4, MEM_R32(attr + 4) | 0x80);
            MEM_W32(attr + 0x1C, (u32)GV(u16, R[21]) << 3);
        }
        if (GV(u8, R[17]) & 0x10) {
            MEM_W32(attr + 4, MEM_R32(attr + 4) | 0x60000);
            MEM_W16(attr + 0x3A, GV(u16, R[22]));
            MEM_W16(attr + 0x3C, GV(u16, R[23]));
        }
        if (MEM_R32(attr + 4)) gcall(0x8001E268u, attr, R[5], R[6], R[7]);
        GV(u8, R[17]) = 0;
        R[17]++;
        R[23] += 0x10; R[22] += 0x10; R[21] += 0x10; R[20] += 0x10; R[19] += 0x10;
        R[16]++;
        R[18] += 0x10;
        if (!((s32)R[16] < 24)) break;
        LOOP_POLL(0x80020F98u);
    }
    gcall(0x800183D8u, 0, ((u32)GV(u8, 0x8006563Au) << 16) | GV(u16, 0x80065638u), R[6], R[7]);   /* key off */
    gcall(0x800183D8u, 1, ((u32)GV(u8, 0x80064F02u) << 16) | GV(u16, 0x80064F00u), R[6], R[7]);   /* key on */
    R[16] = (u32)((s32)0xFFFFFF >> ((24 - VM_VOICES) & 31));
    R[18] = (((u32)GV(u16, 0x80064F06u) << 16) | GV(u16, 0x80064F04u)) & R[16];
    u32 cur = gcall(0x80021448u, R[4], R[5], R[6], R[7]);
    R[17] = ~R[16];
    gcall(0x80021418u, 8, R[18] | (cur & R[17]), R[6], R[7]);
    R[18] = (((u32)GV(u16, 0x80064F0Au) << 16) | GV(u16, 0x80064F08u)) & R[16];
    cur = gcall(0x800213B8u, R[4], R[5], R[6], R[7]);
    gcall(0x800210C8u, 8, R[18] | (cur & R[17]), R[6], R[7]);
    GV(u16, 0x80065638u) = 0;
    GV(u16, 0x8006563Au) = 0;
    GV(u16, 0x80064F00u) = 0;
    GV(u16, 0x80064F02u) = 0;
    GV(u16, 0x80064F08u) = 0;
    GV(u16, 0x80064F0Au) = 0;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
}

/* 800197E8 (voice, left, right): a voice's volume registers (the SPU
 * at *0x80059754), then the library's little delay loop on the stack. */
void n_libspu_SetVoiceVol(void) {
    FRAME(8);
    u32 v = GV(u32, 0x80059754u) + (A0 << 4);
    MEM_W16(v, (u16)(A1 & 0x7FFF));
    MEM_W16(v + 2, (u16)(A2 & 0x7FFF));
    MEM_W32(sp + 4, 1);
    MEM_W32(sp, 0);
    for (;;) {
        if (!((s32)MEM_R32(sp) < 2)) break;
        LOOP_POLL(0x80019854u);
        MEM_W32(sp + 4, MEM_R32(sp + 4) * 13);
        MEM_W32(sp, MEM_R32(sp) + 1);
    }
    V0 = 0;
}

/* 80019268 (voice bit): the voice's key status: -1 for none, else by
 * its key-on bit (0x800597C0) and envelope (+0xC): on 1 (sounding) or 3
 * (silent), off 2 (releasing) or 0. */
void n_libspu_KeyStatus(void) {
    u32 m = A0;
    s32 v = -1;
    for (u32 i = 0;;) {
        if (m & (1u << (i & 31))) { v = (s32)i; LOOP_POLL(0x800192A8u); break; }
        i++;
        if (!((s32)i < 24)) break;
        LOOP_POLL(0x8001928Cu);
    }
    if (v == -1) { V0 = (u32)-1; return; }
    u32 on = GV(u32, 0x800597C0u) & (1u << (v & 31));
    u32 env = MEM_R16(GV(u32, 0x80059754u) + ((u32)v << 4) + 0xC);
    if (on) V0 = env != 0 ? 1 : 3;
    else V0 = (u32)(env != 0) << 1;
}

/* 80018EE8 (reg, addr): an SPU RAM address (rounded up to the transfer
 * unit 0x80059780 when 0x80059778 is set, masked by ~0x80059784) in
 * 8-byte units (shift 0x8005977C) into register reg (index into the SPU
 * at *0x80059754). reg -1: just return the units, -2: the address. */
void n_libspu_SetAddr(void) {
    u32 reg = A0, addr = A1;
    if (GV(u32, 0x80059778u) != 0) {
        u32 unit = GV(u32, 0x80059780u);
        if (unit != 0 && addr % unit != 0) addr = (addr + unit) & ~GV(u32, 0x80059784u);
    }
    u32 u = addr >> (GV(u32, 0x8005977Cu) & 31);
    if (reg == 0xFFFFFFFEu) { V0 = addr; return; }
    if (reg == 0xFFFFFFFFu) { V0 = u & 0xFFFF; return; }
    MEM_W16(GV(u32, 0x80059754u) + (reg << 1), (u16)u);
    V0 = addr;
}

/* Volume with its sweep mode (attr +0xC / +0xE: 1..7 to 0x8000..0xE000;
 * a sweep keeps the value 0..0x7F). */
static u32 spu_vol(u32 attr, u32 vo, u32 mo, int mode, u32 poll) {
    u32 v = MEM_R16(attr + vo) & 0x7FFF, m = 0;
    if (mode) {
        u32 k = (u32)(s32)(s16)(MEM_R16(attr + mo) - 1);
        if (k < 7) {
            LOOP_POLL(poll);
            m = 0x8000u + (k << 12);
        }
    }
    if (m) {
        s32 x = (s16)MEM_R16(attr + vo);
        if (!(x < 128)) v = 0x7F;
        else if (x < 0) v = 0;
    }
    return v | m;
}

/* 8001E268 (attr): libspu's SpuSetVoiceAttr. For every voice in the
 * mask attr+0 the fields named in attr+4 (0: all) into the voice's
 * registers (base 0x80059754, 16 bytes a voice): pitch (+0x14), note
 * (+0x18, into 0x800597F0) and the pitch for a sample note (+0x16, by
 * 8001E868), the volumes and their modes, the start and loop addresses
 * (SetAddr), the ADSR words (+0x3A / +0x3C) and the clamped attack,
 * decay, sustain and release rates and levels with their modes; then
 * libspu's short spin. 0. The original's s-registers. */
void n_libspu_SetVoiceAttr(void) {
    FRAME(0x38);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x30, R[31]);
    MEM_W32(sp + 0x2C, sv[5]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    const u32 a = A0;
    R[16] = a;
    R[20] = 0;
    const u32 m = MEM_R32(a + 4);
    R[17] = m;
    R[21] = 0x800597F0u;
    const int all = m < 1;
    R[18] = (u32)all;
#define ON(b) (all || (m & (b)))
#define VREG(o) (GV(u32, 0x80059754u) + (R[19] << 1) + (o))
    for (;;) {
        const u32 v = R[20];
        if (MEM_R32(a) & (1u << (v & 31))) {
            R[19] = v << 3;
            if (ON(0x10)) MEM_W16(VREG(4), MEM_R16(a + 0x14));
            if (ON(0x40)) MEM_W16(R[21], MEM_R16(a + 0x18));
            if (ON(0x20)) {
                u32 n = MEM_R16(R[21]), s = MEM_R16(a + 0x16);
                u32 p = gcall(0x8001E868u, n >> 8, n & 0xFF, s >> 8, s & 0xFF);
                MEM_W16(VREG(4), (u16)p);
            }
            if (ON(1)) {
                u32 x = spu_vol(a, 8, 0xC, ON(4), 0x8001E39Cu);
                MEM_W16(VREG(0), (u16)x);
            }
            if (ON(2)) {
                u32 x = spu_vol(a, 0xA, 0xE, ON(8), 0x8001E47Cu);
                MEM_W16(VREG(2), (u16)x);
            }
            if (ON(0x80)) gcall(0x80018EE8u, R[19] | 3, MEM_R32(a + 0x1C), R[6], R[7]);
            if (ON(0x10000)) gcall(0x80018EE8u, R[19] | 7, MEM_R32(a + 0x20), R[6], R[7]);
            if (ON(0x20000)) MEM_W16(VREG(8), MEM_R16(a + 0x3A));
            if (ON(0x40000)) MEM_W16(VREG(0xA), MEM_R16(a + 0x3C));
            if (ON(0x800)) {
                u32 r = MEM_R16(a + 0x30), f = 0;
                if (!(r < 0x80)) r = 0x7F;
                if (ON(0x100) && MEM_R32(a + 0x24) == 5) f = 0x80;
                u32 g = VREG(8);
                MEM_W16(g, (u16)((MEM_R16(g) & 0xFF) | ((r | f) << 8)));
            }
            if (ON(0x1000)) {
                u32 r = MEM_R16(a + 0x32);
                if (!(r < 0x10)) r = 0xF;
                u32 g = VREG(8);
                MEM_W16(g, (u16)((MEM_R16(g) & 0xFF0F) | (r << 4)));
            }
            if (ON(0x2000)) {
                u32 r = MEM_R16(a + 0x34), f = 0x100;
                if (!(r < 0x80)) r = 0x7F;
                if (ON(0x200)) {
                    u32 md = MEM_R32(a + 0x28);
                    if (md == 5) f = 0x200;
                    else if (md == 1) f = 0;
                    else if (md == 7) f = 0x300;
                }
                u32 g = VREG(0xA);
                MEM_W16(g, (u16)((MEM_R16(g) & 0x3F) | ((r | f) << 6)));
            }
            if (ON(0x4000)) {
                u32 r = MEM_R16(a + 0x36), f = 0;
                if (!(r < 0x20)) r = 0x1F;
                if (ON(0x400) && MEM_R32(a + 0x2C) == 7) f = 0x20;
                u32 g = VREG(0xA);
                MEM_W16(g, (u16)((MEM_R16(g) & 0xFFC0) | r | f));
            }
            if (ON(0x8000)) {
                u32 r = MEM_R16(a + 0x38);
                if (!(r < 0x10)) r = 0xF;
                u32 g = VREG(8);
                MEM_W16(g, (u16)((MEM_R16(g) & 0xFFF0) | r));
            }
        }
        R[20] = v + 1;
        R[21] += 2;
        if (!((s32)R[20] < 24)) break;
        LOOP_POLL(0x8001E7E4u);
    }
#undef ON
#undef VREG
    MEM_W32(sp + 0x14, 1);
    MEM_W32(sp + 0x10, 0);
    while ((s32)MEM_R32(sp + 0x10) < 2) {
        LOOP_POLL(0x8001E834u);
        MEM_W32(sp + 0x14, MEM_R32(sp + 0x14) * 13);
        MEM_W32(sp + 0x10, MEM_R32(sp + 0x10) + 1);
    }
    V0 = 0;
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* Wait (up to 0xF00 polls) for the halfword at reg & mask to equal v. */
static int spu_wait16(u32 reg, u32 mask, u32 v, u32 poll) {
    if ((MEM_R16(reg) & mask) == v) return 1;
    for (u32 n = 1;; n++) {
        if (!(n < 0xF01)) return 0;
        if ((MEM_R16(reg) & mask) == v) return 1;
        LOOP_POLL(poll);
    }
}

/* 80018B3C _spu_t(mode, ...): libspu's transfer primitive. 2 (addr):
 * the transfer address (>> 0x8005977C) into 0x8005976C and the SPU;
 * 1 / 0: wait for it to latch, then the SPU control's transfer mode DMA
 * write (0x20, 0x800597A4 0) / read (0x30, 1); 3 (addr, size): wait for
 * the mode, set up the SPU side (80019020 / 80019048) and DMA 4 from
 * addr in 64-byte blocks. Timeout -2, else 0. */
void n_libspu_Transfer(void) {
    MEM_W32(R[29], A0);
    MEM_W32(R[29] + 4, A1);
    MEM_W32(R[29] + 8, A2);
    MEM_W32(R[29] + 0xC, A3);
    FRAME(0x18);
    const u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = sp + 0x1C;
    const u32 mode = A0;
    V0 = 0;
    const u32 spu = GV(u32, 0x80059754u);
    if (mode == 2) {
        u32 a = MEM_R32(sp + 0x1C) >> (GV(u32, 0x8005977Cu) & 31);
        GV(u16, 0x8005976Cu) = (u16)a;
        MEM_W16(spu + 0x1A6, (u16)a);
    } else if (mode == 1 || mode == 0) {
        u32 want = GV(u16, 0x8005976Cu);
        GV(u32, 0x800597A4u) = mode == 1 ? 0 : 1;
        if (!spu_wait16(spu + 0x1A6, 0xFFFF, want, mode == 1 ? 0x80018C04u : 0x80018C70u)) V0 = (u32)-2;
        else {
            u32 r = GV(u32, 0x80059754u) + 0x1AA;
            if (mode == 1) MEM_W16(r, (u16)((MEM_R16(r) & 0xFFCF) | 0x20));
            else MEM_W16(r, (u16)(MEM_R16(r) | 0x30));
        }
    } else if (mode == 3) {
        u32 m = GV(u32, 0x800597A4u) != 1 ? 0x20 : 0x30;
        if (!spu_wait16(spu + 0x1AA, 0x30, m, 0x80018CECu)) V0 = (u32)-2;
        else {
            R[16] += 4;
            if (GV(u32, 0x800597A4u) != 1) gcall(0x80019020u, m, spu, R[6], R[7]);
            else gcall(0x80019048u, m, spu, R[6], R[7]);
            GV(u32, 0x800597A8u) = MEM_R32(R[16] - 4);
            u32 size = MEM_R32(R[16]);
            GV(u32, 0x800597ACu) = (size >> 6) + ((size & 0x3F) != 0);
            MEM_W32(GV(u32, 0x80059758u), GV(u32, 0x800597A8u));
            MEM_W32(GV(u32, 0x8005975Cu), (GV(u32, 0x800597ACu) << 16) | 0x10);
            MEM_W32(GV(u32, 0x80059760u), GV(u32, 0x800597A4u) != 1 ? 0x01000201u : 0x01000200u);
            V0 = 0;
        }
    }
    R[16] = s0;
}

/* SpuSetCommonAttr's master volume word: mode (+m) 1-7 the sweep modes
 * (0x8000 + (mode - 1) << 12, the volume clamped to 0-127), else (0, out
 * of range, or the mode bit not in the mask) the raw volume. */
static u16 spu_common_vol(u32 a, u32 vo, u32 mo, int use_mode) {
    u32 mode = use_mode ? (u32)(s32)(s16)MEM_R16(a + mo) : 0;
    u32 v;
    if (mode - 1 < 7) {
        s32 x = (s16)MEM_R16(a + vo);
        v = !(x < 128) ? 0x7F : x < 0 ? 0 : MEM_R16(a + vo);
        return (u16)((v & 0x7FFF) | (0x8000u + ((mode - 1) << 12)));
    }
    return (u16)(MEM_R16(a + vo) & 0x7FFF);
}

/* 80019468 (attr): SpuSetCommonAttr: by the mask (+0; 0 all) the master
 * volume left/right (with their modes, mask 4/8), CD and external volume,
 * and the CD/external audio and reverb enable bits of SPUCNT (+0x1AA). */
void n_libspu_SetCommonAttr(void) {
    const u32 a = A0, m = MEM_R32(a);
    const int all = m == 0;
    if (all || (m & 1)) MEM_W16(SPU + 0x180, spu_common_vol(a, 4, 8, all || (m & 4)));
    if (all || (m & 2)) MEM_W16(SPU + 0x182, spu_common_vol(a, 6, 0xA, all || (m & 8)));
    if (all || (m & 0x40)) MEM_W16(SPU + 0x1B0, MEM_R16(a + 0x10));
    if (all || (m & 0x80)) MEM_W16(SPU + 0x1B2, MEM_R16(a + 0x12));
    if (all || (m & 0x400)) MEM_W16(SPU + 0x1B4, MEM_R16(a + 0x1C));
    if (all || (m & 0x800)) MEM_W16(SPU + 0x1B6, MEM_R16(a + 0x1E));
    static const struct { u32 bit, off; u16 cnt; } en[4] = {
        { 0x100, 0x14, 4 }, { 0x200, 0x18, 1 }, { 0x1000, 0x20, 8 }, { 0x2000, 0x24, 2 } };
    for (int k = 0; k < 4; k++)
        if (all || (m & en[k].bit)) {
            u32 r = SPU + 0x1AA;
            u16 v = MEM_R16(r);
            MEM_W16(r, MEM_R32(a + en[k].off) ? (u16)(v | en[k].cnt) : (u16)(v & ~en[k].cnt));
        }
}

/* 80024188: _spu_gcSPU: tidy the SPU memory table (0x8005983C, 8-byte
 * entries: address | flags, size; 0x80059838 the last index): merge free
 * blocks (bit 31) with the free blocks right after them (skipping unused
 * entries, 0x2FFFFFFF), mark empty entries unused, sort the entries by
 * address up to the end marker (bit 30), move the end marker into the
 * first unused entry, then fold trailing free blocks into the end. */
void n_libspu_GcSPU(void) {
#define N   GV(s32, 0x80059838u)
#define TBL GV(u32, 0x8005983Cu)
    if (N >= 0) {
        const s32 last = N;
        const u32 t = TBL;
        u32 e = t;
        for (s32 i = 0;;) {
            int next = 1;
            if (MEM_R32(e) & 0x80000000u) {
                s32 j = i + 1;
                for (;;) {
                    if (MEM_R32(t + ((u32)j << 3)) != 0x2FFFFFFFu) break;
                    j++;
                    LOOP_POLL(0x800241ECu);
                }
                const u32 f = t + ((u32)j << 3);
                const u32 v = MEM_R32(f);
                if ((v & 0x80000000u) && (v & 0x0FFFFFFFu) == (MEM_R32(e) & 0x0FFFFFFFu) + MEM_R32(e + 4)) {
                    MEM_W32(f, 0x2FFFFFFFu);
                    MEM_W32(e + 4, MEM_R32(e + 4) + MEM_R32(f + 4));
                    next = 0;
                }
            }
            if (next) { e += 8; i++; }
            if (last < i) break;
            LOOP_POLL(0x80024250u);
        }
    }
    if (N >= 0) {
        const s32 last = N;
        u32 e = TBL;
        for (s32 i = 0;;) {
            if (MEM_R32(e + 4) == 0) MEM_W32(e, 0x2FFFFFFFu);
            i++;
            e += 8;
            if (last < i) break;
            LOOP_POLL(0x800242A0u);
        }
    }
    if (N >= 0) {
        const u32 t = TBL;
        u32 a = t;
        for (s32 i = 0;;) {
            if (MEM_R32(a) & 0x40000000u) break;
            s32 j = i + 1;
            if (!(N < j)) {
                const s32 last = N;
                u32 b = t + ((u32)j << 3);
                for (;;) {
                    const u32 vb = MEM_R32(b);
                    if (vb & 0x40000000u) break;
                    const u32 va = MEM_R32(a);
                    if ((vb & 0x0FFFFFFFu) < (va & 0x0FFFFFFFu)) {
                        MEM_W32(a, vb);
                        u32 sa = MEM_R32(a + 4);
                        MEM_W32(a + 4, MEM_R32(b + 4));
                        MEM_W32(b, va);
                        MEM_W32(b + 4, sa);
                    }
                    j++;
                    if (last < j) break;
                    b += 8;
                    LOOP_POLL(0x80024354u);
                }
            }
            i++;
            if (N < i) break;
            a += 8;
            LOOP_POLL(0x8002436Cu);
        }
    }
    if (N >= 0) {
        const u32 t = TBL;
        u32 e = t;
        for (s32 i = 0;;) {
            const u32 v = MEM_R32(e);
            if (v & 0x40000000u) break;
            if (v == 0x2FFFFFFFu) {
                const u32 end = t + ((u32)N << 3);
                MEM_W32(e, MEM_R32(end));
                N = i;
                MEM_W32(e + 4, MEM_R32(end + 4));
                break;
            }
            i++;
            if (N < i) break;
            e += 8;
            LOOP_POLL(0x800243F0u);
        }
    }
    s32 i = N - 1;
    if (i >= 0) {
        const u32 t = TBL;
        u32 e = t + ((u32)i << 3);
        for (;;) {
            const u32 v = MEM_R32(e);
            if (!(v & 0x80000000u)) break;
            const s32 old = N;
            MEM_W32(e, (v & 0x0FFFFFFFu) | 0x40000000u);
            N = i;
            MEM_W32(e + 4, MEM_R32(e + 4) + MEM_R32(t + ((u32)old << 3) + 4));
            i--;
            if (i < 0) break;
            e -= 8;
            LOOP_POLL(0x80024478u);
        }
    }
#undef N
#undef TBL
}

/* 80024528 (size): SpuMalloc: the size rounded (0x80059784, then down to
 * the unit 1 << *0x8005977C); the table tidied (GcSPU) unless it's empty,
 * then the first free block big enough, or the end marker (bit 30) if
 * there's room before the reverb area (0x800597CC on: 0x10000 - its start
 * 0x800597D0, in units). A free block is split (the rest into a new entry
 * at 0x80059838) and the end marker moved past an allocation from it.
 * Returns the block's address word, or -1. */
void n_libspu_Malloc(void) {
    FRAME(0x28);
    u32 sv[4];
    for (int i = 0; i < 4; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x20, R[31]);
#define N   GV(s32, 0x80059838u)
#define MAX GV(s32, 0x80059834u)
#define TBL GV(u32, 0x8005983Cu)
    u32 size = A0;
    s32 found = -1;
    u32 rev = GV(u32, 0x800597CCu) == 0 ? 0
            : (0x10000u - GV(u32, 0x800597D0u)) << (GV(u32, 0x8005977Cu) & 31);
    u32 al = GV(u32, 0x80059784u);
    if (size & ~al) size += al;
    u32 sh = GV(u32, 0x8005977Cu) & 31;
    size = (u32)((s32)size >> sh) << sh;
    if (MEM_R32(TBL) & 0x40000000u) found = 0;
    else {
        gcall(0x80024188u, R[4], R[5], R[6], R[7]);
        const s32 max = MAX;
        if (0 < max) {
            u32 e = TBL;
            for (s32 i = 0;;) {
                u32 v = MEM_R32(e);
                if ((v & 0x40000000u) || ((v & 0x80000000u) && !(MEM_R32(e + 4) < size))) { found = i; break; }
                i++;
                if (!(i < max)) break;
                e += 8;
                LOOP_POLL(0x8002464Cu);
            }
        }
    }
    u32 ret = 0xFFFFFFFFu;
    if (found != -1) {
        const u32 off = (u32)found << 3;
        const u32 t = TBL;
        const u32 e = t + off;
        const u32 v = MEM_R32(e);
        if (v & 0x40000000u) {
            if (found < MAX && !(MEM_R32(e + 4) - rev < size)) {
                const u32 nx = t + ((u32)(found + 1) << 3);
                MEM_W32(nx, ((MEM_R32(e) & 0x0FFFFFFFu) + size) | 0x40000000u);
                MEM_W32(nx + 4, MEM_R32(e + 4) - size);
                u32 w = MEM_R32(e);
                N = found + 1;
                MEM_W32(e + 4, size);
                MEM_W32(e, w & 0x0FFFFFFFu);
                gcall(0x80024188u, R[4], R[5], R[6], R[7]);
                ret = MEM_R32(TBL + off);
            }
        } else {
            const u32 bs = MEM_R32(e + 4);
            if (size < bs) {
                const s32 n = N;
                if (n < MAX) {
                    const u32 s = t + ((u32)n << 3);
                    u32 o0 = MEM_R32(s), o1 = MEM_R32(s + 4);
                    MEM_W32(s, (v + size) | 0x80000000u);
                    MEM_W32(s + 4, bs - size);
                    N = n + 1;
                    MEM_W32(s + 8, o0);
                    MEM_W32(s + 0xC, o1);
                }
            }
            const u32 e2 = TBL + off;
            u32 w = MEM_R32(e2);
            MEM_W32(e2 + 4, size);
            MEM_W32(e2, w & 0x0FFFFFFFu);
            gcall(0x80024188u, R[4], R[5], R[6], R[7]);
            ret = MEM_R32(TBL + off);
        }
    }
#undef N
#undef MAX
#undef TBL
    V0 = ret;
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* SpuSetReverbModeParam's copy of a mode's register set (0x44 bytes at
 * 0x80059DE4 + mode * 0x44) onto the stack. */
static void spu_rev_params(u32 dst, u32 pc) {
    u32 s = 0x80059DE4u + (u32)GV(s32, 0x800597D8u) * 0x44;
    for (s32 n = 0x43;;) {
        MEM_W8(dst, MEM_R8(s));
        s++; dst++; n--;
        if (n == -1) break;
        LOOP_POLL(pc);
    }
}

/* 8001F758 (attr): SpuSetReverbModeParam: by the mask (+0; 0 all) the
 * reverb mode (+4; bit 8: clear the work area too; its SPU memory checked
 * with 8001AC58, its start in 0x800597D0), the delay (+0xC) and feedback
 * (+0x10) of the echo/delay modes (7, 8), and the depth (+8/+0xA). A new
 * mode writes the registers (8001FC38) with reverb off while it does, the
 * depth zeroed, and the work area set up (80018EA4) / cleared (8001AA38). */
void n_libspu_SetReverbModeParam(void) {
    FRAME(0x88);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    for (int i = 0; i < 8; i++) MEM_W32(sp + 0x60 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x80, sfp);
    MEM_W32(sp + 0x84, R[31]);
    const u32 at = A0, m = MEM_R32(at);
    const int all = m == 0;
    int setmode = 0, delay = 0, fb = 0, wasOn = 0;
    u32 ret = 0;
    MEM_W32(sp + 0x58, 0);
    MEM_W32(sp + 0x10, 0);
#define MODE GV(s32, 0x800597D8u)
    if (all || (m & 1)) {
        u32 mode = MEM_R32(at + 4);
        if (mode & 0x100) { mode &= ~0x100u; MEM_W32(sp + 0x58, 1); }
        if (!(mode < 10) || gcall(0x8001AC58u, GV(u32, 0x80059844u + (mode << 2)), R[5], R[6], R[7]) != 0) {
            ret = 0xFFFFFFFFu;
            goto out;
        }
        setmode = 1;
        MODE = (s32)mode;
        GV(u32, 0x800597D0u) = GV(u32, 0x80059844u + ((u32)MODE << 2));
        spu_rev_params(sp + 0x10, 0x8001F864u);
        if (MODE == 7) { GV(u32, 0x800597E4u) = 0x7F; GV(u32, 0x800597E0u) = 0x7F; }
        else if (MODE == 8) { GV(u32, 0x800597E4u) = 0; GV(u32, 0x800597E0u) = 0x7F; }
        else { GV(u32, 0x800597E4u) = 0; GV(u32, 0x800597E0u) = 0; }
    }
    if ((all || (m & 8)) && MODE < 9 && !(MODE < 7)) {
        delay = 1;
        if (!setmode) {
            spu_rev_params(sp + 0x10, 0x8001F934u);
            MEM_W32(sp + 0x10, 0x0C011C00u);
        }
        u32 d = MEM_R32(at + 0xC);
        GV(u32, 0x800597E0u) = d;
        u32 a = (u32)((s32)(d << 13) / 127), b = (u32)((s32)(d << 12) / 127);
        u16 r14 = MEM_R16(sp + 0x14), r36 = MEM_R16(sp + 0x36), r16 = MEM_R16(sp + 0x16);
        u16 r2e = MEM_R16(sp + 0x2E), r4e = MEM_R16(sp + 0x4E), r4c = MEM_R16(sp + 0x4C);
        MEM_W16(sp + 0x28, (u16)(a - r14));
        MEM_W16(sp + 0x2A, (u16)(b - r16));
        MEM_W16(sp + 0x34, (u16)(r36 + b));
        MEM_W16(sp + 0x2C, (u16)(r2e + b));
        MEM_W16(sp + 0x4A, (u16)(r4e + b));
        MEM_W16(sp + 0x48, (u16)(r4c + b));
    }
    if ((all || (m & 0x10)) && MODE < 9 && !(MODE < 7)) {
        fb = 1;
        if (!setmode) {
            if (!delay) {
                spu_rev_params(sp + 0x10, 0x8001FA58u);
                MEM_W32(sp + 0x10, 0x80);
            } else MEM_W32(sp + 0x10, MEM_R32(sp + 0x10) | 0x80);
        }
        u32 f = MEM_R32(at + 0x10);
        GV(u32, 0x800597E4u) = f;
        MEM_W16(sp + 0x22, (u16)((s32)((f * 129) << 8) / 127));
    }
    if (setmode) {
        wasOn = (MEM_R16(SPU + 0x1AA) >> 7) & 1;
        if (wasOn) MEM_W16(SPU + 0x1AA, MEM_R16(SPU + 0x1AA) & 0xFF7F);
        MEM_W16(SPU + 0x184, 0);
        MEM_W16(SPU + 0x186, 0);
        GV(u16, 0x800597DCu) = 0;
        GV(u16, 0x800597DEu) = 0;
    } else {
        if (all || (m & 2)) {
            MEM_W16(SPU + 0x184, MEM_R16(at + 8));
            GV(u16, 0x800597DCu) = MEM_R16(at + 8);
        }
        if (all || (m & 4)) {
            MEM_W16(SPU + 0x186, MEM_R16(at + 0xA));
            GV(u16, 0x800597DEu) = MEM_R16(at + 0xA);
        }
    }
    if (setmode || delay || fb) gcall(0x8001FC38u, sp + 0x10, R[5], R[6], R[7]);
    if (MEM_R32(sp + 0x58)) gcall(0x8001AA38u, (u32)MODE, R[5], R[6], R[7]);
    if (setmode) {
        gcall(0x80018EA4u, 0xD1, GV(u32, 0x800597D0u), 0, R[7]);
        if (wasOn) MEM_W16(SPU + 0x1AA, MEM_R16(SPU + 0x1AA) | 0x80);
    }
#undef MODE
out:
    V0 = ret;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8001FC38 (params): the reverb registers 0x1C0-0x1FE from params + 4..,
 * bit k of the mask (+0; 0 all) for register k. */
void n_libspu_SetReverbRegs(void) {
    const u32 a = A0, m = MEM_R32(a);
    for (u32 k = 0; k < 32; k++)
        if (m == 0 || (m & (1u << k))) MEM_W16(SPU + 0x1C0 + k * 2, MEM_R16(a + 4 + k * 2));
}
