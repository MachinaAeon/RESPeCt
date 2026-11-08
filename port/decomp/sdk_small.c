/* Small Psy-Q library functions (kernel, 0x80019020-0x8003DF84): setters,
 * getters, wrappers around one call, fills, primitive set-up.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). Calls into game code pass the original's return address
 * (callees may keep it in a stack frame the comparison sees). */
#include "poll.h"

void rt_syscall(uint32_t pc);

static inline u32 call_ra(u32 ra, u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    u32 old = R[31];
    R[31] = ra;
    u32 v = gcall(addr, a0, a1, a2, a3);
    R[31] = old;
    return v;
}

/* A wrapper's frame: 0x18 bytes, $ra at +0x10. */
#define WRAP() FRAME(0x18); MEM_W32(sp + 0x10, R[31])

/* ---- libspu ---- */

/* 80019020: the SPU DMA priority (DPCR bits 24-27 at *0x80059768) 2. */
void n_libspu_DmaPriority(void) {
    const u32 p = GV(u32, 0x80059768u);
    MEM_W32(p, (MEM_R32(p) & 0xF0FFFFFFu) | 0x20000000u);
}

/* 80019238 (fn): the SPU DMA callback (DMACallback 4). */
void n_libspu_SetTransferCallback(void) { WRAP(); V0 = call_ra(0x8001924Cu, 0x80025868u, 4, A0, R[6], R[7]); }

/* 8001A834 (mode): 8001A6B8 with mode as a short. */
void n_libspu_Wrap1A834(void) { WRAP(); V0 = call_ra(0x8001A848u, 0x8001A6B8u, (u32)(s32)(s16)A0, R[5], R[6], R[7]); }

/* 8001AA18: 800190D8 (0). */
void n_libspu_Wrap1AA18(void) { WRAP(); V0 = call_ra(0x8001AA28u, 0x800190D8u, 0, R[5], R[6], R[7]); }

/* 80021848 (on): the "SPU busy" flag 0x80059788: 0 for 1, else 1. */
void n_libspu_SetBusy(void) { GV(u32, 0x80059788u) = A0 == 1 ? 0 : 1; V0 = 1; }

/* 80021870: whether 0x80059788 isn't 1. */
void n_libspu_IsBusy(void) { V0 = (GV(u32, 0x80059788u) ^ 1) != 0; }

/* 80024D18 (mode): the transfer mode (0x800597C4) and whether it's 1
 * (0x80059770). */
void n_libspu_SetTransferMode(void) {
    GV(u32, 0x800597C4u) = A0;
    GV(u32, 0x80059770u) = A0 == 1;
    V0 = A0 == 1;
}

/* ---- libsnd ---- */

/* 8001C708 (-, -, -, a3, [byte at +0x30]): 80020108 with that byte as
 * both of its first arguments ($a3 kept in the caller's argument slot). */
void n_libsnd_Wrap1C708(void) {
    FRAME(0x18);
    const u32 b = MEM_R8(sp + 0x48);
    MEM_W32(sp + 0x10, R[31]);
    MEM_W32(sp + 0x24, A3);
    V0 = call_ra(0x8001C720u, 0x80020108u, b, b, R[6], R[7]);
}

/* 8001D5D8: TickStart (1). */
void n_libsnd_TickStart1(void) { WRAP(); V0 = call_ra(0x8001D5E8u, 0x8001D3A8u, 1, R[5], R[6], R[7]); }

/* 8001D838 (n): 8001D6B8 (n, 0). */
void n_libsnd_Wrap1D838(void) { WRAP(); V0 = call_ra(0x8001D850u, 0x8001D6B8u, (u32)(s32)(s16)A0, 0, R[6], R[7]); }

/* 8001E0F8 (mask): the reserved voices (0x80059874). */
void n_libsnd_SetReserved(void) { GV(u32, 0x80059874u) = A0; }

/* 80020358 / 80020378: 80020248 (0 / 1). */
void n_libsnd_Wrap20358(void) { WRAP(); V0 = call_ra(0x80020368u, 0x80020248u, 0, R[5], R[6], R[7]); }
void n_libsnd_Wrap20378(void) { WRAP(); V0 = call_ra(0x80020388u, 0x80020248u, 1, R[5], R[6], R[7]); }

/* 80020BD8: 0x800656D8 cleared. 80024048: 0x80065720 (mono) cleared. */
void n_libsnd_Clear656D8(void) { GV(u16, 0x800656D8u) = 0; }
void n_libsnd_SetStereo(void) { GV(u16, 0x80065720u) = 0; }

/* 80024D48 (n): 800193B8 (n), its answer as a short. */
void n_libsnd_Wrap24D48(void) {
    WRAP();
    u32 r = call_ra(0x80024D5Cu, 0x800193B8u, (u32)(s32)(s16)A0, R[5], R[6], R[7]);
    V0 = (u32)(s32)(s16)r;
}

/* 80024D78 / 80024DA4: take / release the library's busy flag
 * (0x8006466C): 1, or -1 if it already was set / clear. */
void n_libsnd_Lock(void) {
    if (GV(u32, 0x8006466Cu) == 1) V0 = 0xFFFFFFFFu;
    else { GV(u32, 0x8006466Cu) = 1; V0 = 1; }
}
void n_libsnd_Unlock(void) {
    if (GV(u32, 0x8006466Cu) == 0) V0 = 0xFFFFFFFFu;
    else { GV(u32, 0x8006466Cu) = 0; V0 = 1; }
}

/* ---- libetc ---- */

/* 80025808 / 80025838 / 80025868: the interrupt module's entries (the
 * table at *0x8005B2BC: +0xC ResetCallback, +8 VSyncCallbacks, +4
 * DMACallback), arguments passed on. */
void n_libetc_ResetCallback(void) {
    WRAP();
    V0 = call_ra(0x80025828u, MEM_R32(GV(u32, 0x8005B2BCu) + 0xC), R[4], R[5], R[6], R[7]);
}
void n_libetc_VSyncCallbacks(void) {
    WRAP();
    V0 = call_ra(0x80025858u, MEM_R32(GV(u32, 0x8005B2BCu) + 8), R[4], R[5], R[6], R[7]);
}
void n_libetc_DMACallback(void) {
    WRAP();
    V0 = call_ra(0x80025888u, MEM_R32(GV(u32, 0x8005B2BCu) + 4), R[4], R[5], R[6], R[7]);
}

/* 80025EA4 / 80025FB8 / 80026260 (p, n): n words cleared. */
static void clear_words(u32 p, u32 n, u32 pc) {
    if (n == 0) return;
    for (u32 r = n - 1;;) {
        MEM_W32(p, 0);
        r--;
        p += 4;
        if (r == 0xFFFFFFFFu) break;
        LOOP_POLL(pc);
    }
}
void n_libetc_ClearWords1(void) { clear_words(A0, A1, 0x80025EB8u); V0 = 0xFFFFFFFFu; }
void n_libetc_ClearWords2(void) { clear_words(A0, A1, 0x80025FCCu); V0 = 0xFFFFFFFFu; }
void n_libetc_ClearWords3(void) { clear_words(A0, A1, 0x80026274u); V0 = 0xFFFFFFFFu; }

/* 80025F8C (n, fn): a DMA channel's callback (0x8005B2DC); returns the
 * old one. */
void n_libetc_SetDmaCallback(void) {
    const u32 slot = 0x8005B2DCu + (A0 << 2);
    const u32 old = MEM_R32(slot);
    if (A1 != old) MEM_W32(slot, A1);
    V0 = old;
}

/* ---- libcd ---- */

/* 800262B8 (a, b): StSetRing: the ring kept (0x80065950 / 54), 80028EE8. */
void n_libcd_StSetRing(void) {
    WRAP();
    GV(u32, 0x80065950u) = A0;
    GV(u32, 0x80065954u) = A1;
    V0 = call_ra(0x800262D4u, 0x80028EE8u, A0, A1, R[6], R[7]);
}

/* 80026500 / 800265BC / 800265D0 / 80028B58 (fn): set a callback,
 * returning the old one (0x8005B3FC, 0x8005B3F4, 0x8005B3F8, 0x8005B35C). */
void n_libcd_SetCb3FC(void) { V0 = GV(u32, 0x8005B3FCu); GV(u32, 0x8005B3FCu) = A0; }
void n_libcd_SetCb3F4(void) { V0 = GV(u32, 0x8005B3F4u); GV(u32, 0x8005B3F4u) = A0; }
void n_libcd_SetCb3F8(void) { V0 = GV(u32, 0x8005B3F8u); GV(u32, 0x8005B3F8u) = A0; }
void n_libcd_SetCb35C(void) { V0 = GV(u32, 0x8005B35Cu); GV(u32, 0x8005B35Cu) = A0; }

/* 800269A0: 80027900, then 1. */
void n_libcd_Wrap269A0(void) { WRAP(); call_ra(0x800269B0u, 0x80027900u, R[4], R[5], R[6], R[7]); V0 = 1; }

/* 80026A00 (fn): the CD DMA callback (DMACallback 3). */
void n_libcd_SetDmaCallback(void) { WRAP(); V0 = call_ra(0x80026A14u, 0x80025868u, 3, A0, R[6], R[7]); }

/* 800292F8 (a, b, c): the streaming set-up values 0x80065948 / 24 / 44. */
void n_libcd_StSetValues(void) {
    GV(u32, 0x80065948u) = A0;
    GV(u32, 0x80065924u) = A1;
    GV(u32, 0x80065944u) = A2;
}

/* ---- libgte / libgs ---- */

/* 8002A608 (m, v): TransMatrix: v into m's translation; returns m. */
void n_gte_TransMatrix(void) {
    const u32 m = A0, v = A1;
    u32 x = MEM_R32(v), y = MEM_R32(v + 4), z = MEM_R32(v + 8);
    MEM_W32(m + 0x14, x);
    MEM_W32(m + 0x18, y);
    MEM_W32(m + 0x1C, z);
    V0 = m;
}

/* 8002A8D8 (r, g, b): SetBackColor (<< 4 into the GTE). */
void n_gte_SetBackColor2(void) {
    gte_write_ctrl(21, A0 << 4);
    gte_write_ctrl(22, A1 << 4);
    gte_write_ctrl(23, A2 << 4);
}

/* 8002A928 (v, sxy, p, flag): RotTransPers: the screen position, depth
 * cue and flags; returns SZ3 / 4. */
void n_gte_RotTransPers(void) {
    gte_write_data(0, MEM_R32(A0));
    gte_write_data(1, MEM_R32(A0 + 4));
    gte_command(0x0180001u);
    gte_swc2(A1, 14);
    gte_swc2(A2, 8);
    MEM_W32(A3, gte_read_ctrl(31));
    V0 = (u32)((s32)gte_read_data(19) >> 2);
}

/* 8002CE58 (x, y): 0x80065A08 / 0A (GsSetOffset-style draw offset). */
void n_gs_SetOffset(void) { GV(u16, 0x80065A08u) = (u16)A0; GV(u16, 0x80065A0Au) = (u16)A1; }

/* 8002D818 (h): SetGeomScreen passed on. */
void n_gte_Wrap2D818(void) { WRAP(); V0 = call_ra(0x8002D828u, 0x8002A918u, A0, R[5], R[6], R[7]); }

/* 8002DDA8 (m): the light colour matrix (GTE control 16-20). */
void n_gte_SetColorMatrixRaw(void) {
    const u32 m = A0;
    u32 w[5];
    for (int i = 0; i < 5; i++) w[i] = MEM_R32(m + (u32)i * 4);
    for (int i = 0; i < 5; i++) gte_write_ctrl(16 + i, w[i]);
}

/* 8002BDA8 (box, ot, pri): GsSortBoxFill: a draw mode (semi-transparency
 * rate from attribute bits 28-29, dither) and TILE (colour +0xC..E, the
 * offset 0x80065A84 added to x, y; size +8 / +0xA) sorted (SortPacket,
 * 4 words), unless the attribute's bit 31 hides it. */
void n_gs_SortBoxFill(void) {
    WRAP();
    const u32 b = A0;
    const u32 at = MEM_R32(b);
    if ((s32)at < 0) return;
    const u32 pk = GV(u32, 0x80065C08u);
    MEM_W32(pk + 4, (((u32)((s32)at >> 17)) & 0x180) | (((u32)((s32)at >> 23)) & 0x60) | 0xE1000200u);
    MEM_W8(pk + 8, MEM_R8(b + 0xC));
    MEM_W8(pk + 9, MEM_R8(b + 0xD));
    const u32 c3 = MEM_R8(b + 0xE);
    MEM_W8(pk + 0xB, (u8)((((u32)((s32)at >> 29)) & 2) | 0x60));
    MEM_W8(pk + 0xA, (u8)c3);
    MEM_W16(pk + 0xC, (u16)(MEM_R16(b + 4) + GV(u16, 0x80065A84u)));
    MEM_W16(pk + 0xE, (u16)(MEM_R16(b + 6) + GV(u16, 0x80065A86u)));
    MEM_W16(pk + 0x10, MEM_R16(b + 8));
    MEM_W16(pk + 0x12, MEM_R16(b + 0xA));
    u32 r = call_ra(0x8002BE68u, 0x8002BE88u, pk, A1, A2 & 0xFFFF, 4);
    GV(u32, 0x80065C08u) = r;
}

/* 800333C0: 0x80060D54. */
void n_gs_Get60D54(void) { V0 = GV(u32, 0x80060D54u); }

/* ---- libgpu ---- */

/* 800370B8 (n): a GPU info request (GP1 0x10 | n); the answer's low 24
 * bits. */
void n_libgpu_GetInfo(void) {
    MEM_W32(GV(u32, 0x80061A54u), A0 | 0x10000000u);
    V0 = MEM_R32(GV(u32, 0x80061A50u)) & 0xFFFFFF;
}

/* 80037F00 (p, c, n): memset with polls. */
void n_libgpu_Memset(void) {
    u32 p = A0;
    const u32 c = A1, n = A2;
    V0 = n - 1;
    if (n == 0) return;
    for (u32 r = n - 1;;) {
        MEM_W8(p, (u8)c);
        r--;
        p++;
        if (r == 0xFFFFFFFFu) break;
        LOOP_POLL(0x80037F14u);
    }
    V0 = 0xFFFFFFFFu;
}

/* 800382A8 SetPolyF4, 80038348 SetTile: length and code. */
void n_libgpu_SetPolyF4(void) { MEM_W8(A0 + 3, 5); MEM_W8(A0 + 7, 0x28); V0 = 0x28; }
void n_libgpu_SetTile(void) { MEM_W8(A0 + 3, 3); MEM_W8(A0 + 7, 0x60); V0 = 0x60; }

/* 80038388 SetDrawMode(p, dfe, dtd, tpage, -): one word: texpage with
 * dither (dtd) and drawing to the display area (dfe). */
void n_libgpu_SetDrawMode2(void) {
    MEM_W8(A0 + 3, 1);
    u32 w = A2 ? 0xE1000200u : 0xE1000000u;
    u32 t = A3 & 0x9FF;
    if (A1) t |= 0x400;
    V0 = w | t;
    MEM_W32(A0 + 4, V0);
}

/* 800383B8 SetDrawStp(p, pbw): the mask bit setting (E6). */
void n_libgpu_SetDrawStp(void) {
    MEM_W8(A0 + 3, 2);
    V0 = A1 ? 0xE6000001u : 0xE6000000u;
    MEM_W32(A0 + 4, V0);
    MEM_W32(A0 + 8, 0);
}

/* 80038718 (fn): the MDEC-out DMA callback (DMACallback 1). */
void n_libpress_SetOutCallback(void) { WRAP(); V0 = call_ra(0x8003872Cu, 0x80025868u, 1, A0, R[6], R[7]); }

/* ---- libapi ---- */

/* 80039A08 EnterCriticalSection, 80039A18 ExitCriticalSection. */
void n_api_EnterCritical(void) { R[4] = 1; rt_syscall(0x80039A0Cu); }
void n_api_ExitCritical(void) { R[4] = 2; rt_syscall(0x80039A1Cu); }

/* 80039A28 SetSp(sp): returns the old one. 80039AB8 GetGp, 80039AC8 GetSp. */
void n_api_SetSp(void) { V0 = R[29]; R[29] = A0; }
void n_api_GetGp(void) { V0 = R[28]; }
void n_api_GetSp(void) { V0 = R[29]; }

/* ---- libpad ---- */

/* 80039F24: 0x80062A5C. */
void n_libpad_Get62A5C(void) { V0 = GV(u32, 0x80062A5Cu); }

/* 8003A3A8: PadStartCom: 8003AB98. */
void n_libpad_StartCom(void) { WRAP(); V0 = call_ra(0x8003A3B8u, 0x8003AB98u, R[4], R[5], R[6], R[7]); }

/* 8003A960: the pad handlers at 0x8006BD50-5C (0, 8003A9F4, 8003A98C, 0). */
void n_libpad_SetHandlers(void) {
    GV(u32, 0x8006BD54u) = 0x8003A9F4u;
    GV(u32, 0x8006BD58u) = 0x8003A98Cu;
    GV(u32, 0x8006BD50u) = 0;
    GV(u32, 0x8006BD5Cu) = 0;
    V0 = 0x8006BD54u;
}

/* 8003B618 (port, w, b): a port's pending word (+0x28) and byte (+0x34). */
void n_libpad_SetPending(void) { MEM_W32(A0 + 0x28, A1); MEM_W8(A0 + 0x34, (u8)A2); }

/* 8003DF84 (x): the word at $gp + 0x234. */
void n_libpad_SetGp234(void) { MEM_W32(R[28] + 0x234, A0); }
