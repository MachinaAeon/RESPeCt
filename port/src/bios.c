/* High-level replacement for the PS1 BIOS (the calls through 0xA0, 0xB0,
 * 0xC0 and syscall). Only what Resident Evil Survivor uses. */
#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>

void card_init(void);
int card_bios_call(uint32_t table, uint32_t fn);

/* ------------------------------------------------------------ events */

#define MAX_EVENTS 32
typedef struct {
    uint32_t cls, spec, mode, func;
    int used, enabled, ready;
} Event;
static Event events[MAX_EVENTS];

static uint32_t open_event(uint32_t cls, uint32_t spec, uint32_t mode, uint32_t func) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (!events[i].used) {
            events[i] = (Event){cls, spec, mode, func, 1, 0, 0};
            return 0xF1000000u | i;
        }
    }
    rt_log("OpenEvent: out of events");
    return 0xFFFFFFFF;
}

static Event *event_of(uint32_t h) {
    uint32_t i = h & 0xFFFF;
    if ((h & 0xFF000000) != 0xF1000000 || i >= MAX_EVENTS || !events[i].used) return NULL;
    return &events[i];
}

static void call_game(uint32_t func) {
    uint32_t ra = R[31];
    g_hle_depth++;                         /* a save can't continue from in here */
    rt_call(func);
    g_hle_depth--;
    R[31] = ra;
}

void bios_deliver_event(uint32_t cls, uint32_t spec) {
    for (int i = 0; i < MAX_EVENTS; i++) {
        Event *e = &events[i];
        if (!e->used || !e->enabled || e->cls != cls || e->spec != spec) continue;
        if (e->mode == 0x2000)
            e->ready = 1;
        else if (e->mode == 0x1000 && e->func) {
            Cpu saved = g_cpu;
            call_game(e->func);
            g_cpu = saved;
        }
    }
}

/* ------------------------------------------------------------ interrupts */

/* SysEnqIntRP priority chains: entries are {next, func2, func1, pad}. */
static uint32_t chains[4];
static uint32_t hook_entry;            /* HookEntryInt jmp_buf address */
static int clear_pad = 1, clear_rcnt[4] = {1, 1, 1, 1};

void bios_exception(void) {
    /* Queued handlers, highest priority first. */
    for (int pr = 0; pr < 4; pr++) {
        for (uint32_t e = chains[pr], guard = 0; e && guard < 64; e = MEM_R32(e), guard++) {
            uint32_t f1 = MEM_R32(e + 8), f2 = MEM_R32(e + 4);
            if (!f1) continue;
            call_game(f1);
            if (R[2] && f2) {
                R[4] = R[2];
                call_game(f2);
            }
        }
    }
    /* BIOS default handling for root counters and vblank. */
    uint32_t pend = g_istat & g_imask;
    for (int t = 0; t < 3; t++) {
        if (pend & (1u << (IRQ_TMR0 + t))) {
            bios_deliver_event(0xF2000000u | t, 2);
            if (clear_rcnt[t]) g_istat &= ~(1u << (IRQ_TMR0 + t));
        }
    }
    if (pend & 1) {
        bios_deliver_event(0xF2000003u, 2);
        if (clear_rcnt[3]) g_istat &= ~1u;
    }
    /* libetc's handler (HookEntryInt): resume its setjmp buffer. */
    if (hook_entry) {
        uint32_t b = hook_entry;
        R[31] = MEM_R32(b + 0);
        R[29] = MEM_R32(b + 4);
        R[30] = MEM_R32(b + 8);
        for (int i = 0; i < 8; i++) R[16 + i] = MEM_R32(b + 12 + 4 * i);
        R[28] = MEM_R32(b + 44);
        R[2] = 1;
        rt_call(R[31]);
        rt_log("HookEntryInt handler returned without ReturnFromException");
    }
    rt_return_from_exception();
}

/* ------------------------------------------------------------ pads */

static uint32_t pad_buf[2], pad_size[2];
static int pad_started;

/* Fill the BIOS pad buffers (done by the BIOS at every vblank). */
void bios_vblank(void) {
    if (!pad_started) return;
    for (int port = 0; port < 2; port++) {
        uint32_t b = pad_buf[port];
        if (!b) continue;
        if (port == 0) {
            uint16_t btn = (uint16_t)~g_input.buttons;
            MEM_W8(b + 0, 0x00);
            MEM_W8(b + 1, 0x41);                       /* digital pad */
            MEM_W8(b + 2, btn & 0xFF);
            MEM_W8(b + 3, btn >> 8);
        } else if (g_input.mouse_present) {
            uint16_t btn = 0xFFFF;
            if (g_input.mouse_left) btn &= ~0x0800;    /* byte 3 bit 3 */
            if (g_input.mouse_right) btn &= ~0x0400;
            int dx = g_input.mouse_dx, dy = g_input.mouse_dy;
            dx = dx < -128 ? -128 : dx > 127 ? 127 : dx;
            dy = dy < -128 ? -128 : dy > 127 ? 127 : dy;
            g_input.mouse_dx -= dx;
            g_input.mouse_dy -= dy;
            MEM_W8(b + 0, 0x00);
            MEM_W8(b + 1, 0x12);                       /* mouse */
            MEM_W8(b + 2, btn & 0xFF);
            MEM_W8(b + 3, btn >> 8);
            MEM_W8(b + 4, (uint8_t)dx);
            MEM_W8(b + 5, (uint8_t)dy);
        } else {
            MEM_W8(b + 0, 0xFF);
        }
    }
}

/* ------------------------------------------------------------ printf */

static void bios_printf(uint32_t fmt_addr) {
    const char *fmt = ram_str(fmt_addr);
    char out[1024];
    int o = 0, arg = 1;
    uint32_t sp = R[29];
#define NEXT_ARG() (arg < 4 ? R[4 + arg++] : MEM_R32(sp + 4 * arg++))
    for (const char *p = fmt; *p && o < 1000; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        char spec[16] = "%";
        int k = 1;
        p++;
        while (*p && strchr("-+ #0123456789.lh", *p) && k < 14) { if (*p != 'l' && *p != 'h') spec[k++] = *p; p++; }
        spec[k++] = *p;
        spec[k] = 0;
        switch (*p) {
        case 's': o += snprintf(out + o, sizeof out - o, spec, ram_str(NEXT_ARG())); break;
        case 'c': case 'd': case 'i': o += snprintf(out + o, sizeof out - o, spec, (int)NEXT_ARG()); break;
        case 'u': case 'x': case 'X': case 'o': o += snprintf(out + o, sizeof out - o, spec, (unsigned)NEXT_ARG()); break;
        case 'p': o += snprintf(out + o, sizeof out - o, "%08X", NEXT_ARG()); break;
        case '%': out[o++] = '%'; break;
        default: break;
        }
        if (o > 1000) o = 1000;
    }
    out[o] = 0;
    rt_log("game printf: %s", out);
#undef NEXT_ARG
}

/* ------------------------------------------------------------ calls */

static uint32_t rand_seed = 1;

static void a0(uint32_t fn) {
    uint32_t a = R[4], b = R[5], c = R[6];
    switch (fn) {
    case 0x13: {                                       /* setjmp */
        MEM_W32(a + 0, R[31]); MEM_W32(a + 4, R[29]); MEM_W32(a + 8, R[30]);
        for (int i = 0; i < 8; i++) MEM_W32(a + 12 + 4 * i, R[16 + i]);
        MEM_W32(a + 44, R[28]);
        R[2] = 0;
        return;
    }
    case 0x15: {                                       /* strcat */
        uint32_t d = a;
        while (MEM_R8(d)) d++;
        do { MEM_W8(d++, MEM_R8(b)); } while (MEM_R8(b++));
        R[2] = a;
        return;
    }
    case 0x17: case 0x18: {                            /* strcmp, strncmp */
        uint32_t n = fn == 0x18 ? c : 0xFFFFFFFF;
        int r = 0;
        for (uint32_t i = 0; i < n; i++) {
            int x = MEM_R8(a + i), y = MEM_R8(b + i);
            if (x != y) { r = x - y; break; }
            if (!x) break;
        }
        R[2] = (uint32_t)r;
        return;
    }
    case 0x19: {                                       /* strcpy */
        uint32_t i = 0;
        do { MEM_W8(a + i, MEM_R8(b + i)); } while (MEM_R8(b + i++));
        R[2] = a;
        return;
    }
    case 0x1B: {                                       /* strlen */
        uint32_t n = 0;
        while (MEM_R8(a + n)) n++;
        R[2] = n;
        return;
    }
    case 0x27:                                         /* bcopy(src, dst, n) */
        for (uint32_t i = 0; i < c; i++) MEM_W8(b + i, MEM_R8(a + i));
        return;
    case 0x28:                                         /* bzero */
        for (uint32_t i = 0; i < b; i++) MEM_W8(a + i, 0);
        return;
    case 0x2A:                                         /* memcpy */
        for (uint32_t i = 0; i < c; i++) MEM_W8(a + i, MEM_R8(b + i));
        R[2] = a;
        return;
    case 0x2B:                                         /* memset */
        for (uint32_t i = 0; i < c; i++) MEM_W8(a + i, b);
        R[2] = a;
        return;
    case 0x2F:                                         /* rand */
        rand_seed = rand_seed * 1103515245 + 12345;
        R[2] = (rand_seed >> 16) & 0x7FFF;
        return;
    case 0x30: rand_seed = a; return;                  /* srand */
    case 0x39: return;                                 /* InitHeap */
    case 0x3F: bios_printf(a); return;                 /* printf */
    case 0x44: return;                                 /* FlushCache */
    case 0x49: gpu_write_gp0(a); return;               /* GPU_cw */
    case 0x70: case 0x72: case 0x9F: return;           /* _bu_init, _96_remove, SetMemSize */
    }
    if (card_bios_call(0xA0, fn)) return;
    rt_log("BIOS A(%02X) not implemented (a0=%08X a1=%08X ra=%08X)", fn, a, b, R[31]);
    R[2] = 0;
}

static void b0(uint32_t fn) {
    uint32_t a = R[4], b = R[5], c = R[6], d = R[7];
    switch (fn) {
    case 0x07: bios_deliver_event(a, b); R[2] = 1; return;
    case 0x08:
        R[2] = open_event(a, b, c, d);
        if (getenv("RT_TRACE_EVENTS")) rt_log("OpenEvent(%08X, %04X, %04X, %08X) = %08X", a, b, c, d, R[2]);
        return;
    case 0x09: { Event *e = event_of(a); if (e) e->used = 0; R[2] = 1; return; }
    case 0x0A: case 0x0B: {                            /* WaitEvent, TestEvent */
        Event *e = event_of(a);
        R[2] = 0;
        if (e && e->ready) { e->ready = 0; R[2] = 1; }
        return;
    }
    case 0x0C: { Event *e = event_of(a); if (e) { e->enabled = 1; e->ready = 0; } R[2] = 1; return; }
    case 0x0D: { Event *e = event_of(a); if (e) e->enabled = 0; R[2] = 1; return; }
    case 0x0E: R[2] = thread_open(a, b, c); return;    /* OpenThread(pc, sp, gp) */
    case 0x0F: R[2] = (uint32_t)thread_close(a); return; /* CloseThread */
    case 0x10: R[2] = (uint32_t)thread_change(a); return; /* ChangeThread */
    case 0x3F: rt_log("game puts: %s", ram_str(a)); return;
    case 0x12:                                         /* InitPad */
        pad_buf[0] = a; pad_size[0] = b; pad_buf[1] = c; pad_size[1] = d;
        R[2] = 2;
        return;
    case 0x13: pad_started = 1; R[2] = 1; return;      /* StartPad */
    case 0x14: pad_started = 0; return;                /* StopPad */
    case 0x15:                                         /* OutdatedPadInitAndStart */
        pad_buf[0] = b; pad_buf[1] = c; pad_started = 1; R[2] = 2;
        return;
    case 0x17: rt_return_from_exception();
    case 0x18: hook_entry = 0; return;                 /* ResetEntryInt */
    case 0x19: hook_entry = a; return;                 /* HookEntryInt */
    case 0x5B: clear_pad = (int)a; return;             /* ChangeClearPad */
    case 0x4A: case 0x4B: case 0x4C: R[2] = 1; return;  /* InitCard, StartCard, StopCard */
    case 0x56: R[2] = 0x674; return;                   /* GetC0Table */
    case 0x57: R[2] = 0x874; return;                   /* GetB0Table */
    }
    if (card_bios_call(0xB0, fn)) return;
    rt_log("BIOS B(%02X) not implemented (a0=%08X a1=%08X ra=%08X)", fn, a, b, R[31]);
    R[2] = 0;
}

static void c0(uint32_t fn) {
    uint32_t a = R[4], b = R[5];
    switch (fn) {
    case 0x02: {                                       /* SysEnqIntRP(prio, entry) */
        if (a > 3) break;
        MEM_W32(b, chains[a]);
        chains[a] = b;
        R[2] = 0;
        return;
    }
    case 0x03: {                                       /* SysDeqIntRP(prio, entry) */
        if (a > 3) break;
        uint32_t *p = &chains[a];
        while (*p) {
            if (*p == b) { *p = MEM_R32(b); break; }
            uint32_t next = MEM_R32(*p);
            if (next == b) { MEM_W32(*p, MEM_R32(b)); break; }
            if (!next) break;
            /* walk */
            uint32_t cur = next;
            while (cur && MEM_R32(cur) != b) cur = MEM_R32(cur);
            if (cur) MEM_W32(cur, MEM_R32(b));
            break;
        }
        R[2] = 0;
        return;
    }
    case 0x0A:                                         /* ChangeClearRCnt(t, flag) */
        if (a < 4) { R[2] = (uint32_t)clear_rcnt[a]; clear_rcnt[a] = (int)b; }
        return;
    }
    rt_log("BIOS C(%02X) not implemented (a0=%08X a1=%08X ra=%08X)", fn, a, b, R[31]);
    R[2] = 0;
}

void bios_call(uint32_t table, uint32_t fn) {
    g_hle_depth++;
    switch (table) {
    case 0xA0: a0(fn & 0xFF); break;
    case 0xB0: b0(fn & 0xFF); break;
    case 0xC0: c0(fn & 0xFF); break;
    }
    g_hle_depth--;
}

void rt_syscall(uint32_t pc) {
    (void)pc;
    switch (R[4]) {
    case 1:                                            /* EnterCriticalSection */
        R[2] = (g_cop0_sr & 0x404) == 0x404;
        g_cop0_sr &= ~0x404u;
        return;
    case 2:                                            /* ExitCriticalSection */
        g_cop0_sr |= 0x404;
        irq_check();
        return;
    default:
        rt_log("syscall %u at %08X", R[4], pc);
    }
}

/* Calls to addresses that aren't game code (BIOS-internal vectors). */
int rt_hle_call(uint32_t addr) {
    uint32_t p = addr & 0x1FFFFFFF;
    if (p < 0x10000) {
        rt_log("call into BIOS RAM %08X ignored (ra=%08X)", addr, R[31]);
        return 1;
    }
    return 0;
}

void bios_init(void) {
    card_init();
    /* The BIOS leaves interrupts enabled in the status register. */
    g_cop0_sr = 0x401;
}

void bios_state(StateIO *io) {
    ss_module(io, "bios");
    SS(io, events);
    SS(io, chains);
    SS(io, hook_entry);
    SS(io, clear_pad);
    SS(io, clear_rcnt);
    SS(io, pad_buf);
    SS(io, pad_size);
    SS(io, pad_started);
    SS(io, rand_seed);
}
