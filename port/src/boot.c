/* Start the game: load KERNEL.BIN (a PS-X EXE) from the disc into RAM and
 * call its entry point, as the boot stub SLUS_010.87 does. */
#include "render.h"
#include "rt.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

int iso_read_file(const char *name, uint8_t **out, uint32_t *size);

static FILE *logf;
int g_opt_scale = 1, g_opt_threads = 0;

void rt_log(const char *fmt, ...) {
    va_list ap;
    FILE *f = logf ? logf : stderr;
    fprintf(f, "[%6u] ", g_frame);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}

void rt_set_log(FILE *f) { logf = f; }

void rt_dump_state(void) {
    if (getenv("RT_NO_DUMP")) return;
#ifdef _WIN32
    /* .dmp, not .bin: the Windows build picks up a .bin next to it as the disc */
    FILE *f = fopen("ram_dump.dmp", "wb");
    if (f) { fwrite(g_ram, 1, RAM_SIZE, f); fclose(f); }
    f = fopen("vram_dump.dmp", "wb");
#else
    FILE *f = fopen("ram_dump.bin", "wb");
    if (f) { fwrite(g_ram, 1, RAM_SIZE, f); fclose(f); }
    f = fopen("vram_dump.bin", "wb");
#endif
    if (f) { fwrite(g_vram, 1, sizeof g_vram, f); fclose(f); }
}

int rt_boot(const char *disc_path) {
    if (!cd_open(disc_path)) {
        fprintf(stderr, "can't open disc image %s\n", disc_path);
        return 0;
    }
    uint8_t *exe;
    uint32_t n;
    if (!iso_read_file("KERNEL.BIN", &exe, &n) || memcmp(exe, "PS-X EXE", 8)) {
        fprintf(stderr, "KERNEL.BIN not found on the disc\n");
        return 0;
    }
    uint32_t pc0, taddr, tsize;
    memcpy(&pc0, exe + 0x10, 4);
    memcpy(&taddr, exe + 0x18, 4);
    memcpy(&tsize, exe + 0x1C, 4);
    memcpy(ram_ptr(taddr), exe + 0x800, tsize);
    free(exe);

    hw_init();
    bios_init();
    {
        /* renderer: scale from the platform (g_opt_scale), one thread per core */
        extern int g_opt_scale, g_opt_threads;
        int cores = 4;
#ifdef _SC_NPROCESSORS_ONLN
        cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
        if (g_opt_threads > 0) cores = g_opt_threads;
        if (getenv("RT_THREADS")) cores = atoi(getenv("RT_THREADS"));
        render_init(g_opt_scale, cores > 0 ? cores : 1);
    }
    memset(&g_cpu, 0, sizeof g_cpu);
    R[29] = R[30] = 0x801FFF00;            /* SYSTEM.CNF STACK */
    R[31] = 0;
    rt_log("boot: KERNEL.BIN entry %08X, %u bytes at %08X", pc0, tsize, taddr);
    rt_call(pc0);
    rt_log("game returned from its entry point");
    return 1;
}
