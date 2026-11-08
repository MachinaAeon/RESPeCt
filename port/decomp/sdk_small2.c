/* More small Psy-Q library functions (kernel): wrappers, set-up and
 * table helpers of libspu, libsnd, libetc, libcd, libgte/libgs, libgpu,
 * libcard and libpad.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). Calls into game code pass the original's return address. */
#include "poll.h"

void rt_syscall(uint32_t pc);

static inline u32 call_ra(u32 ra, u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    u32 old = R[31];
    R[31] = ra;
    u32 v = gcall(addr, a0, a1, a2, a3);
    R[31] = old;
    return v;
}

/* A BIOS vector call (B0 / A0 number) with $ra as the original's jal
 * leaves it. */
static inline u32 bios_ra(u32 ra, u32 vec, u32 num) {
    u32 old = R[31];
    R[31] = ra;
    R[9] = num;
    R[10] = vec;
    rt_call(vec);
    R[31] = old;
    return R[2];
}

#define WRAP() FRAME(0x18); MEM_W32(sp + 0x10, R[31])

/* ---- libspu ---- */

/* 80018EA4 (reg, value, scaled): an SPU register (reg * 2 from the
 * base); scaled values are shifted down by the address unit (0x8005977C). */
void n_libspu_WriteReg(void) {
    const u32 a = (A0 << 1) + GV(u32, 0x80059754u);
    if (A2 == 0) MEM_W16(a, (u16)A1);
    else MEM_W16(a, (u16)(A1 >> (GV(u32, 0x8005977Cu) & 31)));
    V0 = a;
}

/* 80019070: a delay: 60 multiplications of a stack word by 13. */
void n_libspu_Delay(void) {
    FRAME(8);
    MEM_W32(sp + 4, 0xD);
    MEM_W32(sp, 0);
    for (;;) {
        if (!((s32)MEM_R32(sp) < 60)) break;
        LOOP_POLL(0x800190BCu);
        MEM_W32(sp + 4, MEM_R32(sp + 4) * 13);
        MEM_W32(sp, MEM_R32(sp) + 1);
    }
    V0 = 0;
}

/* 800191C0: SpuStart: once (0x80059824): the transfer callback 800189D8,
 * the SPU DMA event opened (0xF0000009, 0x20, 0x2000) into 0x800597BC and
 * enabled, in a critical section. */
void n_libspu_Start(void) {
    WRAP();
    if (GV(u32, 0x80059824u) != 0) return;
    GV(u32, 0x80059824u) = 1;
    call_ra(0x800191E4u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    call_ra(0x800191F0u, 0x80019238u, 0x800189D8u, R[5], R[6], R[7]);
    u32 ev = call_ra(0x80019208u, 0x80039948u, 0xF0000009u, 0x20, 0x2000, 0);
    GV(u32, 0x800597BCu) = ev;
    call_ra(0x80019218u, 0x80039988u, ev, R[5], R[6], R[7]);
    call_ra(0x80019220u, 0x80039A18u, R[4], R[5], R[6], R[7]);
}

/* 800192F8 (addr, size): SpuWrite: at most 0x7EFF0 bytes (80018DBC);
 * the busy flag cleared unless transfers are by DMA (0x8005978C). Returns
 * the size. */
void n_libspu_Write(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    u32 size = A1;
    if (0x7EFF0u < size) size = 0x7EFF0u;
    R[16] = size;
    call_ra(0x80019328u, 0x80018DBCu, A0, size, R[6], R[7]);
    if (GV(u32, 0x8005978Cu) == 0) GV(u32, 0x80059788u) = 0;
    V0 = size;
    R[16] = s16v;
}

/* 80019358 (addr): SpuSetTransferStartAddr: out of 0x1010-0x7FFF8: 0;
 * else kept (SetAddr -1) in 0x8005976C, returned in bytes. */
void n_libspu_SetTransferStartAddr(void) {
    WRAP();
    const u32 a = A0;
    if (0x7EFE8u < a - 0x1010u) { V0 = 0; return; }
    u32 r = call_ra(0x80019380u, 0x80018EE8u, 0xFFFFFFFFu, a, R[6], R[7]);
    GV(u16, 0x8005976Cu) = (u16)r;
    V0 = (u32)GV(u16, 0x8005976Cu) << (GV(u32, 0x8005977Cu) & 31);
}

/* 8001A9D8: SpuInit's tail: callbacks reset, 8001AA18, 8001AA38 (7),
 * 8001A938. */
void n_libspu_InitTail(void) {
    WRAP();
    call_ra(0x8001A9E8u, 0x80025808u, R[4], R[5], R[6], R[7]);
    call_ra(0x8001A9F0u, 0x8001AA18u, R[4], R[5], R[6], R[7]);
    call_ra(0x8001A9F8u, 0x8001AA38u, 7, R[5], R[6], R[7]);
    V0 = call_ra(0x8001AA00u, 0x8001A938u, R[4], R[5], R[6], R[7]);
}

/* 800217E8 (n, table): SpuInitMalloc: n blocks; the table's first entry
 * the end marker at 0x1010 with all the memory after it. Returns n, or 0. */
void n_libspu_InitMalloc(void) {
    const s32 n = (s32)A0;
    const u32 t = A1;
    if (!(n > 0)) { V0 = 0; return; }
    MEM_W32(t, 0x40001010u);
    GV(u32, 0x8005983Cu) = t;
    GV(u32, 0x80059838u) = 0;
    GV(u32, 0x80059834u) = (u32)n;
    MEM_W32(t + 4, (0x10000u << (GV(u32, 0x8005977Cu) & 31)) - 0x1010u);
    V0 = (u32)n;
}

/* ---- libsnd ---- */

/* 8001CD58 (sep, seq, prog): program change: the current channel's
 * program (+0x37 + channel), the next delta time. */
void n_libsnd_ProgramChange(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    R[16] = rec;
    MEM_W8(rec + MEM_R8(rec + 0x17) + 0x37, (u8)A2);
    MEM_W32(rec + 0x90, call_ra(0x8001CDB0u, 0x8001A4A8u, a, b, R[6], R[7]));
    R[16] = s16v;
}

/* 8001D118 (a, b, c): 8001D238 (a, 0, b as a byte, c). */
void n_libsnd_Wrap1D118(void) {
    WRAP();
    V0 = call_ra(0x8001D140u, 0x8001D238u, (u32)(s32)(s16)A0, 0, (u32)(s32)(s8)A1, (u32)(s32)(s16)A2);
}

/* 8001D358 (l, r): SsSetMVol: the master volume (* 129) through
 * SpuSetCommonAttr (mask 3, raw volumes). */
void n_libsnd_SetMVol(void) {
    FRAME(0x40);
    const s32 l = (s16)A0, r = (s16)A1;
    MEM_W32(sp + 0x10, 3);
    MEM_W16(sp + 0x14, (u16)(l * 129));
    MEM_W32(sp + 0x38, R[31]);
    MEM_W16(sp + 0x16, (u16)(r * 129));
    V0 = call_ra(0x8001D398u, 0x80019468u, sp + 0x10, R[5], R[6], R[7]);
}

/* 8001DDA8 (id, l, r): SsSeqSetVol through the sequence's state: playing
 * (+0x98 1): SeqSetVol on the voices, else just the volume (+0x58/5A). */
void n_libsnd_SsSeqSetVol(void) {
    WRAP();
    const u32 id = (u32)(s32)(s16)A0;
    const u32 rec = GV(u32, 0x80064678u + (id << 2));
    if (MEM_R32(rec + 0x98) == 1)
        V0 = call_ra(0x8001DE00u, 0x800231F8u, id, A1 & 0xFFFF, A2 & 0xFFFF, 1);
    else {
        MEM_W16(rec + 0x58, (u16)A1);
        MEM_W16(rec + 0x5A, (u16)A2);
    }
}

/* 8001DEA0 (lo, hi, l, r): 80023764 (lo | hi << 8, l, r). */
void n_libsnd_Wrap1DEA0(void) {
    WRAP();
    V0 = call_ra(0x8001DEC4u, 0x80023764u, (u32)(s32)(s16)(A0 | (A1 << 8)), A2, A3, A3);
}

/* 8001F6A4 (voice): SsUtKeyOff-style: the voice (< 24) keyed off under
 * the busy flag; 0, or -1. */
void n_libsnd_UtVoiceOff(void) {
    WRAP();
    const u32 v = A0;
    if (GV(u32, 0x8006466Cu) == 1) { V0 = 0xFFFFFFFFu; return; }
    GV(u32, 0x8006466Cu) = 1;
    if (!((v & 0xFFFF) < 0x18)) { GV(u32, 0x8006466Cu) = 0; V0 = 0xFFFFFFFFu; return; }
    GV(u16, 0x80065758u) = (u16)v;
    call_ra(0x8001F6FCu, 0x80022818u, 0, R[5], R[6], R[7]);
    V0 = 0;
    GV(u32, 0x8006466Cu) = 0;
}

/* 80023764 (id, &l, &r): SsSeqGetVol (the id kept in 0x80065754). */
void n_libsnd_SeqGetVol(void) {
    const u32 id = A0;
    const u32 rec = GV(u32, 0x80064678u + ((id & 0xFF) << 2)) + (u32)((s32)(id & 0xFF00) >> 8) * 176;
    GV(u16, 0x80065754u) = (u16)id;
    MEM_W16(A1, MEM_R16(rec + 0x58));
    MEM_W16(A2, MEM_R16(rec + 0x5A));
    V0 = (u32)(s32)GV(s16, 0x80065754u);
}

/* 80024488 (header, id): SsVabOpenHead with SPU memory from 800244BC. */
void n_libsnd_SsVabOpenHead(void) {
    WRAP();
    u32 r = call_ra(0x800244A8u, 0x80024868u, A0, (u32)(s32)(s16)A1, 0x800244BCu, 0);
    V0 = (u32)(s32)(s16)r;
}

/* 800244BC (size, mode, id): SpuMalloc for a VAB; on failure the VAB
 * slot freed, the lock released and the count lowered; -1. */
void n_libsnd_VabAlloc(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    R[16] = A2;
    const u32 id = A2;
    u32 r = call_ra(0x800244D0u, 0x80024528u, A0, A1, A2, A3);
    if (r != 0xFFFFFFFFu) V0 = r;
    else {
        GV(u8, 0x80065760u + (u32)(s32)(s16)id) = 0;
        call_ra(0x800244FCu, 0x80021848u, 0, R[5], R[6], R[7]);
        GV(u16, 0x800657B8u) = (u16)(GV(u16, 0x800657B8u) - 1);
        V0 = 0xFFFFFFFFu;
    }
    R[16] = s16v;
}

/* 80025460 (vab, prog): a program's index among the VAB's non-empty ones
 * (+8 of its record), or -1. */
void n_libsnd_ProgIndex(void) {
    const s32 p = (s16)A1;
    if (!((A0 & 0xFFFF) < 0x11) || p < 0 || (s32)GV(s16, 0x80065722u) < p) { V0 = 0xFFFFFFFFu; return; }
    const u32 t = GV(u32, 0x80065658u + (u32)((s32)(A0 << 16) >> 14));
    V0 = (u32)(s32)(s16)MEM_R16(t + ((u32)p << 4) + 8);
}

/* ---- libetc ---- */

/* 80025898 (fn): the interrupt module's +0x14 entry (4, fn). */
void n_libetc_Entry14(void) {
    WRAP();
    V0 = call_ra(0x800258BCu, MEM_R32(GV(u32, 0x8005B2BCu) + 0x14), 4, A0, R[6], R[7]);
}

/* 80025EC8: the DMA interrupt set-up: DICR (*0x8005B300) 0x100, the
 * channel callbacks (0x8005B2DC) cleared, its handler 80025F20 hooked on
 * interrupt 3; returns the callback setter 80025F8C. */
void n_libetc_DmaInit(void) {
    WRAP();
    MEM_W32(GV(u32, 0x8005B300u), 0x100);
    GV(u32, 0x8005B2FCu) = 0;
    call_ra(0x80025EF8u, 0x80025FB8u, 0x8005B2DCu, 8, R[6], R[7]);
    call_ra(0x80025F08u, 0x80025838u, 0, 0x80025F20u, R[6], R[7]);
    V0 = 0x80025F8Cu;
}

/* 80025FE8: the root counter set-up: its callbacks (0x8005B310) and the
 * counter mask (*0x8005B30C) cleared, its handler 80026034 hooked on
 * interrupt 3; returns 800261B4. */
void n_libetc_CounterInit(void) {
    WRAP();
    call_ra(0x80026000u, 0x80026260u, 0x8005B310u, 8, R[6], R[7]);
    MEM_W32(GV(u32, 0x8005B30Cu), 0);
    call_ra(0x8002601Cu, 0x80025838u, 3, 0x80026034u, R[6], R[7]);
    V0 = 0x800261B4u;
}

/* ---- libcd ---- */

/* 80026378: 80027B98, and if that fails, whether 80027A5C gives 0. */
void n_libcd_Wrap26378(void) {
    WRAP();
    u32 r = call_ra(0x80026388u, 0x80027B98u, R[4], R[5], R[6], R[7]);
    if (r != 0) { V0 = 0; return; }
    r = call_ra(0x80026398u, 0x80027A5Cu, R[4], R[5], R[6], R[7]);
    V0 = r == 0;
}

/* 80028EE8: the stream state cleared (0x80065910-3C), the ring's frames
 * freed (800291F8 0, count *0x80065954). */
void n_libcd_StReset(void) {
    WRAP();
    const u32 n = GV(u32, 0x80065954u);
    GV(u32, 0x8006593Cu) = 0;
    GV(u32, 0x80065938u) = 0;
    GV(u32, 0x80065934u) = 0;
    GV(u32, 0x8006592Cu) = 0;
    call_ra(0x80028F20u, 0x800291F8u, 0, n, R[6], R[7]);
    GV(u32, 0x8006591Cu) = 0;
    GV(u16, 0x80065914u) = 0;
    GV(u32, 0x80065910u) = 0;
}

/* 800291F8 (first, n): n ring frames' first words cleared (32 bytes
 * each at *0x80065950). */
void n_libcd_StClearFrames(void) {
    const u32 first = A0, n = A1;
    V0 = 0;
    if (n == 0) return;
    for (u32 i = 0;;) {
        const u32 k = i + first;
        i++;
        MEM_W32(GV(u32, 0x80065950u) + (k << 5), 0);
        if (!(i < n)) break;
        LOOP_POLL(0x8002921Cu);
    }
}

/* ---- libgte / libgs ---- */

/* 8002DD54 (m): the light matrix kept at 0x80065B50 copied out. */
void n_gte_ReadColorMatrix(void) {
    for (u32 k = 0; k < 0x20; k += 4) MEM_W32(A0 + k, GV(u32, 0x80065B50u + k));
}

/* 8002DCF0 (m): the colour matrix kept (0x80065B50) and set. */
void n_gte_SetColorMatrix(void) {
    WRAP();
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, 0x80065B50u + k) = MEM_R32(A0 + k);
    call_ra(0x8002DD44u, 0x8002DDA8u, A0, R[5], R[6], R[7]);
}

/* 8002DFF8: InitGeom, a black back colour, offset 0, the draw offsets
 * (0x80065A84/86) cleared. */
void n_gs_InitGeomAll(void) {
    WRAP();
    call_ra(0x8002E008u, 0x8002A350u, R[4], R[5], R[6], R[7]);
    call_ra(0x8002E018u, 0x8002A8D8u, 0, 0, 0, R[7]);
    call_ra(0x8002E024u, 0x8002A8F8u, 0, 0, R[6], R[7]);
    GV(u16, 0x80065A86u) = 0;
    GV(u16, 0x80065A84u) = 0;
}

/* 8002C638 (w, h, mode, dither, [vram]): GsInitGraph and the 3D set-up:
 * InitGraph, 8002DFF8, 0x80065A94 cleared, Init3D, the clip and the draw
 * offset applied. */
void n_gs_InitGraphAll(void) {
    FRAME(0x28);
    MEM_W32(sp + 0x1C, R[17]);
    MEM_W32(sp + 0x18, R[16]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 w = A0 & 0xFFFF, h = A1 & 0xFFFF;
    R[17] = w; R[16] = h;
    MEM_W32(sp + 0x20, R[31]);
    MEM_W32(sp + 0x10, MEM_R16(sp + 0x38));
    call_ra(0x8002C66Cu, 0x8002C6ACu, w, h, A2 & 0xFFFF, A3 & 0xFFFF);
    call_ra(0x8002C674u, 0x8002DFF8u, R[4], R[5], R[6], R[7]);
    GV(u16, 0x80065A94u) = 0;
    call_ra(0x8002C688u, 0x8002C830u, w, h, R[6], R[7]);
    call_ra(0x8002C690u, 0x8002CCA8u, R[4], R[5], R[6], R[7]);
    V0 = call_ra(0x8002C698u, 0x8002CB98u, R[4], R[5], R[6], R[7]);
    R[16] = s16v; R[17] = s17v;
}

/* 8002C7C8 (w, h, mode, dither, [vram]): the environments for a new
 * screen size without a reset: draw environment fields (0x80065A24-28),
 * display size, interlace and 24-bit (0x80065A74-81), mode bit 2 into
 * 0x80065A96, then Init3D. */
void n_gs_SetScreenSize(void) {
    WRAP();
    const u32 vram = MEM_R16(sp + 0x28);
    GV(u16, 0x80065A24u) = 0;
    GV(u8, 0x80065A26u) = (u8)A3;
    GV(u8, 0x80065A27u) = 0;
    GV(u8, 0x80065A28u) = 0;
    GV(u16, 0x80065A74u) = (u16)A0;
    GV(u16, 0x80065A76u) = (u16)A1;
    GV(u8, 0x80065A80u) = (u8)(A2 & 1);
    GV(u16, 0x80065A96u) = (u16)(A2 & 4);
    GV(u8, 0x80065A81u) = (u8)vram;
    V0 = call_ra(0x8002C820u, 0x8002C830u, A0 & 0xFFFF, A1 & 0xFFFF, R[6], R[7]);
}

/* 8002D798: the draw offset to the screen's centre (0x80065A08/0A),
 * applied; the projection's z range (0x80065B20-28: 0x3FFF, 0, 10). */
void n_gs_CenterOffset(void) {
    WRAP();
    GV(u16, 0x80065A08u) = (u16)((s32)GV(u32, 0x80065B18u) / 2);
    GV(u16, 0x80065A0Au) = (u16)((s32)GV(u32, 0x80065B1Cu) / 2);
    call_ra(0x8002D7DCu, 0x8002CB98u, 0x80065A08u, R[5], R[6], R[7]);
    GV(u32, 0x80065B28u) = 10;
    GV(u32, 0x80065B24u) = 0;
    GV(u32, 0x80065B20u) = 0x3FFF;
}

/* 8002F9E0 (out, hmd): the HMD's block pointers (n at +6; blocks from
 * +8, each its size in words at +4). Returns n. */
void n_gs_HmdBlocks(void) {
    u32 out = A0, p = A1 + 8;
    const u32 n = MEM_R16(A1 + 6);
    V0 = n;
    if (n == 0) return;
    for (s32 i = 0;;) {
        MEM_W32(out, p);
        p += (u32)MEM_R16(p + 4) << 2;
        i++;
        out += 4;
        if (!(i < (s32)n)) break;
        LOOP_POLL(0x8002FA0Cu);
    }
}

/* ---- libgpu ---- */

/* 8003590C (p): DrawPrim through the driver: wait (+0x3C, 0), then
 * send the primitive's words (+0x14: p + 4, its length). */
void n_libgpu_DrawPrim(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 p = A0, len = MEM_R8(p + 3);
    R[16] = p; R[17] = len;
    call_ra(0x80035938u, MEM_R32(GV(u32, 0x80061944u) + 0x3C), 0, R[5], R[6], R[7]);
    V0 = call_ra(0x80035954u, MEM_R32(GV(u32, 0x80061944u) + 0x14), p + 4, len, R[6], R[7]);
    R[16] = s16v; R[17] = s17v;
}

/* 80035680 (rect, data): LoadImage: checked ("LoadImage"), queued through
 * the driver (+8: its load entry +0x1C, rect, 8 bytes, data). */
void n_libgpu_LoadImage2(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[31]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 r = A0, d = A1;
    R[16] = r; R[17] = d;
    call_ra(0x800356A8u, 0x800353DCu, 0x800109A8u, r, R[6], R[7]);
    const u32 drv = GV(u32, 0x80061944u);
    V0 = call_ra(0x800356CCu, MEM_R32(drv + 8), MEM_R32(drv + 0x1C), r, 8, d);
    R[16] = s16v; R[17] = s17v;
}

/* 80037030 (p, n): n words sent to GP0 by hand (GP1 0x04000000 first). */
void n_libgpu_SendWords(void) {
    u32 p = A0;
    const u32 n = A1;
    MEM_W32(GV(u32, 0x80061A54u), 0x04000000u);
    if (n != 0) {
        for (u32 r = n - 1;;) {
            const u32 w = MEM_R32(p);
            p += 4;
            r--;
            MEM_W32(GV(u32, 0x80061A50u), w);
            if (r == 0xFFFFFFFFu) break;
            LOOP_POLL(0x80037060u);
        }
    }
    V0 = 0;
}

/* 80038148 SetDefDispEnv(env, x, y, w, [h]). */
void n_libgpu_SetDefDispEnv(void) {
    const u32 e = A0;
    const u32 h = MEM_R32(R[29] + 0x10);
    MEM_W16(e, (u16)A1);
    MEM_W16(e + 2, (u16)A2);
    MEM_W16(e + 4, (u16)A3);
    for (u32 k = 8; k < 0x10; k += 2) MEM_W16(e + k, 0);
    MEM_W8(e + 0x11, 0);
    MEM_W8(e + 0x10, 0);
    MEM_W8(e + 0x13, 0);
    MEM_W8(e + 0x12, 0);
    MEM_W16(e + 6, (u16)h);
    V0 = e;
}

/* 80038188 GetTPage(tp, abr, x, y). */
void n_libgpu_GetTPage(void) {
    V0 = ((A0 & 3) << 7) | ((A1 & 3) << 5) | (u32)((s32)(A3 & 0x100) >> 4)
       | (u32)((s32)(A2 & 0x3FF) >> 6) | ((A3 & 0x200) << 2);
}

/* 80038488 (mode): DecDCTReset: callbacks reset for mode 0, then
 * 8003873C (mode). */
void n_libpress_DecDCTReset(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const u32 m = A0;
    R[16] = m;
    if (m == 0) call_ra(0x800384A4u, 0x80025808u, R[4], R[5], R[6], R[7]);
    V0 = call_ra(0x800384ACu, 0x8003873Cu, m, R[5], R[6], R[7]);
    R[16] = s16v;
}

/* ---- the anti-modchip check's region read ---- */

/* 800170B8: the console's region from the BIOS ROM (0xBFC7FF52): 'A'
 * (America) 1 / 1, 'E' (Europe) 0 / 2, else 1 / 0 into 0x80059704 /
 * 0x800645B4. */
void n_kernel_RegionCheck(void) {
    const u32 c = MEM_R8(0xBFC7FF52u);
    if (c == 0x45) { GV(u32, 0x80059704u) = 0; GV(u32, 0x800645B4u) = 2; V0 = 2; }
    else if (c == 0x41) { GV(u32, 0x80059704u) = 1; GV(u32, 0x800645B4u) = 1; V0 = 1; }
    else { GV(u32, 0x80059704u) = 1; GV(u32, 0x800645B4u) = 0; V0 = 1; }
}

/* ---- libcard / libapi ---- */

/* 800390C8: card stop: StartCARD's opposite (B0 4B, then B0 5B with 0)
 * in a critical section. */
void n_libcard_Stop(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    u32 c = call_ra(0x800390D8u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    R[16] = c;
    call_ra(0x800390E0u, 0x80039148u, R[4], R[5], R[6], R[7]);
    call_ra(0x800390E8u, 0x80039AA8u, 0, R[5], R[6], R[7]);
    if (c == 1) call_ra(0x800390FCu, 0x80039A18u, R[4], R[5], R[6], R[7]);
    V0 = 0;
    R[16] = s16v;
}

/* 80039168: a word of the BIOS's card state (B0 57's table +0x16C,
 * +0x1988) cleared, then FlushCache; $ra kept in 0x8006BA10 meanwhile. */
void n_libcard_ClearState(void) {
    GV(u32, 0x8006BA10u) = R[31];
    u32 t = bios_ra(0x80039180u, 0xB0, 0x57);
    R[10] = 9;
    const u32 p = MEM_R32(t + 0x16C);
    MEM_W32(p + 0x1988, 0);
    call_ra(0x80039198u, 0x800398E8u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x8006BA10u);
}

/* 800392D8: patch five words (from 0x80039230) into the BIOS's card
 * code (B0 57's table +0x16C, +0x9C8), then FlushCache; interrupts left
 * off. */
void n_libcard_Patch(void) {
    GV(u32, 0x8006BA10u) = R[31];
    call_ra(0x800392E8u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    u32 t = bios_ra(0x800392F8u, 0xB0, 0x57);
    u32 p = MEM_R32(t + 0x16C);
    R[3] = MEM_R32(p + 0x9C8);
    for (u32 s = 0x80039230u;;) {
        MEM_W32(p + 0x9C8, MEM_R32(s));
        s += 4;
        p += 4;
        if (s == 0x80039244u) break;
        LOOP_POLL(0x80039324u);
    }
    call_ra(0x80039334u, 0x800398E8u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x8006BA10u);
}

/* 80039348: copy 28 words (0x800391AC) to 0xDF80 (the BIOS's RAM). */
void n_libcard_CopyLow(void) {
    u32 d = 0xDF80u;
    for (u32 s = 0x800391ACu;;) {
        MEM_W32(d, MEM_R32(s));
        s += 4;
        d += 4;
        if (s == 0x8003921Cu) break;
        LOOP_POLL(0x8003936Cu);
    }
}

/* ---- libpad ---- */

/* 8003A7F4 (-, w, b): the port record from the handler at 0x80062A88,
 * its pending word and byte set (8003B618). */
void n_libpad_SetPortPending(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[31]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 w = A1, b = A2;
    R[16] = w; R[17] = b;
    u32 port = call_ra(0x8003A818u, GV(u32, 0x80062A88u), R[4], R[5], R[6], R[7]);
    V0 = call_ra(0x8003A828u, 0x8003B618u, port, w, b, R[7]);
    R[16] = s16v; R[17] = s17v;
}

/* 8003CEA8: the pad handlers at 0x80062A8C-94. */
void n_libpad_SetHandlers2(void) {
    GV(u32, 0x80062A8Cu) = 0x8003CEDCu;
    GV(u32, 0x80062A90u) = 0x8003D330u;
    GV(u32, 0x80062A94u) = 0x8003CFECu;
    V0 = 0x8003CFECu;
}

/* 8003DC2C: three words (from 0x8003DCCC) patched into the BIOS's pad
 * code (B0 56's table +0x18, +0x80), FlushCache, in a critical section;
 * $ra kept in 0x8006C100 meanwhile. */
void n_libpad_PatchBios(void) {
    GV(u32, 0x8006C100u) = R[31];
    call_ra(0x8003DC3Cu, 0x80039A08u, R[4], R[5], R[6], R[7]);
    u32 t = bios_ra(0x8003DC4Cu, 0xB0, 0x56);
    u32 p = MEM_R32(t + 0x18);
    for (u32 s = 0x8003DCCCu;;) {
        MEM_W32(p + 0x80, MEM_R32(s));
        s += 4;
        p += 4;
        if (s == 0x8003DCD8u) break;
        LOOP_POLL(0x8003DC70u);
    }
    call_ra(0x8003DC80u, 0x800398E8u, R[4], R[5], R[6], R[7]);
    call_ra(0x8003DC88u, 0x80039A18u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x8006C100u);
}

/* ---- batch: 31-37 instruction functions ---- */

/* 80018DBC (addr, size): the SPU write: by DMA (0x80059770 set:
 * 80018818), else by hand through _spu_t (2: the address, 1, 3: the
 * data). Returns size. */
void n_libspu_WriteData(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x18, R[31]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 a = A0, n = A1;
    R[17] = a; R[16] = n;
    if (GV(u32, 0x80059770u) != 0) call_ra(0x80018E28u, 0x80018818u, a, n, R[6], R[7]);
    else {
        const u32 ad = (u32)GV(u16, 0x8005976Cu) << (GV(u32, 0x8005977Cu) & 31);
        call_ra(0x80018DFCu, 0x80018B3Cu, 2, ad, R[6], R[7]);
        call_ra(0x80018E04u, 0x80018B3Cu, 1, ad, R[6], R[7]);
        call_ra(0x80018E14u, 0x80018B3Cu, 3, a, n, R[7]);
    }
    V0 = n;
    R[16] = s16v; R[17] = s17v;
}

/* 8001AC58 (addr): whether an SPU address (in units) lies in an
 * allocated block of the table (0x8005983C); 0 if there's no table. */
void n_libspu_IsAllocated(void) {
    u32 e = GV(u32, 0x8005983Cu);
    const u32 a = A0 << (GV(u32, 0x8005977Cu) & 31);
    if (e == 0) { V0 = 0; return; }
    for (;;) {
        const u32 v = MEM_R32(e);
        if (!(v & 0x80000000u)) {
            if (v & 0x40000000u) { V0 = 0; return; }
            const u32 s = v & 0x0FFFFFFFu;
            if (!(s < a) || a < s + MEM_R32(e + 4)) { V0 = 1; return; }
        }
        e += 8;
        LOOP_POLL(0x8001ACD0u);
    }
}

/* 80024108 (addr): SpuFree: the block starting there marked free (up to
 * the end marker), then the table tidied (GcSPU). */
void n_libspu_Free(void) {
    WRAP();
    const s32 n = (s32)GV(u32, 0x80059834u);
    if (n > 0) {
        u32 e = GV(u32, 0x8005983Cu);
        for (s32 i = 0;;) {
            const u32 v = MEM_R32(e);
            if (v & 0x40000000u) break;
            i++;
            if (v == A0) { MEM_W32(e, A0 | 0x80000000u); break; }
            if (!(i < n)) break;
            e += 8;
            LOOP_POLL(0x80024164u);
        }
    }
    call_ra(0x80024174u, 0x80024188u, R[4], R[5], R[6], R[7]);
}

/* 800206B4 (voice, l, r): a voice's SPU volume (* 129) set directly. */
void n_libsnd_SetVoiceVol(void) {
    const u32 v = A0;
    if (!((v & 0xFFFF) < 0x18)) { V0 = 0xFFFFFFFFu; return; }
    const s32 i = (s16)v;
    GV(u16, 0x80065472u + ((u32)i << 4)) = (u16)((s16)A2 * 129);
    GV(u16, 0x80065470u + ((u32)i << 4)) = (u16)((s16)A1 * 129);
    GV(u8, 0x80065450u + (u32)i) |= 3;
    V0 = 0;
}

/* 80020108 (l, r): SsUtSetReverbDepth: the depth (* 32767 / 127) through
 * SpuSetReverbModeParam (0x80065640, mask 6). */
void n_libsnd_UtSetReverbDepth(void) {
    WRAP();
    const s32 l = (s16)A0, r = (s16)A1;
    GV(u32, 0x80065640u) = 6;
    GV(u16, 0x80065648u) = (u16)(((s32)((u32)l * 0x7FFFu)) / 127);
    GV(u16, 0x8006564Au) = (u16)(((s32)((u32)r * 0x7FFFu)) / 127);
    V0 = call_ra(0x80020184u, 0x8001F758u, 0x80065640u, R[5], R[6], R[7]);
}

/* 800262E8: CdInit: up to five tries of the reset (80026378); then the
 * library's callbacks (0x8005B3F4 / F8 / 35C) installed, 1; else a
 * message and 0. */
void n_libcd_CdInit(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    for (u32 k = 4;;) {
        R[16] = k;
        if (call_ra(0x80026300u, 0x80026378u, R[4], R[5], R[6], R[7]) == 1) {
            GV(u32, 0x8005B3F4u) = 0x800263B4u;
            GV(u32, 0x8005B3F8u) = 0x800263DCu;
            GV(u32, 0x8005B35Cu) = 0x80026404u;
            GV(u32, 0x8005B360u) = 0;
            V0 = 1;
            R[16] = s16v;
            return;
        }
        k--;
        if (k == 0xFFFFFFFFu) break;
        LOOP_POLL(0x80026350u);
    }
    call_ra(0x80026364u, 0x800398C8u, 0x800103A0u, R[5], R[6], R[7]);
    V0 = 0;
    R[16] = s16v;
}

/* 80027900 (vol): CdMix: the CD audio volumes into the CD registers
 * (index 2: left to left / right, 3: right to left / right, apply). */
void n_libcd_CdMix(void) {
    const u32 p = A0;
    MEM_W8(GV(u32, 0x8005B6B8u), 2);
    MEM_W8(GV(u32, 0x8005B6C0u), MEM_R8(p));
    MEM_W8(GV(u32, 0x8005B6C4u), MEM_R8(p + 1));
    MEM_W8(GV(u32, 0x8005B6B8u), 3);
    MEM_W8(GV(u32, 0x8005B6BCu), MEM_R8(p + 2));
    MEM_W8(GV(u32, 0x8005B6C0u), MEM_R8(p + 3));
    MEM_W8(GV(u32, 0x8005B6C4u), 0x20);
    V0 = 0;
}

/* 80028B88 (mode): CdSetmode-style stream start: Setmode (0xE); with
 * bit 8 the streaming DMA and sector callbacks installed (bit 5 noted in
 * 0x80065988); then ReadS (0x1B). */
void n_libcd_StStart(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x18, R[16]);
    MEM_W32(sp + 0x1C, R[31]);
    const u32 s16v = R[16];
    const u32 m = A0;
    R[16] = m;
    MEM_W8(sp + 0x10, (u8)m);
    call_ra(0x80028BACu, 0x800265E4u, 0xE, sp + 0x10, 0, R[7]);
    if (m & 0x100) {
        GV(u32, 0x80065988u) = (m & 0x20) ? 0 : 1;
        call_ra(0x80028BE0u, 0x80026A00u, 0x80028FC8u, R[5], R[6], R[7]);
        call_ra(0x80028BECu, 0x800265D0u, 0x80028C0Cu, R[5], R[6], R[7]);
    }
    V0 = call_ra(0x80028BFCu, 0x800265E4u, 0x1B, 0, 0, R[7]);
    R[16] = s16v;
}

/* 80028F48: the streaming callbacks removed (by 0x8005B414: 80029FD4 /
 * 80029FAC, else the CD DMA and sector callbacks), two CD flags cleared,
 * in a critical section. */
void n_libcd_StUnhook(void) {
    WRAP();
    call_ra(0x80028F58u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    if (GV(u32, 0x8005B414u) == 1) {
        call_ra(0x80028F74u, 0x80029FD4u, 0, R[5], R[6], R[7]);
        call_ra(0x80028F7Cu, 0x80029FACu, 0, R[5], R[6], R[7]);
    } else {
        call_ra(0x80028F8Cu, 0x80026A00u, 0, R[5], R[6], R[7]);
        call_ra(0x80028F94u, 0x800265D0u, 0, R[5], R[6], R[7]);
    }
    MEM_W8(GV(u32, 0x8005B75Cu), 0);
    MEM_W8(GV(u32, 0x8005B768u), 0);
    call_ra(0x80028FB8u, 0x80039A18u, R[4], R[5], R[6], R[7]);
}

/* 800290B8 (mode, a, b, c, [d]): StSetStream: the values (StSetValues 1,
 * a, b), the stream state cleared, mode bit 0, c and d kept. */
void n_libcd_StSetStream(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x1C, R[31]);
    const u32 sv[3] = { R[16], R[17], R[18] };
    const u32 m = A0, c = A3, d = MEM_R32(sp + 0x30);
    R[16] = m; R[17] = c; R[18] = d;
    call_ra(0x800290E0u, 0x800292F8u, 1, A1, A2, R[7]);
    GV(u32, 0x80065940u) = 0;
    GV(u32, 0x800659A0u) = c;
    GV(u32, 0x80065918u) = m & 1;
    GV(u32, 0x80065928u) = 0;
    GV(u32, 0x80065920u) = 0;
    GV(u16, 0x80065914u) = 0;
    GV(u32, 0x80065910u) = 0;
    GV(u32, 0x800659A4u) = d;
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 8002A350 InitGeom: libgs's set-up (8002BCC8; $ra kept in 0x8005C05C),
 * the GTE enabled (SR bit 30) and its constants: H 1000, DQA/DQB, the
 * Z-average factors 0x155 / 0x100, offset 0. */
void n_gte_InitGeom(void) {
    GV(u32, 0x8005C05Cu) = R[31];
    call_ra(0x8002A360u, 0x8002BCC8u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x8005C05Cu);
    rt_mtc0(12, rt_mfc0(12) | 0x40000000u);
    gte_write_ctrl(29, 0x155);
    gte_write_ctrl(30, 0x100);
    gte_write_ctrl(26, 0x3E8);
    gte_write_ctrl(27, 0xFFFFEF9Eu);
    gte_write_ctrl(28, 0x01400000u);
    gte_write_ctrl(24, 0);
    gte_write_ctrl(25, 0);
    V0 = rt_mfc0(12);
}

/* 8002DDD8 (mode): the projection mode 0-3 into 0x80065B24 (0 for 0),
 * else a message. */
void n_gs_SetProjectionMode(void) {
    WRAP();
    const s32 m = (s32)A0;
    if (m == 0) GV(u32, 0x80065B24u) = 0;
    else if (m >= 1 && m <= 3) GV(u32, 0x80065B24u) = (u32)m;
    else call_ra(0x8002DE44u, 0x800398C8u, 0x80010720u, A0, R[6], R[7]);
}

/* 8002FC18 (ctx): an HMD image block's images loaded (LoadImage each:
 * the 12-byte entries' rect, data at the block base + offset * 4). */
void n_gs_HmdLoadImages(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 sv[3] = { R[16], R[17], R[18] };
    const u32 c = A0;
    R[16] = c;
    const u32 p = MEM_R32(c);
    const u32 n = MEM_R16(p + 2);
    R[18] = n;
    MEM_W32(c, p + 4);
    if (n != 0) {
        for (s32 i = 0;;) {
            R[17] = (u32)i;
            const u32 e = MEM_R32(c);
            call_ra(0x8002FC64u, 0x80035620u, e, MEM_R32(c + 0x14) + (MEM_R32(e + 8) << 2), R[6], R[7]);
            MEM_W32(c, MEM_R32(c) + 0xC);
            i++;
            if (!(i < (s32)n)) break;
            LOOP_POLL(0x8002FC78u);
        }
    }
    V0 = MEM_R32(c);
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 800336A8 (base): pop a model list (0x80060D54 stack: count, entries);
 * the popped list's 0x50-byte records' pointers (+0x4C) made absolute
 * from base. Returns the records. */
void n_gs_PopModelList(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const u32 base = A0;
    R[16] = base;
    const u32 st = call_ra(0x800336BCu, 0x800333C0u, R[4], R[5], R[6], R[7]);
    const u32 n = MEM_R32(st);
    const u32 e = MEM_R32(st + (n << 2));
    MEM_W32(st, n - 1);
    const s32 cnt = (s32)MEM_R32(e);
    const u32 recs = e + 4;
    if (cnt > 0) {
        u32 q = recs + 0x4C;
        for (s32 i = 0;;) {
            const u32 v = MEM_R32(q);
            if (v != 0) MEM_W32(q, base + (v << 2));
            i++;
            if (!(i < cnt)) break;
            q += 0x50;
            LOOP_POLL(0x8003370Cu);
        }
    }
    V0 = recs;
    R[16] = s16v;
}

/* 800354F8 ClearImage(rect, r, g, b): checked ("ClearImage"), queued
 * through the driver (+8: its clear entry +0xC, rect, 8 bytes, the
 * colour). */
void n_libgpu_ClearImageQ(void) {
    FRAME(0x28);
    u32 sv[4];
    for (int i = 0; i < 4; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x20, R[31]);
    const u32 rect = A0, r = A1, g = A2, b = A3;
    R[19] = rect; R[18] = r; R[17] = g; R[16] = b;
    call_ra(0x80035530u, 0x800353DCu, 0x80010984u, rect, R[6], R[7]);
    const u32 col = ((b & 0xFF) << 16) | ((g & 0xFF) << 8) | (r & 0xFF);
    const u32 drv = GV(u32, 0x80061944u);
    V0 = call_ra(0x8003556Cu, MEM_R32(drv + 8), MEM_R32(drv + 0xC), rect, 8, col);
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* 80039244: five words (0x8003921C) patched into the BIOS's pad
 * handler area (B0 56's table +0x18: +0x70/+0x74 halves form the
 * address, + 0x28), the end kept at 0xDFFC, FlushCache; interrupts left
 * off; $ra in 0x8006BA10 meanwhile. */
void n_libcard_PatchPad(void) {
    GV(u32, 0x8006BA10u) = R[31];
    call_ra(0x80039254u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    u32 t = bios_ra(0x80039264u, 0xB0, 0x56);
    const u32 p = MEM_R32(t + 0x18);
    u32 d = ((MEM_R32(p + 0x70) & 0xFFFF) << 16) + (MEM_R32(p + 0x74) & 0xFFFF) + 0x28;
    for (u32 s = 0x8003921Cu;;) {
        MEM_W32(d, MEM_R32(s));
        s += 4;
        d += 4;
        if (s == 0x80039230u) break;
        LOOP_POLL(0x800392B0u);
    }
    MEM_W32(0xDFFCu, d);
    call_ra(0x800392C4u, 0x800398E8u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x8006BA10u);
}

/* ---- batch: 38-45 instruction functions ---- */

/* 8001A888: SsEnd-style: the sound tick unhooked unless the tick is
 * manual (0x80059CA8) or already off (0x80059CB6 0x7F): the vsync tick
 * (+0xC) via the +0x14 entry, else the root counter callback (6, or
 * interrupt 0 back to the old one +8); then marked off (0x7F). */
void n_libsnd_TickStop(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const u32 st = 0x80059CA8u;
    R[16] = st;
    V0 = 0x7F;
    if (MEM_R32(st) == 0) GV(u8, st + 0xD) = 0;
    if (MEM_R32(st) == 0 && GV(s8, st + 0xE) != 0x7F) {
        call_ra(0x8001A8C4u, 0x80039A08u, R[4], R[5], R[6], R[7]);
        if (GV(s8, st + 0xC) != 0) {
            call_ra(0x8001A8DCu, 0x80025898u, 0, R[5], R[6], R[7]);
            GV(u8, st + 0xC) = 0;
        } else if (GV(s8, st + 0xE) == 0) {
            call_ra(0x8001A900u, 0x80025838u, 0, MEM_R32(st + 8), R[6], R[7]);
            MEM_W32(st + 8, 0);
        } else call_ra(0x8001A910u, 0x80025838u, 6, 0, R[6], R[7]);
        call_ra(0x8001A918u, 0x80039A18u, R[4], R[5], R[6], R[7]);
        GV(u8, 0x80059CB6u) = 0x7F;
    }
    R[16] = s16v;
}

/* 8001A938: libsnd's init tail: 16 saved SPU registers (0x80059880)
 * written back (0x1F801D80), 80021498 (24 voices), the 32 sequence
 * slots' 16-word records (0x800646F8) cleared, tick rate 60, nothing
 * open or busy. */
void n_libsnd_InitTail(void) {
    WRAP();
    u32 s = 0x80059880u, d = 0x1F801D80u;
    for (s32 i = 0;;) {
        const u32 v = MEM_R16(s);
        s += 2;
        i++;
        MEM_W16(d, (u16)v);
        if (!(i < 16)) break;
        d += 2;
        LOOP_POLL(0x8001A968u);
    }
    call_ra(0x8001A978u, 0x80021498u, 0x18, R[5], R[6], R[7]);
    u32 r = 0x800646F8u;
    for (s32 k = 0;;) {
        u32 p = r + 0x3C;
        for (s32 n = 15;;) {
            MEM_W32(p, 0);
            n--;
            if (!(n >= 0)) break;
            p -= 4;
            LOOP_POLL(0x8001A994u);
        }
        k++;
        if (!(k < 32)) break;
        r += 0x40;
        LOOP_POLL(0x8001A9A4u);
    }
    GV(u32, 0x80064674u) = 0x3C;
    GV(u32, 0x80064670u) = 0;
    GV(u32, 0x8006466Cu) = 0;
}

/* 800193B8 (wait): SpuIsTransferCompleted: 1 when not DMA (0x800597C4 1)
 * or no transfer is pending (0x80059788 1); else the DMA event tested
 * (B0 0B), waited for with wait 1; done marks 0x80059788. */
void n_libspu_IsTransferCompleted(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x18, R[31]);
    const u32 s16v = R[16], s17v = R[17];
    const u32 w = A0;
    R[17] = w; R[16] = 1;
    u32 v = 1;
    if (GV(u32, 0x800597C4u) != 1 && GV(u32, 0x80059788u) != 1) {
        v = call_ra(0x80019408u, 0x80039978u, GV(u32, 0x800597BCu), R[5], R[6], R[7]);
        if (w != 1) {
            if (v == 1) GV(u32, 0x80059788u) = v;
        } else {
            if (v == 0) {
                for (;;) {
                    v = call_ra(0x80019428u, 0x80039978u, GV(u32, 0x800597BCu), R[5], R[6], R[7]);
                    if (v != 0) break;
                    LOOP_POLL(0x80019428u);
                }
            }
            v = 1;
            GV(u32, 0x80059788u) = v;
        }
    }
    V0 = v;
    R[16] = s16v; R[17] = s17v;
}

/* 80020198 (type): SsUtSetReverbType: types 0-9 (negative: the same
 * with the clear bit 0x100); type 0 also turns reverb off (80020248 0).
 * SpuSetReverbModeParam (0x80065640, mask 1). Returns the type, or -1. */
void n_libsnd_UtSetReverbType(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    u32 t = A0, neg = 0;
    if ((s32)(A0 << 16) < 0) { neg = 1; t = 0u - A0; }
    if (!((t & 0xFFFF) < 0xA)) { V0 = 0xFFFFFFFFu; R[16] = s16v; return; }
    GV(u32, 0x80065640u) = 1;
    GV(u32, 0x80065644u) = neg ? (u32)(s32)(s16)(t | 0x100) : (u32)(s32)(s16)t;
    const u32 ty = (u32)(s32)(s16)t;
    R[16] = ty;
    if (ty == 0) call_ra(0x8002020Cu, 0x80020248u, 0, R[5], R[6], R[7]);
    call_ra(0x80020218u, 0x8001F758u, 0x80065640u, R[5], R[6], R[7]);
    V0 = ty;
    R[16] = s16v;
}

/* 80023858 (id): key off every voice playing that sequence (not
 * reserved). */
void n_libsnd_SeqVoicesOff(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 sv[3] = { R[16], R[17], R[18] };
    if ((s32)GV(s8, 0x80065734u) > 0) {
        const s32 id = (s16)A0;
        R[18] = 1; R[17] = (u32)id;
        for (u32 k = 0;;) {
            R[16] = k;
            const u32 v = k & 0xFF;
            if (!(GV(u32, 0x80059874u) & (1u << (v & 31)))
                && (s32)GV(s16, 0x80064F20u + v * 56) == id) {
                GV(u16, 0x80065758u) = (u16)v;
                call_ra(0x800238D4u, 0x80022818u, 0, R[5], R[6], R[7]);
            }
            k++;
            if (!((s32)(k & 0xFF) < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x800238E8u);
        }
    }
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 80024058 (id): SsVabClose: an open VAB's SPU memory freed (SpuFree),
 * its slot and count released, the lock dropped if held. */
void n_libsnd_VabClose(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    if ((A0 & 0xFFFF) < 0x10) {
        const s32 id = (s16)A0;
        R[16] = (u32)id;
        const s32 st = GV(u8, 0x80065760u + (u32)id);
        if (st < 3 && st != 0) {
            call_ra(0x800240B0u, 0x80024108u, GV(u32, 0x800657C0u + ((u32)id << 2)), R[5], R[6], R[7]);
            GV(u8, 0x80065760u + (u32)id) = 0;
            GV(u16, 0x800657B8u) = (u16)(GV(u16, 0x800657B8u) - 1);
            if (call_ra(0x800240D8u, 0x80021870u, R[4], R[5], R[6], R[7]) == 1)
                call_ra(0x800240ECu, 0x80021848u, 0, R[5], R[6], R[7]);
        }
    }
    R[16] = s16v;
}

/* 800261B4 (n, fn): a root counter's callback (0x8005B310) set or
 * cleared, its interrupt bit (16 + n) in the mask word (*0x8005B30C,
 * keeping bit 23). Returns the old one. */
void n_libetc_SetCounterCallback(void) {
    const u32 n = A0, fn = A1;
    const u32 slot = 0x8005B310u + (n << 2);
    const u32 old = MEM_R32(slot);
    if (fn != old) {
        const u32 m = GV(u32, 0x8005B30Cu);
        const u32 bit = 1u << ((n + 0x10) & 31);
        if (fn != 0) {
            MEM_W32(slot, fn);
            MEM_W32(m, (MEM_R32(m) & 0xFFFFFF) | bit | 0x800000u);
        } else {
            MEM_W32(slot, 0);
            MEM_W32(m, ((MEM_R32(m) & 0xFFFFFF) | 0x800000u) & ~bit);
        }
    }
    V0 = old;
}

/* 8002BCC8: libgs's BIOS patch: if the BIOS's exception code (B0 56's
 * table +0x18, +0x28) matches the 6 words at 0x8002BD74 (the known
 * version), the 6 at 0x8002BD8C copied over it; FlushCache; in a
 * critical section;
 * $ra kept in 0x800659C8. */
void n_gs_PatchBios(void) {
    GV(u32, 0x800659C8u) = R[31];
    call_ra(0x8002BCD8u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    u32 t = bios_ra(0x8002BCE8u, 0xB0, 0x56);
    const u32 base = MEM_R32(t + 0x18) + 0x28;
    u32 p = base;
    int same = 1;
    for (u32 s = 0x8002BD74u;;) {
        const u32 a = MEM_R32(s), b = MEM_R32(p);
        s += 4;
        p += 4;
        if (a != b) { same = 0; break; }
        if (s == 0x8002BD8Cu) break;
        LOOP_POLL(0x8002BD1Cu);
    }
    if (same) {
        p = base;
        for (u32 s = 0x8002BD8Cu;;) {
            MEM_W32(p, MEM_R32(s));
            s += 4;
            p += 4;
            if (s == 0x8002BDA4u) break;
            LOOP_POLL(0x8002BD48u);
        }
    }
    call_ra(0x8002BD58u, 0x800398E8u, R[4], R[5], R[6], R[7]);
    call_ra(0x8002BD60u, 0x80039A18u, R[4], R[5], R[6], R[7]);
    R[31] = GV(u32, 0x800659C8u);
}

/* 8002D6F8 (x0, y0, x1, y1): GsDefDispBuff: the two buffers' offsets
 * (0x800659F8 / FC), the clip offsets (0x80065A00 / 04; zero when
 * interlaced, 0x80065A96), clip and draw offset applied. */
void n_gs_DefDispBuff2(void) {
    WRAP();
    const u32 x0 = A0, y0 = A1, x1 = A2, y1 = A3;
    GV(u16, 0x800659F8u) = (u16)x0;
    GV(u16, 0x800659FAu) = (u16)x1;
    GV(u16, 0x800659FCu) = (u16)y0;
    GV(u16, 0x800659FEu) = (u16)y1;
    if (GV(s16, 0x80065A96u) != 0) {
        GV(u16, 0x80065A00u) = 0; GV(u16, 0x80065A02u) = 0;
        GV(u16, 0x80065A04u) = 0; GV(u16, 0x80065A06u) = 0;
    } else {
        GV(u16, 0x80065A00u) = (u16)x0; GV(u16, 0x80065A02u) = (u16)x1;
        GV(u16, 0x80065A04u) = (u16)y0; GV(u16, 0x80065A06u) = (u16)y1;
    }
    call_ra(0x8002D778u, 0x8002CCA8u, x0, y0, x1, y1);
    V0 = call_ra(0x8002D780u, 0x8002CB98u, R[4], R[5], R[6], R[7]);
}

/* 8002F938 (list, out): an HMD pointer list walker: a new list (from its
 * header 8 bytes back: count masked into 0x80065C20, the next pointer in
 * 0x80065C28), or with 0 the next entry: out = { the word, the pointer }
 * (0 when done). */
void n_gs_HmdNext(void) {
    const u32 l = A0, out = A1;
    if (l != 0) {
        const u32 h = MEM_R32(MEM_R32(l - 8) + 8);
        GV(u32, 0x80065C24u) = h;
        const u32 n = MEM_R32(h) & 0x7FFFFFFFu;
        GV(u32, 0x80065C20u) = n;
        MEM_W32(h, n);
        GV(u32, 0x80065C28u) = GV(u32, 0x80065C24u) + 4;
        V0 = l;
        return;
    }
    const u32 n = GV(u32, 0x80065C20u);
    if (n == 0) { V0 = 0; return; }
    const u32 p = GV(u32, 0x80065C28u);
    MEM_W32(out + 4, p);
    V0 = MEM_R32(out + 4);
    const u32 w = MEM_R32(p);
    GV(u32, 0x80065C28u) = p + 4;
    GV(u32, 0x80065C20u) = n - 1;
    MEM_W32(out, w);
}

/* 8002FCA8 (ctx): an HMD image block with CLUTs: each 24-byte pair's
 * image (data at +0x14 base) and CLUT (at +0x18 base) loaded. */
void n_gs_HmdLoadImagesClut(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 sv[3] = { R[16], R[17], R[18] };
    const u32 c = A0;
    R[16] = c;
    const u32 p = MEM_R32(c);
    const u32 n = MEM_R16(p + 2);
    R[18] = n;
    MEM_W32(c, p + 4);
    if (n != 0) {
        for (s32 i = 0;;) {
            R[17] = (u32)i;
            u32 e = MEM_R32(c);
            call_ra(0x8002FCF4u, 0x80035620u, e, MEM_R32(c + 0x14) + (MEM_R32(e + 8) << 2), R[6], R[7]);
            e = MEM_R32(c) + 0xC;
            MEM_W32(c, e);
            call_ra(0x8002FD18u, 0x80035620u, e, MEM_R32(c + 0x18) + (MEM_R32(e + 8) << 2), R[6], R[7]);
            MEM_W32(c, MEM_R32(c) + 0xC);
            i++;
            if (!(i < (s32)n)) break;
            LOOP_POLL(0x8002FD2Cu);
        }
    }
    V0 = MEM_R32(c);
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 80037A20 (mode): the GPU's version check (GP1 0x10000007): type 2
 * (the newer GPU: mode bit 3 sets GP1 0x09000001; returns 2, else 1);
 * otherwise a draw mode written to test it, returns 0. */
void n_libgpu_GpuType(void) {
    MEM_W32(GV(u32, 0x80061A54u), 0x10000007u);
    const u32 gp0 = GV(u32, 0x80061A50u);
    const u32 v = MEM_R32(gp0) & 0xFFFFFF;
    if (v == 2) {
        if (A0 & 8) { MEM_W32(GV(u32, 0x80061A54u), 0x09000001u); V0 = 2; }
        else V0 = 1;
        return;
    }
    MEM_W32(gp0, (MEM_R32(GV(u32, 0x80061A54u)) & 0x3FFF) | 0xE1001000u);
    R[3] = MEM_R32(GV(u32, 0x80061A50u));
    V0 = 0;
}

/* 80038088 (env, x, y, w, [h]): SetDefDrawEnv: the clip rect, offset
 * (x, y), texture window cleared, dither on, drawing to the display area
 * for h up to 256 (288 on PAL), no clearing, the background 10. */
void n_libgpu_SetDefDrawEnv(void) {
    FRAME(0x28);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x24, R[31]);
    const u32 e = A0, x = A1, y = A2, w = A3, h = MEM_R32(sp + 0x38);
    R[18] = h; R[17] = e; R[19] = x; R[20] = y; R[16] = w;
    const u32 vm = call_ra(0x800380BCu, 0x8002629Cu, R[4], R[5], R[6], R[7]);
    MEM_W16(e, (u16)x);
    MEM_W16(e + 2, (u16)y);
    MEM_W16(e + 4, (u16)w);
    MEM_W16(e + 0xC, 0);
    MEM_W16(e + 0xE, 0);
    MEM_W16(e + 0x10, 0);
    MEM_W16(e + 0x12, 0);
    MEM_W8(e + 0x19, 0);
    MEM_W8(e + 0x1A, 0);
    MEM_W8(e + 0x1B, 0);
    MEM_W8(e + 0x16, 1);
    MEM_W16(e + 6, (u16)h);
    MEM_W8(e + 0x17, (u8)(vm ? (s32)h < 289 : (s32)h < 257));
    MEM_W16(e + 8, (u16)x);
    MEM_W16(e + 0xA, (u16)y);
    MEM_W16(e + 0x14, 0xA);
    MEM_W8(e + 0x18, 0);
    V0 = e;
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 800467CC (out, a, b): the midpoint of two 0xC-byte G4 vertices
 * (position halves rounded toward zero, colour bytes halved, the code
 * byte copied from a). */
void n_gs_MidpointG(void) {
    const u32 o = A0, a = A1, b = A2;
    for (u32 k = 0; k < 6; k += 2) {
        s32 s = (s32)(s16)MEM_R16(a + k) + (s16)MEM_R16(b + k);
        MEM_W16(o + k, (u16)((s + (s32)((u32)s >> 31)) >> 1));
    }
    for (u32 k = 8; k < 0xB; k++) MEM_W8(o + k, (u8)((MEM_R8(a + k) + MEM_R8(b + k)) >> 1));
    V0 = MEM_R8(a + 0xB);
    MEM_W8(o + 0xB, (u8)V0);
}

/* ---- batch: 47-51 instruction functions ---- */

/* 800189D8: the SPU DMA finish (the transfer callback): the delay unless
 * 0x800597A4, the SPU's transfer mode cleared (SPUCNT bits 4-5, waiting
 * up to 0xF00 polls for them to clear); then the user's callback
 * (0x8005978C) or the DMA event delivered (B0 07 0xF0000009, 0x20). */
void n_libspu_TransferDone(void) {
    WRAP();
    if (GV(u32, 0x800597A4u) == 0) call_ra(0x800189F0u, 0x80019070u, R[4], R[5], R[6], R[7]);
    const u32 spu = GV(u32, 0x80059754u);
    MEM_W16(spu + 0x1AA, MEM_R16(spu + 0x1AA) & 0xFFCF);
    if (MEM_R16(spu + 0x1AA) & 0x30) {
        for (u32 c = 1;;) {
            if (!(c < 0xF01)) break;
            if (!(MEM_R16(spu + 0x1AA) & 0x30)) break;
            c++;
            LOOP_POLL(0x80018A40u);
        }
    }
    if (GV(u32, 0x8005978Cu) != 0) V0 = call_ra(0x80018A70u, GV(u32, 0x8005978Cu), 0xF0000000u, R[5], R[6], R[7]);
    else V0 = call_ra(0x80018A84u, 0x80039938u, 0xF0000009u, 0x20, R[6], R[7]);
}

/* 80020248 (on): SpuSetReverb: off, or on when the reverb area isn't
 * allocated (0x800597CC already on, or 8001AC58 says it's free); the
 * state in 0x800597C8, SPUCNT bit 7. Returns the state. */
void n_libspu_SetReverb(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const u32 on = A0;
    R[16] = on;
    const u32 spu_l = 0x80059754u;
    if (on == 0) {
        const u32 s = GV(u32, spu_l);
        u32 v = MEM_R16(s + 0x1AA);
        GV(u32, 0x800597C8u) = 0;
        MEM_W16(s + 0x1AA, (u16)(v & 0xFF7F));
    } else if (on == 1) {
        int ok = GV(u32, 0x800597CCu) == on
              || call_ra(0x800202B4u, 0x8001AC58u, GV(u32, 0x800597D0u), R[5], R[6], R[7]) == 0;
        const u32 s = GV(u32, spu_l);
        u32 v = MEM_R16(s + 0x1AA);
        if (ok) { GV(u32, 0x800597C8u) = on; MEM_W16(s + 0x1AA, (u16)(v | 0x80)); }
        else { GV(u32, 0x800597C8u) = 0; MEM_W16(s + 0x1AA, (u16)(v & 0xFF7F)); }
    }
    V0 = GV(u32, 0x800597C8u);
    R[16] = s16v;
}

/* 80024C58 (data, id): SsVabTransBody: an opened VAB's (state 2) sample
 * data written to its SPU memory by hand (transfer mode 0, the start
 * address, SpuWrite of its size); state 1. Else the lock dropped, -1. */
void n_libsnd_VabTransBody(void) {
    FRAME(0x28);
    u32 sv[4];
    for (int i = 0; i < 4; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x20, R[31]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x10, sv[0]);
    const u32 data = A0;
    R[19] = data;
    u32 ret = 0xFFFFFFFFu;
    int ok = 0;
    if ((A1 & 0xFFFF) < 0x11) {
        const s32 id = (s16)A1;
        R[17] = (u32)id; R[18] = (u32)id << 2;
        if (GV(u8, 0x80065760u + (u32)id) == 2) {
            const u32 a = GV(u32, 0x800657C0u + ((u32)id << 2));
            R[16] = a;
            call_ra(0x80024CB4u, 0x80024D18u, 0, R[5], R[6], R[7]);
            if (call_ra(0x80024CBCu, 0x80019358u, a, R[5], R[6], R[7]) != 0) {
                call_ra(0x80024CD8u, 0x800192F8u, data, GV(u32, 0x80065778u + ((u32)id << 2)), R[6], R[7]);
                GV(u8, 0x80065760u + (u32)id) = 1;
                ret = (u32)id;
                ok = 1;
            }
        }
    }
    if (!ok) call_ra(0x80024CF8u, 0x80021848u, 0, R[5], R[6], R[7]);
    V0 = ret;
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* 8001B3F8 (sep, seq, vol): MIDI volume (7): the playing voices of the
 * channel's program (ToneSetVol, the channel's pan on the stack), the
 * channel's volume (+0x60), the next delta time. */
void n_libsnd_ChannelVolume(void) {
    FRAME(0x30);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x2C, R[31]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1, v = A2;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    const u32 ch = MEM_R8(rec + 0x17);
    R[20] = a; R[19] = b; R[16] = v; R[17] = rec; R[18] = ch;
    MEM_W32(sp + 0x10, MEM_R8(rec + ch + 0x27));
    call_ra(0x8001B484u, 0x800239D8u, (u32)(s32)(s16)(A0 | (A1 << 8)), (u32)(s32)(s8)MEM_R8(rec + 0x26),
            MEM_R8(rec + ch + 0x37), v & 0xFF);
    MEM_W16(rec + (ch << 1) + 0x60, (u16)(v & 0xFF));
    MEM_W32(rec + 0x90, call_ra(0x8001B4A0u, 0x8001A4A8u, a, b, R[6], R[7]));
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 8001B4C8 (sep, seq, pan): MIDI pan (10): the playing voices (ToneSetVol
 * with the channel's volume, this pan), the channel's pan (+0x27), the
 * next delta time. */
void n_libsnd_ChannelPan(void) {
    FRAME(0x30);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x2C, R[31]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x18, sv[0]);
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1, p = A2;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    const u32 ch = MEM_R8(rec + 0x17);
    R[18] = a; R[17] = b; R[20] = p; R[16] = rec; R[19] = rec + ch;
    MEM_W32(sp + 0x10, p & 0xFF);
    call_ra(0x8001B560u, 0x800239D8u, (u32)(s32)(s16)(A0 | (A1 << 8)), (u32)(s32)(s8)MEM_R8(rec + 0x26),
            MEM_R8(rec + ch + 0x37), MEM_R16(rec + (ch << 1) + 0x60));
    MEM_W8(rec + ch + 0x27, (u8)p);
    MEM_W32(rec + 0x90, call_ra(0x8001B570u, 0x8001A4A8u, a, b, R[6], R[7]));
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 8003AB98: the pad library's start: stopped (0x80062AAC 0), the pad
 * BIOS buffers (C0 03 / C0 02 with 0x8006BD50) installed, the pad timer
 * set (*0x80062AD4: -2, bit 0), C0 0A (3, 0), both ports reset through
 * 0x80062A78, the exchange state (0x8006BD60/64) cleared, started. */
void n_libpad_StartExchange(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    GV(u32, 0x80062AACu) = 0;
    call_ra(0x8003ABB0u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    R[16] = 0x8006BD50u;
    call_ra(0x8003ABC4u, 0x8003A268u, 2, 0x8006BD50u, R[6], R[7]);
    call_ra(0x8003ABD0u, 0x8003A258u, 2, 0x8006BD50u, R[6], R[7]);
    const u32 t = GV(u32, 0x80062AD4u);
    MEM_W32(t, 0xFFFFFFFEu);
    MEM_W32(t + 4, MEM_R32(t + 4) | 1);
    call_ra(0x8003ABF8u, 0x80039AD8u, 3, 0, R[6], R[7]);
    call_ra(0x8003AC00u, 0x80039A18u, R[4], R[5], R[6], R[7]);
    call_ra(0x8003AC1Cu, GV(u32, 0x80062A78u), GV(u32, 0x80062AA8u), R[5], R[6], R[7]);
    call_ra(0x8003AC38u, GV(u32, 0x80062A78u), GV(u32, 0x80062AA8u) + 0xF0, R[5], R[6], R[7]);
    GV(u32, 0x8006BD64u) = 0;
    GV(u32, 0x8006BD60u) = 0;
    GV(u32, 0x80062AACu) = 1;
    V0 = 1;
    R[16] = s16v;
}

/* ---- batch: 57-60 instruction functions ---- */

/* 80038E88 (dst): unpack the MDEC's table (0x80061C1C): bytes below 0xF0
 * copy n + 1 bytes, literally or (after a distance code 0xF1-0xFE xx)
 * from that far back; 0xF0 back to literals; the distance 0xF00 ends.
 * Then each halfword from +8 on XORed with the one 8 bytes before. */
void n_libpress_UnpackTable(void) {
    u32 d = A0, s = 0x80061C1Cu, dist = 0;
    for (;;) {
        u32 c = MEM_R8(s);
        s++;
        if (c < 0xF0) {
            if (dist != 0) {
                for (s32 n = (s32)c;;) {
                    MEM_W8(d, MEM_R8(d - dist));
                    n--;
                    d++;
                    if (!(n >= 0)) break;
                    LOOP_POLL(0x80038EE0u);
                }
            } else {
                for (s32 n = (s32)c;;) {
                    MEM_W8(d, MEM_R8(s));
                    s++;
                    n--;
                    d++;
                    if (!(n >= 0)) break;
                    LOOP_POLL(0x80038F08u);
                }
            }
        } else {
            dist = 0;
            if (c != 0xF0) {
                const u32 b = MEM_R8(s);
                s++;
                dist = ((c << 8) | b) + 0xFFFF0F01u;
            }
        }
        if (dist == 0xF00) break;
        LOOP_POLL(0x80038F34u);
    }
    u32 p = A0 + 8;
    for (s32 k = 4;;) {
        MEM_W16(p, (u16)(MEM_R16(p) ^ MEM_R16(p - 8)));
        k++;
        p += 2;
        if (0x87FF < k) break;
        LOOP_POLL(0x80038F5Cu);
    }
    V0 = 1;
}

/* 800190D8 (mode): SpuInit's body: callbacks reset, the SPU reset
 * (80018598), mode 0 also the 24 voices' ADSR shadows (0x800597EE-
 * 0x8005981E) set to 0xC000; SpuStart; the reverb state cleared, its
 * work area start (0x800597D0 from mode 0's 0x80059844) written
 * (80018EA4 0xD1); the allocation table, transfer and key state
 * cleared. */
void n_libspu_InitBody(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const u32 mode = A0;
    R[16] = mode;
    call_ra(0x800190ECu, 0x80025808u, R[4], R[5], R[6], R[7]);
    call_ra(0x800190F4u, 0x80018598u, mode, R[5], R[6], R[7]);
    if (mode == 0) {
        u32 p = 0x8005981Eu;
        for (s32 k = 0x17;;) {
            MEM_W16(p, 0xC000);
            k--;
            if (!(k >= 0)) break;
            p -= 2;
            LOOP_POLL(0x80019110u);
        }
    }
    call_ra(0x8001911Cu, 0x800191C0u, R[4], R[5], R[6], R[7]);
    const u32 rv = GV(u32, 0x80059844u);
    GV(u32, 0x800597C8u) = 0;
    GV(u32, 0x800597CCu) = 0;
    GV(u32, 0x800597D8u) = 0;
    GV(u16, 0x800597DCu) = 0;
    GV(u16, 0x800597DEu) = 0;
    GV(u32, 0x800597E0u) = 0;
    GV(u32, 0x800597E4u) = 0;
    GV(u32, 0x800597D0u) = rv;
    call_ra(0x80019168u, 0x80018EA4u, 0xD1, rv, 0, R[7]);
    GV(u32, 0x80059834u) = 0;
    GV(u32, 0x80059838u) = 0;
    GV(u32, 0x8005983Cu) = 0;
    GV(u32, 0x800597C4u) = 0;
    GV(u32, 0x80059770u) = 0;
    GV(u32, 0x800597C0u) = 0;
    GV(u32, 0x800597ECu) = 0;
    GV(u32, 0x800597E8u) = 0;
    GV(u32, 0x80059820u) = 0;
    R[16] = s16v;
}

/* 80027A5C: the CD audio's SPU side: master volume full unless the
 * external volume is set, CD volume full, SPUCNT 0xC001 (on, unmuted,
 * CD audio); the CD's own mix 0x80 left / right (CdMix-style writes;
 * the values staged on the stack). */
void n_libcd_SpuCdOn(void) {
    FRAME(8);
    u32 s = GV(u32, 0x8005B6CCu);
    if (MEM_R16(s + 0x1B8) == 0 && MEM_R16(s + 0x1BA) == 0) {
        MEM_W16(s + 0x180, 0x3FFF);
        MEM_W16(s + 0x182, 0x3FFF);
        s = GV(u32, 0x8005B6CCu);
    }
    MEM_W16(s + 0x1B0, 0x3FFF);
    MEM_W16(s + 0x1B2, 0x3FFF);
    MEM_W16(s + 0x1AA, 0xC001);
    MEM_W8(sp + 2, 0x80);
    MEM_W8(sp, 0x80);
    MEM_W8(sp + 3, 0);
    MEM_W8(sp + 1, 0);
    MEM_W8(GV(u32, 0x8005B6B8u), 2);
    MEM_W8(GV(u32, 0x8005B6C0u), MEM_R8(sp));
    MEM_W8(GV(u32, 0x8005B6C4u), MEM_R8(sp + 1));
    MEM_W8(GV(u32, 0x8005B6B8u), 3);
    MEM_W8(GV(u32, 0x8005B6BCu), MEM_R8(sp + 2));
    MEM_W8(GV(u32, 0x8005B6C0u), MEM_R8(sp + 3));
    MEM_W8(GV(u32, 0x8005B6C4u), 0x20);
    V0 = 0;
}

/* 8003873C (mode): the MDEC reset: control 0x80000000, the MDEC DMA
 * channels (in / out CHCR) stopped, then 0x60000000; mode 0 also loads
 * the default quantisation (8003882C 0x80061A9C) and scale (0x80061B20)
 * tables; other modes than 0 / 1 print a message. */
void n_libpress_MdecReset(void) {
    WRAP();
    const u32 m = A0;
    if (m == 0) {
        MEM_W32(GV(u32, 0x80061BE0u), 0x80000000u);
        MEM_W32(GV(u32, 0x80061BB4u), 0);
        MEM_W32(GV(u32, 0x80061BC0u), 0);
        MEM_W32(GV(u32, 0x80061BE0u), 0x60000000u);
        call_ra(0x800387A8u, 0x8003882Cu, 0x80061A9Cu, 0x20, R[6], R[7]);
        V0 = call_ra(0x800387B8u, 0x8003882Cu, 0x80061B20u, 0x20, R[6], R[7]);
    } else if (m == 1) {
        MEM_W32(GV(u32, 0x80061BE0u), 0x80000000u);
        MEM_W32(GV(u32, 0x80061BB4u), 0);
        MEM_W32(GV(u32, 0x80061BC0u), 0);
        (void)MEM_R32(GV(u32, 0x80061BC0u));
        MEM_W32(GV(u32, 0x80061BE0u), 0x60000000u);
        V0 = 0x60000000u;
    } else V0 = call_ra(0x8003881Cu, 0x800398C8u, 0x80010A90u, m, R[6], R[7]);
}

/* ---- batch: 64-70 instruction functions ---- */

/* 8001B7C8 (sep, seq, value): MIDI data entry (6): for NRPN 40 the
 * sequence's handler (0x800646F8 by sep and seq) with the value; NRPN
 * 20 / 30 / 40 then just the delta time; anything else keeps the value
 * (+0x1A) and counts it (+0x1F). The next delta time into +0x90. */
void n_libsnd_DataEntry(void) {
    FRAME(0x28);
    u32 sv[4];
    for (int i = 0; i < 4; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x20, R[31]);
    MEM_W32(sp + 0x10, sv[0]);
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1, val = A2;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    R[18] = A0; R[19] = A1; R[16] = rec; R[17] = val;
    u32 nr = MEM_R8(rec + 0x1B);
    if (nr == 0x28) {
        const u32 fn = GV(u32, 0x800646F8u + (a << 6) + (b << 2));
        if (fn) call_ra(0x8001B860u, fn, a, b, val & 0xFF, R[7]);
        nr = MEM_R8(rec + 0x1B);
    }
    if (nr != 0x1E && nr != 0x14 && nr != 0x28) {
        const u32 c = MEM_R8(rec + 0x1F);
        MEM_W8(rec + 0x1A, (u8)val);
        MEM_W8(rec + 0x1C, 0);
        MEM_W8(rec + 0x1F, (u8)(c + 1));
    }
    MEM_W32(rec + 0x90, call_ra(0x8001B8A8u, 0x8001A4A8u, a, b, R[6], R[7]));
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* 8001D238 (id, seq, mode, count): start a sequence: the read pointer
 * back to the start (+4), the pause and stop bits (+0x98 9, 2) cleared,
 * the play count (+0x20); mode 1 plays (+0x98 bit 0, +0x14, volumes
 * applied with SeqSetVol), mode 0 pauses (bit 1). */
void n_libsnd_SeqPlay(void) {
    WRAP();
    const u32 tp = 0x80064678u + (u32)((s32)(A0 << 16) >> 14);
    const u32 off = (u32)(s32)(s16)A1 * 176;
    const u32 rec = MEM_R32(tp) + off;
    const u32 st = MEM_R32(rec + 4);
    MEM_W32(rec, st);
    MEM_W32(rec + 8, st);
    MEM_W32(rec + 0xC, st);
    u32 r = MEM_R32(tp) + off;
    MEM_W32(r + 0x98, MEM_R32(r + 0x98) & 0xFFFFFDFFu);
    r = MEM_R32(tp) + off;
    MEM_W32(r + 0x98, MEM_R32(r + 0x98) & 0xFFFFFFFBu);
    const s32 m = (s8)A2;
    MEM_W8(rec + 0x20, (u8)A3);
    if (m == 1) {
        r = MEM_R32(tp) + off;
        MEM_W32(r + 0x98, MEM_R32(r + 0x98) | 1);
        const u32 l = MEM_R16(rec + 0x58);
        MEM_W8(rec + 0x14, (u8)m);
        const u32 rr = MEM_R16(rec + 0x5A);
        MEM_W8(rec + 0x21, 0);
        V0 = call_ra(0x8001D314u, 0x800231F8u, (u32)(s32)(s16)(A0 | (A1 << 8)), l, rr, 1);
    } else if (m == 0) {
        r = MEM_R32(tp) + off;
        MEM_W32(r + 0x98, MEM_R32(r + 0x98) | 2);
    }
}

/* 8001D898 (which, l, r): the CD (0) or external (1) input volume
 * (capped at 127, * 258) through SpuSetCommonAttr (the attribute
 * staged on the stack). */
void n_libsnd_SetInputVol(void) {
    FRAME(0x40);
    MEM_W32(sp + 0x38, R[31]);
    u32 l = A1, r = A2;
    const u32 w = A0;
    if ((w << 24) == 0) {
        MEM_W32(sp + 0x10, 0xC0);
        if (!((s32)(s16)l < 0x80)) l = 0x7F;
        if (!((s32)(s16)r < 0x80)) r = 0x7F;
        MEM_W16(sp + 0x20, (u16)((s16)l * 258));
        MEM_W16(sp + 0x22, (u16)((s16)r * 258));
    }
    if ((s8)w == 1) {
        MEM_W32(sp + 0x10, 0xC00);
        if (!((s32)(s16)l < 0x80)) l = 0x7F;
        if (!((s32)(s16)r < 0x80)) r = 0x7F;
        MEM_W16(sp + 0x2C, (u16)((s16)l * 258));
        MEM_W16(sp + 0x2E, (u16)((s16)r * 258));
    }
    V0 = call_ra(0x8001D98Cu, 0x80019468u, sp + 0x10, R[5], R[6], R[7]);
}

/* ---- batch: 79-96 instruction functions ---- */

/* 800265E4 (com, param, result): CdControl: up to four tries of the
 * command (800274F4), the ready callback (0x8005B3F4) held off while it
 * goes out; a pending "seeking" state (0x8005B400 bit 4) first gets a
 * Nop (1) unless the command is one; commands with parameters
 * (0x8005B36C) first send them (2). Returns 1, or 0 after four failures. */
void n_libcd_CdControl(void) {
    FRAME(0x38);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    const u32 sfp = R[30];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x28, sv[6]);
    MEM_W32(sp + 0x2C, sv[7]);
    MEM_W32(sp + 0x30, sfp);
    MEM_W32(sp + 0x34, R[31]);
    const u32 com = A0, param = A1, res = A2;
    const u32 c = com & 0xFF;
    const u32 cb = GV(u32, 0x8005B3F4u);
    const u32 np = 0x8005B36Cu + (c << 2);
    R[20] = com; R[17] = param; R[18] = res; R[19] = c; R[21] = cb; R[22] = np; R[23] = 0; R[30] = 0xFFFFFFFFu;
    u32 ret = 0;
    for (u32 t = 3;;) {
        R[16] = t;
        GV(u32, 0x8005B3F4u) = 0;
        if (c != 1 && (GV(u8, 0x8005B400u) & 0x10))
            call_ra(0x80026680u, 0x800274F4u, 1, 0, 0, 0);
        int retry = 0;
        if (param != 0 && MEM_R32(np) != 0) {
            if (call_ra(0x800266A8u, 0x800274F4u, 2, param, res, 0) != 0) retry = 1;
        }
        if (!retry) {
            GV(u32, 0x8005B3F4u) = cb;
            if (call_ra(0x800266CCu, 0x800274F4u, com & 0xFF, param, res, 0) == 0) { ret = 1; break; }
        }
        t--;
        if (t == 0xFFFFFFFFu) { GV(u32, 0x8005B3F4u) = cb; ret = 0; break; }
        LOOP_POLL(0x800266D8u);
    }
    V0 = ret;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8001A6B8 (id): SsSeqClose: the sequence silenced (SeqSetVol 0, its
 * voices keyed off), its slot freed (0x80064670), its sequence records
 * (count 0x80064EFA, 0xB0 bytes each) reset. */
void n_libsnd_SsSeqClose(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 s16v = R[16];
    const s32 id = (s16)A0;
    R[16] = (u32)id;
    call_ra(0x8001A6E0u, 0x800231F8u, (u32)id, 0, 0, 1);
    call_ra(0x8001A6E8u, 0x80023858u, (u32)id, R[5], R[6], R[7]);
    GV(u32, 0x80064670u) &= ~(1u << (id & 31));
    if (GV(s16, 0x80064EFAu) > 0) {
        const u32 tp = 0x80064678u + ((u32)id << 2);
        for (s32 i = 0;;) {
            const u32 r = MEM_R32(tp) + (u32)i * 0xB0;
            MEM_W32(r + 0x98, 0);
            MEM_W8(r + 0x22, 0xFF);
            MEM_W8(r + 0x23, 0);
            MEM_W16(r + 0x48, 0);
            MEM_W16(r + 0x4A, 0);
            MEM_W32(r + 0x9C, 0);
            MEM_W32(r + 0xA0, 0);
            MEM_W16(r + 0x4C, 0);
            MEM_W32(r + 0xAC, 0);
            MEM_W32(r + 0xA8, 0);
            MEM_W32(r + 0xA4, 0);
            MEM_W16(r + 0x4E, 0);
            MEM_W16(r + 0x58, 0x7F);
            i++;
            MEM_W16(r + 0x5A, 0x7F);
            if (!(i < (s32)GV(s16, 0x80064EFAu))) break;
            LOOP_POLL(0x8001A81Cu);
        }
    }
    R[16] = s16v;
}

/* 8001D6B8 (id, seq): SsSeqStop: playing / paused / 8 / 0x400 bits
 * cleared and stopped (4) set, its voices keyed off, 0x800656D8 cleared;
 * the sequence back to its start (the first delta time, tempo, ticks per
 * frame), its controller state cleared, channels to program = channel,
 * pan 64, volume 127, the volume 127 / 127. */
void n_libsnd_SsSeqStop(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    const u32 tp = 0x80064678u + (u32)((s32)(A0 << 16) >> 14);
    const u32 off = (u32)(s32)(s16)A1 * 176;
    const u32 rec = MEM_R32(tp) + off;
    R[16] = rec;
    static const u32 clr[4] = { 0xFFFFFFFEu, 0xFFFFFFFDu, 0xFFFFFFF7u, 0xFFFFFBFFu };
    for (int k = 0; k < 4; k++) {
        const u32 r = MEM_R32(tp) + off;
        MEM_W32(r + 0x98, MEM_R32(r + 0x98) & clr[k]);
    }
    {
        const u32 r = MEM_R32(tp) + off;
        MEM_W32(r + 0x98, MEM_R32(r + 0x98) | 4);
    }
    call_ra(0x8001D780u, 0x80023858u, (u32)(s32)(s16)(A0 | (A1 << 8)), R[5], R[6], R[7]);
    call_ra(0x8001D788u, 0x80020BD8u, R[4], R[5], R[6], R[7]);
    const u32 d = MEM_R32(rec + 0x84), tempo = MEM_R32(rec + 0x8C);
    const u32 tpf = MEM_R16(rec + 0x56), start = MEM_R32(rec + 4);
    MEM_W8(rec + 0x14, 0);
    MEM_W32(rec + 0x88, 0);
    static const u8 z[] = { 0x1C, 0x18, 0x19, 0x1E, 0x1A, 0x1B, 0x1F, 0x17, 0x21, 0x1C, 0x1D, 0x15, 0x16 };
    for (unsigned k = 0; k < sizeof z; k++) MEM_W8(rec + z[k], 0);
    MEM_W32(rec + 0x90, d);
    MEM_W32(rec + 0x94, tempo);
    MEM_W16(rec + 0x54, (u16)tpf);
    MEM_W32(rec, start);
    MEM_W32(rec + 8, start);
    for (s32 k = 0;;) {
        MEM_W8(rec + (u32)k + 0x37, (u8)k);
        MEM_W8(rec + (u32)k + 0x27, 0x40);
        MEM_W16(rec + 0x60 + (u32)k * 2, 0x7F);
        k++;
        if (!(k < 0x10)) break;
        LOOP_POLL(0x8001D814u);
    }
    MEM_W16(rec + 0x5C, 0x7F);
    MEM_W16(rec + 0x5E, 0x7F);
    R[16] = s16v;
}

/* ---- batch: SPU memory transfers ---- */

/* 8001AA38 (mode): SpuClearReverbWorkArea: unless the area (mode 0: the
 * last 16 units; else from the mode's start 0x80059844 to the end) is
 * allocated, it is zeroed from the zero buffer 0x800598A4 in 0x400-byte
 * DMA writes, each waited for (the DMA event, B0 0A), with the transfer
 * mode forced to DMA and the user's callback held off meanwhile.
 * Returns 0, or -1. */
void n_libspu_ClearReverbWorkArea(void) {
    FRAME(0x38);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x2C, sv[5]);
    MEM_W32(sp + 0x30, R[31]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    MEM_W32(sp + 0x10, 0);
    const u32 mode = A0;
    R[16] = mode; R[21] = 0;
    u32 ret = 0xFFFFFFFFu;
    if (mode < 0xA) {
        const u32 slot = 0x80059844u + (mode << 2);
        R[17] = slot;
        if (call_ra(0x8001AA88u, 0x8001AC58u, MEM_R32(slot), R[5], R[6], R[7]) == 0) {
            const u32 sh = GV(u32, 0x8005977Cu) & 31;
            u32 size, addr;
            if (mode == 0) { size = 0x10u << sh; addr = 0xFFF0u << sh; }
            else { const u32 a = MEM_R32(slot); size = (0x10000u - a) << sh; addr = a << sh; }
            const u32 oldmode = GV(u32, 0x80059770u);
            int forced = 0;
            if (oldmode == 1) { GV(u32, 0x80059770u) = 0; forced = 1; }
            R[20] = oldmode; R[21] = (u32)forced;
            if (GV(u32, 0x8005978Cu) != 0) {
                MEM_W32(sp + 0x10, GV(u32, 0x8005978Cu));
                GV(u32, 0x8005978Cu) = 0;
            }
            u32 more = 1;
            for (;;) {
                u32 n = 0x400;
                if (size < 0x401) { n = size; more = 0; }
                R[16] = n; R[17] = size; R[18] = addr; R[19] = more;
                call_ra(0x8001AB40u, 0x80018B3Cu, 2, addr, R[6], R[7]);
                call_ra(0x8001AB48u, 0x80018B3Cu, 1, addr, R[6], R[7]);
                call_ra(0x8001AB5Cu, 0x80018B3Cu, 3, 0x800598A4u, n, R[7]);
                size -= 0x400;
                addr += 0x400;
                call_ra(0x8001AB70u, 0x80039968u, GV(u32, 0x800597BCu), R[5], R[6], R[7]);
                if (!more) break;
                LOOP_POLL(0x8001AB70u);
            }
            if (forced) GV(u32, 0x80059770u) = oldmode;
            if (MEM_R32(sp + 0x10) != 0) GV(u32, 0x8005978Cu) = MEM_R32(sp + 0x10);
            ret = 0;
        }
    }
    V0 = ret;
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 80018818 (data, size): the SPU write by hand: from the transfer
 * address (0x8005976C), 64 bytes at a time into the FIFO (+0x1A8), each
 * block started (SPUCNT mode 1) and waited for (busy bit 0x400 of
 * SPUSTAT, up to 0xF00 polls, else a message); the mode cleared and the
 * status's mode bits waited back to what they were. */
void n_libspu_WriteByHand(void) {
    FRAME(0x28);
    u32 sv[4];
    for (int i = 0; i < 4; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x20, R[31]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x10, sv[0]);
#define SPUB GV(u32, 0x80059754u)
    u32 size = A1, src = A0;
    const u32 st = MEM_R16(SPUB + 0x1AE);
    MEM_W16(SPUB + 0x1A6, GV(u16, 0x8005976Cu));
    const u32 mode0 = st & 0x7FF;
    R[17] = size; R[18] = src; R[19] = mode0;
    call_ra(0x80018858u, 0x80019070u, R[4], R[5], R[6], R[7]);
    if (size != 0) {
        for (;;) {
            u32 n = 0x40;
            if (size < 0x41) n = size;
            R[16] = n;
            if ((s32)n > 0) {
                const u32 s = SPUB;
                for (s32 k = 0;;) {
                    const u32 v = MEM_R16(src);
                    src += 2;
                    k += 2;
                    MEM_W16(s + 0x1A8, (u16)v);
                    if (!(k < (s32)n)) break;
                    LOOP_POLL(0x80018890u);
                }
            }
            R[18] = src;
            {
                const u32 s = SPUB;
                MEM_W16(s + 0x1AA, (u16)((MEM_R16(s + 0x1AA) & 0xFFCF) | 0x10));
            }
            call_ra(0x800188BCu, 0x80019070u, R[4], R[5], R[6], R[7]);
            if (MEM_R16(SPUB + 0x1AE) & 0x400) {
                for (u32 c = 1;;) {
                    if (!(c < 0xF01)) {
                        call_ra(0x80018900u, 0x800398C8u, 0x80010190u, 0x800101B0u, R[6], R[7]);
                        break;
                    }
                    if (!(MEM_R16(SPUB + 0x1AE) & 0x400)) break;
                    c++;
                    LOOP_POLL(0x80018920u);
                }
            }
            size -= n;
            R[17] = size;
            call_ra(0x80018930u, 0x80019070u, R[4], R[5], R[6], R[7]);
            call_ra(0x80018938u, 0x80019070u, R[4], R[5], R[6], R[7]);
            if (size == 0) break;
            LOOP_POLL(0x80018938u);
        }
    }
    {
        const u32 s = SPUB;
        MEM_W16(s + 0x1AA, (u16)(MEM_R16(s + 0x1AA) & 0xFFCF));
        if ((MEM_R16(s + 0x1AE) & 0x7FF) != mode0) {
            for (u32 c = 1;;) {
                if (!(c < 0xF01)) {
                    call_ra(0x80018994u, 0x800398C8u, 0x80010190u, 0x800101C4u, R[6], R[7]);
                    break;
                }
                if ((MEM_R16(SPUB + 0x1AE) & 0x7FF) == mode0) break;
                c++;
                LOOP_POLL(0x800189B4u);
            }
        }
    }
#undef SPUB
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* ---- batch: 101-120 instruction functions ---- */

/* 80033508 (hmd): map an HMD in place (once: bit 0 of +4): the
 * primitive header's pointer section (+8) and its marked words (bit 31,
 * word offsets) made absolute; the block list (+0xC count, from +0x10)
 * made absolute, each block's coordinate chain (marked by +8 bit 31)
 * linked up. */
void n_gs_HmdMap(void) {
    const u32 base = A0;
    u32 p = base + 4;
    const u32 f = MEM_R32(p);
    V0 = f | 1;
    if (f & 1) return;
    MEM_W32(p, f | 1);
    p += 4;
    u32 a = base + (MEM_R32(p) << 2);
    MEM_W32(p, a);
    const s32 nb = (s32)MEM_R32(a);
    a += 4;
    if (nb > 0) {
        for (s32 i = 0;;) {
            const s32 cnt = (s32)MEM_R32(a);
            a += 4;
            if (cnt > 0) {
                for (s32 j = 0;;) {
                    const u32 w = MEM_R32(a);
                    if ((s32)w < 0) MEM_W32(a, base + ((w & 0x7FFFFFFFu) << 2));
                    j++;
                    a += 4;
                    if (!(j < cnt)) break;
                    LOOP_POLL(0x80033590u);
                }
            }
            i++;
            if (!(i < nb)) break;
            LOOP_POLL(0x800335A0u);
        }
    }
    p += 4;
    const s32 n = (s32)MEM_R32(p);
    p += 4;
    if (n > 0) {
        for (s32 i = 0;;) {
            const u32 w = MEM_R32(p);
            if (w != 0) {
                u32 c = base + (w << 2);
                MEM_W32(p, c);
                if (MEM_R32(c) != 0xFFFFFFFFu) {
                    int done = 0;
                    for (;;) {
                        if (!(MEM_R32(c + 8) & 0x80000000u)) { done = 1; break; }
                        const u32 v0 = MEM_R32(c), v1 = MEM_R32(c + 4);
                        MEM_W32(c, base + (v0 << 2));
                        const u32 v2 = MEM_R32(c + 8);
                        MEM_W32(c + 4, base + (v1 << 2));
                        MEM_W32(c + 8, v2 & 0x7FFFFFFFu);
                        c = MEM_R32(c);
                        if (MEM_R32(c) == 0xFFFFFFFFu) break;
                        LOOP_POLL(0x8003364Cu);
                    }
                    if (done) goto next;
                }
                if (MEM_R32(c + 8) & 0x80000000u) {
                    const u32 v1 = MEM_R32(c + 4), v2 = MEM_R32(c + 8);
                    MEM_W32(c + 4, base + (v1 << 2));
                    MEM_W32(c + 8, v2 & 0x7FFFFFFFu);
                }
            }
        next:
            i++;
            if (!(i < n)) break;
            p += 4;
            LOOP_POLL(0x8003368Cu);
        }
    }
}

/* 8001CAA8 (sep, seq): the tempo meta event: 60000000 / the three-byte
 * microseconds per beat (+0x94); the ticks per frame from the tick rate
 * as in SeqOpen (+0x52, +0x54); the next delta time. */
void n_libsnd_SetTempo(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 s16v = R[16];
    const u32 a = (u32)(s32)(s16)A0, b = (u32)(s32)(s16)A1;
    const u32 rec = GV(u32, 0x80064678u + (a << 2)) + b * 176;
    R[16] = rec;
    const u32 p = MEM_R32(rec);
    const u32 b0 = MEM_R8(p);
    MEM_W32(rec, p + 1);
    const u32 b1 = MEM_R8(p + 1);
    MEM_W32(rec, p + 2);
    const u32 b2 = MEM_R8(p + 2);
    const s32 q = mips_div(60000000, (s32)(b2 | (b0 << 16) | (b1 << 8)));
    const u32 prod = (u32)(s32)(s16)MEM_R16(rec + 0x50) * (u32)q;
    const u32 vs = GV(u32, 0x80064674u);
    MEM_W32(rec, p + 3);
    MEM_W32(rec + 0x94, (u32)q);
    const u32 v15 = vs * 15, v60 = v15 * 4;
    if (prod * 10 < v60) {
        const u32 q2 = prod ? (vs * 600) / prod : 0xFFFFFFFFu;
        MEM_W16(rec + 0x52, (u16)q2);
        MEM_W16(rec + 0x54, (u16)q2);
    } else {
        const u32 nn = (u32)(s32)(s16)MEM_R16(rec + 0x50) * MEM_R32(rec + 0x94) * 10;
        const u32 q3 = v60 ? nn / v60 : 0xFFFFFFFFu, r3 = v60 ? nn % v60 : nn;
        MEM_W16(rec + 0x52, 0xFFFF);
        MEM_W16(rec + 0x54, (u16)q3);
        if (v15 * 2 < r3) MEM_W16(rec + 0x54, (u16)(q3 + 1));
    }
    MEM_W32(rec + 0x90, call_ra(0x8001CC5Cu, 0x8001A4A8u, a, b, R[6], R[7]));
    R[16] = s16v;
}

/* 80027B98: the CD drive reset: the library's banner, its state and
 * callbacks cleared, the CD interrupt (2) hooked (800280D8), pending CD
 * interrupts acknowledged, the registers' mix cleared, the CD's
 * delay/size register 0x1325, then Nop (twice if seeking), Init (0xA)
 * and Demute (0xC), and a CdSync for complete. 0, or -1. */
void n_libcd_Reset(void) {
    WRAP();
    call_ra(0x80027BB0u, 0x80039800u, 0x80010600u, R[5], R[6], R[7]);
    call_ra(0x80027BC4u, 0x800398C8u, 0x8001060Cu, 0x8005B6D4u, R[6], R[7]);
    GV(u8, 0x8005B411u) = 0;
    GV(u8, 0x8005B410u) = 0;
    GV(u32, 0x8005B3F8u) = 0;
    GV(u32, 0x8005B3F4u) = 0;
    GV(u32, 0x8005B404u) = 0;
    GV(u32, 0x8005B400u) = 0;
    call_ra(0x80027BF8u, 0x80025808u, R[4], R[5], R[6], R[7]);
    call_ra(0x80027C08u, 0x80025838u, 2, 0x800280D8u, R[6], R[7]);
    MEM_W8(GV(u32, 0x8005B6B8u), 1);
    if (MEM_R8(GV(u32, 0x8005B6C4u)) & 7) {
        for (;;) {
            MEM_W8(GV(u32, 0x8005B6B8u), 1);
            MEM_W8(GV(u32, 0x8005B6C4u), 7);
            MEM_W8(GV(u32, 0x8005B6C0u), 7);
            if (!(MEM_R8(GV(u32, 0x8005B6C4u)) & 7)) break;
            LOOP_POLL(0x80027C84u);
        }
    }
    GV(u8, 0x8005B6D2u) = 0;
    GV(u8, 0x8005B6D1u) = GV(u8, 0x8005B6D2u);
    GV(u8, 0x8005B6D0u) = 2;
    MEM_W8(GV(u32, 0x8005B6B8u), 0);
    MEM_W8(GV(u32, 0x8005B6C4u), 0);
    MEM_W32(GV(u32, 0x8005B6C8u), 0x1325);
    call_ra(0x80027CE4u, 0x800274F4u, 1, 0, 0, 0);
    if (GV(u32, 0x8005B400u) & 0x10) call_ra(0x80027D0Cu, 0x800274F4u, 1, 0, 0, 0);
    u32 ret = 0xFFFFFFFFu;
    if (call_ra(0x80027D20u, 0x800274F4u, 0xA, 0, 0, 0) == 0
        && call_ra(0x80027D3Cu, 0x800274F4u, 0xC, 0, 0, 0) == 0
        && call_ra(0x80027D4Cu, 0x80026FACu, 0, 0, R[6], R[7]) == 2) ret = 0;
    V0 = ret;
}

/* 8001D9A8 (attr table, sequences, stride): the sequence attribute
 * records: the per-sequence table (0x80064678) of 176-byte records from
 * a0, sequences a1 apart by a2 records; the sequence numbers a1..31
 * marked busy (0x80064670); every record's state and volumes reset. */
void n_libsnd_SeqAttrInit(void) {
    const u32 base = A0;
    GV(u16, 0x80064EF8u) = (u16)A1;
    GV(u16, 0x80064EFAu) = (u16)A2;
    const s32 n = (s32)(s16)A1, stride = (s32)(s16)A2;
    if (n > 0) {
        u32 t = 0x80064678u;
        s32 v = 0;
        for (s32 i = 0;;) {
            MEM_W32(t, base + (u32)v * 176);
            t += 4;
            i++;
            v += stride;
            if (!(i < n)) break;
            LOOP_POLL(0x8001DA08u);
        }
    }
    s32 k = n;
    if (k < 32) {
        for (;;) {
            GV(u32, 0x80064670u) |= 1u << (k & 31);
            k++;
            if (!(k < 32)) break;
            LOOP_POLL(0x8001DA48u);
        }
    }
    if ((s32)GV(s16, 0x80064EF8u) > 0) {
        u32 t = 0x80064678u;
        for (s32 i = 0;;) {
            if ((s32)GV(s16, 0x80064EFAu) > 0) {
                u32 o = 0;
                for (s32 j = 0;;) {
                    const u32 r = MEM_R32(t) + o;
                    MEM_W32(r + 0x98, 0);
                    MEM_W8(r + 0x22, 0xFF);
                    MEM_W8(r + 0x23, 0);
                    MEM_W16(r + 0x48, 0);
                    MEM_W16(r + 0x4A, 0);
                    MEM_W32(r + 0x9C, 0);
                    MEM_W32(r + 0xA0, 0);
                    MEM_W16(r + 0x4C, 0);
                    MEM_W32(r + 0xAC, 0);
                    MEM_W32(r + 0xA8, 0);
                    MEM_W32(r + 0xA4, 0);
                    MEM_W16(r + 0x4E, 0);
                    MEM_W16(r + 0x58, 0x7F);
                    MEM_W16(r + 0x5A, 0x7F);
                    MEM_W16(r + 0x5C, 0x7F);
                    MEM_W16(r + 0x5E, 0x7F);
                    j++;
                    o += 0xB0;
                    if (!(j < (s32)GV(s16, 0x80064EFAu))) break;
                    LOOP_POLL(0x8001DB98u);
                }
            }
            i++;
            t += 4;
            if (!(i < (s32)GV(s16, 0x80064EF8u))) break;
            LOOP_POLL(0x8001DBB0u);
        }
    }
    V0 = 0;
}

/* 80018598 (no-clear flag): the SPU set-up: DMA priority bits, transfer
 * state cleared, the SPU off and waited for (with a time-out message),
 * the address unit, mode and transfer defaults; unless a0, the volumes,
 * reverb and keys cleared, the reverb area and the 24 voices reset and
 * the key off/on registers cleared; then the SPU on (0xC000). 0. */
void n_libspu_Init(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 o16 = R[16], o17 = R[17];
    const u32 flag = A0;
    R[16] = flag;
    {
        const u32 d = GV(u32, 0x80059764u);
        MEM_W32(d, MEM_R32(d) | 0xB0000u);
    }
    u32 spu = GV(u32, 0x80059754u);
    GV(u32, 0x80059770u) = 0;
    GV(u32, 0x80059774u) = 0;
    GV(u16, 0x8005976Cu) = 0;
    MEM_W16(spu + 0x180, 0);
    MEM_W16(spu + 0x182, 0);
    MEM_W16(spu + 0x1AA, 0);
    call_ra(0x800185F4u, 0x80019070u, R[4], R[5], R[6], R[7]);
    spu = GV(u32, 0x80059754u);
    MEM_W16(spu + 0x180, 0);
    MEM_W16(spu + 0x182, 0);
    if (MEM_R16(spu + 0x1AE) & 0x7FF) {
        u32 n = 1;
        for (;;) {
            if (!(n < 0xF01)) {
                call_ra(0x80018640u, 0x800398C8u, 0x80010190u, 0x800101A0u, R[6], R[7]);
                break;
            }
            if (!(MEM_R16(GV(u32, 0x80059754u) + 0x1AE) & 0x7FF)) break;
            n++;
            LOOP_POLL(0x80018660u);
        }
    }
    GV(u32, 0x80059778u) = 2;
    GV(u32, 0x8005977Cu) = 3;
    GV(u32, 0x80059780u) = 8;
    GV(u32, 0x80059784u) = 7;
    spu = GV(u32, 0x80059754u);
    MEM_W16(spu + 0x1AC, 4);
    MEM_W16(spu + 0x184, 0);
    MEM_W16(spu + 0x186, 0);
    MEM_W16(spu + 0x18C, 0xFFFF);
    MEM_W16(spu + 0x18E, 0xFFFF);
    MEM_W16(spu + 0x198, 0);
    MEM_W16(spu + 0x19A, 0);
    for (s32 i = 0;;) {
        MEM_W16(0x800645B8u + (u32)i * 2, 0);
        i++;
        if (!(i < 10)) break;
        LOOP_POLL(0x800186DCu);
    }
    if (flag == 0) {
        spu = GV(u32, 0x80059754u);
        GV(u16, 0x8005976Cu) = 0x200;
        MEM_W16(spu + 0x190, 0);
        MEM_W16(spu + 0x192, 0);
        MEM_W16(spu + 0x194, 0);
        MEM_W16(spu + 0x196, 0);
        MEM_W16(spu + 0x1B0, 0);
        MEM_W16(spu + 0x1B2, 0);
        MEM_W16(spu + 0x1B4, 0);
        MEM_W16(spu + 0x1B6, 0);
        call_ra(0x80018730u, 0x80018818u, 0x80059794u, 0x10, R[6], R[7]);
        u32 v = GV(u32, 0x80059754u);
        for (s32 i = 0;;) {
            MEM_W16(v + 0, 0);
            MEM_W16(v + 2, 0);
            MEM_W16(v + 4, 0x3FFF);
            MEM_W16(v + 6, 0x200);
            MEM_W16(v + 8, 0);
            MEM_W16(v + 0xA, 0);
            i++;
            v += 0x10;
            if (!(i < 0x18)) break;
            LOOP_POLL(0x80018768u);
        }
        R[17] = 0xFFFF;
        R[16] = 0xFF;
        spu = GV(u32, 0x80059754u);
        MEM_W16(spu + 0x188, 0xFFFF);
        MEM_W16(spu + 0x18A, 0xFF);
        call_ra(0x8001878Cu, 0x80019070u, 0, 0x200, 0x3FFF, R[7]);
        call_ra(0x80018794u, 0x80019070u, R[4], R[5], R[6], R[7]);
        call_ra(0x8001879Cu, 0x80019070u, R[4], R[5], R[6], R[7]);
        call_ra(0x800187A4u, 0x80019070u, R[4], R[5], R[6], R[7]);
        spu = GV(u32, 0x80059754u);
        MEM_W16(spu + 0x18C, 0xFFFF);
        MEM_W16(spu + 0x18E, 0xFF);
        call_ra(0x800187BCu, 0x80019070u, R[4], R[5], R[6], R[7]);
        call_ra(0x800187C4u, 0x80019070u, R[4], R[5], R[6], R[7]);
        call_ra(0x800187CCu, 0x80019070u, R[4], R[5], R[6], R[7]);
        call_ra(0x800187D4u, 0x80019070u, R[4], R[5], R[6], R[7]);
    }
    spu = GV(u32, 0x80059754u);
    GV(u32, 0x80059788u) = 1;
    MEM_W16(spu + 0x1AA, 0xC000);
    GV(u32, 0x8005978Cu) = 0;
    GV(u32, 0x80059790u) = 0;
    R[16] = o16;
    R[17] = o17;
    V0 = 0;
}

/* 80021498 (voices): SsInit's voice set-up: the voice tables and flags
 * cleared, the voice count (a0 as a signed byte; 24 if not below 24) at
 * 0x80065734; for each voice its record (0x80064F10, 56 bytes) reset and
 * the SPU voice attributes set (mask 0x60093: volumes 0, pitch 0x1000,
 * ADSR 0x80FF / 0x4000) and the voice keyed off; then the master and
 * reverb state reset and the mode set (80020BF8). */
void n_libsnd_VoiceInit(void) {
    FRAME(0x70);
    MEM_W32(sp + 0x54, R[17]);
    MEM_W32(sp + 0x68, R[31]);
    MEM_W32(sp + 0x64, R[21]);
    MEM_W32(sp + 0x60, R[20]);
    MEM_W32(sp + 0x5C, R[19]);
    MEM_W32(sp + 0x58, R[18]);
    MEM_W32(sp + 0x50, R[16]);
    u32 save[6];
    for (int i = 0; i < 6; i++) save[i] = R[16 + i];
    const u32 nv = A0;
    R[17] = nv;
    call_ra(0x800214C4u, 0x80021848u, 0, R[5], R[6], R[7]);
    GV(u16, 0x800656D8u) = 0;
    call_ra(0x800214DCu, 0x800217E8u, 0x20, 0x80065808u, R[6], R[7]);
    for (u32 i = 0;;) {
        MEM_W16(0x80065470u + (i & 0xFFFF) * 2, 0);
        i++;
        if (!((i & 0xFFFF) < 0xC0)) break;
        LOOP_POLL(0x80021504u);
    }
    for (u32 i = 0;;) {
        MEM_W8(0x80065450u + (i & 0xFFFF), 0);
        i++;
        if (!((i & 0xFFFF) < 0x18)) break;
        LOOP_POLL(0x8002152Cu);
    }
    GV(u16, 0x800657B8u) = 0;
    for (u32 i = 0;;) {
        MEM_W8(0x80065760u + (i & 0xFFFF), 0);
        i++;
        if (!((i & 0xFFFF) < 0x10)) break;
        LOOP_POLL(0x8002155Cu);
    }
    {
        const u32 c = (u32)(s32)(s8)nv;
        GV(u8, 0x80065734u) = (u8)(c < 0x18 ? c : 0x18);
    }
    u32 s0 = 0;
    MEM_W32(sp + 0x14, 0x60093u);
    MEM_W16(sp + 0x24, 0x1000);
    MEM_W32(sp + 0x2C, 0x1000);
    MEM_W16(sp + 0x4A, 0x80FF);
    MEM_W16(sp + 0x18, 0);
    MEM_W16(sp + 0x1A, 0);
    MEM_W16(sp + 0x4C, 0x4000);
    R[16] = s0;
    if ((s32)GV(s8, 0x80065734u) > 0) {
        R[21] = 0x18; R[17] = 0xFF; R[20] = 0xFFFFFFFFu; R[19] = 0x40; R[18] = 1;
        for (;;) {
            const u32 v = s0 & 0xFFFF;
            const u32 r = 0x80064F10u + v * 56;
            MEM_W16(r + 0x02, 0x18);
            MEM_W16(r + 0x00, 0xFF);
            MEM_W8(r + 0x1D, 0);
            MEM_W16(r + 0x04, 0);
            MEM_W16(r + 0x06, 0);
            MEM_W16(r + 0x10, 0xFFFF);
            MEM_W16(r + 0x12, 0);
            MEM_W16(r + 0x14, 0);
            MEM_W16(r + 0x16, 0xFF);
            MEM_W16(r + 0x08, 0);
            MEM_W16(r + 0x0C, 0);
            MEM_W8(r + 0x0A, 0x40);
            MEM_W16(r + 0x36, 0);
            MEM_W16(r + 0x1E, 0);
            MEM_W16(r + 0x20, 0);
            MEM_W16(r + 0x22, 0);
            MEM_W16(r + 0x24, 0);
            MEM_W16(r + 0x2A, 0);
            MEM_W16(r + 0x2C, 0);
            MEM_W16(r + 0x2E, 0);
            MEM_W16(r + 0x30, 0);
            MEM_W16(r + 0x32, 0);
            MEM_W16(r + 0x26, 0);
            MEM_W32(sp + 0x10, 1u << (v & 31));
            call_ra(0x8002171Cu, 0x8001E268u, sp + 0x10, R[5], R[6], R[7]);
            GV(u16, 0x80065758u) = (u16)s0;
            call_ra(0x8002172Cu, 0x80022818u, 1, R[5], R[6], R[7]);
            s0++;
            R[16] = s0;
            if (!((s32)(s0 & 0xFFFF) < (s32)GV(s8, 0x80065734u))) break;
            LOOP_POLL(0x80021740u);
        }
    }
    {
        const u32 m = 0x80065640u;
        MEM_W32(m, 0);
        MEM_W16(m + 8, 0x3FFF);
        MEM_W16(m + 0xA, 0x3FFF);
        MEM_W32(m + 4, 0);
    }
    GV(u16, 0x80064F00u) = 0;
    GV(u16, 0x80064F02u) = 0;
    GV(u16, 0x80065638u) = 0;
    GV(u16, 0x80064F04u) = 0;
    GV(u16, 0x80064F06u) = 0;
    GV(u16, 0x80064F08u) = 0;
    GV(u16, 0x80064F0Au) = 0;
    GV(u8, 0x80065770u) = 0;
    GV(u16, 0x80065720u) = 0;
    GV(u32, 0x80065738u) = 0;
    GV(u16, 0x80065722u) = 0x80;
    const u32 ret = call_ra(0x800217C4u, 0x80020BF8u, R[4], R[5], R[6], R[7]);
    for (int i = 0; i < 6; i++) R[16 + i] = save[i];
    V0 = ret;
}

/* 80039038 (shared): the memory-card set-up: the card events and BIOS
 * card handlers (80039AA8 0, 800255F8 0), a critical section around
 * InitCARD (the shared flag cleared if the pad's ChangeClearPAD said
 * so), StartCARD, the card's handlers and buffers; the section left if
 * it was entered here. */
void n_libcard_Init(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x14, R[17]);
    const u32 o16 = R[16], o17 = R[17];
    u32 shared = A0;
    R[16] = shared;
    call_ra(0x80039054u, 0x80039AA8u, 0, R[5], R[6], R[7]);
    call_ra(0x8003905Cu, 0x800255F8u, 0, R[5], R[6], R[7]);
    const u32 crit = call_ra(0x80039064u, 0x80039A08u, R[4], R[5], R[6], R[7]);
    R[17] = crit;
    if (call_ra(0x8003906Cu, 0x80039F24u, R[4], R[5], R[6], R[7]) == 0) shared = 0;
    R[16] = shared;
    call_ra(0x80039080u, 0x80039138u, shared, R[5], R[6], R[7]);
    call_ra(0x80039088u, 0x80039348u, R[4], R[5], R[6], R[7]);
    call_ra(0x80039090u, 0x80039244u, R[4], R[5], R[6], R[7]);
    call_ra(0x80039098u, 0x800392D8u, R[4], R[5], R[6], R[7]);
    call_ra(0x800390A0u, 0x80039168u, R[4], R[5], R[6], R[7]);
    u32 ret = 1;
    if (crit == 1) ret = call_ra(0x800390B4u, 0x80039A18u, R[4], R[5], R[6], R[7]);
    R[16] = o16;
    R[17] = o17;
    V0 = ret;
}

/* 8002EC58 (driver args): the HMD shared-vertex flat-lit G3 driver: for
 * each polygon (16 bytes: colour, three vertex/normal index pairs) whose
 * three pre-projected vertices are all valid (+4 not -1) and face the
 * camera, a POLY_G3 (0x30) with each corner's colour the polygon colour
 * scaled by that vertex's light (the colour as a diagonal rotation
 * matrix times the light vector, capped at 0xFF), linked into the
 * ordering table by its average Z. The header is not advanced. */
void n_gs_DriverSharedG3Lit(void) {
    const u32 args = A0;
    const u32 hdr = MEM_R32(args);
    u32 pk = MEM_R32(args + 0x10);
    u32 n = MEM_R16(hdr + 2);
    const u32 verts = MEM_R32(args + 0x1C);
    const u32 lights = MEM_R32(args + 0x24);
    u32 poly = MEM_R32(args + 0x14) + (MEM_R32(hdr + 4) << 2);
    if (n != 0) {
        for (;;) {
            const u32 a1 = MEM_R32(poly + 4), a2 = MEM_R32(poly + 8), a3 = MEM_R32(poly + 0xC);
            const u32 p0 = verts + ((a1 >> 16) << 3);
            const u32 p1 = verts + ((a2 >> 16) << 3);
            const u32 p2 = verts + ((a3 >> 16) << 3);
            u32 v;
            if ((v = MEM_R32(p0 + 4)) == 0xFFFFFFFFu) goto next;
            gte_write_data(17, v);
            if ((v = MEM_R32(p1 + 4)) == 0xFFFFFFFFu) goto next;
            gte_write_data(18, v);
            if ((v = MEM_R32(p2 + 4)) == 0xFFFFFFFFu) goto next;
            gte_write_data(19, v);
            {
                const u32 s0 = MEM_R32(p0), s1 = MEM_R32(p1), s2 = MEM_R32(p2);
                gte_write_data(12, s0);
                gte_write_data(13, s1);
                gte_write_data(14, s2);
                gte_command(0x1400006u);
                MEM_W32(pk + 8, s0);
                MEM_W32(pk + 0x10, s1);
                MEM_W32(pk + 0x18, s2);
            }
            if ((s32)gte_read_data(24) <= 0) goto next;
            {
                const u32 c = MEM_R32(poly);
                gte_write_ctrl(0, c & 0xFF);
                gte_write_ctrl(1, 0);
                gte_write_ctrl(2, (c >> 8) & 0xFF);
                gte_write_ctrl(3, 0);
                gte_write_ctrl(4, (c >> 16) & 0xFF);
            }
            for (int k = 0;; k++) {
                const u32 idx = (k == 0 ? a1 : k == 1 ? a2 : a3) & 0xFFFF;
                const u32 lp = lights + (idx << 3);
                const u32 w0 = MEM_R32(lp), w1 = MEM_R32(lp + 4);
                gte_write_data(9, w0 & 0xFFFF);
                gte_write_data(10, w0 >> 16);
                gte_write_data(11, w1);
                gte_command(0x049E012u);
                u32 r = gte_read_data(9), g = gte_read_data(10), b = gte_read_data(11);
                if (!((s32)r < 0xFF)) r = 0xFF;
                if (!((s32)g < 0xFF)) g = 0xFF;
                if (!((s32)b < 0xFF)) b = 0xFF;
                const u32 col = r | (g << 8) | (b << 16);
                if (k == 0) {
                    MEM_W32(pk + 4, col | 0x30000000u);
                    LOOP_POLL(0x8002EE30u);
                } else if (k == 1) {
                    MEM_W32(pk + 0xC, col);
                    LOOP_POLL(0x8002EE38u);
                } else {
                    MEM_W32(pk + 0x14, col);
                    break;
                }
            }
            gte_command(0x158002Du);
            {
                const u32 ot = MEM_R32(MEM_R32(args + 4) + 4);
                const u32 zoff = MEM_R32(args + 0xC), sh = MEM_R32(args + 8);
                const u32 z = gte_read_data(7);
                const u32 tag = ot + ((((z - zoff) >> (sh & 31)) & 0xFFFF) << 2);
                const u32 t = MEM_R32(tag);
                MEM_W32(pk, 0x06000000u | (t & 0xFFFFFFu));
                MEM_W32(tag, (t & 0xFF000000u) | (pk & 0xFFFFFFu));
                pk += 0x1C;
            }
        next:
            n--;
            poly += 0x10;
            if (n == 0) break;
            LOOP_POLL(0x8002EEA8u);
        }
    }
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(args) + 8;
}
