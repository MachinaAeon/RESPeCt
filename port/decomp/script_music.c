/* Event script music (TEST overlay): the script instructions that change
 * the music (opcodes 0x63..0x65), and the per-frame volume ramp.
 *
 * The ramp state at 0x800BB770: +0 on, +4 frames left, +8 total frames,
 * +0xC full volume. Each frame (from the frame callback, 0x8006F1E4) the
 * master volume follows the ramp: up over the first 16 frames, down over
 * the last 16. The sound library calls (0x80014B14 .. 0x80015D44) are
 * waited on with VSync until they accept.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"
#include "poll.h"

void n_script_MusicStart(void);
void n_script_MusicLoad(void);
void n_script_MusicStop(void);
void n_music_VolumeRamp(void);

#define RAMP            0x800BB770u
#define g_rampOn        GV(u32, RAMP)
#define g_soundVolume   GV(s16, 0x80062DB6)   /* option: 0..100 */
#define g_volumeScale   GV(u32, 0x80062E20)   /* nonzero: scale every channel */
#define F_SND_BUSY      0x80014B14u   /* (mask): nonzero while busy */
#define F_SND_READY     0x80015AE8u   /* (0): negative while busy */
#define F_SND_OPEN      0x80015840u   /* (1, id) */
#define F_SND_MVOL      0x80015484u   /* (l, r, l2, r2): master volume */
#define F_SND_PLAY      0x8001569Cu   /* (track) */
#define F_SND_LOAD      0x80015D44u   /* (buffer, 1, file) */

/* 8008C3E4 (thread): {op, s16 track, u8 id, _, s16 frames}: start a music
 * track, ramping it up over frames. */
void n_script_MusicStart(void) {
    FRAME(0x20);
    u32 t = A0;
    for (;;) {
        if ((s32)GCALL1(F_SND_READY, 0) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C410);
    }
    for (;;) {
        if ((s32)GCALL2(F_SND_OPEN, 1, GV(u8, GV(u32, t) + 4)) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C43C);
    }
    GCALL4(F_SND_MVOL, 0, 0, 0, 0);
    for (;;) {
        u32 track = GV(u32, 0x800A3128u + (u32)((s32)GV(s16, GV(u32, t) + 2) * 4));
        if ((s32)GCALL1(F_SND_PLAY, track) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C490);
    }
    s32 frames = GV(s16, GV(u32, t) + 6);
    g_rampOn = 1;
    GV(s32, RAMP + 8) = frames;
    GV(s32, RAMP + 4) = frames;
    GV(u32, t) += 8;
    V0 = 0;
}

/* 8008C4E0 (thread): {op, s16 file}: load a music file into the sound
 * buffer (0x800B3D54) at the option volume, ramp off. */
void n_script_MusicLoad(void) {
    FRAME(0x20);
    u32 t = A0;
    for (;;) {
        if (GCALL1(F_SND_BUSY, 0x40) == 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C510);
    }
    for (;;) {
        if ((s32)GCALL1(F_SND_READY, 0) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C530);
    }
    for (;;) {
        u32 file = GV(u32, 0x800A30BCu + (u32)((s32)GV(s16, GV(u32, t) + 2) * 4));
        if ((s32)GCALL3(F_SND_LOAD, GV(u32, 0x800B3D54), 1, file) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C574);
    }
    for (;;) {
        if (GCALL1(F_SND_BUSY, 0x40) == 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C594);
    }
    s32 vol = (s32)((u32)(s32)g_soundVolume * 255u) / 100;
    g_rampOn = 0;
    GV(s32, RAMP + 0xC) = vol;
    GV(u32, t) += 4;
    V0 = 2;
}

/* 8008C600 (thread): {op}: stop the music: with a ramp running, cut it to
 * its last 8 frames and wait; else once the library is ready, go on. */
void n_script_MusicStop(void) {
    FRAME(0x18);
    u32 t = A0;
    if (g_rampOn) {
        if (GV(u32, RAMP + 4) >= 8) GV(u32, RAMP + 4) = 7;
        V0 = 1;
        return;
    }
    for (;;) {
        if ((s32)GCALL1(F_SND_READY, 0) >= 0) break;
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8008C658);
    }
    GV(u32, t) += 2;
    V0 = 2;
}

/* Set the master volume for ramp level v (0..full). */
static void ramp_volume(u32 ra, u32 v) {
    if (g_volumeScale == 0) {
        u32 a = (u32)(s32)(s16)v;
        gcall_ra(ra, F_SND_MVOL, a, 0, a, 0);
    } else {
        u32 hi = (u32)(((uint64_t)(v << 7) * 0x80808081u) >> 32);
        u32 a = (u32)((s32)(hi << 9) >> 16);
        gcall_ra(ra, F_SND_MVOL, a, a, a, a);
    }
}

/* 8008C680: the volume ramp, once a frame. It runs from the frame
 * callback, on that stack in compared RAM, so it keeps the original's
 * frame ($ra saved) and calls with the original's return addresses. */
void n_music_VolumeRamp(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (g_rampOn == 0) { V0 = 0x800C0000u; return; }
    u32 left = GV(u32, RAMP + 4), total = GV(u32, RAMP + 8);
    u32 full = GV(u32, RAMP + 0xC);
    if (left < 0x10) {
        u32 v = (left * (full << 4)) / 255;
        if (full < v) v = full;
        ramp_volume(0x8008C738u, v);
        if (GV(u32, RAMP + 4) == 0) {
            g_rampOn = 0;
            V0 = 0;
            return;
        }
    } else {
        u32 done = total - left;
        if (done < 0x10) {
            u32 v = (done * (full << 4)) / 255;
            if (full < v) v = full;
            ramp_volume(0x8008C7E4u, v);
        }
    }
    GV(u32, RAMP + 4) -= 1;
    V0 = GV(u32, RAMP + 4);
}
