/* Runtime-internal declarations (not used by generated code). */
#ifndef RT_H
#define RT_H

#include <stdarg.h>
#include <stdint.h>

#include "recomp.h"

/* The PS1 CPU runs at 33.8688 MHz. Each poll slice stands for a guess at
 * how long the game's code between polls took on the real machine. */
#define CPU_HZ        33868800
#define POLL_SLICE    64          /* backward branches per poll */
#define POLL_CYCLES   (POLL_SLICE * 24)

extern uint64_t g_cycles;
extern uint32_t g_frame;
extern uint32_t g_cop0_sr, g_cop0_cause, g_cop0_epc;

void rt_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_dump_state(void);
int rt_hle_call(uint32_t addr);
int rt_irq_enabled(void);
void irq_check(void);
void rt_return_from_exception(void) __attribute__((noreturn));

/* Pointer into RAM for a game address (RAM only). */
static inline uint8_t *ram_ptr(uint32_t a) { return g_ram + (a & 0x1FFFFF); }
void ram_read(uint32_t a, void *dst, uint32_t n);
void ram_write(uint32_t a, const void *src, uint32_t n);
const char *ram_str(uint32_t a);

/* thread.c */
uint32_t thread_open(uint32_t pc, uint32_t sp, uint32_t gp);
int thread_close(uint32_t h);
int thread_change(uint32_t h);
void coro_init(void);
void coro_irq_run(void (*body)(void));
void coro_irq_return(void) __attribute__((noreturn));

/* mdec.c: the frame the last movie picture was decoded on */
extern uint32_t g_mdec_frame;

/* card.c */
void card_set_dir(const char *dir);
const char *card_last_file(int written);         /* "bu00:NAME", "" if none */
int card_file_digest(const char *full, uint32_t *out);
unsigned card_write_count(void);

/* bios.c */
void bios_call(uint32_t table, uint32_t fn);
void bios_exception(void);
void bios_init(void);
void bios_vblank(void);

/* hardware: interrupts (irq.c) */
enum { IRQ_VBLANK = 0, IRQ_GPU, IRQ_CDROM, IRQ_DMA, IRQ_TMR0, IRQ_TMR1, IRQ_TMR2, IRQ_SIO0, IRQ_SIO, IRQ_SPU };
extern uint32_t g_istat, g_imask;
void irq_raise(int n);
int irq_pending(void);

/* hardware clock: advances timers, vblank, CD, etc. */
void hw_advance(uint32_t cycles);
void hw_init(void);
uint32_t hw_read(uint32_t a, int size);
void hw_write(uint32_t a, uint32_t v, int size);

/* gpu.c */
void gpu_init(void);
void gpu_write_gp0(uint32_t v);
void gpu_write_gp1(uint32_t v);
uint32_t gpu_read(void);
uint32_t gpu_status(void);
void gpu_vblank(void);
extern uint32_t g_gp0_src;
extern int g_widescreen, g_subpixel;
int gte_precise(uint32_t addr, uint32_t word, float *x, float *y, float *z);
void gte_precise_frame(void);
extern uint16_t g_vram[512][1024];
typedef struct {
    int x, y, w, h;          /* displayed VRAM area */
    int rgb24;
    int enabled;
} GpuDisplay;
void gpu_display(GpuDisplay *d);
void display_grab_rgb(uint8_t *rgb, int *w, int *h);
void display_grab_bgrx(uint32_t *out, int *w, int *h);   /* out: 2560x1920 */
void display_frame_update(void);
extern int g_opt_widescreen;

/* dma.c */
void dma_write(uint32_t a, uint32_t v);
uint32_t dma_read(uint32_t a);

/* cdrom.c */
int cd_open(const char *path);
void cd_init(void);
uint8_t cd_read_reg(int r);
void cd_write_reg(int r, uint8_t v);
void cd_advance(uint32_t cycles);
uint32_t cd_dma_read(void);
int cd_read_sector_raw(uint32_t lba, uint8_t out[2352]);

/* timers.c */
void timers_advance(uint32_t cycles);
uint32_t timers_read(uint32_t a);
void timers_write(uint32_t a, uint32_t v);
void timers_hblank(int n);

/* spu.c */
uint16_t spu_read(uint32_t a);
void spu_write(uint32_t a, uint16_t v);
void spu_dma_write(uint32_t v);
uint32_t spu_dma_read(void);
void spu_advance(uint32_t cycles);
void spu_xa_sector(const uint8_t *sector);
int spu_take_samples(int16_t *dst, int n);   /* stereo frames at 44.1 kHz */
int spu_samples_available(void);

/* sio.c (controllers, memory cards) */
uint32_t sio_read(uint32_t a, int size);
void sio_write(uint32_t a, uint32_t v, int size);
void sio_advance(uint32_t cycles);

/* mdec.c */
uint32_t mdec_read(uint32_t a);
void mdec_write(uint32_t a, uint32_t v);
void mdec_dma_in(uint32_t v);
uint32_t mdec_dma_out(void);
int mdec_out_ready(void);
void dma_mdec_kick(void);

/* input from the platform layer: PS1 pad and mouse state */
typedef struct {
    uint16_t buttons;        /* PS1 button bits, 1 = pressed */
    int mouse_present;
    int mouse_dx, mouse_dy;  /* accumulated since last poll */
    int mouse_left, mouse_right;
} InputState;
extern InputState g_input;

/* platform hooks (headless or SDL) */
int game_player_in_control(void);
void rt_set_look_limit(int degrees);
void plat_frame(void);      /* called once per vblank */
int plat_quit(void);
void plat_fatal(const char *msg);   /* show the error before exiting */

#endif
