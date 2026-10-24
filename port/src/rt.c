/* Runtime core: CPU state, calls through pointers and into overlays,
 * arithmetic helpers, COP0, and the time/interrupt poll. */
#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>

Cpu g_cpu;
uint8_t g_ram[RAM_SIZE];
uint8_t g_scratch[SCRATCH_SIZE];
int32_t g_rt_budget = POLL_SLICE;
uint32_t g_poll_pc;
uint64_t g_cycles;
uint32_t g_resume_pc;
int g_resume_miss;

/* ------------------------------------------------------------ calls */

/* Module currently loaded in each region: checked against RAM on use (its
 * first bytes, or in the interpreter build a hash of them). */
static uint64_t fnv64(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

static int module_loaded(const RtModule *m) {
    const uint8_t *ram = g_ram + (m->vram & 0x1FFFFF);
    if (m->sig) return memcmp(ram, m->sig, m->sig_len) == 0;
    return fnv64(ram, m->sig_len) == m->sig_hash;
}

static const RtModule *find_module(uint32_t addr) {
    uint32_t p = addr & 0x1FFFFFFF;
    for (int i = 0; i < rt_module_count; i++) {
        const RtModule *m = &rt_modules[i];
        uint32_t lo = m->code_lo & 0x1FFFFFFF, hi = m->code_hi & 0x1FFFFFFF;
        if (p < lo || p >= hi)
            continue;
        if (module_loaded(m))
            return m;
    }
    return NULL;
}

static const RtFunc *find_func(const RtModule *m, uint32_t addr) {
    uint32_t lo = 0, hi = m->nfuncs;
    addr = (addr & 0x1FFFFFFF) | 0x80000000;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (m->funcs[mid].addr < addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < m->nfuncs && m->funcs[lo].addr == addr)
        return &m->funcs[lo];
    return NULL;
}

/* Small cache of the last lookups (overlays rarely change). */
#define CALL_CACHE 256
static struct { uint32_t addr; const RtModule *mod; const RtFunc *fn; } call_cache[CALL_CACHE];

void rt_call(uint32_t addr) {
    uint32_t a = addr & 0x1FFFFFFF;
    if (a == 0xA0 || a == 0xB0 || a == 0xC0) {
        bios_call(a, R[9]);
        return;
    }
    unsigned h = (addr >> 2) & (CALL_CACHE - 1);
    if (call_cache[h].addr == addr && call_cache[h].fn) {
        const RtModule *m = call_cache[h].mod;
        if (module_loaded(m)) {
            interp_call(m, call_cache[h].fn);
            return;
        }
    }
    const RtModule *m = find_module(addr);
    const RtFunc *fn = m ? find_func(m, addr) : NULL;
    if (!fn) {
        if (rt_hle_call(addr))
            return;
        if (m) {
            /* Known module, unknown entry point: note it for the recompiler
             * (tools/recomp/learned_entries.txt) before stopping. */
            const char *path = getenv("RT_LEARN_FILE");
            FILE *lf = fopen(path ? path : "learned_entries.txt", "a");
            if (lf) { fprintf(lf, "%s %08X\n", m->name, addr); fclose(lf); }
        }
        rt_fatal("call to unknown code %08X (module %s), ra=%08X", addr, m ? m->name : "none", R[31]);
    }
    call_cache[h].addr = addr;
    call_cache[h].mod = m;
    call_cache[h].fn = fn;
    interp_call(m, fn);
}

void rt_reset_call_cache(void) { memset(call_cache, 0, sizeof call_cache); }

/* Functions that may contain pc, nearest first (for resuming a save). */
int rt_resume_candidates(uint32_t pc, const RtModule **mod, const RtFunc **out, int max) {
    const RtModule *m = find_module(pc);
    if (!m) return 0;
    *mod = m;
    uint32_t a = (pc & 0x1FFFFFFF) | 0x80000000;
    uint32_t lo = 0, hi = m->nfuncs;
    while (lo < hi) {                        /* first function starting after pc */
        uint32_t mid = (lo + hi) / 2;
        if (m->funcs[mid].addr <= a) lo = mid + 1;
        else hi = mid;
    }
    int n = 0;
    for (int i = (int)lo - 1; i >= 0 && n < max; i--) out[n++] = &m->funcs[i];
    return n;
}

void rt_badjump(uint32_t pc, uint32_t target) {
    rt_fatal("jump table at %08X: target %08X not in table", pc, target);
}

void rt_fatal(const char *fmt, ...) {
    va_list ap;
    char msg[512];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s\n", msg);
    rt_log("FATAL: %s (last loop %08X)", msg, g_poll_pc);
    fprintf(stderr, "  regs:");
    for (int i = 0; i < 32; i++)
        fprintf(stderr, "%s%08X", i % 8 ? " " : "\n   ", R[i]);
    fprintf(stderr, "\n  cycles=%llu frame=%u last loop %08X\n", (unsigned long long)g_cycles, g_frame, g_poll_pc);
    rt_dump_state();
    plat_fatal(msg);
    exit(1);
}

/* ------------------------------------------------------------ arithmetic */

void rt_div(uint32_t a, uint32_t b) {
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (y == 0) {
        LO = x >= 0 ? 0xFFFFFFFF : 1;
        HI = a;
    } else if (x == INT32_MIN && y == -1) {
        LO = 0x80000000;
        HI = 0;
    } else {
        LO = (uint32_t)(x / y);
        HI = (uint32_t)(x % y);
    }
}

void rt_divu(uint32_t a, uint32_t b) {
    if (b == 0) {
        LO = 0xFFFFFFFF;
        HI = a;
    } else {
        LO = a / b;
        HI = a % b;
    }
}

/* Unaligned word access: little-endian semantics of the R3000. */
uint32_t rt_lwl(uint32_t old, uint32_t addr) {
    uint32_t w = MEM_R32(addr & ~3u), s = addr & 3;
    switch (s) {
    case 0: return (old & 0x00FFFFFF) | (w << 24);
    case 1: return (old & 0x0000FFFF) | (w << 16);
    case 2: return (old & 0x000000FF) | (w << 8);
    default: return w;
    }
}

uint32_t rt_lwr(uint32_t old, uint32_t addr) {
    uint32_t w = MEM_R32(addr & ~3u), s = addr & 3;
    switch (s) {
    case 0: return w;
    case 1: return (old & 0xFF000000) | (w >> 8);
    case 2: return (old & 0xFFFF0000) | (w >> 16);
    default: return (old & 0xFFFFFF00) | (w >> 24);
    }
}

void rt_swl(uint32_t addr, uint32_t v) {
    uint32_t a = addr & ~3u, w = MEM_R32(a), s = addr & 3;
    switch (s) {
    case 0: w = (w & 0xFFFFFF00) | (v >> 24); break;
    case 1: w = (w & 0xFFFF0000) | (v >> 16); break;
    case 2: w = (w & 0xFF000000) | (v >> 8); break;
    default: w = v; break;
    }
    MEM_W32(a, w);
}

void rt_swr(uint32_t addr, uint32_t v) {
    uint32_t a = addr & ~3u, w = MEM_R32(a), s = addr & 3;
    switch (s) {
    case 0: w = v; break;
    case 1: w = (w & 0x000000FF) | (v << 8); break;
    case 2: w = (w & 0x0000FFFF) | (v << 16); break;
    default: w = (w & 0x00FFFFFF) | (v << 24); break;
    }
    MEM_W32(a, w);
}

/* ------------------------------------------------------------ COP0 */

uint32_t g_cop0_sr = 0;      /* status register: bit 0 IEc (interrupts on) */
uint32_t g_cop0_cause = 0;
uint32_t g_cop0_epc = 0;

uint32_t rt_mfc0(int reg) {
    switch (reg) {
    case 12: return g_cop0_sr;
    case 13: return g_cop0_cause;
    case 14: return g_cop0_epc;
    case 15: return 2;           /* PRId */
    default: return 0;
    }
}

void rt_mtc0(int reg, uint32_t v) {
    if (reg == 12)
        g_cop0_sr = v;
    else if (reg == 13)
        g_cop0_cause = (g_cop0_cause & ~0x300u) | (v & 0x300u);
}

void rt_rfe(void) {
    /* Pop the interrupt-enable/kernel-mode stack. */
    g_cop0_sr = (g_cop0_sr & ~0xFu) | ((g_cop0_sr >> 2) & 0xFu);
}

void rt_break(uint32_t pc, uint32_t code) {
    /* PsyQ uses break 7 for divide by zero checks and break 6 for overflow;
     * the real hardware would raise an exception the BIOS reports. */
    if (code == 0x1C00 || code == 0x1800 || code == 7 || code == 6)
        return;
    rt_log("break %u at %08X (ignored)", code, pc);
}

/* ------------------------------------------------------------ time */

uint32_t g_frame;

/* Game state the platform layer needs. */
int game_player_in_control(void) {
    /* TEST.BIN (overlay ID 6) in slot 0 and the "player can act" flag */
    return MEM_R32(0x8006C118) == 6 && MEM_R8(0x80062DEC) != 0;
}

/* FPS settings block read by the control code (include/c/game.h
 * PortConfig): 1 heading/pitch unit per mouse count (sensitivity is
 * applied on the PC side), pitch limit in 4096-per-turn units. */
int16_t g_fps_pitch_limit = 0x300;          /* used by port/decomp/player.c */
void rt_set_look_limit(int degrees) { g_fps_pitch_limit = (int16_t)(degrees * 4096 / 360); }

static void write_port_config(void) {
    MEM_W32(0x80011288, 0x52535043);
    MEM_W16(0x8001128C, 1);
    MEM_W16(0x8001128E, 1);
    MEM_W16(0x80011290, (uint16_t)g_fps_pitch_limit);
    MEM_W16(0x80011292, 0);
}

/* Advance the hardware clock by one slice and run due interrupts. */
int rt_in_interrupt(void);
void rt_poll(void) {
    static uint32_t cfg_frame = 0xFFFFFFFF;
    if (cfg_frame != g_frame) { cfg_frame = g_frame; write_port_config(); }
    g_rt_budget = POLL_SLICE;
    uint32_t here = g_poll_pc;               /* polls inside the interrupt handler change it */
    hw_advance(POLL_CYCLES);
    irq_check();
    g_poll_pc = here;
    if (g_ss_pending) ss_service();
}

/* Interrupt delivery: the game's handler runs (on its own coroutine, see
 * thread.c) with the interrupted register state saved, like the BIOS
 * exception handler. It ends with ReturnFromException, which switches back
 * here. */
static int irq_depth;

int rt_in_interrupt(void) { return irq_depth != 0; }

void rt_state(StateIO *io) {
    ss_module(io, "rt");
    SS(io, g_cpu);
    SS(io, g_ram);
    SS(io, g_scratch);
    SS(io, g_rt_budget);
    SS(io, g_poll_pc);
    SS(io, g_cycles);
    SS(io, g_cop0_sr);
    SS(io, g_cop0_cause);
    SS(io, g_cop0_epc);
    SS(io, g_frame);
}

int rt_irq_enabled(void) {
    return (g_cop0_sr & 1) && irq_depth == 0;
}

void irq_check(void) {
    if (!rt_irq_enabled() || !irq_pending())
        return;
    Cpu saved = g_cpu;
    uint32_t sr = g_cop0_sr;
    irq_depth++;
    /* Like the exception entry: interrupts off, kernel mode. */
    g_cop0_sr = (sr & ~0x3Fu) | ((sr << 2) & 0x3Cu);
    int hle = g_hle_depth, native = g_native_depth;   /* the handler's own end with it */
    coro_irq_run(bios_exception);
    g_hle_depth = hle;
    g_native_depth = native;
    irq_depth--;
    g_cpu = saved;
    g_cop0_sr = sr;
}

void rt_return_from_exception(void) {
    if (!irq_depth)
        rt_fatal("ReturnFromException outside an exception");
    coro_irq_return();
    __builtin_unreachable();
}

#ifdef RT_WATCH_WRITES
/* Debug builds: RT_WATCHW=lo-hi (hex) logs every write there. */
uint32_t g_watch_lo = 0xFFFFFFFF, g_watch_hi;
__attribute__((constructor)) static void watch_init(void) {
    const char *e = getenv("RT_WATCHW");
    unsigned lo, hi;
    if (e && sscanf(e, "%x-%x", &lo, &hi) == 2) { g_watch_lo = lo & 0x1FFFFFFF; g_watch_hi = hi & 0x1FFFFFFF; }
}
void rt_watch_write(uint32_t a, uint32_t v, int size) {
    rt_log("write%d %08X = %X (ra %08X, last loop %08X)", size * 8, a, v, R[31], g_poll_pc);
}
#endif
