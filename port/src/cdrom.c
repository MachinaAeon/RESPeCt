/* CD-ROM controller (psx-spx "CDROM Controller"), backed by a raw
 * 2352-byte/sector disc image. Commands answer with INT3 (acknowledge),
 * some with a later INT2 (complete); reads deliver one INT1 per sector.
 * XA-ADPCM audio sectors go to the SPU instead of the data buffer. */
#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_CYCLES (CPU_HZ / 75)

uint32_t disc_image_sectors(void);      /* discimg.c */
int disc_image_open(const char *path, char *err, size_t n);

typedef struct {
    int irq;                    /* INT type 1-5 */
    uint8_t data[16];
    int n;
    int32_t delay;              /* cycles before it can be delivered */
    int is_sector;
    uint32_t lba;
} Resp;

#define QMAX 8
static struct {
    int index;
    uint8_t params[16];
    int nparams;
    uint8_t resp[16];
    int nresp, rpos;
    uint8_t ienable, iflag;
    Resp q[QMAX];
    int qn;
    uint8_t stat;
    uint8_t mode;
    uint32_t setloc, pos;       /* LBA */
    int reading, playing, muted;
    int32_t read_timer;
    uint8_t filter_file, filter_chan;
    uint8_t sector[2352];
    uint8_t lastheader[8];
    uint8_t data[2352];
    int data_len, data_pos, data_ready;
    int busy;
} cd;

static uint8_t atv[4] = {0x80, 0, 0x80, 0}, atv_next[4] = {0x80, 0, 0x80, 0};
static uint8_t adpcm_mute;
void spu_cd_volume(int ll, int lr, int rr, int rl, int mute);

int cd_open(const char *path) {
    char err[512];
    if (disc_image_open(path, err, sizeof err)) return 1;
    rt_log("disc: %s: %s", path, err);
    return 0;
}

void cd_init(void) {
    memset(&cd, 0, sizeof cd);
    cd.stat = 0x02;                       /* motor on */
}

static int bcd(int v) { return ((v / 10) << 4) | (v % 10); }
static int unbcd(int v) { return (v >> 4) * 10 + (v & 15); }

static void push(int irq, int32_t delay, const uint8_t *d, int n) {
    if (cd.qn >= QMAX) {
        /* Drop the oldest pending sector to make room (like an overrun). */
        for (int i = 0; i < cd.qn; i++) {
            if (cd.q[i].is_sector) {
                memmove(&cd.q[i], &cd.q[i + 1], (size_t)(cd.qn - i - 1) * sizeof(Resp));
                cd.qn--;
                break;
            }
        }
        if (cd.qn >= QMAX) return;
    }
    Resp *r = &cd.q[cd.qn++];
    r->irq = irq;
    r->delay = delay;
    r->n = n;
    r->is_sector = 0;
    memcpy(r->data, d, (size_t)n);
}

static void push_stat(int irq, int32_t delay) {
    uint8_t s = cd.stat;
    push(irq, delay, &s, 1);
}

static void stop_reading(void) {
    cd.reading = 0;
    cd.stat &= ~0xE0u;
}

int g_trace_cd = -1;

static void command(uint8_t c) {
    uint8_t *p = cd.params;
    if (g_trace_cd < 0) g_trace_cd = getenv("RT_TRACE_CD") != NULL;
    if (g_trace_cd) {
        char buf[64] = "";
        for (int i = 0; i < cd.nparams; i++) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %02X", p[i]);
        rt_log("CD cmd %02X%s (mode %02X)", c, buf, cd.mode);
    }
    cd.stat &= ~1u;
    switch (c) {
    case 0x01: push_stat(3, 20000); break;                         /* Getstat */
    case 0x02:                                                       /* Setloc */
        cd.setloc = (uint32_t)((unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2])) - 150;
        push_stat(3, 20000);
        break;
    case 0x03: cd.playing = 1; push_stat(3, 20000); break;          /* Play (no CD audio) */
    case 0x06: case 0x1B:                                            /* ReadN, ReadS */
        cd.pos = cd.setloc;
        cd.reading = 1;
        cd.stat = (cd.stat & ~0xE0u) | 0x20 | 0x02;
        cd.read_timer = (cd.mode & 0x80) ? SECTOR_CYCLES / 2 : SECTOR_CYCLES;
        push_stat(3, 20000);
        break;
    case 0x07: cd.stat |= 2; push_stat(3, 20000); push_stat(2, 400000); break;   /* MotorOn */
    case 0x08: stop_reading(); push_stat(3, 20000); cd.stat &= ~2u; push_stat(2, 400000); break;
    case 0x09:                                                       /* Pause */
        push_stat(3, 20000);
        stop_reading();
        cd.playing = 0;
        push_stat(2, (cd.mode & 0x80) ? 140000 : 280000);
        break;
    case 0x0A:                                                       /* Init */
        cd.mode = 0;
        stop_reading();
        cd.stat = 0x02;
        push_stat(3, 20000);
        push_stat(2, 80000);
        break;
    case 0x0B: cd.muted = 1; push_stat(3, 20000); break;
    case 0x0C: cd.muted = 0; push_stat(3, 20000); break;
    case 0x0D: cd.filter_file = p[0]; cd.filter_chan = p[1]; push_stat(3, 20000); break;
    case 0x0E: cd.mode = p[0]; push_stat(3, 20000); break;
    case 0x0F: {
        uint8_t r[5] = {cd.stat, cd.mode, 0, cd.filter_file, cd.filter_chan};
        push(3, 20000, r, 5);
        break;
    }
    case 0x10: push(3, 20000, cd.lastheader, 8); break;            /* GetlocL */
    case 0x11: {                                                     /* GetlocP */
        uint32_t a = cd.pos + 150, rel = cd.pos;
        uint8_t r[8] = {1, 1, (uint8_t)bcd((int)(rel / 75 / 60)), (uint8_t)bcd((int)(rel / 75 % 60)),
                        (uint8_t)bcd((int)(rel % 75)), (uint8_t)bcd((int)(a / 75 / 60)),
                        (uint8_t)bcd((int)(a / 75 % 60)), (uint8_t)bcd((int)(a % 75))};
        push(3, 20000, r, 8);
        break;
    }
    case 0x12: push_stat(3, 20000); push_stat(2, 400000); break;   /* SetSession */
    case 0x13: { uint8_t r[3] = {cd.stat, 1, 1}; push(3, 20000, r, 3); break; }     /* GetTN */
    case 0x14: {                                                     /* GetTD */
        uint32_t a = p[0] == 0 ? disc_image_sectors() + 150 : 150;
        uint8_t r[3] = {cd.stat, (uint8_t)bcd((int)(a / 75 / 60)), (uint8_t)bcd((int)(a / 75 % 60))};
        push(3, 20000, r, 3);
        break;
    }
    case 0x15: case 0x16:                                            /* SeekL, SeekP */
        stop_reading();
        cd.pos = cd.setloc;
        push_stat(3, 20000);
        push_stat(2, 100000);
        break;
    case 0x19:                                                       /* Test */
        if (p[0] == 0x20) { uint8_t r[4] = {0x94, 0x09, 0x19, 0xC0}; push(3, 20000, r, 4); }
        else push_stat(3, 20000);
        break;
    case 0x1A: {                                                     /* GetID */
        push_stat(3, 20000);
        uint8_t r[8] = {0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'};
        push(2, 40000, r, 8);
        break;
    }
    case 0x1E: push_stat(3, 20000); push_stat(2, 400000); break;   /* ReadTOC */
    default:
        rt_log("CD: command %02X not handled", c);
        { uint8_t r[2] = {(uint8_t)(cd.stat | 1), 0x40}; push(5, 20000, r, 2); }
        break;
    }
    cd.nparams = 0;
}

/* A sector arrives under the read head. */
static void sector_tick(void) {
    uint8_t *s = cd.sector;
    cd_read_sector_raw(cd.pos, s);
    memcpy(cd.lastheader, s + 12, 8);
    cd.pos++;
    uint8_t file = s[16], chan = s[17], submode = s[18];
    if ((cd.mode & 0x40) && (submode & 0x04) && s[15] == 2) {
        /* XA-ADPCM audio: to the SPU (if the filter matches), not to the host. */
        if (!(cd.mode & 0x08) || (file == cd.filter_file && chan == cd.filter_chan))
            if (!cd.muted) spu_xa_sector(s);
        return;
    }
    if ((cd.mode & 0x08) && (submode & 0x04)) return;
    Resp r = {0};
    r.irq = 1;
    r.data[0] = cd.stat;
    r.n = 1;
    r.is_sector = 1;
    r.lba = cd.pos - 1;
    if (cd.qn < QMAX) cd.q[cd.qn++] = r;
}

static void deliver(void) {
    if (cd.iflag & 7) return;                /* previous one not acknowledged */
    if (!cd.qn || cd.q[0].delay > 0) return;
    Resp r = cd.q[0];
    memmove(&cd.q[0], &cd.q[1], (size_t)(cd.qn - 1) * sizeof(Resp));
    cd.qn--;
    if (r.is_sector) {
        uint8_t s[2352];
        cd_read_sector_raw(r.lba, s);
        if (cd.mode & 0x20) { memcpy(cd.data, s + 12, 2340); cd.data_len = 2340; }
        else { memcpy(cd.data, s + 24, 2048); cd.data_len = 2048; }
        cd.data_ready = 0;
        cd.data_pos = 0;
    }
    memcpy(cd.resp, r.data, (size_t)r.n);
    cd.nresp = r.n;
    cd.rpos = 0;
    cd.iflag = (uint8_t)((cd.iflag & ~7u) | (unsigned)r.irq);
    if (cd.iflag & cd.ienable & 0x1F) irq_raise(IRQ_CDROM);
}

void cd_advance(uint32_t cycles) {
    for (int i = 0; i < cd.qn; i++) cd.q[i].delay -= (int32_t)cycles;
    if (cd.reading) {
        cd.read_timer -= (int32_t)cycles;
        while (cd.read_timer <= 0) {
            cd.read_timer += (cd.mode & 0x80) ? SECTOR_CYCLES / 2 : SECTOR_CYCLES;
            sector_tick();
        }
    }
    deliver();
}

uint8_t cd_read_reg(int r) {
    switch (r) {
    case 0: {
        uint8_t s = (uint8_t)cd.index;
        if (cd.nparams == 0) s |= 0x08;
        if (cd.nparams < 16) s |= 0x10;
        if (cd.rpos < cd.nresp) s |= 0x20;
        if (cd.data_ready && cd.data_pos < cd.data_len) s |= 0x40;
        return s;
    }
    case 1: return cd.rpos < cd.nresp ? cd.resp[cd.rpos++] : 0;
    case 2: return (cd.data_ready && cd.data_pos < cd.data_len) ? cd.data[cd.data_pos++] : 0;
    case 3:
        if (cd.index & 1) return cd.iflag | 0xE0;
        return cd.ienable | 0xE0;
    }
    return 0;
}

void cd_write_reg(int r, uint8_t v) {
    if (r == 0) { cd.index = v & 3; return; }
    switch (cd.index * 4 + r) {
    case 1: command(v); break;                                       /* idx0: command */
    case 2: if (cd.nparams < 16) cd.params[cd.nparams++] = v; break; /* idx0: parameter */
    case 3:                                                          /* idx0: request */
        /* "Want data": loads the sector into the data FIFO. Asking again
         * while it's loaded doesn't rewind it (the loader reads the header
         * with one DMA and the data with the next). */
        if (v & 0x80) {
            if (!cd.data_ready) { cd.data_ready = 1; cd.data_pos = 0; }
        } else {
            cd.data_ready = 0;
        }
        break;
    case 6: cd.ienable = v & 0x1F; break;                            /* idx1: int enable */
    case 7:                                                          /* idx1: int flag ack */
        cd.iflag &= (uint8_t)~(v & 0x1F);
        if (v & 0x40) cd.nparams = 0;
        deliver();
        break;
    /* CD audio volumes (psx-spx "CDROM Audio Volume"): staged, then applied
     * together; the game fades its music with them. */
    case 10: atv_next[0] = v; break;                                 /* idx2: left -> left */
    case 11: atv_next[1] = v; break;                                 /* idx2: left -> right */
    case 13: atv_next[2] = v; break;                                 /* idx3: right -> right */
    case 14: atv_next[3] = v; break;                                 /* idx3: right -> left */
    case 15:                                                         /* idx3: apply / mute */
        if (v & 0x20) memcpy(atv, atv_next, 4);
        adpcm_mute = v & 1;
        spu_cd_volume(atv[0], atv[1], atv[2], atv[3], adpcm_mute);
        break;
    default: break;
    }
}

uint32_t cd_dma_read(void) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t b = cd.data_pos < cd.data_len ? cd.data[cd.data_pos++] : 0;
        v |= (uint32_t)b << (8 * i);
    }
    return v;
}

void cd_state(StateIO *io) {
    ss_module(io, "cd");
    SS(io, cd);
    SS(io, atv);
    SS(io, atv_next);
    SS(io, adpcm_mute);
}

/* After loading: hand the CD audio volumes to the SPU again. */
void cd_reapply_volume(void) { spu_cd_volume(atv[0], atv[1], atv[2], atv[3], adpcm_mute); }
