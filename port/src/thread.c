/* BIOS threads (OpenThread / ChangeThread / CloseThread).
 *
 * Each game thread runs the recompiled code on its own native stack, so a
 * thread switch is a coroutine switch plus swapping the game's register
 * file. Linux uses ucontext, Windows uses fibers. */
#ifdef _WIN32
#include <windows.h>          /* before rt.h: its R macro clashes with winnt.h */
#else
#include <ucontext.h>
#endif

#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 8
#define HOST_STACK (1 << 20)

typedef struct {
    int used, started;
    int hle_depth, native_depth;          /* savestate.c bookkeeping, per thread */
    Cpu cpu;
    uint32_t pc, sp, gp;
    uint32_t sr;
#ifdef _WIN32
    void *fiber;
#else
    ucontext_t ctx;
    void *stack;
#endif
} Thread;

static Thread threads[MAX_THREADS];
static int current;

static void thread_main(void) {
    Thread *t = &threads[current];
    R[29] = R[30] = t->sp;
    R[28] = t->gp;
    R[31] = 0;
    rt_call(t->pc);
    rt_fatal("thread %d returned from %08X", current, t->pc);
}

#ifdef _WIN32
static void CALLBACK fiber_entry(void *arg) { (void)arg; thread_main(); }
#endif

static void init_main(void) {
    if (threads[0].used) return;
    threads[0].used = 1;
    threads[0].started = 1;
#ifdef _WIN32
    threads[0].fiber = ConvertThreadToFiber(NULL);
#endif
}

uint32_t thread_open(uint32_t pc, uint32_t sp, uint32_t gp) {
    init_main();
    if (getenv("RT_SS_DEBUG")) rt_log("thread: open pc %08X sp %08X", pc, sp);
    for (int i = 1; i < MAX_THREADS; i++) {
        Thread *t = &threads[i];
        if (t->used || i == current) continue;
        memset(&t->cpu, 0, sizeof t->cpu);
        t->used = 1;
        t->started = 0;
        t->pc = pc;
        t->sp = sp;
        t->gp = gp;
        t->sr = 0x404 | 1;
#ifdef _WIN32
        if (t->fiber) DeleteFiber(t->fiber);
        t->fiber = CreateFiber(HOST_STACK, fiber_entry, NULL);
#else
        if (!t->stack) t->stack = malloc(HOST_STACK);
        getcontext(&t->ctx);
        t->ctx.uc_stack.ss_sp = t->stack;
        t->ctx.uc_stack.ss_size = HOST_STACK;
        t->ctx.uc_link = NULL;
        makecontext(&t->ctx, thread_main, 0);
#endif
        return 0xFF000000u | (uint32_t)i;
    }
    rt_log("OpenThread: no free thread");
    return 0xFFFFFFFF;
}

int thread_close(uint32_t h) {
    /* A thread may close itself and then switch away: it keeps running
     * until then, and its slot isn't reused while it's current. */
    int i = (int)(h & 0xFFFF);
    if (i <= 0 || i >= MAX_THREADS) return 0;
    threads[i].used = 0;
    return 1;
}

int thread_change(uint32_t h) {
    init_main();
    int i = (int)(h & 0xFFFF);
    if ((h & 0xFF000000) != 0xFF000000 || i >= MAX_THREADS || (!threads[i].used && i != 0)) {
        rt_log("ChangeThread: bad handle %08X", h);
        return 0;
    }
    if (getenv("RT_SS_DEBUG")) rt_log("thread: change %d -> %d (ra %08X)", current, i, R[31]);
    if (i == current) return 1;
    Thread *from = &threads[current], *to = &threads[i];
    R[2] = 1;                             /* ChangeThread returns 1 when resumed */
    from->cpu = g_cpu;
    from->sr = g_cop0_sr;
    from->hle_depth = g_hle_depth;
    from->native_depth = g_native_depth;
    g_hle_depth = to->started ? to->hle_depth : 0;
    g_native_depth = to->started ? to->native_depth : 0;
    if (to->started)
        g_cpu = to->cpu;                  /* a new thread's are set by thread_main */
    to->started = 1;
    g_cop0_sr = to->sr;
    current = i;
#ifdef _WIN32
    SwitchToFiber(to->fiber);
#else
    swapcontext(&from->ctx, &to->ctx);
#endif
    /* Back on this thread: whoever switched here loaded our registers. */
    return 1;
}

int thread_current(void) { return current; }

/* ------------------------------------------------------------ save states */

void thread_state(StateIO *io) {
    ss_module(io, "thread");
    for (int i = 0; i < MAX_THREADS; i++) {
        char n[32];
        Thread *t = &threads[i];
        snprintf(n, sizeof n, "t%d.used", i);    ss_field(io, n, &t->used, sizeof t->used);
        snprintf(n, sizeof n, "t%d.started", i); ss_field(io, n, &t->started, sizeof t->started);
        snprintf(n, sizeof n, "t%d.cpu", i);     ss_field(io, n, &t->cpu, sizeof t->cpu);
        snprintf(n, sizeof n, "t%d.pc", i);      ss_field(io, n, &t->pc, sizeof t->pc);
        snprintf(n, sizeof n, "t%d.sp", i);      ss_field(io, n, &t->sp, sizeof t->sp);
        snprintf(n, sizeof n, "t%d.gp", i);      ss_field(io, n, &t->gp, sizeof t->gp);
        snprintf(n, sizeof n, "t%d.sr", i);      ss_field(io, n, &t->sr, sizeof t->sr);
    }
    SS(io, current);
}

/* After loading: every game thread gets a fresh coroutine. The current one
 * continues where the save was made (just after a poll); one that was
 * switched away from sits in ChangeThread, which returns straight to its
 * caller, so it continues at its saved return address; one never started
 * starts from the beginning. The coroutine we're leaving is dropped. */
static uint32_t resume_at[MAX_THREADS];

#ifdef _WIN32
static void *primary_fiber, *zombie;
#else
static void *zombie;
static void *stack_of[MAX_THREADS];
#endif

static void resumed_entry(void) {
    if (zombie) {
#ifdef _WIN32
        if (zombie != primary_fiber) DeleteFiber(zombie);
#else
        free(zombie);
#endif
        zombie = NULL;
    }
    uint32_t pc = resume_at[current];
    resume_at[current] = 0;
    if (pc) ss_resume_chain(pc);
    thread_main();
}

#ifdef _WIN32
static void CALLBACK resumed_fiber(void *arg) { (void)arg; resumed_entry(); }
#endif

void thread_resume_all(void) {
    g_hle_depth = g_native_depth = 0;
    for (int i = 0; i < MAX_THREADS; i++) threads[i].hle_depth = threads[i].native_depth = 0;
    for (int i = 0; i < MAX_THREADS; i++)
        resume_at[i] = !threads[i].used ? 0 : i == current ? (g_poll_pc | 1) : threads[i].started ? threads[i].cpu.r[31] : 0;
#ifdef _WIN32
    void *leaving = GetCurrentFiber();
    if (!primary_fiber) primary_fiber = threads[0].fiber;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = &threads[i];
        if (t->fiber && t->fiber != leaving && t->fiber != primary_fiber) DeleteFiber(t->fiber);
        /* reserve as much stack as the exe's main thread has (8 MB) */
        t->fiber = t->used ? CreateFiberEx(64 << 10, 8 << 20, 0, resumed_fiber, NULL) : NULL;
    }
    zombie = leaving;
    SwitchToFiber(threads[current].fiber);
#else
    void *leaving_stack = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = &threads[i];
        /* the stack we're running on is freed by the next thread to start */
        if (t->stack) {
            char here;
            if (&here >= (char *)t->stack && &here < (char *)t->stack + HOST_STACK) leaving_stack = t->stack;
            else free(t->stack);
        }
        t->stack = NULL;
        if (!t->used) continue;
        t->stack = malloc(HOST_STACK);
        getcontext(&t->ctx);
        t->ctx.uc_stack.ss_sp = t->stack;
        t->ctx.uc_stack.ss_size = HOST_STACK;
        t->ctx.uc_link = NULL;
        makecontext(&t->ctx, resumed_entry, 0);
    }
    (void)stack_of;
    zombie = leaving_stack;
    setcontext(&threads[current].ctx);
#endif
    rt_fatal("couldn't switch to the loaded game");
}

/* ------------------------------------------------------------ interrupts
 * The game's interrupt handler runs on its own coroutine. It ends with
 * ReturnFromException, which switches back to whatever was interrupted;
 * the handler's stack is simply abandoned (a fresh one is set up next
 * time), like the real BIOS throwing away its exception frame. */

#define IRQ_STACK (512 << 10)
static void (*irq_body)(void);

#ifdef _WIN32
static void *irq_fiber, *irq_ret_fiber;
static void CALLBACK irq_entry(void *arg) { (void)arg; irq_body(); rt_fatal("interrupt handler fell through"); }

void coro_init(void) { init_main(); }

void coro_irq_run(void (*body)(void)) {
    init_main();
    irq_body = body;
    irq_ret_fiber = GetCurrentFiber();
    if (irq_fiber) DeleteFiber(irq_fiber);
    irq_fiber = CreateFiber(IRQ_STACK, irq_entry, NULL);
    SwitchToFiber(irq_fiber);
}

void coro_irq_return(void) {
    SwitchToFiber(irq_ret_fiber);
    rt_fatal("resumed a finished interrupt handler");
}
#else
static ucontext_t irq_ctx, irq_ret_ctx;
static void *irq_stack;
static void irq_entry(void) { irq_body(); rt_fatal("interrupt handler fell through"); }

void coro_init(void) {}

void coro_irq_run(void (*body)(void)) {
    irq_body = body;
    if (!irq_stack) irq_stack = malloc(IRQ_STACK);
    getcontext(&irq_ctx);
    irq_ctx.uc_stack.ss_sp = irq_stack;
    irq_ctx.uc_stack.ss_size = IRQ_STACK;
    irq_ctx.uc_link = NULL;
    makecontext(&irq_ctx, irq_entry, 0);
    swapcontext(&irq_ret_ctx, &irq_ctx);
}

void coro_irq_return(void) {
    setcontext(&irq_ret_ctx);
    rt_fatal("setcontext failed");
}
#endif
