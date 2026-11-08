/* libcd's streaming library (kernel): the ring of sectors that movies
 * are read into (StSetRing). 0x80065950 is the ring of 32-byte sector
 * headers (status u16 at +0: 1 being read, 2 ready, 4 handed out),
 * 0x80065954 the data ring's start index, 0x8006593C the current entry,
 * 0x80065944 nonzero to reset an entry left half-read.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libcd_StGetNext(void);
void n_libcd_CdIntr(void);
void n_libcd_CdSync(void);
void n_libcd_CdReady(void);
void n_libcd_CdGetSector(void);
void n_libcd_CdPosToInt(void);
void n_libcd_DmaSector(void);
void n_libcd_IntrCallback(void);
void n_libcd_DmaStart(void);
void n_libcd_Sync(void);
void n_libcd_Ready(void);
void n_libcd_StSector(void);
void n_libcd_StCallback(void);
void n_libcd_StFrameDone(void);
void n_libcd_StFreeFrame(void);
void n_libcd_Command(void);
void n_libcd_ControlF(void);
void n_libcd_CdIntToPos(void);

/* 80029238 StGetNext (addr, header): hand out the current frame if it is
 * ready: its data (2016 bytes per entry) and header. 0 if one was handed
 * out, 1 if not. */
void n_libcd_StGetNext(void) {
    u32 addr = A0, header = A1;
    u32 e = GV(u32, 0x80065950u) + (GV(u32, 0x8006593Cu) << 5);
    if (MEM_R16(e) == 1) {
        GV(u32, 0x8006593Cu) = 0;
        if (GV(u32, 0x80065944u) != 0) MEM_W16(e, 0);
        e = GV(u32, 0x80065950u) + (GV(u32, 0x8006593Cu) << 5);
    }
    if (MEM_R16(e) != 2) { V0 = 1; return; }
    MEM_W16(e, 4);
    u32 cur = GV(u32, 0x8006593Cu);
    MEM_W32(addr, GV(u32, 0x80065950u) + (GV(u32, 0x80065954u) << 5) + cur * 0x7E0u);
    MEM_W32(header, e);
    V0 = 0;
}

/* libcd's CD-ROM interrupt reader (80026A48), run from the interrupt
 * handler: which interrupt the drive raised (0x1F801803 & 7: 1 data
 * ready, 2 complete, 3 acknowledge, 4 end of data, 5 error), its
 * response bytes (up to 8, zero-filled) copied to the result buffers
 * (0x80065958 for commands, 0x80065960 for data, 0x80065968 at the end),
 * the drive status kept in 0x8005B400 (and the next byte in B404; B408
 * counts shell openings) and the sync / ready / end states in
 * 0x8005B6D0-D2. Returns what happened: 0 nothing, 1 acknowledged with
 * a second response to come, 2 command done, 4 data ready or ended, 6
 * error. */
#define CD_IDX   GV(u32, 0x8005B6B8u)      /* 0x1F801800 */
#define CD_RESP  GV(u32, 0x8005B6BCu)      /* 0x1F801801 */
#define CD_REG2  GV(u32, 0x8005B6C0u)
#define CD_IFLG  GV(u32, 0x8005B6C4u)      /* 0x1F801803 */
#define CD_CMD   GV(u8, 0x8005B411u)       /* the last command */

static void copy_result(u32 dst, const u8 *buf, u32 poll) {
    for (int i = 0;; i++) {
        MEM_W8(dst + i, buf[i]);
        if (i == 7) break;
        LOOP_POLL(poll);
    }
}

void n_libcd_CdIntr(void) {
    FRAME(0x30);
    u8 buf[8];
    MEM_W8(CD_IDX, 1);
    u32 iflg = CD_IFLG;
    u32 intr = MEM_R8(iflg) & 7;
    if (intr == 0) { V0 = 0; return; }
    for (;;) {
        if (intr == (MEM_R8(iflg) & 7u)) break;
        LOOP_POLL(0x80026AB8u);
        intr = MEM_R8(iflg) & 7;
    }
    u32 n = 0;
    while (MEM_R8(CD_IDX) & 0x20) {
        buf[n] = (u8)MEM_R8(CD_RESP);
        n++;
        if (!(n < 8)) break;
        LOOP_POLL(0x80026B00u);
    }
    if (n < 8) {
        for (u32 i = n;;) {
            buf[i] = 0;
            i++;
            if (!(i < 8)) break;
            LOOP_POLL(0x80026B28u);
        }
    }
    MEM_W8(CD_IDX, 1);
    MEM_W8(CD_IFLG, 7);
    MEM_W8(CD_REG2, 7);
    u32 err = 0;
    if (intr != 3 || GV(u32, 0x8005B5B8u + CD_CMD * 4) != 0) {
        if (!(GV(u32, 0x8005B400u) & 0x10) && (buf[0] & 0x10)) GV(u32, 0x8005B408u)++;
        err = buf[0] & 0x1D;
        GV(u32, 0x8005B400u) = buf[0];
        GV(u32, 0x8005B404u) = buf[1];
    }
    if (intr == 5 && (s32)GV(u32, 0x8005B3FCu) >= 3) {
        gcall(0x800398C8u, 0x80010538u, R[5], R[6], R[7]);
        if ((s32)GV(u32, 0x8005B3FCu) >= 3)
            gcall(0x800398C8u, 0x80010544u, GV(u32, 0x8005B418u + CD_CMD * 4),
                  GV(u32, 0x8005B400u), GV(u32, 0x8005B404u));
    }
    if (intr - 1 >= 5) {
        gcall(0x80039800u, 0x80010560u, R[5], R[6], R[7]);
        gcall(0x800398C8u, 0x80010574u, intr, R[6], R[7]);
        V0 = 0;
        return;
    }
    LOOP_POLL(0x80026CA4u);
    switch (intr) {
    case 3:
        if (err) {
            GV(u8, 0x8005B6D0u) = 5;
            copy_result(0x80065958u, buf, 0x80026CE8u);
            V0 = 2;
        } else if (GV(u32, 0x8005B4B8u + CD_CMD * 4) != 0) {
            GV(u8, 0x8005B6D0u) = 3;
            copy_result(0x80065958u, buf, 0x80026D54u);
            V0 = 1;
        } else {
            GV(u8, 0x8005B6D0u) = 2;
            copy_result(0x80065958u, buf, 0x80026D9Cu);
            V0 = 2;
        }
        return;
    case 2:
        GV(u8, 0x8005B6D0u) = err ? 5 : 2;
        copy_result(0x80065958u, buf, 0x80026DE8u);
        V0 = 2;
        return;
    case 1:
        if (err && n == 1) err = 0;
        GV(u8, 0x8005B6D1u) = err ? 5 : 1;
        copy_result(0x80065960u, buf, 0x80026E4Cu);
        MEM_W8(CD_IDX, 0);
        MEM_W8(CD_IFLG, 0);
        V0 = 4;
        return;
    case 4:
        GV(u8, 0x8005B6D2u) = 4;
        GV(u8, 0x8005B6D1u) = GV(u8, 0x8005B6D2u);
        copy_result(0x80065968u, buf, 0x80026EB8u);
        copy_result(0x80065960u, buf, 0x80026EE8u);
        V0 = 4;
        return;
    default:
        GV(u8, 0x8005B6D1u) = 5;
        GV(u8, 0x8005B6D0u) = GV(u8, 0x8005B6D1u);
        copy_result(0x80065958u, buf, 0x80026F38u);
        copy_result(0x80065960u, buf, 0x80026F68u);
        V0 = 6;
        return;
    }
}

static void cd_call(u32 f) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(f, R[4], R[5], R[6], R[7]);
}

/* 8002657C CdSync (mode, result): 80026FAC. */
void n_libcd_CdSync(void) { cd_call(0x80026FACu); }
/* 8002659C CdReady (mode, result): 8002722C. */
void n_libcd_CdReady(void) { cd_call(0x8002722Cu); }
/* 800269C0 CdGetSector (buffer, words): copy from the sector buffer
 * (80027EE0); 1 when that succeeded (returned 0). */
void n_libcd_CdGetSector(void) {
    cd_call(0x80027EE0u);
    V0 = V0 < 1;
}

/* 80029F0C CdPosToInt(pos): a BCD minute:second:sector position as a
 * sector number (from 0:02:00). */
static inline u32 bcd(u32 b) { return (b >> 4) * 10 + (b & 0xF); }
void n_libcd_CdPosToInt(void) {
    u32 m = bcd(MEM_R8(A0)), sec = bcd(MEM_R8(A0 + 1)), f = bcd(MEM_R8(A0 + 2));
    V0 = (m * 60 + sec) * 75 + f - 150;
}

/* Pointers to the hardware registers libcd uses. */
#define CD_INDEX     GV(u32, 0x8005B6B8u)   /* 0x1F801800 index / status */
#define CD_REQ       GV(u32, 0x8005B6C4u)   /* 0x1F801803 (index 0): request */
#define CD_COMDELAY  GV(u32, 0x8005B6C8u)   /* 0x1F801020 */
#define CD_DELAY     GV(u32, 0x8005B6ECu)   /* 0x1F801018 CD-ROM bus timing */
#define DMA_DPCR     GV(u32, 0x8005B6F0u)   /* 0x1F8010F0 */
#define DMA3_MADR    GV(u32, 0x8005B6F4u)   /* 0x1F8010B0 */
#define DMA3_BCR     GV(u32, 0x8005B6F8u)
#define DMA3_CHCR    GV(u32, 0x8005B6FCu)

/* 80027EE0 (dest, words): DMA the sector buffer's next words to dest:
 * ask for the data, wait for it, start channel 3 and wait for it to
 * finish. Returns 0. */
void n_libcd_DmaSector(void) {
    MEM_W8(CD_INDEX, 0);
    MEM_W8(CD_REQ, 0x80);
    MEM_W32(CD_DELAY, 0x20943);
    MEM_W32(CD_COMDELAY, 0x1323);
    u32 p = DMA_DPCR;
    MEM_W32(p, MEM_R32(p) | 0x8000);
    MEM_W32(DMA3_MADR, A0);
    MEM_W32(DMA3_BCR, A1 | 0x10000);
    u32 st = CD_INDEX;
    for (;;) {
        if (MEM_R8(st) & 0x40) break;
        LOOP_POLL(0x80027F78u);
    }
    MEM_W32(DMA3_CHCR, 0x11000000);
    u32 c = DMA3_CHCR;
    if (MEM_R32(c) & 0x01000000) {
        for (;;) {
            if (!(MEM_R32(c) & 0x01000000)) break;
            LOOP_POLL(0x80027FC0u);
        }
    }
    MEM_W32(CD_COMDELAY, 0x1325);
    V0 = 0;
}

/* 800280D8: the CD-ROM interrupt: until CdIntr (80026A48) finds nothing
 * more, run the data ready callback (0x8005B3F8 (status 0x8005B6D1,
 * result 0x80065960), for 4) and the command one (0x8005B3F4 (status
 * 0x8005B6D0, result 0x80065958), for 2); the index register is put back
 * as it was. s-registers as the original's. */
void n_libcd_IntrCallback(void) {
    FRAME(0x28);
    u32 sv[4] = { R[16], R[17], R[18], R[19] };
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x20, R[31]);
    R[17] = 0x8005B6D1u;
    R[19] = 0x8005B6D0u;
    R[18] = MEM_R8(CD_INDEX) & 3;
    for (;;) {
        R[16] = gcall(0x80026A48u, R[4], R[5], R[6], R[7]);
        if (R[16] == 0) break;
        if (R[16] & 4) {
            u32 f = GV(u32, 0x8005B3F8u);
            if (f) gcall(f, GV(u8, R[17]), 0x80065960u, R[6], R[7]);
        }
        if (R[16] & 2) {
            u32 f = GV(u32, 0x8005B3F4u);
            if (f) {
                gcall(f, GV(u8, R[19]), 0x80065958u, R[6], R[7]);
                LOOP_POLL(0x8002817Cu);
            } else LOOP_POLL(0x80028164u);
        } else LOOP_POLL(0x80028150u);
    }
    MEM_W8(CD_INDEX, (u8)R[18]);
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2]; R[19] = sv[3];
}

/* 80029C60 (ch, madr, bs, bc, chcr, irq): start DMA channel ch (its
 * registers at 0x1F801080 + ch * 16): wait for it to be idle (about
 * 0x10000 polls, then print a timeout), its interrupt enabled or not
 * (DICR byte 2 at *0x8005B7E8), the channel enabled in DPCR
 * (*0x8005B7E4), address and block size set, then wait for the CD-ROM's
 * data (status *0x8005B7CC bit 6) and write CHCR. The registers read
 * back into the stack as the original does. */
void n_libcd_DmaStart(void) {
    FRAME(0x30);
    u32 sv[5] = { R[16], R[17], R[18], R[19], R[20] };
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x2C, R[31]);
    MEM_W32(sp + 0x1C, sv[1]);
    R[16] = A0; R[18] = A1; R[19] = A2; R[20] = A3;
    u32 regs = 0x1F801080u + (R[16] << 4);
    R[17] = MEM_R8(sp + 0x44);
    if (MEM_R32(regs + 8) & 0x01000000u) {
        for (u32 i = 0;;) {
            if (i == 0x10000) {
                gcall(0x800398C8u, 0x800106A0u, MEM_R32(regs + 8), R[6], R[7]);
                LOOP_POLL(0x80029D18u);
                break;
            }
            u32 busy = MEM_R32(regs + 8) & 0x01000000u;
            i++;
            if (!busy) break;
            LOOP_POLL(0x80029CD0u);
        }
    }
    u32 dicr = GV(u32, 0x8005B7E8u);
    if (R[17] == 1) MEM_W8(dicr + 2, (u8)(MEM_R8(dicr + 2) | (1u << (R[16] & 31))));
    else MEM_W8(dicr + 2, (u8)(MEM_R8(dicr + 2) & ~(1u << (R[16] & 31))));
    MEM_W32(sp + 0x10, MEM_R32(GV(u32, 0x8005B7E8u)));
    u32 bit = 1u << (((R[16] << 2) + 3) & 31);
    u32 dpcr = GV(u32, 0x8005B7E4u);
    MEM_W32(dpcr, MEM_R32(dpcr) | bit);
    MEM_W32(regs, R[18]);
    MEM_W32(regs + 4, (R[19] << 16) | R[20]);
    u32 st = GV(u32, 0x8005B7CCu);
    if (!(MEM_R8(st) & 0x40)) {
        for (;;) {
            if (MEM_R8(st) & 0x40) break;
            LOOP_POLL(0x80029DC8u);
        }
    }
    MEM_W32(regs + 8, MEM_R32(sp + 0x40));
    V0 = MEM_R32(regs + 8);
    MEM_W32(sp + 0x10, V0);
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2]; R[19] = sv[3]; R[20] = sv[4];
}

/* 80026FAC (mode, result): libcd's CdSync: wait for the command to end
 * (status 0x8005B6D0 2 complete or 5 error: the 8 result bytes at
 * 0x80065958 copied out, the status returned and set to 2), handling
 * the CD interrupt by hand (CdIntr and the callbacks, as 800280D8) when
 * called from a callback. mode 1: just look (0). A timeout (0x3C0
 * vertical blanks or 0x3C0000 polls, 0x80065970 / 74) prints the state
 * ("CdSync", 0x80065978) and resets (80027988): -1. The original's
 * s-registers. */
void n_libcd_Sync(void) {
    FRAME(0x40);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x18 + (u32)i * 4, sv[i]); }
    u32 sfp = R[30];
    MEM_W32(sp + 0x38, sfp);
    MEM_W32(sp + 0x3C, R[31]);
    R[22] = A0; R[23] = A1;
    u32 now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
    R[30] = 0x8005B418u; R[20] = 0x8005B498u; R[18] = 0x8005B6D0u; R[21] = 0x8005B6D1u; R[19] = 2;
    GV(u32, 0x80065970u) = now + 0x3C0;
    GV(u32, 0x80065974u) = 0;
    GV(u32, 0x80065978u) = 0x80010594u;
    for (;;) {
        now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
        int timeout;
        if ((s32)GV(u32, 0x80065970u) < (s32)now) timeout = 1;
        else {
            u32 c = GV(u32, 0x80065974u);
            GV(u32, 0x80065974u) = c + 1;
            timeout = 0x003C0000 < (s32)c;
        }
        if (timeout) {
            gcall(0x80039800u, 0x8001050Cu, R[5], R[6], R[7]);
            u32 a = GV(u8, R[18]), b = GV(u8, R[18] + 1);
            MEM_W32(sp + 0x10, GV(u32, R[20] + (b << 2)));
            u32 nm = GV(u32, R[30] + ((u32)GV(u8, 0x8005B411u) << 2));
            gcall(0x800398C8u, 0x8001051Cu, GV(u32, 0x80065978u), nm, GV(u32, R[20] + (a << 2)));
            gcall(0x80027988u, R[4], R[5], R[6], R[7]);
            V0 = (u32)-1;
            break;
        }
        if (gcall(0x8002595Cu, R[4], R[5], R[6], R[7]) != 0) {
            R[17] = MEM_R8(GV(u32, 0x8005B6B8u)) & 3;
            for (;;) {
                R[16] = gcall(0x80026A48u, R[4], R[5], R[6], R[7]);
                if (R[16] == 0) break;
                if (R[16] & 4) {
                    u32 f = GV(u32, 0x8005B3F8u);
                    if (f) gcall(f, GV(u8, R[21]), 0x80065960u, R[6], R[7]);
                }
                if (R[16] & 2) {
                    u32 f = GV(u32, 0x8005B3F4u);
                    if (f) {
                        gcall(f, GV(u8, R[18]), 0x80065958u, R[6], R[7]);
                        LOOP_POLL(0x80027184u);
                    } else LOOP_POLL(0x8002716Cu);
                } else LOOP_POLL(0x80027158u);
            }
            MEM_W8(GV(u32, 0x8005B6B8u), (u8)R[17]);
        }
        u32 st = GV(u8, R[18]);
        if (st == R[19] || st == 5) {
            GV(u8, R[18]) = (u8)R[19];
            u32 d = R[23];
            if (d != 0) {
                u32 src = 0x80065958u;
                for (s32 k = 7;;) {
                    u32 v = GV(u8, src);
                    src++;
                    k--;
                    MEM_W8(d, (u8)v);
                    d++;
                    if (k == -1) break;
                    LOOP_POLL(0x800271E4u);
                }
            }
            V0 = st;
            break;
        }
        V0 = 0;
        if (R[22] != 0) break;
        LOOP_POLL(0x800271F4u);
    }
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8002722C (mode, result): libcd's CdReady: wait for data ready
 * (status 0x8005B6D2: its 8 result bytes at 0x80065968; else the
 * callback status 0x8005B6D1, bytes at 0x80065960), clearing the status
 * and returning it; the CD interrupt handled by hand from a callback as
 * in CdSync. mode 1: just look (0). Timeout as CdSync ("CdReady"). The
 * original's s-registers. */
void n_libcd_Ready(void) {
    FRAME(0x40);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x34, sv[7]);
    MEM_W32(sp + 0x24, sv[3]);
    MEM_W32(sp + 0x3C, R[31]);
    MEM_W32(sp + 0x38, sfp);
    MEM_W32(sp + 0x30, sv[6]);
    MEM_W32(sp + 0x2C, sv[5]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    MEM_W32(sp + 0x18, sv[0]);
    R[23] = A0; R[19] = A1;
    u32 now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
    R[30] = 0x8005B418u; R[21] = 0x8005B498u; R[18] = 0x8005B6D0u; R[22] = 0x8005B6D1u; R[20] = 0x8005B6D2u;
    GV(u32, 0x80065970u) = now + 0x3C0;
    GV(u32, 0x80065974u) = 0;
    GV(u32, 0x80065978u) = 0x8001059Cu;
    for (;;) {
        now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
        int timeout;
        if ((s32)GV(u32, 0x80065970u) < (s32)now) timeout = 1;
        else {
            u32 c = GV(u32, 0x80065974u);
            GV(u32, 0x80065974u) = c + 1;
            timeout = 0x003C0000 < (s32)c;
        }
        if (timeout) {
            gcall(0x80039800u, 0x8001050Cu, R[5], R[6], R[7]);
            u32 a = GV(u8, R[18]), b = GV(u8, R[18] + 1);
            MEM_W32(sp + 0x10, GV(u32, R[21] + (b << 2)));
            u32 nm = GV(u32, R[30] + ((u32)GV(u8, 0x8005B411u) << 2));
            gcall(0x800398C8u, 0x8001051Cu, GV(u32, 0x80065978u), nm, GV(u32, R[21] + (a << 2)));
            gcall(0x80027988u, R[4], R[5], R[6], R[7]);
            V0 = (u32)-1;
            break;
        }
        if (gcall(0x8002595Cu, R[4], R[5], R[6], R[7]) != 0) {
            R[17] = MEM_R8(GV(u32, 0x8005B6B8u)) & 3;
            for (;;) {
                R[16] = gcall(0x80026A48u, R[4], R[5], R[6], R[7]);
                if (R[16] == 0) break;
                if (R[16] & 4) {
                    u32 f = GV(u32, 0x8005B3F8u);
                    if (f) gcall(f, GV(u8, R[22]), 0x80065960u, R[6], R[7]);
                }
                if (R[16] & 2) {
                    u32 f = GV(u32, 0x8005B3F4u);
                    if (f) {
                        gcall(f, GV(u8, R[18]), 0x80065958u, R[6], R[7]);
                        LOOP_POLL(0x80027404u);
                    } else LOOP_POLL(0x800273ECu);
                } else LOOP_POLL(0x800273D8u);
            }
            MEM_W8(GV(u32, 0x8005B6B8u), (u8)R[17]);
        }
        u32 st = GV(u8, R[20]), src = 0x80065968u, pc = 0x8002745Cu;
        if (st) GV(u8, R[20]) = 0;
        else {
            st = GV(u8, R[22]);
            src = 0x80065960u; pc = 0x800274ACu;
            if (st) GV(u8, R[22]) = 0;
        }
        if (st) {
            u32 d = R[19];
            if (d != 0) {
                for (s32 k = 7;;) {
                    u32 v = GV(u8, src);
                    src++;
                    k--;
                    MEM_W8(d, (u8)v);
                    d++;
                    if (k == -1) break;
                    LOOP_POLL(pc);
                }
            }
            V0 = st;
            break;
        }
        V0 = 0;
        if (R[23] != 0) break;
        LOOP_POLL(0x800274BCu);
    }
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

#define SG(a) GV(u32, (a))
/* Status 0x8005B814 = code; a read into memory (0x80065940) counts the
 * sector (0x80065930) first. */
static void st_status(u32 code, int count) {
    if (count && SG(0x80065940u) != 0) SG(0x80065930u) += 1;
    SG(0x8005B814u) = code;
}

/* 80029318: libcd's streaming sector handler (data ready during
 * CdRead2 streaming). Skips while the ring's last read is out
 * (0x8006592C 1) or, in the movie decoder's mode (0x80065918), while
 * the MDEC is busy. Reads the result (CdReady, mode 1); on an error bit
 * status 3. The current ring slot (0x80065950 + index * 32): busy:
 * status 4. Takes the sector header (4 bytes, skipping 8 unless
 * 0x80065988) from the CD FIFO and DMAs the 8-word STR header into the
 * slot (or reads it from memory, 80029C34). Checks it: not a video
 * sector of the channel 0x80065928 (status 5), a broken frame sequence
 * (status 6, restart the frame via 800291F8), a frame past the end
 * frame 0x80065944 (status 7, the end callback 0x800659A4), no room in
 * the ring (status 8 / 9); the first sector of a frame starts the frame
 * (header copied to the ring base). Then the sector's data (0x1F8
 * words) DMA'd into the frame's place in the ring (0x8006594C); the
 * frame's last sector marks the slot done (0x8006592C 1, next channel
 * 0x80065920) and, reading from memory, 80028FC8 follows. Status 0xA. */
void n_libcd_StSector(void) {
    FRAME(0x40);
    MEM_W32(sp + 0x38, R[31]);
    if (SG(0x8006592Cu) == 1) return;
    if (SG(0x80065918u) != 0 && (MEM_R32(SG(0x8005B7ECu)) & 0x01000000u)) {
        SG(0x8006591Cu) = 1;
        st_status(1, 1);
        return;
    }
    if (gcall(0x8002722Cu, 1, sp + 0x30, R[6], R[7]) == 5) return;
    MEM_W16(sp + 0x22, MEM_R8(sp + 0x30));
    MEM_W16(sp + 0x24, MEM_R8(sp + 0x31));
    if (MEM_R16(sp + 0x22) & 4) { SG(0x8005B814u) = 3; return; }
    u32 slot = SG(0x80065950u) + (SG(0x80065934u) << 5);
    SG(0x800659A8u) = slot;
    if (MEM_R16(slot) != 0) { st_status(4, 1); return; }
    MEM_W8(SG(0x8005B7CCu), 0);
    MEM_W8(SG(0x8005B7D8u), 0);
    MEM_W8(SG(0x8005B7CCu), 0);
    MEM_W8(SG(0x8005B7D8u), 0x80);
    MEM_W32(SG(0x8005B7DCu), 0x00020943u);
    MEM_W32(SG(0x8005B7E0u), 0x1323);
    if (SG(0x80065988u) == 0) {
        for (u32 i = 0;;) {
            MEM_W8(sp + 0x28 + i, MEM_R8(SG(0x8005B7D4u)));
            i++;
            if (!(i < 4)) break;
            LOOP_POLL(0x800294E0u);
        }
        const u32 f = SG(0x8005B7D4u);
        for (u32 i = 0;;) {
            (void)MEM_R8(f);
            i++;
            if (!(i < 8)) break;
            LOOP_POLL(0x80029504u);
        }
    }
    u32 mem = SG(0x80065940u);
    if (mem != 0) gcall(0x80029C34u, SG(0x800659A8u), mem + (SG(0x80065930u) << 11), 8, 0);
    else {
        MEM_W32(sp + 0x10, 0x11000000u);
        MEM_W32(sp + 0x14, 0);
        MEM_W32(sp + 0x18, 0);
        gcall(0x80029C60u, 3, SG(0x800659A8u), 0, 8);
    }
    {
        const u32 chcr = SG(0x8005B7FCu);
        if (MEM_R32(chcr) & 0x01000000u) {
            for (;;) {
                if (!(MEM_R32(chcr) & 0x01000000u)) break;
                LOOP_POLL(0x800295A0u);
            }
        }
    }
    slot = SG(0x800659A8u);
    for (u32 i = 0; i < 4; i++) MEM_W8(slot + 0x1C + i, MEM_R8(sp + 0x28 + i));
    MEM_W32(SG(0x8005B7DCu), 0x00020843u);
    MEM_W32(SG(0x8005B7E0u), 0x1325);
    if (SG(0x80065948u) == 1) {
        u32 want = SG(0x80065924u);
        if (want != 0) {
            slot = SG(0x800659A8u);
            if (want != MEM_R16(slot + 8)) {
                MEM_W16(slot, 0);
                if (SG(0x80065940u) != 0) SG(0x80065930u) += 1;
                return;
            }
            SG(0x80065948u) = 0;
        }
    }
    slot = SG(0x800659A8u);
    if (!(MEM_R16(slot) == 0x160 && ((MEM_R16(slot + 2) >> 10) & 0x1F) == SG(0x80065928u))) {
        if (SG(0x80065940u) != 0) SG(0x80065930u) = 0;
        SG(0x8005B814u) = 5;
        MEM_W16(SG(0x800659A8u), 0);
        return;
    }
    {
        int restart;
        if ((u32)(s32)(s16)GV(u16, 0x80065914u) != MEM_R16(slot + 4)) restart = 1;
        else {
            u32 fr = SG(0x80065910u);
            restart = fr != 0 && fr != MEM_R16(slot + 8);
        }
        if (restart) {
            SG(0x80065910u) = 0;
            GV(u16, 0x80065914u) = 0;
            u32 b = SG(0x80065938u);
            gcall(0x800291F8u, b, SG(0x80065934u) - b, R[6], R[7]);
            SG(0x80065934u) = SG(0x80065938u);
            MEM_W16(SG(0x800659A8u), 0);
            st_status(6, 1);
            return;
        }
    }
    slot = SG(0x800659A8u);
    if (MEM_R16(slot + 4) == 0) {
        u32 fr = MEM_R16(slot + 8);
        GV(u16, 0x80065914u) = 0;
        SG(0x80065910u) = fr;
        u32 end = SG(0x80065944u);
        if (end != 0 && !(fr < end)) {
            SG(0x80065910u) = 0;
            GV(u16, 0x80065914u) = 0;
            u32 b = SG(0x80065938u);
            gcall(0x800291F8u, b, SG(0x80065934u) - b, R[6], R[7]);
            SG(0x80065934u) = SG(0x80065938u);
            MEM_W16(SG(0x800659A8u), 0);
            u32 cb = SG(0x800659A4u);
            SG(0x80065948u) = 1;
            if (cb) gcall(cb, R[4], R[5], R[6], R[7]);
            st_status(7, 1);
            return;
        }
        u32 s = SG(0x800659A8u);
        if ((SG(0x80065954u) - SG(0x80065934u) - 1) < MEM_R16(s + 6)) {
            if (SG(0x80065944u) == 0) {
                MEM_W16(s, 1);
                u32 cb = SG(0x800659A4u);
                SG(0x80065948u) = 1;
                if (cb) gcall(cb, R[4], R[5], R[6], R[7]);
                st_status(8, 1);
                return;
            }
            if ((s16)GV(u16, SG(0x80065950u)) != 0) {
                MEM_W16(s, 0);
                st_status(9, 1);
                return;
            }
            MEM_W16(s, 1);
            u32 d = SG(0x80065950u), from = SG(0x800659A8u);
            SG(0x80065934u) = 0;
            for (u32 i = 0;;) {
                MEM_W32(d, MEM_R32(from));
                from += 4;
                i++;
                if (!(i < 8)) break;
                d += 4;
                LOOP_POLL(0x800299BCu);
            }
            SG(0x800659A8u) = SG(0x80065950u);
        }
        SG(0x80065938u) = SG(0x80065934u);
    }
    SG(0x8005B814u) = 0xA;
    GV(u16, 0x80065914u) = (u16)(GV(u16, 0x80065914u) + 1);
    SG(0x8006594Cu) = SG(0x80065950u) + (SG(0x80065954u) << 5) + SG(0x80065934u) * 2016;
    u32 dma;
    if (SG(0x80065918u) != 0) {
        MEM_W32(SG(0x8005B7DCu), 0x00020943u);
        MEM_W32(SG(0x8005B7E0u), 0x1323);
        dma = 0x11000000u;
    } else {
        MEM_W32(SG(0x8005B7DCu), 0x21020843u);
        dma = 0x11400100u;
    }
    slot = SG(0x800659A8u);
    const u32 last = (u32)(MEM_R16(slot + 6) - 1) == MEM_R16(slot + 4);
    if (last) SG(0x8006592Cu) = 1;
    mem = SG(0x80065940u);
    if (mem != 0) {
        gcall(0x80029C34u, SG(0x8006594Cu), mem + (SG(0x80065930u) << 11) + 0x20, 0x1F8, last);
        SG(0x80065930u) += 1;
    } else {
        MEM_W32(sp + 0x10, dma);
        MEM_W32(sp + 0x14, last);
        MEM_W32(sp + 0x18, 0);
        gcall(0x80029C60u, 3, SG(0x8006594Cu), 0, 0x1F8);
    }
    if (last) {
        u32 ch = SG(0x80065920u);
        GV(u16, 0x80065914u) = 0;
        SG(0x80065910u) = 0;
        SG(0x80065928u) = ch;
    }
    MEM_W32(SG(0x8005B7E0u), 0x1325);
    MEM_W16(SG(0x800659A8u), 3);
    u32 m2 = SG(0x80065940u);
    SG(0x80065934u) += 1;
    if (m2 != 0 && SG(0x8006592Cu) != 0) gcall(0x80028FC8u, R[4], R[5], R[6], R[7]);
}
#undef SG

/* 80028C0C: the CD data-ready callback for streaming: 80029318. */
void n_libcd_StCallback(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x80029318u, R[4], R[5], R[6], R[7]);
}

/* 80028FC8: a streamed frame is complete: its ring slot (the frame
 * start 0x80065938) marked 2, its sector header's last word and frame
 * number into 0x80065990 / 0x80065994, the next frame start, the
 * frame callback (0x800659A0), the ring free again (0x8006592C). */
void n_libcd_StFrameDone(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 slot = GV(u32, 0x80065950u) + (GV(u32, 0x80065938u) << 5);
    MEM_W16(slot, 2);
    for (u32 i = 0; i < 4; i++) GV(u8, 0x80065990u + i) = MEM_R8(slot + 0x1C + i);
    GV(u32, 0x80065994u) = MEM_R32(slot + 8);
    GV(u32, 0x80065938u) = GV(u32, 0x80065934u);
    u32 cb = GV(u32, 0x800659A0u);
    if (cb) gcall(cb, cb, R[5], 0x80065990u, R[7]);
    GV(u32, 0x8006592Cu) = 0;
}

/* 80029148 StFreeRing(addr): free the frame whose data is at addr (its
 * slot by (addr - data base) / 2016): not a frame in use (slot 4): 1;
 * else its sectors' slots cleared, the free index past them. 0. */
void n_libcd_StFreeFrame(void) {
    u32 d = A0 - (GV(u32, 0x80065950u) + (GV(u32, 0x80065954u) << 5));
    s32 q = (s32)d >> 2;
    s32 hi = (s32)(((s64)q * (s32)0x82082083) >> 32);
    s32 k = ((hi + q) >> 8) - ((s32)d >> 31);
    u32 slot = GV(u32, 0x80065950u) + ((u32)k << 5);
    if ((s16)MEM_R16(slot) != 4) { V0 = 1; return; }
    s32 n = (s16)MEM_R16(slot + 6);
    s32 i = 0;
    if (n > 0) {
        for (;;) {
            u32 s2 = GV(u32, 0x80065950u) + ((u32)(i + k) << 5);
            i++;
            MEM_W16(s2, 0);
            if (!(i < n)) break;
            LOOP_POLL(0x800291D4u);
        }
    }
    GV(u32, 0x8006593Cu) = (u32)(i + k);
    V0 = 0;
}

/* 800274F4 (com, param, result, async): libcd's command issue (behind
 * CdControl): with debug level 2 its name printed; a command that takes
 * parameters (0x8005B638 table) given none: -2 (printed at level 1).
 * Waits for the last one (CdSync), keeps Setloc's position (0x8005B40C)
 * and Setmode's mode (0x8005B410), clears the status (and the data
 * status for read commands, 0x8005B538 table), writes the parameters and
 * the command to the CD registers. async: 0. Else waits for the
 * acknowledge as CdSync does ("CD_cw", timeout -1), copies the 8 result
 * bytes, and returns -1 on an error status (5), else 0. The original's
 * s-registers. */
void n_libcd_Command(void) {
    FRAME(0x38);
    u32 sv[7];
    for (int i = 0; i < 7; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x18, sv[0]);
    MEM_W32(sp + 0x30, sv[6]);
    MEM_W32(sp + 0x20, sv[2]);
    MEM_W32(sp + 0x1C, sv[1]);
    MEM_W32(sp + 0x34, R[31]);
    MEM_W32(sp + 0x2C, sv[5]);
    MEM_W32(sp + 0x28, sv[4]);
    MEM_W32(sp + 0x24, sv[3]);
    const u32 com = A0, param = A1, result = A2, async = A3;
    R[16] = param; R[22] = result; R[18] = async; R[17] = com;
    const u32 ci = (com & 0xFF) << 2;
    if (!((s32)GV(u32, 0x8005B3FCu) < 2)) gcall(0x800398C8u, 0x800105A8u, GV(u32, 0x8005B418u + ci), R[6], R[7]);
    if (GV(u32, 0x8005B638u + ci) != 0 && param == 0) {
        if ((s32)GV(u32, 0x8005B3FCu) > 0) gcall(0x800398C8u, 0x800105B0u, GV(u32, 0x8005B418u + ci), R[6], R[7]);
        V0 = (u32)-2;
        goto out;
    }
    gcall(0x80026FACu, 0, 0, R[6], R[7]);
    if ((com & 0xFF) == 2) {
        for (s32 i = 0;;) {
            GV(u8, 0x8005B40Cu + (u32)i) = MEM_R8(param + (u32)i);
            i++;
            if (!(i < 4)) break;
            LOOP_POLL(0x800275ECu);
        }
    }
    if ((com & 0xFF) == 0xE) GV(u8, 0x8005B410u) = MEM_R8(param);
    GV(u8, 0x8005B6D0u) = 0;
    if (GV(u32, 0x8005B538u + ci) != 0) GV(u8, 0x8005B6D1u) = 0;
    MEM_W8(GV(u32, 0x8005B6B8u), 0);
    {
        const u32 cnt = 0x8005B638u + ci;
        if ((s32)GV(u32, cnt) > 0) {
            for (s32 i = 0;;) {
                MEM_W8(GV(u32, 0x8005B6C0u), MEM_R8(param + (u32)i));
                i++;
                if (!(i < (s32)GV(u32, cnt))) break;
                LOOP_POLL(0x8002768Cu);
            }
        }
    }
    GV(u8, 0x8005B411u) = (u8)com;
    MEM_W8(GV(u32, 0x8005B6BCu), (u8)com);
    if (async != 0) { V0 = 0; goto out; }
    {
        u32 now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
        GV(u32, 0x80065970u) = now + 0x3C0;
        GV(u32, 0x80065974u) = 0;
        u32 acked = GV(u8, 0x8005B6D0u);
        GV(u32, 0x80065978u) = 0x800105C0u;
        if (acked == 0) {
            R[21] = 0x8005B418u; R[19] = 0x8005B498u; R[18] = 0x8005B6D0u; R[20] = 0x8005B6D1u;
            for (;;) {
                now = gcall(0x800255F8u, (u32)-1, R[5], R[6], R[7]);
                int timeout;
                if ((s32)GV(u32, 0x80065970u) < (s32)now) timeout = 1;
                else {
                    u32 c = GV(u32, 0x80065974u);
                    GV(u32, 0x80065974u) = c + 1;
                    timeout = 0x003C0000 < (s32)c;
                }
                if (timeout) {
                    gcall(0x80039800u, 0x8001050Cu, R[5], R[6], R[7]);
                    u32 a = GV(u8, R[18]), b = GV(u8, R[18] + 1);
                    MEM_W32(sp + 0x10, GV(u32, R[19] + (b << 2)));
                    u32 nm = GV(u32, R[21] + ((u32)GV(u8, 0x8005B411u) << 2));
                    gcall(0x800398C8u, 0x8001051Cu, GV(u32, 0x80065978u), nm, GV(u32, R[19] + (a << 2)));
                    gcall(0x80027988u, R[4], R[5], R[6], R[7]);
                    V0 = (u32)-1;
                    goto out;
                }
                if (gcall(0x8002595Cu, R[4], R[5], R[6], R[7]) != 0) {
                    R[17] = MEM_R8(GV(u32, 0x8005B6B8u)) & 3;
                    for (;;) {
                        R[16] = gcall(0x80026A48u, R[4], R[5], R[6], R[7]);
                        if (R[16] == 0) break;
                        if (R[16] & 4) {
                            u32 f = GV(u32, 0x8005B3F8u);
                            if (f) gcall(f, GV(u8, R[20]), 0x80065960u, R[6], R[7]);
                        }
                        if (R[16] & 2) {
                            u32 f = GV(u32, 0x8005B3F4u);
                            if (f) {
                                gcall(f, GV(u8, R[18]), 0x80065958u, R[6], R[7]);
                                LOOP_POLL(0x80027860u);
                            } else LOOP_POLL(0x80027848u);
                        } else LOOP_POLL(0x80027834u);
                    }
                    MEM_W8(GV(u32, 0x8005B6B8u), (u8)R[17]);
                }
                if (GV(u8, R[18]) != 0) break;
                LOOP_POLL(0x80027880u);
            }
        }
    }
    if (result != 0) {
        u32 src = 0x80065958u, d = result;
        for (s32 k = 7;;) {
            u32 v = GV(u8, src);
            src++;
            k--;
            MEM_W8(d, (u8)v);
            d++;
            if (k == -1) break;
            LOOP_POLL(0x800278ACu);
        }
    }
    V0 = GV(u8, 0x8005B6D0u) == 5 ? (u32)-1 : 0;
out:
    for (int i = 0; i < 7; i++) R[16 + i] = sv[i];
}

/* 80026720 CdControlF(com, param): up to four tries, with the
 * ready callback (0x8005B3F4) held off: a Getstat first unless com is
 * Getstat or the drive is idle (0x8005B400 bit 4 clear), a Setloc
 * first for commands that take a position (0x8005B36C table); the
 * command itself async. 1 when issued, else 0. The original's
 * s-registers. */
void n_libcd_ControlF(void) {
    FRAME(0x38);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x30, sfp);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x28, sv[6]);
    MEM_W32(sp + 0x2C, sv[7]);
    MEM_W32(sp + 0x34, R[31]);
    const u32 com = A0, param = A1;
    R[17] = param; R[19] = com; R[16] = 3; R[30] = 1;
    R[18] = com & 0xFF;
    R[20] = GV(u32, 0x8005B3F4u);
    R[21] = 0x8005B36Cu + (R[18] << 2);
    R[22] = 0; R[23] = (u32)-1;
    for (;;) {
        GV(u32, 0x8005B3F4u) = 0;
        if (R[18] != 1 && (GV(u8, 0x8005B400u) & 0x10)) gcall(0x800274F4u, 1, 0, 0, 0);
        int ok = 1;
        if (param != 0 && MEM_R32(R[21]) != 0) ok = gcall(0x800274F4u, 2, param, 0, 0) == 0;
        if (ok) {
            GV(u32, 0x8005B3F4u) = R[20];
            if (gcall(0x800274F4u, com & 0xFF, param, 0, 1) == 0) { V0 = R[22] + 1; break; }
        }
        R[16]--;
        if (R[16] == R[23]) {
            GV(u32, 0x8005B3F4u) = R[20];
            R[22] = (u32)-1;
            V0 = R[22] + 1;
            break;
        }
        LOOP_POLL(0x8002680Cu);
    }
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80029E08 CdIntToPos(i, pos): sector number to BCD minute / second /
 * sector (+150, the 2-second lead-in). Returns pos. */
void n_libcd_CdIntToPos(void) {
    const s32 i = (s32)A0 + 150;
    const u32 p = A1;
    const s32 secs = i / 75, mins = secs / 60;
    const s32 fr = i - secs * 75, sc = secs - mins * 60;
    MEM_W8(p + 2, (u8)(((fr / 10) << 4) + fr % 10));
    MEM_W8(p + 1, (u8)(((sc / 10) << 4) + sc % 10));
    MEM_W8(p, (u8)(((mins / 10) << 4) + mins % 10));
    V0 = p;
}
