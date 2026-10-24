/* Root counters 0-2. Counter 0 counts the system clock (or the dot clock),
 * counter 1 the system clock or horizontal blanks, counter 2 the system
 * clock or 1/8 of it. libetc's VSync uses counter 1 in hblank mode. */
#include "rt.h"
#include "savestate.h"

typedef struct {
    uint32_t count, mode, target;
    uint32_t frac;
} Timer;

static Timer tm[3];

static void tick(int i, uint32_t n) {
    Timer *t = &tm[i];
    while (n) {
        uint32_t limit = (t->mode & 8) ? t->target : 0xFFFF;
        uint32_t step;
        if (t->count < limit)
            step = limit - t->count < n ? limit - t->count : n;
        else
            step = 1;
        t->count += step;
        n -= step;
        if (t->count == t->target && (t->mode & 8 || 1)) {
            if (t->count == t->target) {
                t->mode |= 0x800;
                if (t->mode & 0x10) { irq_raise(IRQ_TMR0 + i); t->mode &= ~0x400u; }
            }
            if (t->mode & 8) t->count = 0;
        }
        if (t->count >= 0xFFFF) {
            t->mode |= 0x1000;
            if (t->mode & 0x20) { irq_raise(IRQ_TMR0 + i); t->mode &= ~0x400u; }
            t->count = 0;
        }
        if (step == 0) break;
    }
}

void timers_advance(uint32_t cycles) {
    uint32_t src0 = (tm[0].mode >> 8) & 1;
    tick(0, src0 ? cycles / 5 : cycles);        /* dot clock: about 1/5 */
    if (!((tm[1].mode >> 8) & 1))
        tick(1, cycles);
    if ((tm[2].mode >> 8) & 2) {
        tm[2].frac += cycles;
        tick(2, tm[2].frac / 8);
        tm[2].frac %= 8;
    } else {
        tick(2, cycles);
    }
}

void timers_hblank(int n) {
    if ((tm[1].mode >> 8) & 1)
        tick(1, n);
}

uint32_t timers_read(uint32_t a) {
    int i = (a >> 4) & 3;
    if (i > 2) return 0;
    Timer *t = &tm[i];
    switch (a & 0xF) {
    case 0: return t->count;
    case 4: {
        uint32_t m = t->mode;
        t->mode &= ~0x1800u;                    /* reached flags clear on read */
        return m;
    }
    case 8: return t->target;
    }
    return 0;
}

void timers_write(uint32_t a, uint32_t v) {
    int i = (a >> 4) & 3;
    if (i > 2) return;
    Timer *t = &tm[i];
    switch (a & 0xF) {
    case 0: t->count = v & 0xFFFF; break;
    case 4: t->mode = (v & 0x3FF) | 0x400; t->count = 0; break;
    case 8: t->target = v & 0xFFFF; break;
    }
}

void timers_state(StateIO *io) {
    ss_module(io, "timers");
    SS(io, tm);
}
