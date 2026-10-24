/* Controller input (KERNEL, 0x8001189C, 0x80013258-0x80013D7C): the
 * game's pad module on top of Sony's libpad.
 *
 * pad_Init gives libpad two receive buffers (ports 1 and 2, pointers at
 * 0x80062EBC/C0) and two 4-byte actuator buffers (vibration, 0x80062EC4/
 * C8). Every vertical blank pad_Update reads the connection state of
 * both ports and keeps a word per port (0x80062ED0/D4):
 *   bits 0-7   1 << libpad state (6: stable, 2: finding the type...)
 *   0x100      was ever disconnected, 0x200 actuators set up
 *   0x400/0x800 a timed vibration runs on motor 0 / 1
 *   0x1000     analog, 0x3000 DualShock (actuators answer)
 *   bits 15-22 1 << (controller type + 15), from the buffer's byte 1
 * A vibration (pad_Vibrate) is a level and a duration in frames
 * (0x80063C60: two words per port, low half the frames left, 0xFFFF
 * forever); pad_VibrationTick counts them down and stops the motor.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). See kernel.h for the frame conventions. */
#include "kernel.h"

#define k_padBuf1      GV(u32, 0x80062EBC)
#define k_padBuf2      GV(u32, 0x80062EC0)
#define k_padAct1      GV(u32, 0x80062EC4)
#define k_padAct2      GV(u32, 0x80062EC8)
#define K_PAD_STATE1   0x80062ED0u
#define K_PAD_STATE2   0x80062ED4u
#define k_padState(p)  GV(u32, K_PAD_STATE1 + (p) * 4)
#define k_padChangeCb  GV(u32, 0x80062ED8)
#define k_padLost      GV(u8, 0x80062ECC)    /* bit 0 / 4: port 1 / 2 lost */
#define k_padPrevState(p) GV(u32, 0x80062D6C + (p) * 4)   /* last libpad state */
#define K_PAD_VIB      0x80063C60u           /* {level << 16 | frames} per port and motor */
#define K_PAD_VIB_ON   0x80062EB8u           /* per port: vibration allowed */
#define K_PAD_ACT_OFF  0x80062D60u           /* 4 bytes: actuators off */
#define K_PAD_ACT_ALIGN 0x80062D54u          /* PadSetActAlign table */
#define K_PAD_AXES     0x80059684u           /* axes handlers by controller type */
#define k_padTypeBit   GV(u32, 0x80062E38)

#define K_PAD_INIT_DIRECT 0x8003C848u
#define K_PAD_START_COM   0x8003A3A8u
#define K_PAD_SET_ACT     0x8003A7F4u
#define K_PAD_GET_STATE   0x8003A434u
#define K_PAD_READY       0x8003A388u        /* nonzero when the pads were read */
#define K_PAD_INFO_ACT    0x8003A500u        /* (port, 2, 0): actuator count */
#define K_PAD_SET_ALIGN   0x8003A774u        /* PadSetActAlign (port, table) */

/* 8001189C (raw buttons): the buttons through the configuration. With
 * a GunCon (0x200000), 0x80062DD6 set swaps 0x800 and 0x40; otherwise
 * buttons 0x10, 0x40 and 0x80 become the bits the configuration
 * (0x80062DD4: one of the 3-byte entries at 0x80058D20) picks from the
 * table at 0x80058D10. */
void n_input_MapButtons(void) {
    u32 in = A0, out;
    u32 cfg = 0x80058D20u + (u32)(s16)GV(s16, 0x80062DD4) * 3;
    if (k_padTypeBit == 0x200000) {
        out = in & ~0x840u;
        if (GV(s16, 0x80062DD6) == 0) {
            out = in;
        } else {
            if (in & 0x800) out |= 0x40;
            if (in & 0x40) out |= 0x800;
        }
    } else {
        out = in & ~0xE0u;
        if (in & 0x10) out |= GV(u32, 0x80058D10u + (u32)GV(u8, cfg + 0) * 4);
        if (in & 0x40) out |= GV(u32, 0x80058D10u + (u32)GV(u8, cfg + 1) * 4);
        if (in & 0x80) out |= GV(u32, 0x80058D10u + (u32)GV(u8, cfg + 2) * 4);
    }
    V0 = out;
}

/* 80013258: the controller-change callback pad_Init installs (does
 * nothing). */
void n_pad_ChangeNone(void) {}

/* 80013260 (buf1, buf2, act1, act2): libpad set-up: receive buffers,
 * actuator buffers, the state words cleared, vibration allowed on both
 * ports. */
void n_pad_Init(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x20) = R[31];
    u32 b1 = A0, b2 = A1, act1 = A2, act2 = A3;
    R[16] = b1; R[17] = b2; R[18] = act1; R[19] = act2;
    JAL(0x8001328Cu, K_PAD_INIT_DIRECT, A0, A1, A2, A3);
    JAL(0x80013294u, K_PAD_START_COM, A0, A1, A2, A3);
    JAL(0x800132A4u, K_PAD_SET_ACT, 0, act1, 4, A3);
    JAL(0x800132B4u, K_PAD_SET_ACT, 0x10, act2, 4, A3);
    k_padChangeCb = 0x80013258u;
    GV(u32, K_PAD_VIB + 0xC) = 0;
    GV(u32, K_PAD_VIB + 0x8) = 0;
    GV(u32, K_PAD_VIB + 0x4) = 0;
    GV(u32, K_PAD_VIB) = 0;
    k_padState(1) = 0;
    k_padState(0) = 0;
    k_padBuf1 = b1;
    k_padBuf2 = b2;
    k_padAct1 = act1;
    k_padAct2 = act2;
    JAL(0x800132F8u, 0x8001367Cu, 0, A1, A2, A3);
    JAL(0x80013300u, 0x8001367Cu, 1, A1, A2, A3);
    RESTORE_S;
}

/* 8001340C (port 0/0x10, motor): stop a motor: with the actuators set
 * up (0x40), that motor's byte to 0; otherwise the whole buffer to the
 * "off" bytes. */
void n_pad_MotorOff(void) {
    u32 port = A0 & 0xFF;
    u32 act = port ? k_padAct2 : k_padAct1;
    u32 st = port ? K_PAD_STATE2 : K_PAD_STATE1;
    if (GV(u32, st) & 0x40) {
        GV(u8, act + ((A1 & 0xFF) != 0)) = 0;
    } else {
        for (u32 i = 0;; i++) {
            GV(u8, act + i) = GV(u8, K_PAD_ACT_OFF + i);
            if (i + 1 >= 4) break;
            LOOP_POLL(0x80013480u);
        }
    }
    V0 = port;
}

/* 80013490: per frame (from pad_Update): count down the timed
 * vibrations of both ports' motors and stop the ones that run out. */
void n_pad_VibrationTick(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x20) = in_s4;
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x24) = R[31];
    u32 vib = K_PAD_VIB, st = K_PAD_STATE1;
    R[20] = 0xFFFF; R[19] = 0xFFFF0000u;
    for (u32 port = 0;; port++, vib += 8, st += 4) {
        R[16] = vib; R[17] = st; R[18] = port;
        u32 s = GV(u32, st);
        if (s & 0x400) {
            u32 w = GV(u32, vib), n = w & 0xFFFF;
            if (n != 0xFFFF) {
                if (n != 0) {
                    GV(u32, vib) = (w & 0xFFFF0000u) | (n - 1);
                } else {
                    GV(u32, st) = s ^ 0x400;
                    GV(u32, vib) = 0;
                    JAL(0x8001351Cu, 0x8001340Cu, port & 0xFF, 0, A2, A3);
                }
                s = GV(u32, st);
            }
        }
        if (s & 0x800) {
            u32 w = GV(u32, vib + 4), n = w & 0xFFFF;
            if (n != 0xFFFF) {
                if (n != 0) {
                    GV(u32, vib + 4) = (w & 0xFFFF0000u) | (n - 1);
                } else {
                    GV(u32, st) = s ^ 0x800;
                    GV(u32, vib + 4) = 0;
                    JAL(0x80013574u, 0x8001340Cu, port & 0xFF, 1, A2, A3);
                }
            }
        }
        if (port + 1 >= 2) break;
        LOOP_POLL(0x80013580u);
    }
    V0 = 0;
    RESTORE_S;
}

/* 800135A8 (port, motor, frames, level): start a timed vibration (on
 * a DualShock whose vibration is allowed): store it, mark the motor
 * running, and set the motor through the handler for on (level != 0,
 * 0x80062D64) or off (0x80062D68). 0 when there is no such pad. */
void n_pad_Vibrate(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    u32 p = (A0 & 0xFF) != 0, motor = A1, frames = A2, level = A3;
    u32 st = K_PAD_STATE1 + p * 4;
    if (!(GV(u32, st) & 0x3000) || GV(u8, K_PAD_VIB_ON + p) == 0) {
        V0 = 0;
        return;
    }
    u32 vib = K_PAD_VIB + (p ? 8 : 0);
    u32 m = motor & 0xFF;
    GV(u32, vib + m * 4) = ((level & 0xFF) << 16) | (frames & 0xFFFF);
    GV(u32, st) = GV(u32, st) | (m ? 0x800 : 0x400);
    u32 fn = GV(u32, (level & 0xFF) ? 0x80062D64u : 0x80062D68u);
    JAL(0x8001366Cu, fn, p, m, A2, A3);
}

/* 8001367C (port): allow vibration on a port; 80013698: forbid it. */
void n_pad_VibrationOn(void) {
    GV(u8, K_PAD_VIB_ON + ((A0 & 0xFF) != 0)) = 1;
    V0 = 1;
}
void n_pad_VibrationOff(void) {
    A0 = (A0 & 0xFF) != 0;
    GV(u8, K_PAD_VIB_ON + A0) = 0;
    V0 = K_PAD_VIB_ON;
}

/* pad_Update's work for one port (p 0/1; libpad port 0/0x10): the type
 * bit from the receive buffer, the actuators when a DualShock is
 * stable. ra: the return addresses of the original's calls. */
static u32 pad_typeBit(u32 buf) { return 1u << ((((u32)GV(u8, buf + 1) >> 4) + 0xF) & 31); }
static void pad_track(u32 p, u32 lp, u32 st_addr, s32 state, u32 buf, const u32 ra[4]) {
    if (state == (s32)k_padPrevState(p) && (pad_typeBit(buf) & GV(u32, st_addr))) return;
    R[16] = 1;
    if (!(pad_typeBit(buf) & GV(u32, st_addr))) {
        JAL(ra[0], 0x8001340Cu, p, 0, A2, A3);
        JAL(ra[1], 0x8001340Cu, p, 1, A2, A3);
        GV(u32, st_addr) = GV(u32, st_addr) & ~0x3FFu;
    }
    u32 v = (1u << ((u32)state & 31)) | pad_typeBit(buf);
    if (GV(u32, st_addr) & 0x200) v |= 0x200;
    R[16] = v;
    GV(u32, st_addr) = v;
    if (state == 6 || (state == 2 && (v & 0x400000))) {
        R[17] = st_addr;
        u32 n = JAL(ra[2], K_PAD_INFO_ACT, lp, 2, 0, A3);
        GV(u32, st_addr) = n ? (v | 0x3000) : (v | 0x1000);
        if ((state & 6) && !(GV(u32, st_addr) & 0x200)) {
            if (JAL(ra[3], K_PAD_SET_ALIGN, lp, K_PAD_ACT_ALIGN, A2, A3))
                GV(u32, st_addr) = GV(u32, st_addr) | 0x200;
        }
    }
}

/* 800136B0: per frame (vertical-blank callback): when libpad has read
 * the pads, each port's state: a lost pad (state 0) stops its motors
 * and calls the change callback; the type and actuator tracking; then
 * the vibration timers. Returns the lost-pad bits (0x80062ECC). */
void n_pad_Update(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x20) = R[31];
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x10) = in_s0;
    if (JAL(0x800136CCu, K_PAD_READY, A0, A1, A2, A3) != 0) {
        s32 st1 = (s32)JAL(0x800136DCu, K_PAD_GET_STATE, 0, A1, A2, A3);
        R[19] = (u32)st1;
        s32 st2 = (s32)JAL(0x800136E8u, K_PAD_GET_STATE, 0x10, A1, A2, A3);
        R[18] = (u32)st2;
        if (st1 == 0) {
            u32 v = k_padState(0) | 0x100;
            k_padLost = k_padLost | 1;
            k_padState(0) = v;
            k_padState(0) = v & ~0x200u;
            JAL(0x80013720u, 0x8001340Cu, 0, 0, A2, A3);
            JAL(0x8001372Cu, 0x8001340Cu, 0, 1, A2, A3);
            JAL(0x8001373Cu, k_padChangeCb, A0, A1, A2, A3);
        }
        if (st2 == 0) {
            u32 v = k_padState(1) | 0x100;
            k_padLost = k_padLost | 0x10;
            k_padState(1) = v;
            k_padState(1) = v & ~0x200u;
            JAL(0x80013770u, 0x8001340Cu, 1, 0, A2, A3);
            JAL(0x8001377Cu, 0x8001340Cu, 1, 1, A2, A3);
            JAL(0x8001378Cu, k_padChangeCb, A0, A1, A2, A3);
        }
        static const u32 ra1[4] = {0x800137F8u, 0x80013804u, 0x80013878u, 0x800138B4u};
        static const u32 ra2[4] = {0x80013938u, 0x80013944u, 0x800139B8u, 0x800139F4u};
        pad_track(0, 0, K_PAD_STATE1, st1, k_padBuf1, ra1);
        pad_track(1, 0x10, K_PAD_STATE2, st2, k_padBuf2, ra2);
        k_padPrevState(0) = (u32)st1;
        k_padPrevState(1) = (u32)st2;
        JAL(0x80013A1Cu, 0x80013490u, A0, A1, A2, A3);
    }
    V0 = k_padLost;
    RESTORE_S;
}

/* 80013A5C: both ports' buttons (active low; 0xFFFF when nothing is
 * read), with an analog pad's (0x20000) stick giving buttons too
 * (0x800000: 0x40, 0x8000: 0x80, 0x80: 0x84). Returns the pressed
 * buttons: port 1 in the low half, port 2 in the high. */
void n_pad_GetButtons(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x10) = in_s0;
    u32 b = k_padBuf1, s1 = 0xFFFF, s0 = 0xFFFF;
    if (GV(u8, b) == 0) s1 = GV(u8, b + 3) | ((u32)GV(u8, b + 2) << 8);
    R[17] = s1;
    b = k_padBuf2;
    if (GV(u8, b) == 0) s0 = GV(u8, b + 3) | ((u32)GV(u8, b + 2) << 8);
    R[16] = s0;
    for (int p = 0; p < 2; p++) {
        if (!(k_padState(p) & 0x20000)) continue;
        u32 ax = JAL(p ? 0x80013B30u : 0x80013AD4u, 0x80013D18u, p, A1, A2, A3);
        u32 m = ((ax & 0x800000) != 0) << 6;
        if (ax & 0x8000) m |= 0x80;
        u32 v = (ax & 0x80) ? ~(m | 4) : ~m;
        if (p) { s0 = (s0 & 0xFFFF) | v; R[16] = s0; }
        else { s1 = (s1 & 0xFFFF) | v; R[17] = s1; }
    }
    V0 = ~((s1 & 0xFFFF) | (s0 << 16));
    RESTORE_S;
}

/* 80013B90 (port): the port's controller type bit (0x7F8000 of its
 * state word). */
void n_pad_TypeBit(void) {
    V0 = GV(u32, (A0 & 0xFF) ? K_PAD_STATE2 : K_PAD_STATE1) & 0x7F8000;
}

/* 80013C30: axes of a digital pad: none. */
void n_pad_AxesNone(void) { V0 = 0; }

/* 80013D18 (port 0/0x10): the port's analog data through the handler
 * for its controller type (-1 for another port). */
void n_pad_GetAxes(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    u32 port = A0 & 0xFF, buf;
    if (port == 0) buf = k_padBuf1;
    else if (port == 0x10) buf = k_padBuf2;
    else { V0 = 0xFFFFFFFFu; return; }
    u32 fn = GV(u32, K_PAD_AXES + ((GV(u8, buf + 1) >> 2) & 0x1C));
    JAL(0x80013D6Cu, fn, buf, A1, A2, A3);
}
