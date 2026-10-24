/* Save states: the whole running game written to a file and read back.
 *
 * Each runtime module lists its state with SS() in a <module>_state()
 * function; the same function saves and loads. The file is a list of named
 * chunks, so a newer build can still read a save as long as the chunks it
 * knows haven't changed size. */
#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct StateIO StateIO;

void ss_field(StateIO *io, const char *name, void *p, size_t n);
/* A growable buffer of *n elements of elem bytes (*cap allocated). */
void ss_buffer(StateIO *io, const char *name, void **p, uint32_t *n, uint32_t *cap, size_t elem);
void ss_module(StateIO *io, const char *module);   /* prefix for the names that follow */

#define SS(io, v) ss_field((io), #v, &(v), sizeof(v))

/* Module state visitors. */
void rt_state(StateIO *io);
void hw_state(StateIO *io);
void timers_state(StateIO *io);
void sio_state(StateIO *io);
void gpu_state(StateIO *io);
void gte_state(StateIO *io);
void spu_state(StateIO *io);
void cd_state(StateIO *io);
void mdec_state(StateIO *io);
void bios_state(StateIO *io);
void card_state(StateIO *io);
void thread_state(StateIO *io);
void decomp_state(StateIO *io);

/* Requests from the platform layer, served at the next safe point. */
void ss_request_save(const char *path);
void ss_request_load(const char *path);
extern volatile int g_ss_pending;
void ss_service(void);                 /* called by rt_poll */
/* The platform layer is told how it went (msg is for the player). */
void plat_ss_done(int ok, int was_load, const char *msg);

/* Safe-point bookkeeping: snapshots aren't taken inside these. */
extern int g_hle_depth, g_native_depth;

/* Loading: continue the translated code at pc (a return address, or a
 * poll's address | 1), then each caller in turn. Doesn't return. */
void ss_resume_chain(uint32_t pc) __attribute__((noreturn));

#endif
