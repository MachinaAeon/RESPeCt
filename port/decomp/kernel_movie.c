/* Movie playback (KERNEL, 0x80013DDC-0x80014954): the game's FMV player
 * on Sony's libpress (MDEC) and the libcd streaming library.
 *
 * The player's state is at 0x80064480: +0xC/+0x10 the two decoded
 * image buffers, +0x14 the one being filled, +0x18 (x, y) of the two
 * display areas (8 bytes each), +0x28 the area shown, +0x2C the rectangle
 * the next slice goes to (x, y, w, h), +0x32 the frame height, +0x34 set
 * when a frame is complete. 0x80064591: 24-bit mode. The display and
 * draw environments are at 0x80064580 (2 x 0x14) and 0x800644C0
 * (2 x 0x5C).
 *
 * The MDEC callback (movie_SliceDone) runs from the interrupt handler,
 * on its stack in compared RAM: see kernel.h for the frame conventions.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "kernel.h"

#define K_MOVIE        0x80064480u
#define k_movie24      GV(u8, 0x80064591)
#define K_DRAW_ENVS    0x800644C0u      /* 2 x 0x5C */
#define K_DISP_ENVS    0x80064580u      /* 2 x 0x14 */

#define K_SET_DEF_DRAW_ENV 0x80038088u
#define K_SET_DEF_DISP_ENV 0x80038148u
#define K_GET_VIDEO_MODE   0x8002629Cu
#define K_DEC_RESET        0x80038488u
#define K_DEC_OUT          0x8003865Cu    /* DecDCTout (buffer, words) */
#define K_DEC_IN           0x80038B38u    /* DecDCTin (data, mode) */
#define K_DEC_VLC_SIZE     0x80038718u
#define K_DEC_VLC          0x80038F78u
#define K_CD_CONTROL       0x800265E4u
#define K_ST_SET_RING      0x800262B8u
#define K_ST_SET_STREAM    0x800290B8u
#define K_ST_GET_NEXT      0x80029238u
#define K_ST_FREE_RING     0x80029148u
#define K_ST_UNSET_RING    0x80029318u

/* 80013DDC (w, h, areas, interlace, -, color bit, r, g, b, buffer): the
 * screen for a movie: two draw / display environments over the two
 * areas (areas: (x, y) of each, s16), the screen offset (0x80062E18/1A,
 * 24 lines lower on PAL), the areas kept at 0x80062F38; for buffer 0 or
 * 1, that buffer's area cleared to the colour (1.5 times as wide in
 * 24-bit mode) and its environments put. */
void n_movie_SetScreen(void) {
    FRAME(0x48);
    SAVE_S;
    GS(u32, sp + 0x2C) = in_s3;
    GS(u32, sp + 0x30) = in_s4;
    GS(u32, sp + 0x40) = in_fp;
    GS(u32, sp + 0x3C) = in_s7;
    GS(u32, sp + 0x28) = in_s2;
    GS(u32, sp + 0x34) = in_s5;
    GS(u32, sp + 0x44) = R[31];
    GS(u32, sp + 0x38) = in_s6;
    GS(u32, sp + 0x24) = in_s1;
    GS(u32, sp + 0x20) = in_s0;
    u32 r = GS(u32, sp + 0x60), g = GS(u32, sp + 0x64), b = GS(u32, sp + 0x68);
    u32 w = A0, areas = A2, il = A3;
    R[19] = r; R[20] = g; R[30] = w; R[23] = areas; R[18] = il; R[21] = b;
    GS(u32, sp + 0x4C) = A1;
    JAL(0x80013E2Cu, K_RESET_GRAPH, 1, A1, A2, A3);
    u32 d0 = K_DRAW_ENVS, d1 = K_DRAW_ENVS + 0x5C, e = K_DISP_ENVS;
    R[16] = d0;
    GS(u32, sp + 0x10) = GS(u32, sp + 0x4C);
    JAL(0x80013E50u, K_SET_DEF_DRAW_ENV, d0, (s32)GS(s16, areas + 0), (s32)GS(s16, areas + 2), w);
    R[22] = e;
    GS(u32, sp + 0x10) = GS(u32, sp + 0x4C);
    JAL(0x80013E74u, K_SET_DEF_DISP_ENV, e, (s32)GS(s16, areas + 4), (s32)GS(s16, areas + 6), w);
    R[17] = d1;
    GS(u32, sp + 0x10) = GS(u32, sp + 0x4C);
    JAL(0x80013E94u, K_SET_DEF_DRAW_ENV, d1, (s32)GS(s16, areas + 4), (s32)GS(s16, areas + 6), w);
    GS(u32, sp + 0x10) = GS(u32, sp + 0x4C);
    JAL(0x80013EB0u, K_SET_DEF_DISP_ENV, e + 0x14, (s32)GS(s16, areas + 0), (s32)GS(s16, areas + 2), w);
    GV(u8, d0 + 0x74) = 0;
    GV(u8, d0 + 0x18) = 0;
    u16 ox = GV(u16, 0x80062E18), oy = GV(u16, 0x80062E1A);
    il &= 1;
    R[18] = il;
    GV(u8, e + 0x24) = (u8)il;
    GV(u8, e + 0x10) = (u8)il;
    GV(u8, e + 0x25) = GS(u8, sp + 0x5C);
    GV(u8, e + 0x11) = GS(u8, sp + 0x5C);
    GV(u8, d0 + 0x19) = (u8)r;
    GV(u8, d0 + 0x1A) = (u8)g;
    GV(u8, d0 + 0x1B) = (u8)b;
    GV(u8, d1 + 0x19) = (u8)r;
    GV(u8, d1 + 0x1A) = (u8)g;
    GV(u8, d1 + 0x1B) = (u8)b;
    GV(u16, e + 0x1C) = ox;
    GV(u16, e + 0x08) = ox;
    GV(u16, e + 0x1E) = oy;
    GV(u16, e + 0x0A) = oy;
    if (JAL(0x80013F18u, K_GET_VIDEO_MODE, A0, A1, A2, A3) == 1) {
        GV(u16, e + 0x0A) = (u16)(GV(u16, e + 0x0A) + 0x18);
        GV(u16, e + 0x1E) = (u16)(GV(u16, e + 0x1E) + 0x18);
    }
    for (int i = 0; i < 8; i++) GV(u8, 0x80062F38 + i) = GS(u8, areas + i);
    s32 buf = GS(s32, sp + 0x6C);
    if (buf >= 2) { V0 = 0; RESTORE_S; return; }
    u32 rect = sp + 0x18;
    u32 ax = buf ? 0 : 4;                 /* buffer 1 clears area 0, 0 area 1 */
    if (GS(u32, sp + 0x5C)) {
        s32 x = (s16)GS(s16, areas + ax) * 3, ww = (s32)w * 3;
        GS(u16, rect + 0) = (u16)(x / 2);
        GS(u16, rect + 4) = (u16)(ww / 2);
    } else {
        GS(u16, rect + 0) = GS(u16, areas + ax);
        GS(u16, rect + 4) = (u16)w;
    }
    GS(u16, rect + 6) = GS(u16, sp + 0x4C);
    GS(u16, rect + 2) = GS(u16, areas + ax + 2);
    JAL(0x8001409Cu, K_CLEAR_IMAGE, rect, r & 0xFF, g & 0xFF, b & 0xFF);
    JAL(0x800140A4u, K_DRAW_SYNC, 0, A1, A2, A3);
    JAL(0x800140ACu, K_VSYNC, 0, A1, A2, A3);
    buf = GS(s32, sp + 0x6C);
    JAL(0x800140CCu, K_PUT_DISP_ENV, K_DISP_ENVS + (u32)buf * 0x14, A1, A2, A3);
    buf = GS(s32, sp + 0x6C);
    JAL(0x800140F4u, K_PUT_DRAW_ENV, K_DRAW_ENVS + (u32)buf * 0x5C, A1, A2, A3);
    RESTORE_S;
}

/* 80014124 (value): set 0x80062F40, returning the old value. */
void n_movie_SetFlag(void) {
    V0 = GV(u32, 0x80062F40);
    GV(u32, 0x80062F40) = A0;
}

/* 800144BC (mode, stream): start a movie's stream: reset the MDEC,
 * set its mode, the ring buffer (stream +0x1C, +0x18), the stream
 * (channel 0x80064591... end frame +8), then seek (CdlSetloc to +0)
 * and start reading (0x80028B88 with +4) until it works, up to 2000
 * tries. 1 when it started, else 0. */
void n_movie_StartStream(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x18) = in_s0;
    GS(u32, sp + 0x1C) = in_s1;
    GS(u32, sp + 0x20) = R[31];
    u32 mode = A0, st = A1;
    R[16] = mode; R[17] = st;
    JAL(0x800144DCu, K_DEC_RESET, 1, A1, A2, A3);
    JAL(0x800144E4u, K_DEC_VLC_SIZE, mode, A1, A2, A3);
    R[16] = 0x7D0;
    JAL(0x800144F4u, K_ST_SET_RING, GV(u32, st + 0x1C), GV(u32, st + 0x18), A2, A3);
    GS(u32, sp + 0x10) = 0;
    JAL(0x80014510u, K_ST_SET_STREAM, GV(u8, 0x80064591), GV(u32, st + 8), 0xFFFFFFFFu, 0);
    GV(u32, st + 0x34) = 0;
    u32 tries = 0x7D0;
    for (;;) {
        if (JAL(0x80014524u, K_CD_CONTROL, 2, GV(u32, st), 0, A3) != 0) {
            if (JAL(0x80014538u, 0x80028B88u, GV(u32, st + 4), A1, A2, A3) != 0) { V0 = 1; break; }
            LOOP_POLL(0x80014538u);
            continue;
        }
        tries--;
        R[16] = tries;
        if (tries == 0xFFFFFFFFu) { V0 = 0; break; }
        LOOP_POLL(0x8001454Cu);
    }
    RESTORE_S;
}

/* 8001456C: the MDEC callback (a slice decoded): end the stream when
 * asked (0x8006591C), swap the decode buffers, step the slice rectangle
 * right (the first step of a frame by the remainder of the width when
 * 0x80062F30), start the next slice or, at the frame's end, show it
 * (+0x34) and set the rectangle up for the other area; then send the
 * slice just decoded to VRAM. */
void n_movie_SliceDone(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x24) = R[31];
    GS(u32, sp + 0x20) = in_s0;
    u32 m = K_MOVIE;
    if (k_movie24 && GV(u32, 0x8006591C)) {
        JAL(0x800145A0u, K_ST_UNSET_RING, A0, A1, A2, A3);
        GV(u32, 0x8006591C) = 0;
    }
    for (int i = 0; i < 8; i++) GS(u8, sp + 0x10 + i) = GV(u8, m + 0x2C + i);
    u32 cur = GV(u32, m + 0x14);
    R[16] = cur;
    GV(u32, m + 0x14) = cur ^ 1;
    int stepped = 0;
    if (GV(u32, 0x80062F30)) {
        s32 r = mips_rem(GV(s16, m + GV(u32, m + 0x28) * 8 + 0x1C), GV(s16, m + 0x30));
        if (r != 0) {
            GV(u32, 0x80062F30) = 0;
            GV(u16, m + 0x2C) = (u16)(GV(u16, m + 0x2C) + r);
            stepped = 1;
        }
    }
    if (!stepped) GV(u16, m + 0x2C) = (u16)(GV(u16, m + 0x2C) + GV(u16, m + 0x30));
    u32 area = GV(u32, m + 0x28);
    s32 end = GV(s16, m + area * 8 + 0x18) + GV(s16, m + area * 8 + 0x1C);
    if (GV(s16, m + 0x2C) < end) {
        s32 h = GV(s16, m + 0x32);
        s32 hh = h - 1;
        if (hh < 0) hh = h + 0xE;
        s32 words = mul32(GV(s16, m + 0x30) << 4, (hh >> 4) + 1);
        JAL(0x800146DCu, K_DEC_OUT, GV(u32, m + GV(u32, m + 0x14) * 4 + 0xC), words >> 1, A2, A3);
    } else {
        u32 na = area ^ 1;
        GV(u32, m + 0x28) = na;
        GV(u32, m + 0x34) = 1;
        GV(u16, m + 0x2C) = GV(u16, m + na * 8 + 0x18);
        GV(u32, 0x80062F30) = 1;
        GV(u16, m + 0x2E) = GV(u16, m + na * 8 + 0x1A);
    }
    JAL(0x8001472Cu, K_LOAD_IMAGE, sp + 0x10, GV(u32, m + cur * 4 + 0xC), A2, A3);
    RESTORE_S;
}

/* 8001473C (decode state, stream): the next frame: wait for its data
 * (up to 2000 tries; 1 when none came), decode its VLC into the other
 * buffer (the largest size in stream +0x34), start the MDEC on it (mode
 * stream +0x20) and free the data. 0 when started. */
void n_movie_DecodeNext(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x20) = R[31];
    GS(u32, sp + 0x14) = in_s1;
    u32 dec = A0, st = A1;
    R[18] = dec; R[19] = st;
    u32 tries = 0x7D0, data;
    R[16] = tries;
    for (;;) {
        data = JAL(0x8001476Cu, 0x80014808u, dec, st, A2, A3);
        R[17] = data;
        tries--;
        R[16] = tries;
        if (data != 0) break;
        if (tries == 0) { V0 = 1; RESTORE_S; return; }
        LOOP_POLL(0x80014778u);
    }
    u32 b = GV(u32, dec + 8) ^ 1;
    GV(u32, dec + 8) = b;
    s32 n = (s32)JAL(0x800147ACu, K_DEC_VLC, GV(u32, dec + b * 4), A1, A2, A3);
    if (GV(s32, st + 0x34) < n) GV(s32, st + 0x34) = n;
    JAL(0x800147E0u, K_DEC_IN, data, GV(u32, dec + GV(u32, dec + 8) * 4), GV(u32, st + 0x20), A3);
    JAL(0x800147E8u, K_ST_FREE_RING, data, A1, A2, A3);
    V0 = 0;
    RESTORE_S;
}

/* 80014808 (movie state, stream): the next frame's data from the ring
 * buffer (up to 2000 tries; 0 when none): the frame number past the
 * stream's last (+0xC) sets 0x80062F34; the frame's size (width 1.5
 * times in 24-bit mode) into the state's rectangles. Returns the data. */
void n_movie_GetFrame(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x1C) = in_s1;
    GS(u32, sp + 0x20) = in_s2;
    GS(u32, sp + 0x18) = in_s0;
    GS(u32, sp + 0x24) = R[31];
    u32 ms = A0, st = A1;
    R[17] = ms; R[18] = st;
    u32 tries = 0x7D0;
    R[16] = tries;
    for (;;) {
        u32 r = JAL(0x80014834u, K_ST_GET_NEXT, sp + 0x10, sp + 0x14, A2, A3);
        tries--;
        R[16] = tries;
        if (r == 0) break;
        if (tries == 0) { V0 = 0; RESTORE_S; return; }
        LOOP_POLL(0x8001483Cu);
    }
    u32 hdr = GS(u32, sp + 0x14);
    if (!(GV(u32, hdr + 8) < GV(u32, st + 0xC))) GV(u32, 0x80062F34) = 1;
    u32 w = GV(u16, hdr + 0x10);
    if (k_movie24) w = (w * 3) >> 1;
    u32 data = GS(u32, sp + 0x10);
    hdr = GS(u32, sp + 0x14);
    GV(u16, ms + 0x24) = (u16)w;
    GV(u16, ms + 0x1C) = (u16)w;
    u16 h = GV(u16, hdr + 0x12);
    GV(u16, ms + 0x26) = h;
    GV(u16, ms + 0x1E) = h;
    GV(u16, ms + 0x32) = GV(u16, hdr + 0x12);
    V0 = data;
    RESTORE_S;
}

/* 800148E4 (movie state): wait for the frame to be complete (+0x34,
 * set by the MDEC callback); after 0x7FFFF tries, take it as complete
 * and switch areas. Clears the flag. */
void n_movie_WaitFrame(void) {
    u32 m = A0;
    V0 = GV(u32, m + 0x34);
    if (V0 == 0) {
        u32 n = 0x7FFFF;
        for (;;) {
            if (n == 0) {
                GV(u32, m + 0x34) = 1;
                u32 a = GV(u32, m + 0x28) ^ 1;
                GV(u32, m + 0x28) = a;
                u16 x = GV(u16, m + a * 8 + 0x18);
                a = GV(u32, m + 0x28);
                GV(u16, m + 0x2C) = x;
                GV(u16, m + 0x2E) = GV(u16, m + a * 8 + 0x1A);
            }
            V0 = GV(u32, m + 0x34);
            n--;
            if (V0 != 0) break;
            LOOP_POLL(0x80014948u);
        }
    }
    GV(u32, m + 0x34) = 0;
}
