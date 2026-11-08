/* libgpu (kernel): primitive setup macros as functions, the ordering
 * table, the GPU's control port.
 *
 *   0x80061A54  -> GP1 (0x1F801814)   0x80061A58 -> DMA2 MADR
 *   0x80061A5C  -> DMA2 BCR           0x80061A60 -> DMA2 CHCR
 *   0x8006A108  the last value sent per GP1 command (low byte)
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libgpu_AddPrim(void);
void n_libgpu_SetSemiTrans(void);
void n_libgpu_SetShadeTex(void);
void n_libgpu_SetPolyFT4(void);
void n_libgpu_SetSprt(void);
void n_libgpu_GetClut(void);
void n_libgpu_Ctrl(void);
void n_libgpu_SetTimeout(void);
void n_libgpu_DmaOt(void);
void n_libgpu_OffsetCmd(void);
void n_libgpu_ModeCmd(void);
void n_libgpu_ClipStartCmd(void);
void n_libgpu_ClipEndCmd(void);
void n_libgpu_TexWindowCmd(void);
void n_libgpu_ClearOTag(void);
void n_libgpu_PutDrawEnv(void);
void n_libgpu_DmaClearOT(void);
void n_libgpu_SetPolyG4(void);
void n_libgpu_SetDrawMode(void);
void n_libgpu_DrawOTag(void);
void n_libgpu_DrawSync(void);
void n_libgpu_SetDispMask(void);
void n_libgpu_DriverSync(void);
void n_libgpu_LoadImage(void);
void n_libgpu_CheckRect(void);
void n_libgpu_SetDrawEnv(void);
void n_libgpu_PutDispEnv(void);
void n_libgpu_TimedOut(void);
void n_libgpu_Enqueue(void);
void n_libgpu_RunQueue(void);
void n_libgpu_DriverLoadImage(void);
void n_libgpu_MoveImage(void);
void n_libgpu_ClearImage(void);
void n_libgpu_DriverStoreImage(void);
void n_libgpu_DriverReset(void);
void n_libgpu_ResetGraph(void);
void n_libpress_InSync(void);
void n_libpress_OutSync(void);
void n_libpress_DmaOutCmd(void);
void n_libpress_DecDCTin(void);
void n_libpress_BufSize(void);
void n_libpress_DecDCTvlc(void);
void n_libpress_DmaIn(void);
void n_libpress_SendIn(void);

/* 800381E8 AddPrim(ot, p): link p in at the head of the OT entry. */
void n_libgpu_AddPrim(void) {
    u32 ot = A0, p = A1;
    MEM_W32(p, (MEM_R32(p) & 0xFF000000u) | (MEM_R32(ot) & 0xFFFFFF));
    V0 = (MEM_R32(ot) & 0xFF000000u) | (p & 0xFFFFFF);
    MEM_W32(ot, V0);
}

/* 80038248 SetSemiTrans(p, on): the code's bit 2. */
void n_libgpu_SetSemiTrans(void) {
    u32 c = MEM_R8(A0 + 7);
    V0 = A1 ? c | 2 : c & 0xFD;
    MEM_W8(A0 + 7, (u8)V0);
}

/* 80038278 SetShadeTex(p, on): the code's bit 1 (texture unshaded). */
void n_libgpu_SetShadeTex(void) {
    u32 c = MEM_R8(A0 + 7);
    V0 = A1 ? c | 1 : c & 0xFE;
    MEM_W8(A0 + 7, (u8)V0);
}

/* 800382C8 SetPolyFT4(p): length 9, code 0x2C. */
void n_libgpu_SetPolyFT4(void) {
    MEM_W8(A0 + 3, 9);
    V0 = 0x2C;
    MEM_W8(A0 + 7, 0x2C);
}

/* 80038328 SetSprt(p): length 4, code 0x64. */
void n_libgpu_SetSprt(void) {
    MEM_W8(A0 + 3, 4);
    V0 = 0x64;
    MEM_W8(A0 + 7, 0x64);
}

/* 800381C8 GetClut(x, y). */
void n_libgpu_GetClut(void) {
    V0 = ((A1 << 6) | ((u32)((s32)A0 >> 4) & 0x3F)) & 0xFFFF;
}

/* 80036FF8 (command): send a GP1 command, remembering it per command. */
void n_libgpu_Ctrl(void) {
    MEM_W32(GV(u32, 0x80061A54u), A0);
    V0 = A0 >> 24;
    GV(u8, 0x8006A108u + V0) = (u8)A0;
}

/* 800378A8: arm libgpu's timeout: 240 vertical blanks from now
 * (0x80061A88), its counter (0x80061A8C) cleared. */
void n_libgpu_SetTimeout(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    V0 = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]) + 0xF0;
    GV(u32, 0x80061A88u) = V0;
    GV(u32, 0x80061A8Cu) = 0;
}

/* 80037070 (ot): start drawing an ordering table: GP1 DMA mode 2, then a
 * linked list DMA on channel 2 from ot. */
void n_libgpu_DmaOt(void) {
    MEM_W32(GV(u32, 0x80061A54u), 0x04000002u);
    MEM_W32(GV(u32, 0x80061A58u), A0);
    MEM_W32(GV(u32, 0x80061A5Cu), 0);
    R[3] = 0x01000401u;
    V0 = GV(u32, 0x80061A60u);
    MEM_W32(V0, R[3]);
}

/* 80036778 (x, y): the draw offset command (0xE5). */
void n_libgpu_OffsetCmd(void) {
    V0 = ((A1 & 0x7FF) << 11) | (A0 & 0x7FF) | 0xE5000000u;
}

/* 80036628 (dfe, dtd, tpage): the draw mode command (0xE1): drawing to
 * the displayed field (0x400), dithering (0x200), the texture page. */
void n_libgpu_ModeCmd(void) {
    u32 c = 0xE1000000u;
    if (A1) c |= 0x200;
    u32 t = A2 & 0x9FF;
    if (A0) t |= 0x400;
    V0 = c | t;
}

/* A drawing area corner command (x, y clamped to the VRAM size at
 * 0x80061950 / 0x80061952). */
static u32 clip_cmd(u32 x, u32 y, u32 code) {
    s32 w1 = (s16)GV(u16, 0x80061950u) - 1, h1 = (s16)GV(u16, 0x80061952u) - 1;
    if ((s16)x < 0) x = 0;
    else if (w1 < (s16)x) x = GV(u16, 0x80061950u) - 1u;
    if ((s16)y < 0) y = 0;
    else if (h1 < (s16)y) y = GV(u16, 0x80061952u) - 1u;
    return ((y & 0x3FF) << 10) | (x & 0x3FF) | code;
}

/* 80036648 (x, y): the drawing area's top left (0xE3). */
void n_libgpu_ClipStartCmd(void) { V0 = clip_cmd(A0, A1, 0xE3000000u); }
/* 800366E0 (x, y): the drawing area's bottom right (0xE4). */
void n_libgpu_ClipEndCmd(void) { V0 = clip_cmd(A0, A1, 0xE4000000u); }

/* 80036794 (rect): the texture window command (0xE2) for a RECT (0 for
 * none); the pieces left on the stack below sp as the original does. */
void n_libgpu_TexWindowCmd(void) {
    FRAME(0x10);
    u32 r = A0;
    if (r == 0) { V0 = 0; return; }
    u32 mx = MEM_R8(r) >> 3;
    MEM_W32(sp, mx);
    u32 ox = (u32)((s32)((0u - (u32)(s32)(s16)MEM_R16(r + 4)) & 0xFF) >> 3);
    MEM_W32(sp + 8, ox);
    u32 my = MEM_R8(r + 2) >> 3;
    MEM_W32(sp + 4, my);
    u32 oy = (u32)((s32)((0u - (u32)(s32)(s16)MEM_R16(r + 6)) & 0xFF) >> 3);
    V0 = (my << 15) | (mx << 10) | 0xE2000000u | (oy << 5) | ox;
    MEM_W32(sp + 0xC, oy);
}

#define GPU_DEBUG   GV(u8, 0x8006194Eu)    /* debug level: 2 and up print calls */
#define GPU_PRINTF  GV(u32, 0x80061948u)
#define GPU_DRIVER  GV(u32, 0x80061944u)   /* the driver's function table */

/* 80035860 ClearOTag(ot, n): the driver's ClearOTag (+0x2C), then the
 * first entry linked to the terminator packet 0x80061A0C (itself linking
 * 0x800619F8, length 4). Returns ot. */
void n_libgpu_ClearOTag(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[16] = A0; R[17] = A1;
    if (!(GPU_DEBUG < 2)) gcall(GPU_PRINTF, 0x800109D8u, R[16], R[17], R[7]);
    gcall(GV(u32, GPU_DRIVER + 0x2C), R[16], R[17], R[6], R[7]);
    GV(u32, 0x80061A0Cu) = (0x800619F8u & 0xFFFFFF) | 0x04000000u;
    MEM_W32(R[16], 0x80061A0Cu & 0xFFFFFF);
    V0 = R[16];
    R[6] = 0xFFFFFF;
    R[16] = s0; R[17] = s1;
}

/* 800359D8 PutDrawEnv(env): build the environment's packet (800363B8,
 * at env + 0x1C, terminated), send it through the driver (+0x8, on its
 * queue +0x18, 0x40 bytes) and keep a copy of env (0x8006195C). Returns
 * env. */
void n_libgpu_PutDrawEnv(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x1C, R[31]);
    R[18] = 0x8006194Eu;
    R[17] = A0;
    if (!(GPU_DEBUG < 2)) gcall(GPU_PRINTF, 0x80010A04u, R[17], R[6], R[7]);
    R[16] = R[17] + 0x1C;
    gcall(0x800363B8u, R[16], R[17], R[6], R[7]);
    MEM_W32(R[17] + 0x1C, MEM_R32(R[17] + 0x1C) | 0xFFFFFF);
    u32 d = GPU_DRIVER;
    gcall(MEM_R32(d + 8), MEM_R32(d + 0x18), R[16], 0x40, 0);
    gcall(0x80039888u, R[18] + 0xE, R[17], 0x5C, R[7]);          /* memcpy */
    V0 = R[17];
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 8003682C (ot, n): the driver's ClearOTagR: DMA channel 6 (enabled in
 * DPCR 0x80061A70; MADR 0x80061A64, BCR 0x80061A68, CHCR 0x80061A6C)
 * writes the reversed empty table ending at ot[n - 1]; wait for it
 * (libgpu's timeout, 800378DC: -1). Returns n. */
void n_libgpu_DmaClearOT(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[16] = A1;
    u32 dpcr = GV(u32, 0x80061A70u);
    MEM_W32(dpcr, MEM_R32(dpcr) | 0x08000000u);
    MEM_W32(GV(u32, 0x80061A6Cu), 0);
    MEM_W32(GV(u32, 0x80061A64u), A0 + (R[16] << 2) - 4);
    MEM_W32(GV(u32, 0x80061A68u), R[16]);
    MEM_W32(GV(u32, 0x80061A6Cu), 0x11000002u);
    gcall(0x800378A8u, R[4], R[5], R[6], R[7]);
    V0 = R[16];
    if (MEM_R32(GV(u32, 0x80061A6Cu)) & 0x01000000u) {
        R[17] = 0x01000000u;
        for (;;) {
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; break; }
            V0 = R[16];
            if (!(MEM_R32(GV(u32, 0x80061A6Cu)) & R[17])) break;
            LOOP_POLL(0x800368F0u);
        }
    }
    R[16] = s0; R[17] = s1;
}

/* 800382E8 SetPolyG4(p): length 8, code 0x38. */
void n_libgpu_SetPolyG4(void) {
    MEM_W8(A0 + 3, 8);
    V0 = 0x38;
    MEM_W8(A0 + 7, 0x38);
}

/* 800383E8 SetDrawMode(p, dfe, dtd, tpage, tw): a DR_MODE (length 2):
 * the draw mode command and the texture window's (0 for none). */
void n_libgpu_SetDrawMode(void) {
    u32 p = A0, tw = MEM_R32(R[29] + 0x10);
    MEM_W8(p + 3, 2);
    u32 c = 0xE1000000u;
    if (A2) c |= 0x200;
    u32 t = A3 & 0x9FF;
    if (A1) t |= 0x400;
    V0 = c | t;
    MEM_W32(p + 4, V0);
    if (tw == 0) { MEM_W32(p + 8, 0); return; }
    u32 w = (((u32)MEM_R8(tw + 2) >> 3) << 15) | (((u32)MEM_R8(tw) >> 3) << 10) | 0xE2000000u;
    w |= ((0u - (u32)(s32)(s16)MEM_R16(tw + 6)) << 2) & 0x3E0;
    V0 = (u32)((s32)((0u - (u32)(s32)(s16)MEM_R16(tw + 4)) & 0xFF) >> 3);
    MEM_W32(p + 8, w | V0);
}

/* A libgpu entry point: s0 the argument, the debug print, then the
 * driver's function. */
static u32 gpu_enter(u32 sp, u32 fmt) {
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = A0;
    if (!(GPU_DEBUG < 2)) gcall(GPU_PRINTF, fmt, R[16], R[6], R[7]);
    return s0;
}

/* 80035968 DrawOTag(ot): queue the table's drawing (driver +0x8, its
 * queue +0x18). */
void n_libgpu_DrawOTag(void) {
    FRAME(0x18);
    u32 s0 = gpu_enter(sp, 0x800109F0u);
    u32 d = GPU_DRIVER;
    gcall(MEM_R32(d + 8), MEM_R32(d + 0x18), R[16], 0, 0);
    R[16] = s0;
}

/* 80035374 DrawSync(mode): the driver's (+0x3C). */
void n_libgpu_DrawSync(void) {
    FRAME(0x18);
    u32 s0 = gpu_enter(sp, 0x8001094Cu);
    gcall(MEM_R32(GPU_DRIVER + 0x3C), R[16], R[5], R[6], R[7]);
    R[16] = s0;
}

/* 800352DC SetDispMask(on): display on (GP1 0x03000000) or off
 * (0x03000001, the cached display environment 0x800619B8 cleared to
 * 0xFF), through the driver (+0x10). */
void n_libgpu_SetDispMask(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[17] = 0x8006194Eu;
    R[16] = A0;
    if (!(GPU_DEBUG < 2)) gcall(GPU_PRINTF, 0x80010938u, R[16], R[6], R[7]);
    if (R[16] == 0) gcall(0x80037F00u, R[17] + 0x6A, (u32)-1, 0x14, R[7]);   /* memset */
    gcall(MEM_R32(GPU_DRIVER + 0x10), R[16] == 0 ? 0x03000001u : 0x03000000u, R[5], R[6], R[7]);
    R[16] = s0; R[17] = s1;
}

/* 8003776C (mode): the driver's DrawSync. 0: run the command queue
 * (0x80061A74 / 0x80061A78, 800373BC) until it is empty, then wait for
 * the DMA (0x80061A60) and the GPU to be ready for commands (GP1 status
 * bit 26), -1 on libgpu's timeout (800378DC); returns 0. Otherwise:
 * run the queue once and return the commands left (or 1 while the GPU
 * is busy and none are). */
void n_libgpu_DriverSync(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
#define Q_IN    GV(u32, 0x80061A74u)
#define Q_OUT   GV(u32, 0x80061A78u)
#define CHCR2   MEM_R32(GV(u32, 0x80061A60u))
#define GPUSTAT MEM_R32(GV(u32, 0x80061A54u))
    if (A0 == 0) {
        gcall(0x800378A8u, R[4], R[5], R[6], R[7]);
        for (;;) {
            if (Q_IN == Q_OUT) break;
            LOOP_POLL(0x800377C0u);
            gcall(0x800373BCu, R[4], R[5], R[6], R[7]);
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; goto out; }
        }
        for (;;) {
            if (CHCR2 & 0x01000000u) LOOP_POLL(0x800377F0u);
            else if (!(GPUSTAT & 0x04000000u)) LOOP_POLL(0x80037810u);
            else { V0 = 0; break; }
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; break; }
        }
    } else {
        R[16] = (Q_IN - Q_OUT) & 0x3F;
        if (R[16] != 0) gcall(0x800373BCu, R[4], R[5], R[6], R[7]);
        if (!(CHCR2 & 0x01000000u) && (GPUSTAT & 0x04000000u)) V0 = R[16];
        else V0 = R[16] ? R[16] : 1;
    }
#undef Q_IN
#undef Q_OUT
#undef CHCR2
#undef GPUSTAT
out:
    R[16] = s0;
}

/* 80035620 LoadImage(rect, p): checked (800353DC, "LoadImage"), then
 * queued through the driver (+0x8, its transfer +0x20, 8 bytes of
 * rect). */
void n_libgpu_LoadImage(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[16] = A0; R[17] = A1;
    gcall(0x800353DCu, 0x8001099Cu, R[16], R[6], R[7]);
    u32 d = GPU_DRIVER;
    gcall(MEM_R32(d + 8), MEM_R32(d + 0x20), R[16], 8, R[17]);
    R[16] = s0; R[17] = s1;
}

/* ---- libpress: the MDEC's input DMA (channel 0; MADR 0x80061BB8, BCR
 * 0x80061BBC, CHCR 0x80061BC0, DPCR 0x80061BE4) ---- */

/* 800389DC: wait for the input DMA to finish (about 0x100000 polls,
 * counted on the stack; then print a timeout, -1). Returns 0. */
void n_libpress_InSync(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x10, 0x00100000u);
    u32 chcr = GV(u32, 0x80061BC0u);
    V0 = 0;
    if (!(MEM_R32(chcr) & 0x01000000u)) return;
    for (;;) {
        MEM_W32(sp + 0x10, MEM_R32(sp + 0x10) - 1);
        if (MEM_R32(sp + 0x10) == 0xFFFFFFFFu) {
            gcall(0x80038A88u, 0x80010ABCu, R[5], R[6], R[7]);
            V0 = (u32)-1;
            return;
        }
        if (!(MEM_R32(GV(u32, 0x80061BC0u)) & 0x01000000u)) { V0 = 0; return; }
        LOOP_POLL(0x80038A58u);
    }
}

/* 800388BC (addr, words): once the last transfer is done (800389DC),
 * send words of data from addr to the MDEC in 0x20-word blocks. */
void n_libpress_DmaIn(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[17] = A0; R[16] = A1;
    gcall(0x800389DCu, R[4], R[5], R[6], R[7]);
    u32 dpcr = GV(u32, 0x80061BE4u);
    MEM_W32(dpcr, MEM_R32(dpcr) | 0x88);
    MEM_W32(GV(u32, 0x80061BC0u), 0);
    MEM_W32(GV(u32, 0x80061BB8u), R[17]);
    MEM_W32(GV(u32, 0x80061BBCu), ((R[16] >> 5) << 16) | 0x20);
    R[3] = 0x01000200u;
    MEM_W32(GV(u32, 0x80061BC0u), R[3]);
    R[16] = s0; R[17] = s1;
}

/* 8003865C (addr, words): 800388BC. */
void n_libpress_SendIn(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x800388BCu, R[4], R[5], R[6], R[7]);
}

/* 800353DC (name, rect): libgpu's debug check of a transfer's RECT: at
 * level 1 only a rect outside VRAM (0x80061950 / 52) or empty is
 * printed, at 2 every one ("name", then x y w h). Returns 2 (other
 * levels), 0 for a good rect at level 1. */
void n_libgpu_CheckRect(void) {
    FRAME(0x20);
    u32 s0 = R[16];
    MEM_W32(sp + 0x18, s0);
    MEM_W32(sp + 0x1C, R[31]);
    u32 name = A0;
    R[16] = A1;
    u32 r = R[16];
    u32 lvl = GV(u8, 0x8006194Eu), fmt;
    if (lvl == 1) {
        s32 x = (s16)MEM_R16(r), y = (s16)MEM_R16(r + 2), w = (s16)MEM_R16(r + 4), h = (s16)MEM_R16(r + 6);
        s32 W = (s16)GV(u16, 0x80061950u), H = (s16)GV(u16, 0x80061952u);
        if (!(W < w) && !(W < w + x) && !(H < y) && !(H < y + h) && w > 0 && x >= 0 && y >= 0 && h > 0) {
            V0 = 0; R[16] = s0; return;
        }
        fmt = 0x80010960u;
    } else if (lvl == 2) {
        fmt = 0x80010980u;
    } else {
        V0 = 2; R[16] = s0; return;
    }
    gcall(GPU_PRINTF, fmt, name, R[6], R[7]);
    MEM_W32(sp + 0x10, (u32)(s32)(s16)MEM_R16(r + 6));
    gcall(GPU_PRINTF, 0x8001096Cu, (u32)(s32)(s16)MEM_R16(r), (u32)(s32)(s16)MEM_R16(r + 2),
          (u32)(s32)(s16)MEM_R16(r + 4));
    R[16] = s0;
}

/* 800363B8 SetDrawEnv(p, env): the DR_ENV packet for a DRAWENV (clip
 * +0, offset +8, texture window +0xC, tpage +0x14, dtd +0x16, dfe +0x17,
 * isbg +0x18, rgb +0x19): area, offset, mode and window commands, 0xE6;
 * with isbg also a clear of the area (clamped to VRAM): a fill (0x02)
 * when it is 64-pixel aligned in x and width, otherwise a TILE (0x60)
 * relative to the offset. Returns its length, also in p + 3. */
void n_libgpu_SetDrawEnv(void) {
    FRAME(0x28);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x18, s0);
    MEM_W32(sp + 0x1C, s1);
    MEM_W32(sp + 0x20, R[31]);
    R[16] = A1; R[17] = A0;
    u32 e = R[16], p = R[17];
    MEM_W32(p + 4, gcall(0x80036648u, (u32)(s32)(s16)MEM_R16(e), (u32)(s32)(s16)MEM_R16(e + 2), R[6], R[7]));
    u32 x1 = (u32)(s32)(s16)(MEM_R16(e + 4) + MEM_R16(e) - 1);
    u32 y1 = (u32)(s32)(s16)(MEM_R16(e + 2) + MEM_R16(e + 6) - 1);
    MEM_W32(p + 8, gcall(0x800366E0u, x1, y1, R[6], R[7]));
    MEM_W32(p + 0xC, gcall(0x80036778u, (u32)(s32)(s16)MEM_R16(e + 8), (u32)(s32)(s16)MEM_R16(e + 0xA), R[6], R[7]));
    MEM_W32(p + 0x10, gcall(0x80036628u, MEM_R8(e + 0x17), MEM_R8(e + 0x16), MEM_R16(e + 0x14), R[7]));
    MEM_W32(p + 0x14, gcall(0x80036794u, e + 0xC, R[5], R[6], R[7]));
    MEM_W32(p + 0x18, 0xE6000000u);
    u32 n = 7;
    if (MEM_R8(e + 0x18)) {
        const u32 r = sp + 0x10;
        MEM_W16(r, MEM_R16(e));
        MEM_W16(r + 2, MEM_R16(e + 2));
        u32 w = MEM_R16(e + 4);
        MEM_W16(r + 4, (u16)w);
        MEM_W16(r + 6, MEM_R16(e + 6));
        s32 W1 = (s16)GV(u16, 0x80061950u) - 1, H1 = (s16)GV(u16, 0x80061952u) - 1;
        if ((s16)w < 0) w = 0;
        else if (W1 < (s16)w) w = GV(u16, 0x80061950u) - 1u;
        MEM_W16(r + 4, (u16)w);
        u32 h = MEM_R16(r + 6);
        if ((s16)h < 0) h = 0;
        else if (H1 < (s16)h) h = GV(u16, 0x80061952u) - 1u;
        u32 x = MEM_R16(r);
        MEM_W16(r + 6, (u16)h);
        u32 rgb = ((u32)MEM_R8(e + 0x1B) << 16) | ((u32)MEM_R8(e + 0x1A) << 8) | MEM_R8(e + 0x19);
        if ((x & 0x3F) || (MEM_R16(r + 4) & 0x3F)) {
            MEM_W16(r, (u16)(x - MEM_R16(e + 8)));
            MEM_W16(r + 2, (u16)(MEM_R16(r + 2) - MEM_R16(e + 0xA)));
            MEM_W32(p + 0x1C, rgb | 0x60000000u);
        } else {
            MEM_W32(p + 0x1C, rgb | 0x02000000u);
        }
        MEM_W32(p + 0x20, MEM_R32(r));
        MEM_W32(p + 0x24, MEM_R32(r + 4));
        n = 10;
    }
    V0 = n - 1;
    MEM_W8(p + 3, (u8)V0);
    R[16] = s0; R[17] = s1;
}

/* 800378DC: has libgpu's timeout (0x80061A88 the last vertical blank,
 * 0x80061A8C a call count up to 0xF0000) run out? If so print the
 * state ("GPU timeout": queue, status, CHCR, MADR), empty the queue,
 * reset the DMA and the GPU's command buffer (with interrupts masked)
 * and return -1; otherwise 0. */
void n_libgpu_TimedOut(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x18, R[31]);
    u32 now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
    if (!((s32)GV(u32, 0x80061A88u) < (s32)now)) {
        u32 c = GV(u32, 0x80061A8Cu);
        GV(u32, 0x80061A8Cu) = c + 1;
        if (!(0x000F0000 < (s32)c)) { V0 = 0; return; }
    }
    u32 gs = GV(u32, 0x80061A54u);
    (void)MEM_R32(gs);
    u32 q = GV(u32, 0x80061A74u) - GV(u32, 0x80061A78u);
    MEM_W32(sp + 0x10, MEM_R32(GV(u32, 0x80061A58u)));
    u32 st = MEM_R32(gs);
    u32 ch = MEM_R32(GV(u32, 0x80061A60u));
    gcall(0x800398C8u, 0x80010A50u, q & 0x3F, st, ch);
    u32 mask = gcall(0x80025984u, 0, R[5], R[6], R[7]);
    GV(u32, 0x80061A78u) = 0;
    GV(u32, 0x80061A84u) = mask;
    GV(u32, 0x80061A74u) = GV(u32, 0x80061A78u);
    MEM_W32(GV(u32, 0x80061A60u), 0x401);
    u32 dpcr = GV(u32, 0x80061A70u);
    MEM_W32(dpcr, MEM_R32(dpcr) | 0x800);
    MEM_W32(GV(u32, 0x80061A54u), 0x02000000u);
    MEM_W32(GV(u32, 0x80061A54u), 0x01000000u);
    gcall(0x80025984u, GV(u32, 0x80061A84u), R[5], R[6], R[7]);
    V0 = (u32)-1;
}

/* 8003710C (func, data, size, param): the driver's command queue (64
 * entries of 0x60 bytes at 0x8006A210: +0 func, +4 data, +8 param, +0xC
 * a copy of up to size bytes of data; in 0x80061A74, out 0x80061A78).
 * Wait while it is full (libgpu's timeout: -1). With interrupts masked:
 * when not queueing (0x8006194D clear, or the queue empty, the DMA idle
 * and 0x80061958 clear) wait for the GPU and call func (data, param) at
 * once, returning 0; otherwise queue it (data copied when size is
 * given), arm the DMA callback (DMACallback (2, 800373BC)) and run the
 * queue, returning the entries left. The original's s-registers. */
void n_libgpu_Enqueue(void) {
    FRAME(0x28);
    u32 sv[4] = { R[16], R[17], R[18], R[19] };
    for (int i = 0; i < 4; i++) MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x20, R[31]);
    R[19] = A0; R[16] = A1; R[17] = A2; R[18] = A3;
#define Q_IN    GV(u32, 0x80061A74u)
#define Q_OUT   GV(u32, 0x80061A78u)
#define Q_ENTRY(i) (0x8006A210u + (i) * 0x60)
    gcall(0x800378A8u, R[4], R[5], R[6], R[7]);
    for (;;) {
        if (((Q_IN + 1) & 0x3F) != Q_OUT) break;
        LOOP_POLL(0x80037170u);
        if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; goto out; }
        gcall(0x800373BCu, R[4], R[5], R[6], R[7]);
    }
    GV(u32, 0x80061A7Cu) = gcall(0x80025984u, 0, R[5], R[6], R[7]);
    GV(u32, 0x80061954u) = 1;
    if (GV(u8, 0x8006194Du) == 0 ||
        (Q_IN == Q_OUT && !(MEM_R32(GV(u32, 0x80061A60u)) & 0x01000000u) && GV(u32, 0x80061958u) == 0)) {
        for (;;) {
            if (MEM_R32(GV(u32, 0x80061A54u)) & 0x04000000u) break;
            LOOP_POLL(0x80037204u);
        }
        gcall(R[19], R[16], R[18], R[6], R[7]);
        gcall(0x80025984u, GV(u32, 0x80061A7Cu), R[5], R[6], R[7]);
        V0 = 0;
        goto out;
    }
    gcall(0x80025868u, 2, 0x800373BCu, R[6], R[7]);
    if (R[17] == 0) {
        GV(u32, Q_ENTRY(Q_IN) + 4) = R[16];
    } else {
        u32 src = R[16];
        for (s32 i = 0;;) {
            s32 sz = (s32)R[17];
            if (!(i < ((sz >= 0 ? sz : sz + 3) >> 2))) break;
            u32 w = MEM_R32(src);
            src += 4;
            GV(u32, Q_ENTRY(Q_IN) + 0xC + (u32)i * 4) = w;
            i++;
            LOOP_POLL(0x800372A0u);
        }
        GV(u32, Q_ENTRY(Q_IN) + 4) = Q_ENTRY(Q_IN) + 0xC;
    }
    GV(u32, Q_ENTRY(Q_IN) + 8) = R[18];
    GV(u32, Q_ENTRY(Q_IN)) = R[19];
    Q_IN = (Q_IN + 1) & 0x3F;
    gcall(0x80025984u, GV(u32, 0x80061A7Cu), R[5], R[6], R[7]);
    gcall(0x800373BCu, R[4], R[5], R[6], R[7]);
    V0 = (Q_IN - Q_OUT) & 0x3F;
#undef Q_IN
#undef Q_OUT
#undef Q_ENTRY
out:
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* 800373BC: run the driver's queue (see 8003710C) while the DMA is
 * idle: with interrupts masked, for each entry wait for the GPU to take
 * commands and call it (on the last one, the DMA callback removed unless
 * 0x80061958 is set). When it is empty and idle, call the drawing-done
 * hook 0x80061958 once (armed by 0x80061954). Returns the entries left
 * (1 when the DMA was busy). The original's s-registers. */
void n_libgpu_RunQueue(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
#define Q_IN    GV(u32, 0x80061A74u)
#define Q_OUT   GV(u32, 0x80061A78u)
#define Q_ENTRY(i) (0x8006A210u + (i) * 0x60)
#define BUSY()  (MEM_R32(GV(u32, 0x80061A60u)) & 0x01000000u)
    R[16] = 0x01000000u;
    if (BUSY()) { V0 = 1; goto out; }
    GV(u32, 0x80061A80u) = gcall(0x80025984u, 0, R[5], R[6], R[7]);
    if (Q_IN != Q_OUT && !BUSY()) {
        R[17] = 0x04000000u;
        for (;;) {
            if (((Q_OUT + 1) & 0x3F) == Q_IN && GV(u32, 0x80061958u) == 0)
                gcall(0x80025868u, 2, 0, R[6], R[7]);
            u32 gs = GV(u32, 0x80061A54u);
            if (!(MEM_R32(gs) & R[17])) {
                for (;;) {
                    if (MEM_R32(gs) & 0x04000000u) break;
                    LOOP_POLL(0x8003749Cu);
                }
            }
            u32 e = Q_ENTRY(Q_OUT);
            gcall(GV(u32, e), GV(u32, e + 4), GV(u32, e + 8), R[6], R[7]);
            Q_OUT = (Q_OUT + 1) & 0x3F;
            if (Q_IN == Q_OUT || BUSY()) break;
            LOOP_POLL(0x80037564u);
        }
    }
    gcall(0x80025984u, GV(u32, 0x80061A80u), R[5], R[6], R[7]);
    if (Q_IN == Q_OUT && !(MEM_R32(GV(u32, 0x80061A60u)) & 0x01000000u) && GV(u32, 0x80061954u) != 0) {
        u32 f = GV(u32, 0x80061958u);
        if (f) {
            GV(u32, 0x80061954u) = 0;
            gcall(f, f, R[5], R[6], R[7]);
        }
    }
    V0 = (Q_IN - Q_OUT) & 0x3F;
#undef Q_IN
#undef Q_OUT
#undef Q_ENTRY
#undef BUSY
out:
    R[16] = s0; R[17] = s1;
}

/* 80036B3C (rect, data): the driver's LoadImage: the rect's size
 * clamped to VRAM (0x80061950 / 52, kept in the rect); wait for the GPU
 * (libgpu's timeout: -1); send the copy-to-VRAM command (0xA0) with the
 * rect, then the odd words by hand and the 16-word blocks by DMA
 * (channel 2). -1 for an empty rect, else 0. The original's
 * s-registers. */
void n_libgpu_DriverLoadImage(void) {
    FRAME(0x30);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x28, R[31]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x10, sv[0]);
    R[17] = A0; R[18] = A1;
    gcall(0x800378A8u, R[4], R[5], R[6], R[7]);
    const u32 r = R[17];
    R[21] = 0;
    u32 w = MEM_R16(r + 4);
    if ((s16)w < 0) w = 0;
    else if ((s16)GV(u16, 0x80061950u) < (s16)w) w = GV(u16, 0x80061950u);
    MEM_W16(r + 4, (u16)w);
    u32 h = MEM_R16(r + 6);
    if ((s16)h < 0) h = 0;
    else if ((s16)GV(u16, 0x80061952u) < (s16)h) h = GV(u16, 0x80061952u);
    u32 area = (u32)((s32)(s16)MEM_R16(r + 4) * (s32)(s16)h);
    MEM_W16(r + 6, (u16)h);
    u32 a1 = area + 1;
    s32 words = (s32)(a1 + (a1 >> 31)) >> 1;
    R[16] = (u32)((s32)a1 >> 5);
    if (!(words > 0)) { V0 = (u32)-1; goto out; }
    R[20] = R[16];
    R[16] = (u32)words - (R[20] << 4);
#define GP0 GV(u32, 0x80061A50u)
#define GP1 GV(u32, 0x80061A54u)
    if (!(MEM_R32(GP1) & 0x04000000u)) {
        R[19] = 0x04000000u;
        for (;;) {
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; goto out; }
            if (MEM_R32(GP1) & R[19]) break;
            LOOP_POLL(0x80036C70u);
        }
    }
    MEM_W32(GP1, 0x04000000u);
    MEM_W32(GP0, 0x01000000u);
    MEM_W32(GP0, 0xA0000000u);
    MEM_W32(GP0, MEM_R32(r));
    MEM_W32(GP0, MEM_R32(r + 4));
    R[16]--;
    if (R[16] != 0xFFFFFFFFu) {
        for (;;) {
            u32 v = MEM_R32(R[18]);
            R[18] += 4;
            R[16]--;
            MEM_W32(GP0, v);
            if (R[16] == 0xFFFFFFFFu) break;
            LOOP_POLL(0x80036CFCu);
        }
    }
    if (R[20] != 0) {
        MEM_W32(GP1, 0x04000002u);
        MEM_W32(GV(u32, 0x80061A58u), R[18]);
        MEM_W32(GV(u32, 0x80061A5Cu), (R[20] << 16) | 0x10);
        MEM_W32(GV(u32, 0x80061A60u), 0x01000201u);
    }
#undef GP0
#undef GP1
    V0 = 0;
out:
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 800356E0 MoveImage(rect, x, y): checked (800353DC, "MoveImage"); an
 * empty rect gives -1; otherwise the copy (rect, then y << 16 | x) built
 * at 0x800619E4 and queued through the driver (+0x8, 0x14 bytes). */
void n_libgpu_MoveImage(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x1C, R[31]);
    R[16] = A0; R[18] = A1; R[17] = A2;
    gcall(0x800353DCu, 0x800109B4u, R[16], R[6], R[7]);
    if ((s16)MEM_R16(R[16] + 4) == 0 || (s16)MEM_R16(R[16] + 6) == 0) { V0 = (u32)-1; goto out; }
    {
        const u32 q = 0x800619ECu;
        u32 d = GPU_DRIVER;
        GV(u32, q + 4) = (R[17] << 16) | (R[18] & 0xFFFF);
        GV(u32, q) = MEM_R32(R[16]);
        GV(u32, q + 8) = MEM_R32(R[16] + 4);
        gcall(MEM_R32(d + 8), MEM_R32(d + 0x18), q - 8, 0x14, 0);
    }
out:
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

static int dispenv_same(u32 a, u32 b) {
    for (u32 i = 0; i < 8; i += 2)
        if ((s16)GV(u16, a + i) != (s16)MEM_R16(b + i)) return 0;
    return 1;
}

/* 80035BA4 (env): libgpu's PutDispEnv. Display area start (GP1 05);
 * when the display mode changed (the 4 bytes at env+0x10 or the rect
 * against the copy at 0x800619B8) the mode word (GP1 08: PAL, 24-bit,
 * interlace, the reversal flag 0x8006194F, the width and height bits;
 * env+0x12 then 8 to force the next step); when the screen rect changed,
 * the horizontal (GP1 06, from the per-width dot clocks 0x80061A48 and
 * the per-mode ranges 0x80061A20) and vertical (GP1 07) display range,
 * clamped as libgpu does for NTSC and PAL. Keeps a copy (0x14 bytes)
 * and returns env. The original's s-registers. */
void n_libgpu_PutDispEnv(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17], s2 = R[18];
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, s2);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x10, s0);
    const u32 d = A0;
    R[17] = d;
    R[18] = 0x8006194Eu;
    R[16] = 0x08000000u;
    if (GV(u8, 0x8006194Eu) >= 2) gcall(GV(u32, 0x80061948u), 0x80010A38u, d, R[6], R[7]);
    u32 tab = GV(u32, 0x80061944u);
    gcall(MEM_R32(tab + 0x10), 0x05000000u | ((MEM_R16(d + 2) & 0x3FFu) << 10) | (MEM_R16(d) & 0x3FFu), tab, R[6], R[7]);
    int skip_mode = GV(u32, 0x800619C8u) == MEM_R32(d + 0x10) && dispenv_same(0x800619B8u, d);
    if (!skip_mode) {
        u32 v = gcall(0x8002629Cu, R[4], R[5], R[6], R[7]);
        MEM_W8(d + 0x12, (u8)v);
        u32 m = R[16];
        if ((v & 0xFF) == 1) m |= 8;
        if (MEM_R8(d + 0x11)) m |= 0x10;
        if (MEM_R8(d + 0x10)) m |= 0x20;
        if (GV(u8, 0x8006194Fu)) m |= 0x80;
        s32 w = (s16)MEM_R16(d + 4);
        if (w < 281) {}
        else if (w < 353) m |= 1;
        else if (w < 401) m |= 0x40;
        else if (w < 561) m |= 2;
        else m |= 3;
        s32 h = (s16)MEM_R16(d + 6);
        if (!(MEM_R8(d + 0x12) ? h < 289 : h < 257)) m |= 0x24;
        R[16] = m;
        tab = GV(u32, 0x80061944u);
        gcall(MEM_R32(tab + 0x10), m, tab, R[6], R[7]);
        MEM_W8(d + 0x12, 8);
    }
    if (!dispenv_same(0x800619C0u, d + 8) || MEM_R8(d + 0x12) == 8) {
        u32 v = gcall(0x8002629Cu, R[4], R[5], R[6], R[7]);
        MEM_W8(d + 0x12, (u8)v);
        s32 y0 = (s16)MEM_R16(d + 0xA);
        s32 ya = (v & 0xFF) ? y0 + 0x13 : y0 + 0x10;
        s32 sh = (s16)MEM_R16(d + 0xE);
        s32 yb = sh ? ya + sh : ya + 0xF0;
        s32 w = (s16)MEM_R16(d + 4);
        u32 k = w < 281 ? 0 : w < 353 ? 1 : w < 401 ? 2 : w < 561 ? 3 : 4;
        s32 mul = GV(u8, 0x80061A48u + k);
        s32 x0 = (s16)MEM_R16(d + 8);
        u32 idx = (((u32)MEM_R8(d + 0x12) * 5) + k) * 4;
        s32 base = GV(u16, 0x80061A20u + idx);
        s32 span = (s32)GV(u16, 0x80061A22u + idx) - base;
        s32 x1 = base + x0 * mul;
        s32 sw = (s16)MEM_R16(d + 0xC);
        s32 x2 = sw ? x1 + ((span * sw) >> 8) : x1 + span;
        if (MEM_R8(d + 0x12)) {
            x1 = x1 < 540 ? 0x21C : x1 < 3221 ? x1 : 0xC94;
            s32 lo = x1 + mul * 4;
            x2 = x2 < lo ? lo : x2 < 3261 ? x2 : 0xCBC;
            ya = ya < 19 ? 0x13 : ya < 304 ? ya : 0x12F;
            yb = yb < ya + 2 ? ya + 2 : yb < 306 ? yb : 0x131;
        } else {
            x1 = x1 < 500 ? 0x1F4 : x1 < 3251 ? x1 : 0xCB2;
            s32 lo = x1 + mul * 4;
            x2 = x2 < lo ? lo : x2 < 3291 ? x2 : 0xCDA;
            ya = ya < 16 ? 0x10 : ya < 258 ? ya : 0x101;
            yb = yb < ya + 2 ? ya + 2 : yb < 259 ? yb : 0x102;
        }
        R[16] = (u32)ya; R[18] = (u32)yb;
        tab = GV(u32, 0x80061944u);
        gcall(MEM_R32(tab + 0x10), 0x06000000u | (((u32)x2 & 0xFFFu) << 12) | ((u32)x1 & 0xFFFu), tab, R[6], R[7]);
        tab = GV(u32, 0x80061944u);
        gcall(MEM_R32(tab + 0x10), 0x07000000u | (((u32)yb & 0x3FFu) << 10) | ((u32)ya & 0x3FFu), tab, R[6], R[7]);
    }
    gcall(0x80039888u, 0x800619B8u, d, 0x14, R[7]);
    V0 = d;
    R[16] = s0; R[17] = s1; R[18] = s2;
}

/* 80038948: wait for the MDEC's output to be taken (status bit 29 of
 * *0x80061BE0 clear), 0x100000 polls on a stack counter; timeout: a
 * message (80038A88), -1. Else 0. */
void n_libpress_OutSync(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x10, 0x00100000u);
    V0 = 0;
    if (!(MEM_R32(GV(u32, 0x80061BE0u)) & 0x20000000u)) return;
    for (;;) {
        MEM_W32(sp + 0x10, MEM_R32(sp + 0x10) - 1);
        if (MEM_R32(sp + 0x10) == 0xFFFFFFFFu) {
            gcall(0x80038A88u, 0x80010AACu, R[5], R[6], R[7]);
            V0 = (u32)-1;
            return;
        }
        if (!(MEM_R32(GV(u32, 0x80061BE0u)) & 0x20000000u)) { V0 = 0; return; }
        LOOP_POLL(0x800389C4u);
    }
}

/* 8003882C (buf, size): send an MDEC command block: wait (80038948),
 * DMA 0 (to the MDEC) from buf+4, size/32 blocks of 32 words, the
 * command word buf[0]. The original's s-registers. */
void n_libpress_DmaOutCmd(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x18, R[31]);
    const u32 buf = A0, size = A1;
    R[17] = buf; R[16] = size;
    gcall(0x80038948u, buf, size, R[6], R[7]);
    u32 a = GV(u32, 0x80061BE4u);
    MEM_W32(a, MEM_R32(a) | 0x88);
    MEM_W32(GV(u32, 0x80061BACu), buf + 4);
    MEM_W32(GV(u32, 0x80061BB0u), ((size >> 5) << 16) | 0x20);
    MEM_W32(GV(u32, 0x80061BDCu), MEM_R32(buf));
    V0 = GV(u32, 0x80061BB4u);
    MEM_W32(V0, 0x01000201u);
    R[16] = s0; R[17] = s1;
}

/* 800385E0 DecDCTin(buf, mode): the decode command's depth bits from
 * mode (bit 0 clear: 24-bit, bit 27; bit 1: bit 25), then sent by
 * 8003882C with the block's own size. */
void n_libpress_DecDCTin(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    const u32 b = A0, m = A1;
    if (m & 1) MEM_W32(b, MEM_R32(b) & 0xF7FFFFFFu);
    else MEM_W32(b, MEM_R32(b) | 0x08000000u);
    if (m & 2) MEM_W32(b, MEM_R32(b) | 0x02000000u);
    else MEM_W32(b, MEM_R32(b) & 0xFDFFFFFFu);
    gcall(0x8003882Cu, b, MEM_R16(b), R[6], R[7]);
}

/* 80038F78 DecDCTBufSize(bs): the low half of the stream's first word. */
void n_libpress_BufSize(void) { V0 = MEM_R16(A0); }

/* 80038B38 DecDCTvlc(bs, buf, table): libpress's VLC decoder (its
 * hand-written assembly, kept as such: the bit buffer r2 with r3 bits
 * used, the stream r4, the output r5, the tables at table+0x800 (short
 * codes, 8 bytes each) and +0x10800 (long codes), the DC tables below
 * table+0x800 for version 3 with r13 the block's DC component (Cr, Cb,
 * Y...) and r15 / r24 / r25 its predictors). bs 0 resumes from the
 * state at 0x80061BF0. The output fills to the size limit 0x80061BEC
 * (state saved, 1) or the stream's end (padded with 0xFE00 to the
 * length in its header, 0). */
void n_libpress_DecDCTvlc(void) {
#define SL(x, n) ((u32)(x) << ((n) & 31))
#define SR(x, n) ((u32)(x) >> ((n) & 31))
    u32 r1, r2, r3, r4 = A0, r5 = A1, r6 = A2, r7, r8, r9, r10, r11 = 0, r12, r13, r14, r15, r24, r25;
    r6 += 0x800;
    r7 = r6 + 0x10000;
    r9 = GV(u32, 0x80061BECu);
    if (r4 == 0) {
        const u32 st = 0x80061BF0u;
        r4 = GV(u32, st); r5 = GV(u32, st + 4); r2 = GV(u32, st + 8); r3 = GV(u32, st + 0xC);
        r12 = GV(u32, st + 0x10); r13 = GV(u32, st + 0x14); r15 = GV(u32, st + 0x18);
        r24 = GV(u32, st + 0x1C); r25 = GV(u32, st + 0x20);
        r9 = r9 + r9;
        r14 = r5 + r9;
        goto L_D18;
    }
    r13 = 0; r15 = 0; r24 = 0; r25 = 0;
    r9 = r9 + r9;
    r14 = r5 + r9;
    r9 = MEM_R32(r4);
    r12 = MEM_R16(r4 + 4);
    r10 = MEM_R16(r4 + 6);
    r2 = MEM_R16(r4 + 8);
    r3 = MEM_R16(r4 + 0xA);
    r10 = r10 - 3;
    r12 = r12 << 10;
    if ((s32)r10 >= 0) r13 = 1;
    r4 += 0xC;
    r2 = (r2 << 16) | r3;
    r3 = 0;
    MEM_W32(r5, r9);
    r9 = ((r9 & 0xFFFF) << 2) + 4 + r5;
    GV(u32, 0x80061C14u) = r9;
    r5 += 2;
L_BFC:
    r8 = r2 >> 22;
    if (r13 == 0) goto L_CD4;
    r1 = r8 ^ 0x3FF;
    r5 += 2;
    if (r1 == 0) goto L_E20;
    r1 = r6 - 0x400;
    if (!((s32)(r13 - 3) < 0)) r1 = r1 - 0x400;
    r8 = ((r2 >> 24) << 2) + r1;
    r9 = MEM_R16(r8);
    r10 = MEM_R16(r8 + 2);
    r8 = 0;
    r2 = SL(r2, r9);
    if (r10 != 0) {
        r1 = 32 - r10;
        r8 = SR(r2, r1);
        int neg = (s32)r2 < 0;
        r2 = SL(r2, r10);
        if (!neg) {
            r11 = SR(0xFFFFFFFFu, r1);
            r8 = r8 - r11;
        }
        r3 = r3 + r10;
    }
    r3 = r3 + r9;
    r1 = r3 & 0x10;
    r3 = r3 & 0xF;
    if (r1 != 0) {
        r9 = MEM_R16(r4);
        r4 += 2;
        r2 = r2 | SL(r9, r3);
    }
    r1 = r13 - 2;
    if ((s32)r1 > 0) { r9 = r25 + r8; r25 = r25 + r8; }
    else if (r1 == 0) { r9 = r24 + r8; r24 = r24 + r8; }
    else { r9 = r15 + r8; r15 = r15 + r8; }
    r9 = ((r9 << 2) & 0x3FF) | r12;
    r13 = r13 + 1;
    MEM_W16(r5, (u16)r9);
    if (r13 == 7) r13 = r13 - 6;
    goto L_D0C;
L_CD4:
    r1 = r8 ^ 0x1FF;
    r5 += 2;
    if (r1 == 0) goto L_E20;
    r2 = r2 << 10;
    r3 = r3 + 0xA;
    r1 = r3 & 0x10;
    r3 = r3 & 0xF;
    if (r1 != 0) {
        r9 = MEM_R16(r4);
        r4 += 2;
        r2 = r2 | SL(r9, r3);
    }
    r8 = r12 | r8;
    MEM_W16(r5, (u16)r8);
L_D0C:
    r1 = r5 - r14;
    r5 += 2;
    if ((s32)r1 >= 0) goto L_E50;
L_D18:
    r8 = ((r2 >> 19) << 3) + r6;
    r9 = MEM_R32(r8);
    r1 = r9 & 0xFF;
    if (r9 != 0) {
        r11 = MEM_R32(r8 + 4);
    } else {
        r2 = r2 << 8;
        r3 = r3 + 8;
        r1 = r3 & 0x10;
        r3 = r3 & 0xF;
        if (r1 != 0) {
            r8 = MEM_R16(r4);
            r4 += 2;
            r2 = r2 | SL(r8, r3);
        }
        r8 = ((r2 >> 23) << 2) + r7;
        r9 = MEM_R32(r8);
        r11 = 0;
        r1 = r9 & 0xFF;
    }
    r2 = SL(r2, r1);
    r3 = r3 + r1;
    r1 = r3 & 0x10;
    r3 = r3 & 0xF;
    if (r1 != 0) {
        r8 = MEM_R16(r4);
        r4 += 2;
        r2 = r2 | SL(r8, r3);
    }
    r9 = r9 >> 16;
    if (r9 == 0x7C1F) goto L_DFC;
    MEM_W16(r5, (u16)r9);
    if (r9 == 0xFE00) { LOOP_POLL(0x80038DACu); goto L_BFC; }
    r5 += 2;
    if (r11 == 0) { LOOP_POLL(0x80038DB4u); goto L_D18; }
    r10 = r11 & 0xFFFF;
    if (r10 == 0x7C1F) goto L_DFC;
    MEM_W16(r5, (u16)r10);
    if (r10 == 0xFE00) { LOOP_POLL(0x80038DCCu); goto L_BFC; }
    r10 = r11 >> 16;
    r5 += 2;
    if (r10 == 0) { LOOP_POLL(0x80038DD8u); goto L_D18; }
    if (r10 == 0x7C1F) goto L_DFC;
    MEM_W16(r5, (u16)r10);
    if (r10 == 0xFE00) { LOOP_POLL(0x80038DECu); goto L_BFC; }
    r5 += 2;
    LOOP_POLL(0x80038DF4u);
    goto L_D18;
L_DFC:
    r8 = r2 >> 16;
    MEM_W16(r5, (u16)r8);
    r5 += 2;
    r8 = MEM_R16(r4);
    r4 += 2;
    r2 = r2 << 16;
    r8 = SL(r8, r3);
    r2 = r2 | r8;
    LOOP_POLL(0x80038E18u);
    goto L_D18;
L_E20:
    r9 = GV(u32, 0x80061C14u);
    for (;;) {
        r1 = r5 - r9;
        if ((s32)r1 >= 0) { V0 = 0; return; }
        MEM_W16(r5, 0xFE00);
        r5 += 2;
        LOOP_POLL(0x80038E40u);
    }
L_E50: {
        const u32 st = 0x80061BF0u;
        GV(u32, st) = r4; GV(u32, st + 4) = r5; GV(u32, st + 8) = r2; GV(u32, st + 0xC) = r3;
        GV(u32, st + 0x10) = r12; GV(u32, st + 0x14) = r13; GV(u32, st + 0x18) = r15;
        GV(u32, st + 0x1C) = r24; GV(u32, st + 0x20) = r25;
        V0 = 1;
    }
    (void)r7; (void)r11;
#undef SL
#undef SR
}

/* 8003690C (rect, colour): the GPU driver's ClearImage: the size clamped
 * to the VRAM (0x80061950/52 less 1), then a packet at 0x8006A0C0 sent
 * (DmaOt): a fill (0x02) with the draw mode (texpage of *0x80061A54 and
 * colour bit 31 as dither) when x and w are multiples of 64, else a
 * tile (0x60) with the drawing area / offset opened up (E3/E4/E5) and a
 * second packet at 0x8006A0E8 restoring them (800370B8 reads them back). */
void n_libgpu_ClearImage(void) {
    FRAME(0x20);
    u32 sv[3];
    for (int i = 0; i < 3; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x1C, R[31]);
    const u32 r = A0, col = A1;
    for (u32 k = 0; k < 2; k++) {
        const u32 f = r + 4 + k * 2;
        s32 v = (s16)MEM_R16(f);
        s32 lim = (s16)GV(u16, 0x80061950u + k * 2) - 1;
        MEM_W16(f, v < 0 ? 0 : lim < v ? (u16)(GV(u16, 0x80061950u + k * 2) - 1) : MEM_R16(f));
    }
    const u32 pk = 0x8006A0C0u;
    const u32 dm = (MEM_R32(GV(u32, 0x80061A54u)) & 0x7FF) | ((col >> 31) << 10) | 0xE1000000u;
    if ((MEM_R16(r) & 0x3F) || (MEM_R16(r + 4) & 0x3F)) {
        const u32 sub = 0x8006A0E8u;
        MEM_W32(pk, (sub & 0xFFFFFF) | 0x08000000u);
        MEM_W32(pk + 0x10, 0xE6000000u);
        MEM_W32(pk + 4, 0xE3000000u);
        MEM_W32(pk + 8, 0xE4FFFFFFu);
        MEM_W32(pk + 0xC, 0xE5000000u);
        MEM_W32(pk + 0x18, (col & 0xFFFFFF) | 0x60000000u);
        MEM_W32(pk + 0x14, dm);
        MEM_W32(pk + 0x1C, MEM_R32(r));
        MEM_W32(sub, 0x03FFFFFFu);
        MEM_W32(pk + 0x20, MEM_R32(r + 4));
        R[16] = sub; R[17] = 0xE3000000u; R[18] = 0xE5000000u;
        MEM_W32(sub + 4, gcall(0x800370B8u, 3, R[5], R[6], R[7]) | 0xE3000000u);
        MEM_W32(sub + 8, gcall(0x800370B8u, 4, R[5], R[6], R[7]) | 0xE4000000u);
        MEM_W32(sub + 0xC, gcall(0x800370B8u, 5, R[5], R[6], R[7]) | 0xE5000000u);
    } else {
        MEM_W32(pk, 0x05FFFFFFu);
        MEM_W32(pk + 4, 0xE6000000u);
        MEM_W32(pk + 0xC, (col & 0xFFFFFF) | 0x02000000u);
        MEM_W32(pk + 8, dm);
        MEM_W32(pk + 0x10, MEM_R32(r));
        MEM_W32(pk + 0x14, MEM_R32(r + 4));
    }
    gcall(0x80037070u, pk, R[5], R[6], R[7]);
    V0 = 0;
    for (int i = 0; i < 3; i++) R[16 + i] = sv[i];
}

/* 80036D78 (rect, data): the driver's StoreImage: as DriverLoadImage,
 * the copy-from-VRAM command (0xC0), then (once the GPU can send) the odd
 * words read by hand and the 16-word blocks by DMA (channel 2, to RAM). */
void n_libgpu_DriverStoreImage(void) {
    FRAME(0x28);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x24, R[31]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x10, sv[0]);
    R[17] = A0; R[18] = A1;
    gcall(0x800378A8u, R[4], R[5], R[6], R[7]);
    const u32 r = R[17];
    u32 w = MEM_R16(r + 4);
    if ((s16)w < 0) w = 0;
    else if ((s16)GV(u16, 0x80061950u) < (s16)w) w = GV(u16, 0x80061950u);
    MEM_W16(r + 4, (u16)w);
    u32 h = MEM_R16(r + 6);
    if ((s16)h < 0) h = 0;
    else if ((s16)GV(u16, 0x80061952u) < (s16)h) h = GV(u16, 0x80061952u);
    u32 area = (u32)((s32)(s16)MEM_R16(r + 4) * (s32)(s16)h);
    MEM_W16(r + 6, (u16)h);
    u32 a1 = area + 1;
    s32 words = (s32)(a1 + (a1 >> 31)) >> 1;
    R[16] = (u32)((s32)a1 >> 5);
    if (!(words > 0)) { V0 = (u32)-1; goto out; }
    R[20] = R[16];
    R[16] = (u32)words - (R[20] << 4);
#define GP0 GV(u32, 0x80061A50u)
#define GP1 GV(u32, 0x80061A54u)
    if (!(MEM_R32(GP1) & 0x04000000u)) {
        R[19] = 0x04000000u;
        for (;;) {
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; goto out; }
            if (MEM_R32(GP1) & R[19]) break;
            LOOP_POLL(0x80036EA4u);
        }
    }
    MEM_W32(GP1, 0x04000000u);
    MEM_W32(GP0, 0x01000000u);
    MEM_W32(GP0, 0xC0000000u);
    MEM_W32(GP0, MEM_R32(r));
    MEM_W32(GP0, MEM_R32(r + 4));
    if (!(MEM_R32(GP1) & 0x08000000u)) {
        R[17] = 0x08000000u;
        for (;;) {
            if (gcall(0x800378DCu, R[4], R[5], R[6], R[7]) != 0) { V0 = (u32)-1; goto out; }
            if (MEM_R32(GP1) & R[17]) break;
            LOOP_POLL(0x80036F50u);
        }
    }
    R[16]--;
    if (R[16] != 0xFFFFFFFFu) {
        for (;;) {
            u32 v = MEM_R32(GP0);
            R[16]--;
            MEM_W32(R[18], v);
            R[18] += 4;
            if (R[16] == 0xFFFFFFFFu) break;
            LOOP_POLL(0x80036F80u);
        }
    }
    if (R[20] != 0) {
        MEM_W32(GP1, 0x04000003u);
        MEM_W32(GV(u32, 0x80061A58u), R[18]);
        MEM_W32(GV(u32, 0x80061A5Cu), (R[20] << 16) | 0x10);
        MEM_W32(GV(u32, 0x80061A60u), 0x01000200u);
    }
#undef GP0
#undef GP1
    V0 = 0;
out:
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 8003761C (mode): the GPU driver's reset, interrupts masked meanwhile
 * (SetIntrMask): the queue counters (0x80061A74/78) cleared; modes 0 and
 * 5 reset the GPU DMA (0x401, enable bit 0x800), GP1 reset (0) and clear
 * the command copies (0x8006A108, 0x100) and the queue (0x8006A210,
 * 0x1800); 1 and 3 only the DMA, the command buffer (GP1 2) and the
 * queue (GP1 1). Mode 0 then returns 80037A20's answer, else 0. */
void n_libgpu_DriverReset(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[16]);
    MEM_W32(sp + 0x14, R[31]);
    const u32 sv16 = R[16];
    const u32 mode = A0;
    R[16] = mode;
    u32 old = gcall(0x80025984u, 0, R[5], R[6], R[7]);
    GV(u32, 0x80061A78u) = 0;
    GV(u32, 0x80061A84u) = old;
    GV(u32, 0x80061A74u) = GV(u32, 0x80061A78u);
    switch (mode & 7) {
    case 0: case 5:
        MEM_W32(GV(u32, 0x80061A60u), 0x401);
        MEM_W32(GV(u32, 0x80061A70u), MEM_R32(GV(u32, 0x80061A70u)) | 0x800);
        MEM_W32(GV(u32, 0x80061A54u), 0);
        gcall(0x80037F00u, 0x8006A108u, 0, 0x100, R[7]);
        gcall(0x80037F00u, 0x8006A210u, 0, 0x1800, R[7]);
        break;
    case 1: case 3:
        MEM_W32(GV(u32, 0x80061A60u), 0x401);
        MEM_W32(GV(u32, 0x80061A70u), MEM_R32(GV(u32, 0x80061A70u)) | 0x800);
        MEM_W32(GV(u32, 0x80061A54u), 0x02000000u);
        MEM_W32(GV(u32, 0x80061A54u), 0x01000000u);
        break;
    }
    gcall(0x80025984u, GV(u32, 0x80061A84u), R[5], R[6], R[7]);
    V0 = (mode & 7) ? 0 : gcall(0x80037A20u, mode, R[5], R[6], R[7]);
    R[16] = sv16;
}

/* 80034FF8 (mode): ResetGraph: modes 0, 3 (with the version message,
 * 800398C8) and 5: libgpu's state at 0x8006194C cleared, callbacks reset
 * (80025808), the DMA queue set up (800398F8 with the driver's table
 * 0x80061944), the driver reset (its answer, the GPU type, at +0, the
 * VRAM size by type from 0x800619CC / 0x800619D8 at +4 / +6), the draw
 * and display environments (+0x10, +0x6C) set to -1; returns the type.
 * Other modes: (the message if 0x8006194E is 2 or more) the driver's
 * reset entry (+0x34) with 1. */
void n_libgpu_ResetGraph(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 sv16 = R[16], sv17 = R[17];
    const u32 mode = A0;
    R[17] = mode;
    const u32 m = mode & 7;
    if (m == 0 || m == 3 || m == 5) {
        if (m != 5) gcall(0x800398C8u, 0x800108A8u, 0x80061904u, 0x8006194Cu, R[7]);
        const u32 g = 0x8006194Cu;
        R[16] = g;
        gcall(0x80037F00u, g, 0, 0x80, R[7]);
        gcall(0x80025808u, R[4], R[5], R[6], R[7]);
        gcall(0x800398F8u, GV(u32, 0x80061944u) & 0xFFFFFF, R[5], R[6], R[7]);
        u32 t = gcall(0x8003761Cu, mode, R[5], R[6], R[7]);
        MEM_W8(g, (u8)t);
        MEM_W8(g + 1, 1);
        MEM_W16(g + 4, GV(u16, 0x800619CCu + ((u32)MEM_R8(g) << 2)));
        MEM_W16(g + 6, GV(u16, 0x800619D8u + ((u32)MEM_R8(g) << 2)));
        gcall(0x80037F00u, g + 0x10, 0xFFFFFFFFu, 0x5C, R[7]);
        gcall(0x80037F00u, g + 0x6C, 0xFFFFFFFFu, 0x14, R[7]);
        V0 = MEM_R8(g);
    } else {
        if (!(GV(u8, 0x8006194Eu) < 2)) gcall(GV(u32, 0x80061948u), 0x800108C8u, mode, R[6], R[7]);
        gcall(MEM_R32(GV(u32, 0x80061944u) + 0x34), 1, R[5], R[6], R[7]);
    }
    R[16] = sv16; R[17] = sv17;
}
