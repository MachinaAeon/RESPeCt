/* SIO0: the controller/memory card port, for code that talks to pads
 * directly (libpad) rather than through the BIOS. Port 1 answers as a
 * digital pad, port 2 as a PlayStation Mouse when one is enabled. Memory
 * cards don't answer here (saves go through the BIOS card functions). */
#include "rt.h"
#include "savestate.h"

InputState g_input;

/* FPS turning takes the mouse's motion once per game frame, all of it. The
 * pad buffer the game would read it from is refilled at every vblank, so at
 * 30 frames a second every other vblank's motion was overwritten before
 * the game saw it: half of every movement lost, unevenly. */
void input_take_look(int *dx, int *dy) {
    *dx = g_input.look_dx;
    *dy = g_input.look_dy;
    g_input.look_dx = g_input.look_dy = 0;
}

static struct {
    uint8_t rx[8];
    int rxn;
    uint16_t ctrl, mode, baud;
    int irq;
    int ack_level;
    int step;                  /* byte index in the current exchange */
    int device;                /* 0 none, 1 pad, 2 mouse, -1 not answering */
    int32_t ack_delay;
    uint8_t reply[8];
    int replyn;
} s;

static void reset_exchange(void) { s.step = 0; s.device = 0; }

static void exchange(uint8_t tx) {
    uint8_t out = 0xFF;
    int ack = 0;
    int port = (s.ctrl >> 13) & 1;
    if (!(s.ctrl & 2)) { s.rx[s.rxn < 8 ? s.rxn++ : 7] = 0xFF; return; }
    if (s.step == 0) {
        if (tx == 0x01) {
            if (port == 0) s.device = 1;
            else s.device = g_input.mouse_present ? 2 : -1;
        } else {
            s.device = -1;                          /* memory card (0x81): absent */
        }
        if (s.device > 0) {
            ack = 1;
            if (s.device == 1) {
                uint16_t b = (uint16_t)~g_input.buttons;
                uint8_t r[5] = {0xFF, 0x41, 0x5A, (uint8_t)b, (uint8_t)(b >> 8)};
                memcpy(s.reply, r, 5);
                s.replyn = 5;
            } else {
                uint16_t b = 0xFFFF;
                if (g_input.mouse_left) b &= ~0x0800;
                if (g_input.mouse_right) b &= ~0x0400;
                int dx = g_input.mouse_dx, dy = g_input.mouse_dy;
                dx = dx < -128 ? -128 : dx > 127 ? 127 : dx;
                dy = dy < -128 ? -128 : dy > 127 ? 127 : dy;
                g_input.mouse_dx -= dx;
                g_input.mouse_dy -= dy;
                uint8_t r[7] = {0xFF, 0x12, 0x5A, (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)dx, (uint8_t)dy};
                memcpy(s.reply, r, 7);
                s.replyn = 7;
            }
        }
    } else if (s.device > 0) {
        ack = s.step + 1 < s.replyn;
    }
    if (s.device > 0 && s.step < s.replyn) out = s.reply[s.step];
    s.step++;
    if (s.rxn < 8) s.rx[s.rxn++] = out;
    if (ack) s.ack_delay = 400;               /* the device pulls /ACK a little later */
    else if (s.device > 0 && s.step >= s.replyn) s.device = -1;
}

uint32_t sio_read(uint32_t a, int size) {
    (void)size;
    switch (a & 0xF) {
    case 0x0: {
        if (!s.rxn) return 0xFF;
        uint8_t v = s.rx[0];
        memmove(s.rx, s.rx + 1, 7);
        s.rxn--;
        return v;
    }
    case 0x4: {
        uint32_t st = 0x5;                    /* TX ready */
        if (s.rxn) st |= 0x2;
        if (s.ack_level) st |= 0x80;
        if (s.irq) st |= 0x200;
        return st;
    }
    case 0x8: return s.mode;
    case 0xA: return s.ctrl;
    case 0xE: return s.baud;
    }
    return 0;
}

void sio_write(uint32_t a, uint32_t v, int size) {
    (void)size;
    switch (a & 0xF) {
    case 0x0: exchange((uint8_t)v); break;
    case 0x8: s.mode = (uint16_t)v; break;
    case 0xA:
        if (v & 0x40) { s.rxn = 0; s.irq = 0; reset_exchange(); }
        if (v & 0x10) s.irq = 0;               /* acknowledge */
        if (!(v & 2)) reset_exchange();
        s.ctrl = (uint16_t)(v & ~0x50u);
        break;
    case 0xE: s.baud = (uint16_t)v; break;
    }
}

void sio_advance(uint32_t cycles) {
    if (s.ack_delay > 0) {
        s.ack_delay -= (int32_t)cycles;
        if (s.ack_delay <= 0) {
            s.ack_level = 0;
            if (s.ctrl & 0x1000) { s.irq = 1; irq_raise(IRQ_SIO0); }
        }
    }
}

void sio_state(StateIO *io) {
    ss_module(io, "sio");
    SS(io, s);
}
