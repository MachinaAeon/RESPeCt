/* The interpreter: runs the game's code straight from RAM (where the game
 * loaded it from the player's disc), for the release build, which carries
 * nothing from the game.
 *
 * It follows exactly the rules tools/recomp/recomp.py translates by, so
 * the interpreter build and the translated build do the same thing, RAM
 * for RAM (tools/eq checks this):
 *
 *   each game function runs in its own host call (interp_run); jal/jalr
 *   call, `jr ra` returns, `j` out of the function calls and returns;
 *   backward branches poll (RT_POLL_AT), jump tables poll without a save
 *   point (RT_POLL_NOSAVE);
 *   a branch's condition (and jr's register) is read before its delay
 *   slot runs; jal sets ra after its delay slot, bltzal/bgezal before;
 *   a saved game continues in a function at a call's return address or
 *   just after a poll (g_resume_pc, see savestate.c), with the same keys
 *   the translation's resume switch has.
 *
 * What the translation worked out from the code (function extents, which
 * j are jumps and which are calls, jump tables) comes from the tables
 * tools/recomp/meta.py writes (port/meta/meta.c). */
#include "rt.h"

#include <stdio.h>
#include <stdlib.h>

#define KERNEL (&rt_modules[0])

/* ------------------------------------------------------------ tables */

static int in_func(const RtModule *m, const InFunc *f, uint32_t pc) {
    const uint32_t *r = m->rng + 2 * f->r_off;
    uint32_t lo = 0, hi = f->r_n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (r[2 * mid + 1] <= pc) lo = mid + 1;
        else hi = mid;
    }
    return lo < f->r_n && r[2 * lo] <= pc;
}

static int annotation(const RtModule *m, const InFunc *f, uint32_t pc) {
    const uint32_t *a = m->ann + 2 * f->a_off;
    uint32_t lo = 0, hi = f->a_n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (a[2 * mid] < pc) lo = mid + 1;
        else hi = mid;
    }
    if (lo < f->a_n && a[2 * lo] == pc) return (int)a[2 * lo + 1];
    rt_fatal("interp: no translation note for the jump at %08X (%s)", pc, m->name);
    return -1;
}

static const RtFunc *find_in(const RtModule *m, uint32_t addr) {
    uint32_t lo = 0, hi = m->nfuncs;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (m->funcs[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    return lo < m->nfuncs && m->funcs[lo].addr == addr ? &m->funcs[lo] : NULL;
}

/* Instruction words: RAM, except where the port's own mods replace a
 * function (tools/build_mod.py; the translated build runs those too). */
static struct { uint32_t lo, hi; } patch_span[256];

static uint32_t patched(const RtModule *m, uint32_t pc) {
    const uint32_t *p = m->patch;
    for (uint32_t i = 0; i < m->npatch; i++) {
        uint32_t a = p[0], n = p[1];
        if (pc >= a && pc < a + 4 * n) return p[2 + (pc - a) / 4];
        p += 2 + n;
    }
    return MEM_R32(pc);
}

/* Code is always in main RAM. */
static inline uint32_t ram_word(uint32_t pc) {
    uint32_t v;
    memcpy(&v, g_ram + (pc & 0x1FFFFC), 4);
    return v;
}

static inline uint32_t fetch(const RtModule *m, uint32_t pc) {
    if (__builtin_expect(m->npatch != 0, 0)) {
        int k = (int)(m - rt_modules);
        if (pc >= patch_span[k].lo && pc < patch_span[k].hi) return patched(m, pc);
    }
    return ram_word(pc);
}

static void init_spans(void) {
    static int done;
    if (done) return;
    done = 1;
    for (int k = 0; k < rt_module_count && k < 256; k++) {
        const RtModule *m = &rt_modules[k];
        const uint32_t *p = m->patch;
        patch_span[k].lo = 0xFFFFFFFF;
        patch_span[k].hi = 0;
        for (uint32_t i = 0; i < m->npatch; i++) {
            if (p[0] < patch_span[k].lo) patch_span[k].lo = p[0];
            if (p[0] + 4 * p[1] > patch_span[k].hi) patch_span[k].hi = p[0] + 4 * p[1];
            p += 2 + p[1];
        }
    }
}

/* recomp.py's control kinds (what makes the next word a delay slot). */
static int is_control(uint32_t w) {
    uint32_t op = w >> 26;
    if (op >= 2 && op <= 7) return 1;
    if (op == 1) {
        uint32_t rt = (w >> 16) & 31;
        return rt == 0 || rt == 1 || rt == 16 || rt == 17;
    }
    if (op == 0) return (w & 63) == 8 || (w & 63) == 9;
    return 0;
}

static int in_delay(const RtModule *m, const InFunc *f, uint32_t pc) {
    return in_func(m, f, pc - 4) && is_control(fetch(m, pc - 4));
}

/* ------------------------------------------------------------ one op */

static void run_func(const RtModule *m, const InFunc *f);

void interp_call(const RtModule *m, const RtFunc *f) {
    if (f->fn) f->fn();
    else run_func(m, f->meta);
}

/* The translation of f (continuing a saved game in it, or a decompiled
 * wrapper calling it): never the native. */
void interp_resume(const RtModule *m, const RtFunc *f) {
    if (f->resume) f->resume();
    else run_func(m, f->meta);
}

/* recomp.py's Emitter.call(): a direct call to a function of the same
 * module or of KERNEL, else through rt_call. */
#define CT_CACHE 1024
static struct { const RtModule *from, *m; uint32_t target; const RtFunc *fn; } ct_cache[CT_CACHE];

static void call_target(const RtModule *m, uint32_t target) {
    unsigned h = ((target >> 2) ^ (unsigned)((uintptr_t)m >> 4)) & (CT_CACHE - 1);
    if (ct_cache[h].target == target && ct_cache[h].from == m) {
        if (ct_cache[h].fn) interp_call(ct_cache[h].m, ct_cache[h].fn);
        else rt_call(target);
        return;
    }
    const RtFunc *fn = NULL;
    const RtModule *to = NULL;
    if (target >= m->code_lo && target < m->code_hi && (fn = find_in(m, target))) to = m;
    else if (m != KERNEL && (fn = find_in(KERNEL, target))) to = KERNEL;
    ct_cache[h].from = m;
    ct_cache[h].target = target;
    ct_cache[h].m = to;
    ct_cache[h].fn = fn;
    if (fn) interp_call(to, fn);
    else rt_call(target);
}

#define RS R[(w >> 21) & 31]
#define RT R[(w >> 16) & 31]
#define RTN ((w >> 16) & 31)
#define RDN ((w >> 11) & 31)
#define SA ((w >> 6) & 31)
#define IMM ((uint32_t)(int32_t)(int16_t)w)
#define UIMM (w & 0xFFFF)
#define SET(n, v) do { uint32_t v_ = (v); if (n) R[n] = v_; } while (0)

/* A non-control instruction (recomp.py's Emitter.op()). */
static inline void exec(uint32_t pc, uint32_t w) {
    if (w == 0) return;
    switch (w >> 26) {
    case 0:
        switch (w & 63) {
        case 0: SET(RDN, RT << SA); return;
        case 2: SET(RDN, RT >> SA); return;
        case 3: SET(RDN, (uint32_t)((int32_t)RT >> SA)); return;
        case 4: SET(RDN, RT << (RS & 31)); return;
        case 6: SET(RDN, RT >> (RS & 31)); return;
        case 7: SET(RDN, (uint32_t)((int32_t)RT >> (RS & 31))); return;
        case 12: rt_syscall(pc); return;
        case 13: rt_break(pc, (w >> 6) & 0xFFFFF); return;
        case 16: SET(RDN, HI); return;
        case 17: HI = RS; return;
        case 18: SET(RDN, LO); return;
        case 19: LO = RS; return;
        case 24: { int64_t p = (int64_t)(int32_t)RS * (int32_t)RT; LO = (uint32_t)p; HI = (uint32_t)(p >> 32); return; }
        case 25: { uint64_t p = (uint64_t)RS * RT; LO = (uint32_t)p; HI = (uint32_t)(p >> 32); return; }
        case 26: rt_div(RS, RT); return;
        case 27: rt_divu(RS, RT); return;
        case 32: case 33: SET(RDN, RS + RT); return;
        case 34: case 35: SET(RDN, RS - RT); return;
        case 36: SET(RDN, RS & RT); return;
        case 37: SET(RDN, RS | RT); return;
        case 38: SET(RDN, RS ^ RT); return;
        case 39: SET(RDN, ~(RS | RT)); return;
        case 42: SET(RDN, (int32_t)RS < (int32_t)RT); return;
        case 43: SET(RDN, RS < RT); return;
        case 48: if ((int32_t)RS >= (int32_t)RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        case 49: if (RS >= RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        case 50: if ((int32_t)RS < (int32_t)RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        case 51: if (RS < RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        case 52: if (RS == RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        case 54: if (RS != RT) rt_break(pc, 0x7E0 | ((w >> 6) & 0x3FF)); return;
        }
        break;
    case 8: case 9: SET(RTN, RS + IMM); return;
    case 10: SET(RTN, (int32_t)RS < (int32_t)IMM); return;
    case 11: SET(RTN, RS < IMM); return;
    case 12: SET(RTN, RS & UIMM); return;
    case 13: SET(RTN, RS | UIMM); return;
    case 14: SET(RTN, RS ^ UIMM); return;
    case 15: SET(RTN, UIMM << 16); return;
    case 16:                                        /* COP0 */
        if (((w >> 21) & 31) == 0) { SET(RTN, rt_mfc0((int)RDN)); return; }
        if (((w >> 21) & 31) == 4) { rt_mtc0((int)RDN, RT); return; }
        if (((w >> 21) & 31) == 16 && (w & 63) == 16) { rt_rfe(); return; }
        break;
    case 18:                                        /* COP2 */
        if ((w >> 21) & 16) {
            extern uint32_t g_gte_pc;
            g_gte_pc = pc;                          /* gte.c: who's projecting */
            gte_command(w & 0x1FFFFFF);
            g_gte_pc = 0;
            return;
        }
        switch ((w >> 21) & 31) {
        case 0: SET(RTN, gte_read_data((int)RDN)); return;
        case 2: SET(RTN, gte_read_ctrl((int)RDN)); return;
        case 4: gte_write_data((int)RDN, RT); return;
        case 6: gte_write_ctrl((int)RDN, RT); return;
        }
        break;
    case 32: SET(RTN, (uint32_t)(int32_t)(int8_t)MEM_R8(RS + IMM)); return;
    case 33: SET(RTN, (uint32_t)(int32_t)(int16_t)MEM_R16(RS + IMM)); return;
    case 34: if (RTN) R[RTN] = rt_lwl(RT, RS + IMM); return;
    case 35: SET(RTN, MEM_R32(RS + IMM)); return;
    case 36: SET(RTN, MEM_R8(RS + IMM)); return;
    case 37: SET(RTN, MEM_R16(RS + IMM)); return;
    case 38: if (RTN) R[RTN] = rt_lwr(RT, RS + IMM); return;
    case 40: MEM_W8(RS + IMM, RT); return;
    case 41: MEM_W16(RS + IMM, RT); return;
    case 42: rt_swl(RS + IMM, RT); return;
    case 43: MEM_W32(RS + IMM, RT); return;
    case 46: rt_swr(RS + IMM, RT); return;
    case 50: gte_write_data((int)RTN, MEM_R32(RS + IMM)); return;
    case 58: gte_swc2(RS + IMM, (int)RTN); return;
    }
    rt_fatal("interp: unknown instruction %08X at %08X", w, pc);
}

/* ------------------------------------------------------------ idle loop */

/* The game waits for vertical blank in a loop (KERNEL 80025798-800257F4,
 * the Sony library's VSync): count a timeout down on the stack, compare
 * the vblank counter, poll. Nothing it reads changes between polls, so
 * the iterations up to the next poll can be done at once: the timeout
 * minus that many, the poll budget minus that many, v0 = 1. Exactly what
 * running them would do, much faster (it's where most frames' time goes).
 * Only when the code there is what this was written for (its hash). */
#define VS_FUNC 0x80025770u
#define VS_LOOP 0x80025798u
#define VS_BRANCH 0x800257F0u
#define VS_COUNT 0x8005B2FCu
static int vsync_known = -1;

static int vsync_check(const RtModule *m) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t a = VS_FUNC; a < VS_FUNC + 38 * 4; a += 4) {
        uint32_t w = fetch(m, a);
        for (int b = 0; b < 4; b++) h = (h ^ ((w >> (8 * b)) & 0xFF)) * 0x100000001B3ull;
    }
    return h == 0xB87EA752C0FCBD0Eull;
}

static inline void vsync_skip(void) {
    if ((int32_t)MEM_R32(VS_COUNT) >= (int32_t)R[4] || R[3] != 0xFFFFFFFFu) return;
    uint32_t sp = R[29] + 16;
    int32_t c = (int32_t)MEM_R32(sp);
    int32_t n = g_rt_budget - 1;
    if (c >= 0 && n > c) n = c;              /* the timeout must not reach -1 */
    if (n <= 0) return;
    MEM_W32(sp, (uint32_t)c - (uint32_t)n);
    g_rt_budget -= n;
    R[2] = 1;
}

/* ------------------------------------------------------------ a function */

/* Resume keys of f (the cases of the translation's resume switch). */
static int resume_ok(const RtModule *m, const InFunc *f, uint32_t key, uint32_t *target) {
    if (key & 1) {                                   /* after the poll of a backward jump */
        uint32_t pc = key & ~1u;
        if (!in_func(m, f, pc) || in_delay(m, f, pc)) return 0;
        uint32_t w = fetch(m, pc), op = w >> 26, t;
        if (op == 2) {
            t = ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2);
            if (annotation(m, f, pc) != 0) return 0;
        } else if ((op >= 4 && op <= 7) || (op == 1 && (((w >> 16) & 31) == 0 || ((w >> 16) & 31) == 1))) {
            t = pc + 4 + (IMM << 2);
        } else return 0;
        if (t > pc) return 0;
        *target = t;
        return 1;
    }
    uint32_t pc = key - 8;                           /* a call's return address */
    if (!in_func(m, f, pc) || in_delay(m, f, pc) || !in_func(m, f, key)) return 0;
    uint32_t w = fetch(m, pc), op = w >> 26;
    if (!(op == 3 || (op == 0 && (w & 63) == 9) || (op == 1 && (((w >> 16) & 31) == 16 || ((w >> 16) & 31) == 17))))
        return 0;
    *target = key;
    return 1;
}

static void run_func(const RtModule *m, const InFunc *f) {
    uint32_t pc, t, w, d;
    uint32_t plo = 0, plen = 0;                 /* words a mod replaces */
    if (__builtin_expect(m->npatch != 0, 0)) {
        int k = (int)(m - rt_modules);
        plo = patch_span[k].lo;
        plen = patch_span[k].hi - plo;
    }
#define FETCH(a) (__builtin_expect((a) - plo < plen, 0) ? patched(m, (a)) : ram_word(a))
    if (__builtin_expect(g_resume_pc != 0, 0)) {
        uint32_t key = g_resume_pc;
        if (!resume_ok(m, f, key, &t)) { g_resume_miss = 1; return; }
        g_resume_pc = 0;
        if (key & 1) goto jump;
        pc = t;
        goto next;
    }
    if (f->hook) f->hook();
    t = f->entry;
jump:
    /* goto L_t: a label in a delay slot runs that instruction, then
     * carries on after it (or calls what follows, outside f) */
    if (__builtin_expect(f->flags & IN_DELAY_LABELS, 0) && in_delay(m, f, t)) {
        exec(t, fetch(m, t));
        if (in_func(m, f, t + 4) && !in_delay(m, f, t + 4)) { pc = t + 4; goto next; }
        call_target(m, t + 4);
        return;
    }
    pc = t;
next:
    for (;;) {
        w = FETCH(pc);
        uint32_t op = w >> 26;
        if (op == 0) {
            uint32_t fn = w & 63;
            if (fn == 8) {                                   /* jr */
                if (((w >> 21) & 31) == 31) { exec(pc + 4, FETCH(pc + 4)); return; }
                t = RS;
                exec(pc + 4, FETCH(pc + 4));
                int code = annotation(m, f, pc);
                if (code >= 3) {
                    RT_POLL_NOSAVE(pc);
                    const uint32_t *jt = m->jt + 2 * (code - 3);
                    for (uint32_t k = 0; k < jt[1]; k++)
                        if (MEM_R32(jt[0] + 4 * k) == t) goto jump;
                    rt_badjump(pc, t);
                    return;
                }
                rt_call(t);
                return;
            }
            if (fn == 9) {                                   /* jalr */
                t = RS;
                exec(pc + 4, FETCH(pc + 4));
                if (RDN) R[RDN] = pc + 8;
                rt_call(t);
                pc += 8;
                continue;
            }
            exec(pc, w);
            pc += 4;
            continue;
        }
        if (op == 2 || op == 3) {
            t = ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2);
            exec(pc + 4, FETCH(pc + 4));
            if (op == 3) {                                   /* jal */
                R[31] = pc + 8;
                call_target(m, t);
                pc += 8;
                continue;
            }
            if (annotation(m, f, pc) == 0) {                 /* j inside f */
                if (t <= pc) RT_POLL_AT(pc);
                goto jump;
            }
            call_target(m, t);                               /* j elsewhere: tail call */
            return;
        }
        if ((op >= 4 && op <= 7) || op == 1) {
            int c;
            uint32_t rs = RS, rt = RT;
            switch (op) {
            case 4: c = rs == rt; break;
            case 5: c = rs != rt; break;
            case 6: c = (int32_t)rs <= 0; break;
            case 7: c = (int32_t)rs > 0; break;
            default:
                switch (RTN) {
                case 0: case 16: c = (int32_t)rs < 0; break;
                case 1: case 17: c = (int32_t)rs >= 0; break;
                default: exec(pc, w); return;                /* invalid: reports */
                }
            }
            t = pc + 4 + (IMM << 2);
            if (op == 1 && RTN >= 16) {                      /* bltzal, bgezal */
                R[31] = pc + 8;
                exec(pc + 4, FETCH(pc + 4));
                if (c) call_target(m, t);
                pc += 8;
                continue;
            }
            d = FETCH(pc + 4);
            exec(pc + 4, d);
            if (c) {
                if (t <= pc) {
                    RT_POLL_AT(pc);
                    if (__builtin_expect(pc == VS_BRANCH, 0) && m == KERNEL) {
                        if (vsync_known < 0) vsync_known = vsync_check(m) && !getenv("RT_NO_IDLE_SKIP");
                        if (vsync_known) vsync_skip();
                    }
                }
                goto jump;
            }
            pc += 8;
            continue;
        }
        exec(pc, w);
        pc += 4;
    }
}

__attribute__((constructor)) static void interp_init(void) { init_spans(); }
