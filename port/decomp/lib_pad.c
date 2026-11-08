/* libpad's serial exchange with the controllers (kernel, 0x8003AFDC-
 * 0x8003B5D8, 0x8003DAF8-0x8003DBB4): run from the vertical blank
 * interrupt, a byte at a time, for both ports every frame.
 *
 *   0x80062AD8  pointer to the SIO registers (0x1F801040: +0 data, +4
 *               status, +0xA control, +0xE baud)
 *   0x80062AD4  pointer to the interrupt status register (bit 7: SIO)
 *   0x80062AB8  the exchange's step; 0x80062AFC its table of step
 *               functions; 0x80062A74 the error callback
 *   0x8006C020  timer 2's count when the current timeout started
 *   0x8006C024  the timeout, in timer 2 units (/8 unless timer 2 runs
 *               at the system clock)
 * A port record has the receive buffer pointer at +0x3C, the send buffer
 * at +0x40, the byte count at +0x44 and the exchange count at +0x45.
 *
 * The waits poll the emulated hardware exactly where the original's loops
 * branch back, so time passes as it did under the original.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libpad_TimerStart(void);
void n_libpad_TimedOut(void);
void n_libpad_WaitAck(void);
void n_libpad_Step(void);
void n_libpad_SendReady(void);
void n_libpad_Send(void);
void n_libpad_ActByte(void);
void n_libpad_ActPower(void);
void n_libpad_Update(void);
void n_libpad_WaitTx(void);
void n_libpad_Start(void);
void n_libpad_Exchange(void);
void n_libpad_LatchMode(void);
void n_libpad_PortBuf(void);
void n_libpad_StepBegin(void);
void n_libpad_StepPoll(void);
void n_libpad_StepExtra(void);
void n_libpad_StepIdent(void);
void n_libpad_NextPort(void);
void n_libpad_GetState(void);
void n_libpad_StepAnswer(void);
void n_libpad_TakeFlag(void);
void n_libpad_TakeFlagW(void);
void n_libpad_SetCommand(void);
void n_libpad_EndCommand(void);
void n_libpad_IrqCheck(void);
void n_libpad_ResetPort(void);
void n_libpad_StepCommand(void);
void n_libpad_Tick(void);
void n_libpad_Close(void);
void n_libpad_InitDirect(void);
void n_libpad_Stop(void);

#define SIO        GV(u32, 0x80062AD8u)
#define ISTAT      GV(u32, 0x80062AD4u)
#define STEP       GV(u32, 0x80062AB8u)
#define T_START    GV(u32, 0x8006C020u)
#define T_LIMIT    GV(u32, 0x8006C024u)
#define TIMER2     0x1F801120u

static void timer_start(u32 limit) {
    u32 now = MEM_R16(TIMER2);
    T_LIMIT = limit;
    T_START = now;
}

/* Whether the timeout started by timer_start has run out (the count
 * wraps at the target, or at 0x10000 when there is none). */
static u32 timed_out(void) {
    u32 v = MEM_R16(TIMER2);
    if (v < T_START) {
        if (MEM_R16(TIMER2 + 8) != 0) v += MEM_R16(TIMER2 + 8);
        else v += 0x10000;
    }
    u32 d = v - T_START;
    if (!(MEM_R16(TIMER2 + 4) & 0x200)) d >>= 3;
    return !(d < T_LIMIT);
}

/* Acknowledge the SIO interrupt and wait (until the timeout) for the
 * controller's acknowledge line to drop; then reset the SIO's interrupt
 * flag. 0 if it timed out. */
static u32 wait_ack(void) {
    MEM_W32(ISTAT, 0xFFFFFF7Fu);
    if (MEM_R16(SIO + 4) & 0x80) {
        for (;;) {
            if (timed_out()) return 0;
            if (!(MEM_R16(SIO + 4) & 0x80)) break;
            LOOP_POLL(0x8003B5B0u);
        }
    }
    MEM_W16(SIO + 0xA, MEM_R16(SIO + 0xA) | 0x10);
    return 1;
}

/* The baud divider for the next byte: 0x22 once a controller in mode 8
 * has sent nine bytes, 0x88 otherwise. */
static u32 baud(u32 port) {
    if ((MEM_R8(MEM_R32(port + 0x3C)) >> 4) == 8 && MEM_R8(port + 0x44) >= 9) return 0x22;
    return 0x88;
}

/* 8003DAF8 (limit): start a timeout. */
void n_libpad_TimerStart(void) { timer_start(A0); }

/* 8003DB18: 1 if the timeout has run out. */
void n_libpad_TimedOut(void) { V0 = timed_out(); }

/* 8003B554: wait_ack. */
void n_libpad_WaitAck(void) { V0 = wait_ack(); }

/* 8003AFDC (port): run the exchange's next step. A step that fails (< 0)
 * goes to the error callback; otherwise, unless the controller has more
 * to say (mode 3 with an 0x80 header), wait for its acknowledge, an error
 * -3 if it doesn't come. From step 5 on the step is held back by one. */
void n_libpad_Step(void) {
    FRAME(0x18);
    u32 port = A0;
    u32 step = STEP;
    u32 fn = GV(u32, 0x80062AFCu + step * 4);
    STEP = step + 1;
    s32 r = (s32)gcall(fn, port, 0x80062AB8u, R[6], R[7]);
    if (r < 0) {
        gcall(GV(u32, 0x80062A74u), (u32)r, R[5], R[6], R[7]);
        return;
    }
    step = STEP;
    if (step != 0) {
        if (step != 3 || MEM_R8(MEM_R32(port + 0x3C)) != 0x80) {
            timer_start(0x3C);
            if (!wait_ack()) gcall(GV(u32, 0x80062A74u), 0xFFFFFFFDu, R[5], R[6], R[7]);
        }
        step = STEP;
    }
    if ((s32)step >= 5) STEP = step - 1;
    V0 = step - 1;
}

/* 8003B2DC (port, byte): once the transmitter is ready, send a byte and
 * keep the one received (the
 * receive buffer holds them from the second on, the first at count
 * 0xFF): wait for the transmitter, then for the controller's acknowledge
 * (-2 on a timeout). Returns the byte received. */
void n_libpad_SendReady(void) {
    u32 port = A0, out = A1;
    u32 bd = baud(port);
    u32 sio = SIO;
    while (!(MEM_R16(sio + 4) & 2)) LOOP_POLL(0x8003B348u);
    timer_start(0x190);
    sio = SIO;
    u32 in = MEM_R8(sio) & 0xFF;
    if (MEM_R8(port + 0x44) != 0 || (in >> 4) != 8) MEM_W16(sio + 0xE, bd);
    else MEM_W16(sio + 0xE, 0x22);
    u32 is = ISTAT;
    if (!(MEM_R32(is) & 0x80)) {
        for (;;) {
            if (timed_out()) { V0 = 0xFFFFFFFEu; return; }
            if (MEM_R32(is) & 0x80) break;
            LOOP_POLL(0x8003B464u);
        }
    }
    if (MEM_R8(port + 0xE8) != 8 && STEP == 2) {
        timer_start(0x3C);
        while (!timed_out()) LOOP_POLL(0x8003B4A0u);
    }
    MEM_W8(SIO, out);
    if (STEP == 3 && in == 0x80) {
        MEM_W32(ISTAT, 0xFFFFFF7Fu);
        sio = SIO;
        MEM_W16(sio + 0xA, MEM_R16(sio + 0xA) | 0x10);
    }
    MEM_W8(port + 0x45, MEM_R8(port + 0x45) + 1);
    if (MEM_R8(port + 0x44) != 0xFF) MEM_W8(MEM_R32(port + 0x3C) + MEM_R8(port + 0x44), in);
    MEM_W8(port + 0x44, MEM_R8(port + 0x44) + 1);
    V0 = in;
}

/* 8003B0CC (port, byte): send a byte and keep the one received. A
 * negative byte starts an exchange instead: the complement goes in the
 * send buffer and, after the timeout runs out, to the SIO. -20 if the
 * controller doesn't acknowledge in time. Returns the byte received. */
void n_libpad_Send(void) {
    u32 port = A0, out = A1;
    if ((s32)out < 0) {
        u32 buf = MEM_R32(port + 0x40);
        u32 in = MEM_R8(SIO) & 0xFF;
        MEM_W8(port + 0x44, 0xFF);
        MEM_W8(port + 0x45, 1);
        MEM_W8(buf, ~out);
        u32 sio = SIO;
        if (!(MEM_R16(sio + 4) & 1))
            while (!(MEM_R16(sio + 4) & 1)) LOOP_POLL(0x8003B144u);
        while (!timed_out()) LOOP_POLL(0x8003B154u);
        MEM_W8(SIO, ~out);
        V0 = in;
        return;
    }
    u32 bd = baud(port);
    u32 sio = SIO;
    u32 now = MEM_R16(TIMER2);
    u32 st = MEM_R16(sio + 4);
    T_LIMIT = 0x1AE;
    T_START = now;
    if (!(st & 2))
        while (!(MEM_R16(sio + 4) & 2)) LOOP_POLL(0x8003B1ECu);
    sio = SIO;
    u32 is = ISTAT;
    u32 in = MEM_R8(sio) & 0xFF;
    MEM_W16(sio + 0xE, bd);
    if (!(MEM_R32(is) & 0x80)) {
        for (;;) {
            if (timed_out()) { V0 = 0xFFFFFFECu; return; }
            if (MEM_R32(ISTAT) & 0x80) break;
            LOOP_POLL(0x8003B248u);
        }
    }
    MEM_W8(SIO, out);
    if (bd == 0x22) {
        MEM_W32(ISTAT, 0xFFFFFF7Fu);
        sio = SIO;
        MEM_W16(sio + 0xA, MEM_R16(sio + 0xA) | 0x10);
    }
    MEM_W8(port + 0x45, MEM_R8(port + 0x45) + 1);
    MEM_W8(MEM_R32(port + 0x3C) + MEM_R8(port + 0x44), in);
    MEM_W8(port + 0x44, MEM_R8(port + 0x44) + 1);
    V0 = in;
}

/* 8003CB14 (port): the byte to send at this point of the exchange (byte
 * count +0x45, less the 3-byte header): in mode 0 an actuator's level
 * (+0x28's bytes, while the actuator is on in +0x57..); in mode 0x4D
 * (+0x37) the command buffer's (+0x2C, 0xFF past its end); otherwise the
 * command buffer's, 0 past its end. */
void n_libpad_ActByte(void) {
    u32 p = A0;
    s32 i = (s32)MEM_R8(p + 0x45) - 3;
    u32 mode = MEM_R8(p + 0x37);
    if (mode == 0) {
        if (i < 6 && MEM_R8(p + (u32)i + 0x57) == 0) { V0 = 0; return; }
        if (!(i < (s32)MEM_R8(p + 0x34))) { V0 = 0; return; }
        V0 = MEM_R8(MEM_R32(p + 0x28) + (u32)i);
    } else if (mode == 0x4D) {
        if (!(i < (s32)MEM_R8(p + 0x36))) { V0 = 0xFF; return; }
        u32 a = MEM_R32(p + 0x2C) + (u32)i;
        LOOP_POLL(0x8003CB9Cu);
        V0 = MEM_R8(a);
    } else {
        if (!(i < (s32)MEM_R8(p + 0x36))) { V0 = 0; return; }
        V0 = MEM_R8(MEM_R32(p + 0x2C) + (u32)i);
    }
}

/* 8003CBD0 (port): which actuators may run (+0x57.., one byte each),
 * within the power budget shared by both ports (0x80062ABC, 60 at most):
 * with an actuator table (+0xE6, +0x28 levels, +4 five bytes per motor:
 * +2 nonzero for a variable level, +3 its cost; +0x5D.. which motor each
 * byte drives) every motor that is asked to run and fits the budget; a
 * DualShock without one (types 4, 5, 7) its small motor if asked; type
 * 3 the first byte; otherwise all six. */
#define PAD_POWER GV(u32, 0x80062ABCu)
void n_libpad_ActPower(void) {
    FRAME(0x18);
    u32 p = A0;
    gcall(0x80039878u, p + 0x57, 6, R[6], R[7]);
    if (MEM_R16(p + 0xE6) != 0 && MEM_R32(p + 0x28) != 0) {
        u32 n = MEM_R8(p + 0x34) < 7 ? MEM_R8(p + 0x34) : 6;
        if (MEM_R8(p + 0xE9) == 0) return;
        for (u32 j = 0, off = 0;;) {
            u32 mask = MEM_R8(MEM_R32(p + 4) + off + 2) ? 0xFF : 1;
            u32 on = 0;
            if (n != 0) {
                u32 a = p + 0x5D, b = MEM_R32(p + 0x28);
                for (u32 k = 0;;) {
                    if (MEM_R8(a) == j && (MEM_R8(b) & mask)) { on = 1; LOOP_POLL(0x8003CCE4u); break; }
                    a++; k++; b++;
                    if (!((s32)k < (s32)n)) break;
                    LOOP_POLL(0x8003CC9Cu);
                }
            }
            if (on) {
                u32 v = MEM_R8(MEM_R32(p + 4) + off + 3) + PAD_POWER;
                if ((s32)v < 61) PAD_POWER = v;
                else on = 0;
                if (on && n != 0) {
                    u32 a = p + 0x5D, d = p + 0x57;
                    for (u32 k = 0;;) {
                        if (MEM_R8(a) == j) MEM_W8(d, 1);
                        a++; k++; d++;
                        if (!((s32)k < (s32)n)) break;
                        LOOP_POLL(0x8003CD24u);
                    }
                }
            }
            j++; off += 5;
            if (!((s32)j < (s32)MEM_R8(p + 0xE9))) break;
            LOOP_POLL(0x8003CD38u);
        }
        return;
    }
    u32 m = MEM_R8(p + 0xE8);
    if ((m - 4 < 2 || m == 7) && MEM_R16(p + 0xE6) == 0 && MEM_R8(p + 0x34) >= 2) {
        u32 b = MEM_R32(p + 0x28);
        if ((MEM_R8(b) & 0xC0) != 0x40) return;
        if (!(MEM_R8(b + 1) & 1)) return;
        if (!((s32)(PAD_POWER + 10) < 61)) return;
        MEM_W8(p + 0x58, 1);
        MEM_W8(p + 0x57, 1);
        PAD_POWER += 10;
        return;
    }
    if (MEM_R8(p + 0xE8) == 3) { MEM_W8(p + 0x57, 1); return; }
    if (MEM_R16(p + 0xE6) != 0) return;
    for (s32 k = 5;;) {
        MEM_W8(p + 0x57 + (u32)k, 1);
        k--;
        if (k < 0) break;
        LOOP_POLL(0x8003CE34u);
    }
}

/* 8003CFEC (port): after an exchange: the controller's type (the reply's
 * first byte's high nibble, 0xF: unchanged) into +0xE8 and the reply
 * copied to the user's buffer (+0x30: 0, then the reply; 0xFF, 0 if no
 * controller). A change (or a reply to act on) goes to the change
 * callback (0x80062A78); then the setup sequence (+0x46: 0 start, 1
 * querying, 2-0xFD running the setup function at +0x18, or 8003B6B8 by
 * default, which says how far to step; 0xFE done; 0xFF idle) moves on. */
void n_libpad_Update(void) {
    FRAME(0x18);
    u32 p = A0;
    u32 b0 = MEM_R8(MEM_R32(p + 0x3C));
    if ((b0 & 0xF0) == 0) {
        MEM_W8(MEM_R32(p + 0x30), 0xFF);
        MEM_W8(MEM_R32(p + 0x30) + 1, 0);
        MEM_W8(p + 0xE8, 0);
        MEM_W8(p + 0x35, 0);
        gcall(GV(u32, 0x80062A78u), p, R[5], R[6], R[7]);
        return;
    }
    u32 old = MEM_R8(p + 0xE8);
    MEM_W8(p + 0xE8, b0 >> 4);
    if ((b0 >> 4) == 0xF) {
        MEM_W8(p + 0xE8, old);
    } else {
        MEM_W8(MEM_R32(p + 0x30), 0);
        MEM_W8(MEM_R32(p + 0x30) + 1, MEM_R8(MEM_R32(p + 0x3C)));
        MEM_W8(p + 0x35, MEM_R8(p + 0x44));
        if (2 < (s32)MEM_R8(p + 0x44)) {
            for (u32 i = 2;;) {
                MEM_W8(MEM_R32(p + 0x30) + i, MEM_R8(MEM_R32(p + 0x3C) + i));
                i++;
                if (!((s32)i < (s32)MEM_R8(p + 0x44))) break;
                LOOP_POLL(0x8003D0C4u);
            }
        }
    }
    int call;
    if (MEM_R8(MEM_R32(p + 0x3C) + 1) == 0 && (MEM_R8(p + 0x46) != 1 || MEM_R32(p + 0x14) != 0))
        call = MEM_R8(p + 0x50) == 0 || MEM_R8(p + 0xE8) != old;
    else
        call = MEM_R8(p + 0xE8) != old;
    if (call) gcall(GV(u32, 0x80062A78u), p, old, R[6], R[7]);
    u32 v = MEM_R8(p + 0x46);
    MEM_W8(p + 0x4A, 0);
    if (v == 0xFF) return;
    if (v != 0 && MEM_R8(p + 0x37) == 0) return;
    if (((v - 2) & 0xFF) < 0xFC && MEM_R8(MEM_R32(p + 0x3C)) != 0xF3) {
        gcall(GV(u32, 0x80062A78u), p, old, R[6], R[7]);
        return;
    }
    v = MEM_R8(p + 0x46);
    if (v == 1) {
        MEM_W8(p + 0x47, 0);
        MEM_W8(p + 0x46, MEM_R8(p + 0x46) + 1);
        return;
    }
    if (v == 0) {
        u32 s = MEM_R8(p + 0x46);
        MEM_W8(p + 0x49, 1);
        MEM_W8(p + 0x46, s + 1);
        return;
    }
    if (v == 0xFE) { MEM_W8(p + 0x46, 0xFF); return; }
    u32 fn = MEM_R32(p + 0x18);
    u32 r = gcall(fn ? fn : 0x8003B6B8u, p, old, R[6], R[7]);
    MEM_W8(p + 0x46, MEM_R8(p + 0x46) + r);
}

/* 8003B5E4: wait for the transmitter to be ready. */
static void wait_tx(void) {
    u32 sio = SIO;
    while (!(MEM_R16(sio + 4) & 2)) LOOP_POLL(0x8003B5FCu);
}
void n_libpad_WaitTx(void) { wait_tx(); }

/* Wait (until the timeout) for the SIO interrupt; 0 on a timeout. */
static u32 wait_irq(u32 poll) {
    for (;;) {
        if (MEM_R32(ISTAT) & 0x80) return 1;
        LOOP_POLL(poll);
        if (timed_out()) return 0;
    }
}

/* 8003ACA8 (port): start a port's exchange: reset the SIO and select
 * the port (0x80062AB4); first the queued multitap/ack work for it
 * (0x80062ACC.., counted down through 0x80062A94; at 0 the port's
 * done callbacks 0x80062A94 and 0x80062A98). If the controller is
 * already pulling the acknowledge line, send the 01 42 01 header
 * (each acknowledged; 0 if it isn't). Returns 1 if the port is to be
 * read (+0x50 set and mode +0x37 0), else 0. */
void n_libpad_Start(void) {
    FRAME(0x20);
    u32 p = A0;
    u32 sio = SIO;
    MEM_W16(sio + 0xA, 0x40);
    MEM_W16(sio + 0xA, 0);
    MEM_W16(sio + 8, 0xD);
    MEM_W16(sio + 0xE, 0x88);
    timer_start(MEM_R8(p + 0xE8) == 8 ? 0x50 : 0x91);
    u32 sel = GV(u32, 0x80062AB4u);
    MEM_W16(SIO + 0xA, sel ? 0x3003 : 0x1003);
    s32 cnt = (s32)GV(u32, 0x80062ACCu + sel * 4);
    if (cnt >= 0) {
        if (cnt > 0) {
            for (;;) {
                u32 e = 0x80062ACCu + GV(u32, 0x80062AB4u) * 4;
                u32 c = GV(u32, e) - 1;
                GV(u32, e) = c;
                gcall(GV(u32, 0x80062A94u), MEM_R32(p + 0xC) + c * 0xF0, R[5], R[6], R[7]);
                if (!((s32)GV(u32, 0x80062ACCu + GV(u32, 0x80062AB4u) * 4) > 0)) break;
                LOOP_POLL(0x8003ADA8u);
            }
        }
        u32 e = 0x80062ACCu + GV(u32, 0x80062AB4u) * 4;
        if (GV(u32, e) == 0) {
            GV(u32, e) = 0xFFFFFFFFu;
            gcall(GV(u32, 0x80062A94u), p, e, R[6], R[7]);
            gcall(GV(u32, 0x80062A98u), p, R[5], R[6], R[7]);
        }
    }
    sio = SIO;
    if (MEM_R16(sio + 4) & 0x200) {
        MEM_W16(sio + 0xA, MEM_R16(sio + 0xA) | 0x10);
        if (!(MEM_R16(sio + 4) & 0x200)) {
            MEM_W32(ISTAT, 0xFFFFFF7Fu);
        } else {
            while (!timed_out()) LOOP_POLL(0x8003AE50u);
            MEM_W8(SIO, 1);
            timer_start(0x7D0);
            if (!wait_ack()) { V0 = 0; return; }
            wait_tx();
            (void)MEM_R8(SIO);
            timer_start(0x1AE);
            if (!wait_irq(0x8003AECCu)) { V0 = 0; return; }
            MEM_W8(SIO, 0x42);
            timer_start(0x3C);
            if (!wait_ack()) { V0 = 0; return; }
            wait_tx();
            (void)MEM_R8(SIO);
            timer_start(0x1AE);
            if (!wait_irq(0x8003AF48u)) { V0 = 0; return; }
            MEM_W8(SIO, 1);
            timer_start(0x3C);
            if (!wait_ack()) { V0 = 0; return; }
            wait_tx();
            (void)MEM_R8(SIO);
            V0 = 0;
            return;
        }
    }
    if (MEM_R8(p + 0x50) == 0) { V0 = 1; return; }
    V0 = MEM_R8(p + 0x37) != 0 ? 0 : 1;
}

static u32 send_with(void (*f)(void), u32 port, u32 byte) {
    R[4] = port; R[5] = byte;
    f();
    return V0;
}

/* 8003C4A8 (port): a multitap exchange (0x80062AF8 nonzero) or the rest
 * of a plain one: 0x80062AEC bytes are left to go. For a multitap, the
 * header bytes from the byte source (0x80062A7C, called with the port
 * and the tap flag), the previous slot's data handed to 0x80062A80; then
 * the other port's queued work (0x80062ACC.., see libpad_Start) is
 * stepped between bytes; then the remaining bytes (not waiting for the
 * acknowledge at the fast baud rate 0x22). The last byte received is
 * stored and the error callback (0x80062A74) told 0. Returns 0, the
 * failing send's result, or -3 if the controller stops answering. */
void n_libpad_Exchange(void) {
    FRAME(0x30);
    u32 p = A0;
    if (GV(u32, 0x80062AF8u) != 0) gcall(GV(u32, 0x80062A80u), p, R[5], R[6], R[7]);
    u32 tap = GV(u32, 0x80062AF8u);
    s32 slot = -1;
    if (tap != 0) {
        for (u32 off = 0xFFFFFF10u;;) {
            u32 c = GV(u32, 0x80062AECu) - 1;
            GV(u32, 0x80062AECu) = c;
            if ((s32)c <= 0) break;
            if (slot >= 0) gcall(GV(u32, 0x80062A80u), MEM_R32(p + 0xC) + off, R[5], R[6], R[7]);
            u32 b = gcall(GV(u32, 0x80062A7Cu), p, 1, R[6], R[7]);
            s32 r = (s32)send_with(n_libpad_SendReady, p, b & 0xFF);
            if (r < 0) { V0 = (u32)r; return; }
            timer_start(0x3C);
            if (!wait_ack()) { V0 = 0xFFFFFFFDu; return; }
            slot++;
            off += 0xF0;
            if (!(slot < 4)) break;
            LOOP_POLL(0x8003C588u);
        }
    }
    u32 work = 0;
    u32 other = GV(u32, 0x80062AB4u) == 0 ? 1 : 0;
    if (!((s32)GV(u32, 0x80062AECu) < 2)) {
        u32 q = 0x80062ACCu + other * 4, base = other * 0xF0;
        for (;;) {
            s32 v = (s32)MEM_R32(q);
            if (v < 0) break;
            if (v > 0) {
                work = MEM_R32(GV(u32, 0x80062AA8u) + base + 0xC) + (u32)v * 0xF0 - 0xF0;
                gcall(GV(u32, 0x80062A94u), work, R[5], R[6], R[7]);
            }
            s32 w = (s32)MEM_R32(q);
            if (w == 3) {
                gcall(GV(u32, 0x80062A94u), work - 0xF0, R[5], R[6], R[7]);
                MEM_W32(q, 1);
            } else if (w == 0 || w == 1) {
                work = GV(u32, 0x80062AA8u) + base;
                gcall(GV(u32, 0x80062A94u), work, R[5], R[6], R[7]);
                gcall(GV(u32, 0x80062A98u), work, R[5], R[6], R[7]);
                MEM_W32(q, 0xFFFFFFFFu);
            } else if (w == 4) {
                MEM_W32(q, 3);
            }
            u32 b = gcall(GV(u32, 0x80062A7Cu), p, tap, R[6], R[7]);
            s32 r = (s32)send_with(n_libpad_Send, p, b & 0xFF);
            if (r < 0) { V0 = (u32)r; return; }
            timer_start(0x3C);
            if (!wait_ack()) { V0 = 0xFFFFFFFDu; return; }
            u32 c = GV(u32, 0x80062AECu) - 1;
            GV(u32, 0x80062AECu) = c;
            if ((s32)c < 2) break;
            LOOP_POLL(0x8003C72Cu);
        }
    }
    u32 c = GV(u32, 0x80062AECu) - 1;
    GV(u32, 0x80062AECu) = c;
    if ((s32)c > 0) {
        for (;;) {
            u32 b = gcall(GV(u32, 0x80062A7Cu), p, tap, R[6], R[7]);
            s32 r = (s32)send_with(n_libpad_Send, p, b & 0xFF);
            if (r < 0) { V0 = (u32)r; return; }
            if (MEM_R16(GV(u32, 0x80062AF0u) + 0xE) != 0x22) {
                timer_start(0x3C);
                if (!wait_ack()) { V0 = 0xFFFFFFFDu; return; }
            }
            c = GV(u32, 0x80062AECu) - 1;
            GV(u32, 0x80062AECu) = c;
            if (!((s32)c > 0)) break;
            LOOP_POLL(0x8003C7C8u);
        }
    }
    wait_tx();
    u32 n = MEM_R8(p + 0x44);
    MEM_W8(p + 0x44, n + 1);
    u32 sio = GV(u32, 0x80062AF0u);
    u32 dst = MEM_R32(p + 0x3C) + n;
    MEM_W8(dst, MEM_R8(sio));
    gcall(GV(u32, 0x80062A74u), 0, R[5], R[6], R[7]);
    V0 = 0;
}

/* 8003CB04 (port): latch the mode (+0x37 into +0x38, cleared); returns
 * it. */
void n_libpad_LatchMode(void) {
    V0 = MEM_R8(A0 + 0x37);
    MEM_W8(A0 + 0x37, 0);
    MEM_W8(A0 + 0x38, (u8)V0);
}

/* 8003CE84 (port): its 0xF0-byte work area (0x8006BE10, the second port's
 * after it). */
void n_libpad_PortBuf(void) {
    V0 = (A0 & 0xF0) ? 0x8006BF00u : 0x8006BE10u;
}

/* Steps of the pad exchange state machine (port p). 0x80062A8C is the
 * buffer hook (dest), 0x80062AF4 its last result. The original keeps p
 * in s0, saved in its frame like this. */
static u32 step_enter(u32 sp) {
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = A0;
    return s0;
}
#define PAD_HOOK     GV(u32, 0x80062A8Cu)
#define PAD_HOOKED   GV(u32, 0x80062AF4u)

/* 8003C218 (p): run the buffer hook on p, remember its result, clear the
 * first byte of p's receive buffer (+0x3C) and send -2. */
void n_libpad_StepBegin(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    PAD_HOOKED = gcall(PAD_HOOK, p, R[5], R[6], R[7]);
    MEM_W8(MEM_R32(p + 0x3C), 0);
    gcall(0x8003B0CCu, p, (u32)-2, R[6], R[7]);
    R[16] = s0;
}

/* 8003C260 (p): when both slots are in step (0x80062AB4 == 0x80062AC4)
 * and 0x80062AB0 is set, run the hooks 0x80062AA4 and 0x80062AA0; if the
 * buffer hook ran, run it on p's two work buffers (+0xC, +0xC + 0xF0);
 * then send the mode (+0x37), or 0x42 (read) when it is 0. */
void n_libpad_StepPoll(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    if (GV(u32, 0x80062AB4u) == GV(u32, 0x80062AC4u) && GV(u32, 0x80062AB0u) != 0) {
        gcall(GV(u32, 0x80062AA4u), R[4], R[5], R[6], R[7]);
        gcall(GV(u32, 0x80062AA0u), R[4], R[5], R[6], R[7]);
    }
    if (PAD_HOOKED) {
        gcall(PAD_HOOK, MEM_R32(p + 0xC), R[5], R[6], R[7]);
        gcall(PAD_HOOK, MEM_R32(p + 0xC) + 0xF0, R[5], R[6], R[7]);
    }
    u32 mode = MEM_R8(p + 0x37);
    gcall(0x8003B2DCu, p, mode ? MEM_R8(p + 0x37) : 0x42, R[6], R[7]);
    R[16] = s0;
}

/* 8003C338 (p): if the buffer hook ran, run it on p's buffers +0x1E0
 * and +0x2D0; send 0 (mode set) or 0x80062AC0; a negative answer is
 * returned, otherwise its low nibble times 2 (0x20 for 0) goes to
 * 0x80062AEC and 0 is returned. */
void n_libpad_StepExtra(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    if (PAD_HOOKED) {
        gcall(PAD_HOOK, MEM_R32(p + 0xC) + 0x1E0, R[5], R[6], R[7]);
        gcall(PAD_HOOK, MEM_R32(p + 0xC) + 0x2D0, R[5], R[6], R[7]);
    }
    u32 r = gcall(0x8003B2DCu, p, MEM_R8(p + 0x37) ? 0 : GV(u32, 0x80062AC0u), R[6], R[7]);
    if ((s32)r >= 0) {
        u32 n = (r & 0xF) << 1;
        GV(u32, 0x80062AECu) = n ? n : 0x20;
        V0 = 0;
    }
    R[16] = s0;
}

/* 8003C3E4 (p): when 0x80062AC0 is set and the pad answered as type 8
 * (receive buffer +0x3C, top nibble) with mode 0, flag it (0x80062AF8);
 * otherwise run the hook 0x80062A80 (p) first. Send the byte the hook
 * 0x80062A7C (p, flag) gives. Returns 0x5A or 0 as answered, a negative
 * answer as it is, anything else -4. */
void n_libpad_StepIdent(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    u32 f = 0;
    if (GV(u32, 0x80062AC0u) != 0 && (MEM_R8(MEM_R32(p + 0x3C)) >> 4) == 8)
        f = MEM_R8(p + 0x37) < 1;
    GV(u32, 0x80062AF8u) = f;
    if (f == 0) gcall(GV(u32, 0x80062A80u), p, R[5], R[6], R[7]);
    u32 b = gcall(GV(u32, 0x80062A7Cu), p, GV(u32, 0x80062AF8u), R[6], R[7]);
    u32 r = gcall(0x8003B2DCu, p, b & 0xFF, R[6], R[7]);
    R[3] = r;
    if (r != 0x5A && r != 0 && (s32)r >= 0) V0 = (u32)-4;
    else V0 = r;
    R[16] = s0;
}

/* 8003CA10 (code): finish the current port (0x80062AB4; its work area
 * 0x8006BE10 + n * 0xF0): code 0 marks it absent (0x80062ACC + n * 4),
 * -9 leaves it, anything else closes it (8003D250, its mode latched);
 * then move to the next port and start it (8003ACA8), until one starts
 * (its result) or there are no more (0x80062AC8 the last; 1). Later
 * ports finish with code 0xFFFF. s-registers as the original's. */
void n_libpad_NextPort(void) {
    FRAME(0x28);
    u32 sv[4] = { R[16], R[17], R[18], R[19] };
    for (int i = 0; i < 4; i++) MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x20, R[31]);
    u32 code = A0;
    R[17] = 0x8006BE10u; R[19] = (u32)-9; R[18] = 0x80062ACCu;
    for (;;) {
        u32 n = GV(u32, 0x80062AB4u);
        R[16] = n * 0xF0 + R[17];
        if (code != R[19]) {
            if (code != 0) {
                gcall(0x8003D250u, R[16], code, R[6], R[7]);
                gcall(0x8003CB04u, R[16], R[5], R[6], R[7]);
            } else {
                GV(u32, R[18] + (n << 2)) = 0;
            }
        }
        u32 t = GV(u32, 0x80062B1Cu);
        n = GV(u32, 0x80062AB4u);
        GV(u32, 0x80062AB8u) = 0;
        MEM_W16(t + 0xA, 0);
        n++;
        GV(u32, 0x80062AB4u) = n;
        code = 0xFFFF;
        if ((s32)GV(u32, 0x80062AC8u) < (s32)n) { V0 = 1; break; }
        if (gcall(0x8003ACA8u, n * 0xF0 + R[17], R[5], R[6], R[7]) != 0) break;
        LOOP_POLL(0x8003CAE0u);
    }
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2]; R[19] = sv[3];
}

/* 8003A434 PadGetState(port): the port's state (+0x49, the port found
 * by the hook 0x80062A88), with 2 and 3 as 1 and 6 as 4, while it is
 * busy (mode +0x37 or +0x38 set, not its own +0x10, +0x39 set, or its
 * command +0x30 pending); otherwise +0x49 as it is. */
void n_libpad_GetState(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 p = gcall(GV(u32, 0x80062A88u), R[4], R[5], R[6], R[7]);
    R[3] = p;
    int busy = MEM_R8(p + 0x37) != 0 || MEM_R8(p + 0x38) != 0
               || (p != MEM_R32(p + 0x10) && MEM_R8(p + 0x39) != 0);
    if (!busy && MEM_R8(MEM_R32(p + 0x30)) == 0) { V0 = MEM_R8(p + 0x49); return; }
    u32 st = MEM_R8(p + 0x49);
    R[4] = st;
    if (st == 3 || st == 2) V0 = 1;
    else if (st == 6) V0 = 4;
    else V0 = MEM_R8(p + 0x49);
}

/* 8003CEDC (p): after an exchange: an answer of 0xF3 with +0xE8 set and
 * state +0x49 2 runs the hook 0x80062A78 (p) (with +0xE8 clear, or
 * +0x46 0xFF: 8003C16C (p, 0) and done). Then by +0x46: 1 8003C16C (p,
 * 1), 0xFE 8003C16C (p, 0), 0 and 0xFF nothing, anything else p's hook
 * +0x14 (p) or 8003B634 (p). Returns 0. */
void n_libpad_StepAnswer(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    if (MEM_R8(MEM_R32(p + 0x3C)) == 0xF3) {
        if (MEM_R8(p + 0xE8) == 0 || MEM_R8(p + 0x46) == 0xFF) {
            gcall(0x8003C16Cu, p, 0, R[6], R[7]);
            V0 = 0; R[16] = s0; return;
        }
        if (MEM_R8(p + 0x49) == 2) gcall(GV(u32, 0x80062A78u), p, R[5], R[6], R[7]);
    }
    u32 st = MEM_R8(p + 0x46);
    if (st == 1) gcall(0x8003C16Cu, p, 1, R[6], R[7]);
    else if (st == 0xFE) gcall(0x8003C16Cu, p, 0, R[6], R[7]);
    else if (st != 0 && st != 0xFF) {
        u32 f = MEM_R32(p + 0x14);
        if (f) gcall(f, p, R[5], R[6], R[7]);
        else gcall(0x8003B634u, p, R[5], R[6], R[7]);
    }
    V0 = 0;
    R[16] = s0;
}

/* 8003AB84: take the flag 0x80062ADC (returned, cleared). */
void n_libpad_TakeFlag(void) {
    V0 = GV(u32, 0x80062ADCu);
    GV(u32, 0x80062ADCu) = 0;
}

/* 8003A388: the same, called through a frame. */
void n_libpad_TakeFlagW(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x8003AB84u, R[4], R[5], R[6], R[7]);
}

/* 8003C16C (p, byte): queue a one-byte command (mode 0x43, the byte in
 * +0x24, +0x2C pointing at it, one byte). Returns 1. */
void n_libpad_SetCommand(void) {
    MEM_W8(A0 + 0x37, 0x43);
    MEM_W32(A0 + 0x2C, A0 + 0x24);
    V0 = 1;
    MEM_W8(A0 + 0x24, (u8)A1);
    MEM_W8(A0 + 0x36, 1);
}

/* 8003C18C (p): mode 0x45, no command bytes. */
void n_libpad_EndCommand(void) {
    V0 = 0x45;
    MEM_W8(A0 + 0x37, 0x45);
    MEM_W32(A0 + 0x2C, 0);
    MEM_W8(A0 + 0x36, 0);
}

/* 8003A98C: the pad's interrupt (bit 0 of the registers 0x80062AD4
 * points at, mask +4 and status +0) both set: run the hook 0x80062A9C
 * if any and return 1; otherwise 0. */
void n_libpad_IrqCheck(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 r = GV(u32, 0x80062AD4u);
    V0 = 0;
    if (!(MEM_R32(r + 4) & 1)) return;
    if (!(MEM_R32(r) & 1)) return;
    u32 f = GV(u32, 0x80062A9Cu);
    if (f) gcall(f, R[4], R[5], R[6], R[7]);
    V0 = 1;
}

/* 8003C9A8 (p): forget a port that was in use (state +0x49 set): its
 * state, answers, hooks and buffers cleared, the six actuator bytes
 * (+0x5D) 0xFF. Returns -1, or 0 if it was not in use. */
void n_libpad_ResetPort(void) {
    u32 p = A0;
    V0 = 0;
    if (MEM_R8(p + 0x49) == 0) return;
    MEM_W8(p + 0x49, 0);
    MEM_W8(p + 0x46, 0);
    MEM_W16(p + 0xE6, 0);
    MEM_W32(p + 0x14, 0);
    MEM_W32(p + 0x18, 0);
    MEM_W8(p + 0xE3, 0);
    MEM_W8(p + 0xE4, 0);
    MEM_W16(p + 0xE6, 0);
    MEM_W8(p + 0xE9, 0);
    MEM_W8(p + 0xEA, 0);
    MEM_W32(p, 0);
    MEM_W32(p + 4, 0);
    MEM_W32(p + 8, 0);
    for (s32 i = 5;;) {
        MEM_W8(p + 0x5D + (u32)(5 - i), 0xFF);
        i--;
        if (!(i >= 0)) break;
        LOOP_POLL(0x8003CA00u);
    }
    V0 = (u32)-1;
}

/* 8003B634 (p): the next command by the request +0x46: 2 end
 * (8003C18C), 3 8003C1A0 (p, +0xE4), 4 8003C1E0 (p, +0x47). */
void n_libpad_StepCommand(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 p = A0, r = MEM_R8(p + 0x46);
    if (r == 3) gcall(0x8003C1A0u, p, MEM_R8(p + 0xE4), R[6], R[7]);
    else if (r == 4) gcall(0x8003C1E0u, p, MEM_R8(p + 0x47), R[6], R[7]);
    else if (r == 2) gcall(0x8003C18Cu, p, R[5], R[6], R[7]);
    else V0 = r < 4 ? 2 : 4;
}

/* 8003A9F4: libpad's per-frame run (timer registers *0x80062AD8): if
 * the timer's bit 1 (+0xA) is set, just clear it. Otherwise flag the
 * frame (0x80062ADC), count frames without each port (0x8006BD60 / 64,
 * up to 150) and, when enabled (0x80062AAC), exchange with the ports
 * 0x80062AC4..0x80062AC8 (their work areas from 0x80062AA8, 0xF0 each):
 * start the first (8003ACA8; failing, the hook 0x80062A74 (0xFFFF)),
 * then step (8003AFDC) until past the last; the timer's +0xE set to
 * 0x88. Returns 0. */
void n_libpad_Tick(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 t = GV(u32, 0x80062AD8u);
    V0 = 0;
    if (MEM_R16(t + 0xA) & 2) { MEM_W16(t + 0xA, 0); return; }
    GV(u32, 0x80062ADCu) = 1;
    if (GV(u32, 0x80062AC4u) != 0 && (s32)GV(u32, 0x8006BD60u) < 150) GV(u32, 0x8006BD60u) += 1;
    if (GV(u32, 0x80062AC8u) == 0 && (s32)GV(u32, 0x8006BD64u) < 150) GV(u32, 0x8006BD64u) += 1;
    if (GV(u32, 0x80062AACu) == 0) { V0 = 0; return; }
    u32 first = GV(u32, 0x80062AC4u);
    if ((s32)GV(u32, 0x80062AC8u) < (s32)first) { V0 = 0; return; }
    GV(u32, 0x80062AB8u) = 0;
    GV(u32, 0x80062AB4u) = first;
    if (gcall(0x8003ACA8u, GV(u32, 0x80062AA8u) + first * 0xF0, R[5], R[6], R[7]) == 0)
        gcall(GV(u32, 0x80062A74u), 0xFFFF, R[5], R[6], R[7]);
    GV(u32, 0x80062ABCu) = 0;
    if (!((s32)GV(u32, 0x80062AC8u) < (s32)GV(u32, 0x80062AB4u))) {
        for (;;) {
            gcall(0x8003AFDCu, GV(u32, 0x80062AA8u) + GV(u32, 0x80062AB4u) * 0xF0, R[5], R[6], R[7]);
            if ((s32)GV(u32, 0x80062AC8u) < (s32)GV(u32, 0x80062AB4u)) break;
            LOOP_POLL(0x8003AB5Cu);
        }
    }
    MEM_W16(GV(u32, 0x80062AD8u) + 0xE, 0x88);
    V0 = 0;
}

/* 8003D250 (p): after a port's exchange: count it (+0x4C); by the
 * request +0x46: 1 retries up to 11 times (+0x4A), then gives up
 * (state 2, request 0xFF); others retry up to 11 times, then run the
 * hook 0x80062A78 if the port is in use; 0 goes straight on. Unless the
 * answer was 0xF3, the command buffer (+0x30) is reset to FF 00 and
 * +0xE8, +0x35 cleared. */
void n_libpad_Close(void) {
    FRAME(0x18);
    u32 s0 = step_enter(sp);
    u32 p = R[16];
    MEM_W32(p + 0x4C, MEM_R32(p + 0x4C) + 1);
    u32 rq = MEM_R8(p + 0x46);
    if (rq != 0) {
        u32 c = MEM_R8(p + 0x4A);
        if (rq == 1) {
            if (c < 11) { V0 = c + 1; MEM_W8(p + 0x4A, (u8)V0); R[16] = s0; return; }
            MEM_W8(p + 0x49, 2);
            V0 = 0xFF;
            MEM_W8(p + 0x46, 0xFF);
            R[16] = s0;
            return;
        }
        if (c < 11) { V0 = c + 1; MEM_W8(p + 0x4A, (u8)V0); R[16] = s0; return; }
        if (MEM_R8(p + 0x49) != 0) gcall(GV(u32, 0x80062A78u), p, R[5], R[6], R[7]);
    }
    if (MEM_R8(MEM_R32(p + 0x3C)) == 0xF3) { V0 = 0xF3; R[16] = s0; return; }
    MEM_W8(MEM_R32(p + 0x30), 0xFF);
    V0 = MEM_R32(p + 0x30);
    MEM_W8(V0 + 1, 0);
    MEM_W8(p + 0xE8, 0);
    MEM_W8(p + 0x35, 0);
    R[16] = s0;
}

/* 8003C848 (buf1, buf2): the pad library's set-up (PadInitDirect style):
 * stopped (0x80062AAC 0, 0x80062AC0 0, 8003CEA8), its handlers installed
 * (0x80062A74-88, 0x80062A98), the two port records (0x8006BE10, 0xF0
 * bytes each, cleared) given their receive buffers (marked 0xFF, 0), their
 * own address (+0x10), their 0x23-byte work areas (+0x3C at 0x8006BD80,
 * +0x40 at 0x8006BDC8) and six 0xFF bytes at +0x5D; started (8003A960,
 * 0x80062AAC 1). */
void n_libpad_InitDirect(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x10, sv[0]);
    const u32 b1 = A0, b2 = A1;
    R[17] = b1; R[18] = b2;
    GV(u32, 0x80062AACu) = 0;
    GV(u32, 0x80062AC0u) = 0;
    gcall(0x8003CEA8u, R[4], R[5], R[6], R[7]);
    u32 p = 0x8006BE10u;
    R[16] = p;
    GV(u32, 0x80062A74u) = 0x8003CA10u;
    GV(u32, 0x80062A78u) = 0x8003C9A8u;
    GV(u32, 0x80062A7Cu) = 0x8003CB14u;
    GV(u32, 0x80062A80u) = 0x8003CBD0u;
    GV(u32, 0x80062A84u) = 0x8003CE4Cu;
    GV(u32, 0x80062A88u) = 0x8003CE84u;
    GV(u32, 0x80062AA8u) = p;
    GV(u32, 0x80062A98u) = 0x8003CB04u;
    gcall(0x80039878u, p, 0x1E0, R[6], R[7]);
    MEM_W32(p + 0x30, b1);
    MEM_W32(p + 0x120, b2);
    u32 w1 = 0x8006BD80u, w2 = 0x8006BDC8u;
    for (s32 k = 0;;) {
        u32 buf = MEM_R32(p + 0x30);
        MEM_W32(p + 0xC, 0);
        MEM_W32(p + 0x10, p);
        MEM_W8(buf, 0xFF);
        MEM_W8(MEM_R32(p + 0x30) + 1, 0);
        MEM_W32(p + 0x3C, w1);
        MEM_W32(p + 0x40, w2);
        u32 q = p + 0x5D;
        for (s32 n = 5;;) {
            MEM_W8(q, 0xFF);
            n--;
            q++;
            if (!(n >= 0)) break;
            LOOP_POLL(0x8003C958u);
        }
        w2 += 0x23; w1 += 0x23;
        k++;
        p += 0xF0;
        if (!(k < 2)) break;
        LOOP_POLL(0x8003C974u);
    }
    R[16] = p;
    gcall(0x8003A960u, R[4], R[5], R[6], R[7]);
    GV(u32, 0x80062AACu) = 1;
    V0 = 1;
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* A call with $ra as the original's jal leaves it (for callees that keep
 * it in a compared stack frame). */
static inline u32 pad_call_ra(u32 ra, u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    u32 old = R[31];
    R[31] = ra;
    u32 v = gcall(addr, a0, a1, a2, a3);
    R[31] = old;
    return v;
}

/* 8003D4CC: the pad library's stop: in a critical section (80039A08 /
 * 80039A18), its flags (0x80062AB0, 0x80062A9C-A4) cleared, each port's
 * pending request (0x8006C000/04: the record, 0x8006BFF8/FC: the word
 * for +0x28, 0x8006C008/0C: the byte for +0x34) finished and cleared,
 * bit 10 cleared in both words at *0x80062B2C, then 8003DC2C. Runs on
 * the pad library's stack (compared): calls with the original's $ra. */
void n_libpad_Stop(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    pad_call_ra(0x8003D4DCu, 0x80039A08u, R[4], R[5], R[6], R[7]);
    GV(u32, 0x80062AB0u) = 0;
    GV(u32, 0x80062A9Cu) = 0;
    GV(u32, 0x80062AA0u) = 0;
    GV(u32, 0x80062AA4u) = 0;
    for (u32 k = 0; k < 2; k++) {
        if (GV(u32, 0x8006C000u + k * 4)) {
            MEM_W32(GV(u32, 0x8006C000u + k * 4) + 0x28, GV(u32, 0x8006BFF8u + k * 4));
            MEM_W8(GV(u32, 0x8006C000u + k * 4) + 0x34, GV(u8, 0x8006C008u + k * 4));
            GV(u32, 0x8006C000u + k * 4) = 0;
            GV(u32, 0x8006BFF8u + k * 4) = 0;
        }
    }
    const u32 c = GV(u32, 0x80062B2Cu);
    MEM_W32(c + 4, MEM_R32(c + 4) & 0xFFFFFBFFu);
    MEM_W32(c, MEM_R32(c) & 0xFFFFFBFFu);
    pad_call_ra(0x8003D5C4u, 0x8003DC2Cu, R[4], R[5], R[6], R[7]);
    pad_call_ra(0x8003D5CCu, 0x80039A18u, R[4], R[5], R[6], R[7]);
}
