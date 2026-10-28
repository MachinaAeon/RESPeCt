/* Save states (see savestate.h).
 *
 * Saving: at a safe point (the end of a poll, on a game thread, not inside
 * an interrupt handler, the BIOS emulation or native decompiled code) every
 * module writes its state. The translated code keeps no state of its own on
 * the host stack between instructions (registers live in g_cpu), so the
 * only thing about "where the game is" that a file can't hold is the chain
 * of host function calls, and that is rebuilt on loading:
 *
 * Loading: the state is read back, then each game thread gets a fresh
 * coroutine that re-enters the translated function containing its saved
 * position (each function has a resume switch for its call return
 * addresses and loop polls, emitted by tools/recomp/recomp.py). When that
 * function returns, R[31] holds its caller's return address: re-enter the
 * caller there, and so on up the chain. */
#include "savestate.h"
#include "render.h"
#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS_MAGIC "RESURVSS"
#define SS_VERSION 1

int g_hle_depth, g_native_depth;
volatile int g_ss_pending;
int g_ss_skip;                  /* testing: let this many safe points pass first */
static int pending_load;
static char pending_path[1024];

typedef struct {
    const char *name;
    uint32_t size;
    const uint8_t *data;
} Chunk;

enum { SS_SAVE, SS_CHECK, SS_APPLY };

struct StateIO {
    int mode;
    FILE *f;
    char prefix[32];
    Chunk *chunks;
    int nchunks;
    int bad, missing;
    char err[256];
};

void ss_module(StateIO *io, const char *module) { snprintf(io->prefix, sizeof io->prefix, "%s", module); }

static const Chunk *find_chunk(StateIO *io, const char *full) {
    for (int i = 0; i < io->nchunks; i++)
        if (!strcmp(io->chunks[i].name, full)) return &io->chunks[i];
    return NULL;
}

static void write_chunk(StateIO *io, const char *full, const void *p, uint32_t n) {
    uint16_t len = (uint16_t)strlen(full);
    fwrite(&len, 2, 1, io->f);
    fwrite(full, 1, len, io->f);
    fwrite(&n, 4, 1, io->f);
    if (n) fwrite(p, 1, n, io->f);
}

void ss_field(StateIO *io, const char *name, void *p, size_t n) {
    char full[96];
    snprintf(full, sizeof full, "%s.%s", io->prefix, name);
    if (io->mode == SS_SAVE) { write_chunk(io, full, p, (uint32_t)n); return; }
    const Chunk *c = find_chunk(io, full);
    if (!c) {
        if (io->mode == SS_CHECK) { io->missing++; rt_log("savestate: %s not in the file (kept)", full); }
        return;
    }
    if (c->size != n) {
        if (io->mode == SS_CHECK && !io->bad)
            snprintf(io->err, sizeof io->err, "%s is %u bytes in the file, %u in this build", full, c->size, (unsigned)n);
        io->bad = 1;
        return;
    }
    if (io->mode == SS_APPLY) memcpy(p, c->data, n);
}

void ss_buffer(StateIO *io, const char *name, void **p, uint32_t *n, uint32_t *cap, size_t elem) {
    char full[96];
    snprintf(full, sizeof full, "%s.%s", io->prefix, name);
    if (io->mode == SS_SAVE) { write_chunk(io, full, *p, (uint32_t)(*n * elem)); return; }
    const Chunk *c = find_chunk(io, full);
    if (!c) { if (io->mode == SS_CHECK) io->missing++; return; }
    if (c->size % elem) { io->bad = 1; return; }
    if (io->mode != SS_APPLY) return;
    uint32_t count = c->size / (uint32_t)elem;
    if (count > *cap) {
        *p = realloc(*p, (size_t)count * elem);
        *cap = count;
    }
    if (count) memcpy(*p, c->data, (size_t)count * elem);
    *n = count;
}

static void visit_all(StateIO *io) {
    rt_state(io);
    hw_state(io);
    timers_state(io);
    sio_state(io);
    gpu_state(io);
    gte_state(io);
    spu_state(io);
    cd_state(io);
    mdec_state(io);
    bios_state(io);
    card_state(io);
    thread_state(io);
    decomp_state(io);
    { void texpack_state(StateIO *io); texpack_state(io); }
}

/* ------------------------------------------------------------ requests */

void ss_request_save(const char *path) {
    snprintf(pending_path, sizeof pending_path, "%s", path);
    pending_load = 0;
    g_ss_pending = 1;
}

void ss_request_load(const char *path) {
    snprintf(pending_path, sizeof pending_path, "%s", path);
    pending_load = 1;
    g_ss_pending = 1;
}

static int save_now(const char *path, char *msg, size_t msgn) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    StateIO io = {0};
    io.mode = SS_SAVE;
    io.f = fopen(tmp, "wb");
    if (!io.f) { snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    uint32_t ver = SS_VERSION;
    fwrite(SS_MAGIC, 1, 8, io.f);
    fwrite(&ver, 4, 1, io.f);
    render_sync();
    visit_all(&io);
    int ok = !ferror(io.f);
    ok &= fclose(io.f) == 0;
    if (!ok) { remove(tmp); snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) { snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    { extern int thread_current(void);
      rt_log("savestate: saved %s at frame %u (resume %08X, thread %d, sp %08X, ra %08X)", path, g_frame, g_poll_pc | 1, thread_current(), R[29], R[31]); }
    snprintf(msg, msgn, "Saved");
    return 1;
}

static uint8_t *read_file(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

void thread_resume_all(void) __attribute__((noreturn));
void render_reload(void);
void gte_reset_caches(void);
void spu_reset_output(void);
void rt_reset_call_cache(void);

/* Returns only if the file can't be loaded (msg says why). */
static void load_now(const char *path, char *msg, size_t msgn) {
    long n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) { snprintf(msg, msgn, "No save in this slot"); return; }
    if (n < 12 || memcmp(b, SS_MAGIC, 8)) { free(b); snprintf(msg, msgn, "Not a save file"); return; }
    uint32_t ver;
    memcpy(&ver, b + 8, 4);
    if (ver != SS_VERSION) { free(b); snprintf(msg, msgn, "Save from an incompatible version"); return; }
    /* index the chunks; names are made NUL-terminated copies */
    StateIO io = {0};
    int cap = 64;
    io.chunks = malloc(sizeof(Chunk) * (size_t)cap);
    long p = 12;
    while (p + 6 <= n) {
        uint16_t len;
        uint32_t size;
        memcpy(&len, b + p, 2);
        if (p + 2 + len + 4 > n) break;
        char *name = malloc((size_t)len + 1);
        memcpy(name, b + p + 2, len);
        name[len] = 0;
        memcpy(&size, b + p + 2 + len, 4);
        const uint8_t *data = b + p + 2 + len + 4;
        if (p + 2 + len + 4 + (long)size > n) { free(name); break; }
        if (io.nchunks == cap) io.chunks = realloc(io.chunks, sizeof(Chunk) * (size_t)(cap *= 2));
        io.chunks[io.nchunks++] = (Chunk){name, size, data};
        p += 2 + len + 4 + size;
    }
    io.mode = SS_CHECK;
    visit_all(&io);
    if (io.bad) {
        rt_log("savestate: can't load %s: %s", path, io.err);
        snprintf(msg, msgn, "This save doesn't fit this version of the game");
        for (int i = 0; i < io.nchunks; i++) free((void *)io.chunks[i].name);
        free(io.chunks);
        free(b);
        return;
    }
    /* From here on the running game is replaced. */
    render_sync();
    io.mode = SS_APPLY;
    visit_all(&io);
    for (int i = 0; i < io.nchunks; i++) free((void *)io.chunks[i].name);
    free(io.chunks);
    free(b);
    render_reload();
    gte_reset_caches();
    spu_reset_output();
    { void cd_reapply_volume(void); cd_reapply_volume(); }
    rt_reset_call_cache();
    { extern int thread_current(void);
      rt_log("savestate: loaded %s, frame %u, continuing at %08X (thread %d, sp %08X, ra %08X)", path, g_frame, g_poll_pc | 1, thread_current(), R[29], R[31]); }
    plat_ss_done(1, 1, "Loaded");
    thread_resume_all();
}

int rt_in_interrupt(void);

void ss_service(void) {
    if (!g_ss_pending) return;
    /* only where the saved position can be continued from */
    if ((g_poll_pc & 3) || rt_in_interrupt() || g_hle_depth || g_native_depth) {
        static uint32_t last;
        if (getenv("RT_SS_DEBUG") && last != g_frame) {
            last = g_frame;
            rt_log("savestate: waiting (poll %08X irq %d hle %d native %d)", g_poll_pc, rt_in_interrupt(), g_hle_depth, g_native_depth);
        }
        return;
    }
    if (g_ss_skip > 0) { g_ss_skip--; return; }
    g_ss_pending = 0;
    char msg[200];
    if (pending_load) {
        load_now(pending_path, msg, sizeof msg);
        plat_ss_done(0, 1, msg);          /* only reached on failure */
    } else {
        int ok = save_now(pending_path, msg, sizeof msg);
        plat_ss_done(ok, 0, msg);
    }
}

/* ------------------------------------------------------------ resuming */

int rt_resume_candidates(uint32_t pc, const RtModule **mod, const RtFunc **out, int max);

static void resume_one(uint32_t pc) {
    const RtFunc *cand[16];
    const RtModule *m = NULL;
    int n = rt_resume_candidates(pc, &m, cand, 16);
    for (int i = 0; i < n; i++) {
        g_resume_pc = pc;
        g_resume_miss = 0;
        interp_resume(m, cand[i]);
        if (!g_resume_miss) return;     /* it continued there and has now returned */
    }
    g_resume_pc = 0;
    rt_fatal("can't continue the saved game at %08X", pc);
}

void ss_resume_chain(uint32_t pc) {
    for (;;) {
        if (!pc) rt_fatal("a resumed game thread returned to nothing");
        if (getenv("RT_SS_DEBUG")) rt_log("savestate: continuing at %08X (sp %08X)", pc, R[29]);
        resume_one(pc);
        pc = R[31];
    }
}
