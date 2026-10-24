/* The CD queue (KERNEL, 0x80014958-0x80016D9C) and the decompressor
 * (0x80016F78).
 *
 * The game never waits on the CD itself: it queues commands, and the
 * vertical-blank callback works through the queue (cd_RunQueue), a step at
 * a time, with libcd's non-blocking calls. The queue is a ring of 16-byte
 * entries (pointer at 0x80062D74; 0x80062EFC its size - 1, 0x80062EFE the
 * next free entry, 0x80062F02 the next to run): +0 (s16) command (-1
 * done), +4 count / mode, +8 a CD position or parameter bytes, +0xC
 * destination. Commands:
 *   0 wait for the previous command to finish (CdSync)
 *   2 read +4 sectors to +0xC (CdlReadN; the ready callback cd_Sector
 *     copies each sector), 3 seek (CdlSetloc) to +8, 4 set mode +4,
 *   5 CdlGetlocL into +0xC, 6 CdlPause... 7 CdlInit... 8 CdlStop,
 *   9 CdlPause and wait, 0xA CdlNop..., 0xB CdlStandby..., 0xC CdlSetfilter +8
 *   0x100 upload TIMs (+0xC, +4 of them), 0x101 LoadImage (+4 rect, +0xC)
 *   0x102 / 0x104 sound transfers, 0x200 wait +4 frames,
 *   0x202 call the queue's callback (0x80062F2C) with +4,
 *   0x203 set the ready callback, 0x204 CD audio volumes.
 * 0x80062D84 is the running command's step. 0x80062F00 the CD mode byte.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). cd_RunQueue and what it calls, and the ready
 * callback, run from interrupt handlers: see kernel.h. */
#include "kernel.h"

#define k_queue        GV(u32, 0x80062D74)
#define k_qMax         GV(u16, 0x80062EFC)     /* size - 1 */
#define k_qTail        GV(u16, 0x80062EFE)     /* next free */
#define k_qHead        GV(u16, 0x80062F02)     /* next to run */
#define k_cdMode       GV(u8, 0x80062F00)
#define k_cmdStep      GV(s32, 0x80062D84)
#define k_qCallback    GV(u32, 0x80062F2C)
#define k_readLeft     GV(s32, 0x80062F18)     /* sectors left to read */
#define k_readDest     GV(u32, 0x80062F1C)
#define k_readPos      GV(s32, 0x80062F08)     /* sector number expected */
#define k_readLast     GV(s32, 0x80062EE8)
#define k_readStall    GV(s32, 0x80062EEC)
#define K_CD_PARAM     0x80062F0Cu             /* 4 bytes */
#define K_CD_SEEK_POS  0x80062F04u

#define K_CD_INIT          0x800262E8u
#define K_CD_SET_DEBUG     0x80026500u
#define K_CD_SYNC_CB       0x800265BCu
#define K_CD_READY_CB      0x800265D0u
#define K_CD_CONTROL       0x800265E4u
#define K_CD_CONTROL_F     0x80026720u
#define K_CD_SYNC          0x8002657Cu
#define K_CD_MIX           0x800269A0u
#define K_CD_GET_SECTOR    0x800269C0u
#define K_CD_POS_TO_INT    0x80029F0Cu
#define K_CD_READ_CB       0x80028B58u
#define K_DEC_RESET        0x80038488u
#define K_CD_ERROR         0x80016DA8u         /* game code: retry after an error */

/* The entry after e in the ring (the remainder as the original takes
 * it). */
static u32 q_next(u32 e) { return (u32)mips_rem((s32)(e + 1), (s32)(k_qMax + 1)); }
static u32 q_entry(u32 e) { return k_queue + e * 16; }

/* 80014958 (entries, ring): set up libcd and the queue (once; -1 when
 * done before): the callbacks (the old ones kept at 0x80062F20/24/28),
 * the entries done, mode 0x28 (double speed...), the MDEC reset. Returns
 * the number of entries. */
void n_cd_Init(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = R[31];
    u32 n = A0, ring = A1;
    R[16] = n; R[17] = ring;
    if (k_queue != 0) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    JAL(0x80014988u, K_CD_INIT, A0, A1, A2, A3);
    JAL(0x80014990u, K_CD_SET_DEBUG, 0, A1, A2, A3);
    k_queue = ring;
    GV(u32, 0x80062F20) = JAL(0x8001499Cu, K_CD_SYNC_CB, 0, A1, A2, A3);
    GV(u32, 0x80062F28) = JAL(0x800149A4u, K_CD_READ_CB, 0, A1, A2, A3);
    GV(u32, 0x80062F24) = JAL(0x800149B8u, K_CD_READY_CB, 0x80016C54u, A1, A2, A3);
    if ((s32)n > 0) {
        u32 e = k_queue;
        for (s32 i = 0;; i++, e += 16) {
            GV(u16, e) = 0xFFFF;
            if (!(i + 1 < (s32)n)) break;
            LOOP_POLL(0x800149DCu);
        }
    }
    k_qMax = (u16)(n - 1);
    k_qHead = 0;
    k_qTail = 0;
    GV(u32, K_CD_SEEK_POS) = 0;
    GV(u16, 0x80062F48) = 0;
    GV(u16, 0x80062F4A) = 0;
    k_cdMode = 0x28;
    JAL(0x80014A18u, K_CD_CONTROL, 0xE, 0x80062F00u, 0, A3);
    JAL(0x80014A20u, K_DEC_RESET, 0, A1, A2, A3);
    V0 = n;
    GV(u32, 0x80062F40) = 0;
    RESTORE_S;
}

/* 80014B14 (n): 1 when n entries don't fit in the queue (then, unless n
 * is the whole queue, the queue's callback is told with -1), else 0. */
void n_cd_QueueFull(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x14) = R[31];
    GS(u32, sp + 0x10) = in_s0;
    u32 n = A0;
    u32 max = k_qMax, head = k_qHead;
    n += (n != max + 1);
    s32 room;
    if (k_qTail < head) room = (s32)(head - k_qTail);
    else room = (s32)(max - (k_qTail - 1u) + k_qHead);
    u32 full = room < (s32)n;
    R[16] = full;
    if (full == 1 && (u32)k_qMax + 1 != n && k_qCallback)
        JAL(0x80014BA8u, k_qCallback, 0xFFFFFFFFu, A1, A2, A3);
    V0 = full;
    RESTORE_S;
}

/* 80014FF4 (double speed): queue a mode change: wait, mode (bit 0x80
 * the speed), wait. Returns the new free entry, -1 when full. */
void n_cd_QueueSpeed(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 dbl = A0;
    R[16] = dbl;
    if (JAL(0x8001500Cu, 0x80014B14u, 3, A1, A2, A3)) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    k_cdMode = dbl ? (u8)(k_cdMode | 0x80) : (u8)(k_cdMode & 0x7F);
    u32 e0 = k_qTail;
    u32 e1 = q_next(e0); k_qTail = (u16)e1; e1 &= 0xFFFF;
    u32 e2 = q_next(e1); k_qTail = (u16)e2; e2 &= 0xFFFF;
    u32 e3 = q_next(e2);
    GV(u16, q_entry(e0)) = 0;
    GV(u16, q_entry(e1)) = 4;
    GV(u32, q_entry(e1) + 4) = k_cdMode;
    GV(u16, q_entry(e2)) = 0x200;
    GV(u32, q_entry(e2) + 4) = 2;
    k_qTail = (u16)e3;
    V0 = e3 & 0xFFFF;
    RESTORE_S;
}

/* 80015484 (l->l, l->r, r->r, r->l): CD audio volumes, now. */
void n_cd_SetVolume(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    GV(u8, K_CD_PARAM + 0) = (u8)A0;
    GV(u8, K_CD_PARAM + 1) = (u8)A1;
    GV(u8, K_CD_PARAM + 2) = (u8)A2;
    GV(u8, K_CD_PARAM + 3) = (u8)A3;
    JAL(0x800154A8u, K_CD_MIX, K_CD_PARAM, A1, A2, A3);
    V0 = 0;
}

/* 8001569C (CD position): queue XA / audio play from a position: wait,
 * seek, mode with 0x48 (real time, XA), standby... Returns the new free
 * entry, -1 when full. */
void n_cd_QueuePlay(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 pos = A0;
    R[16] = pos;
    if (JAL(0x800156B4u, 0x80014B14u, 4, A1, A2, A3)) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    u32 e0 = k_qTail;
    u32 e1 = q_next(e0); k_qTail = (u16)e1; e1 &= 0xFFFF;
    u32 e2 = q_next(e1); k_qTail = (u16)e2; e2 &= 0xFFFF;
    u32 e3 = q_next(e2); k_qTail = (u16)e3; e3 &= 0xFFFF;
    u32 e4 = q_next(e3);
    k_cdMode = (u8)(k_cdMode | 0x48);
    GV(u16, q_entry(e0)) = 0;
    GV(u16, q_entry(e1)) = 3;
    for (int i = 0; i < 4; i++) GV(u8, q_entry(e1) + 8 + i) = GS(u8, pos + i);
    GV(u16, q_entry(e2)) = 4;
    GV(u32, q_entry(e2) + 4) = k_cdMode;
    GV(u16, q_entry(e3)) = 0xB;
    k_qTail = (u16)e4;
    V0 = e4 & 0xFFFF;
    RESTORE_S;
}

/* 80015840 (file, channel): queue an XA filter: wait, CdlSetfilter.
 * Returns the new free entry, -1 when full. */
void n_cd_QueueFilter(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = R[31];
    u32 file = A0, chan = A1;
    R[16] = file; R[17] = chan;
    if (JAL(0x80015860u, 0x80014B14u, 2, A1, A2, A3)) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    u32 e0 = k_qTail;
    u32 e1 = q_next(e0);
    GV(u16, q_entry(e0)) = 0;
    k_qTail = (u16)e1;
    e1 &= 0xFFFF;
    GV(u16, q_entry(e1)) = 0xC;
    GV(u8, q_entry(e1) + 8) = (u8)file;
    GV(u8, q_entry(k_qTail) + 9) = (u8)chan;
    GV(u8, q_entry(k_qTail) + 0xA) = 0;
    GV(u8, q_entry(k_qTail) + 0xB) = 0;
    u32 e2 = q_next(k_qTail);
    k_qTail = (u16)e2;
    V0 = e2 & 0xFFFF;
    RESTORE_S;
}

/* 80015AE8 (how): queue the end of a read or an XA play: wait, then for
 * 0 CdlPause and the mode without real time / XA (0xBE), for 1 the mode
 * with them (0x49) and CdlStandby; then a free entry. Returns the new
 * free entry, -1 when full. */
void n_cd_QueueStop(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 how = A0;
    R[16] = how;
    if (JAL(0x80015B00u, 0x80014B14u, 4, A1, A2, A3)) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    u32 e0 = k_qTail;
    u32 e1 = q_next(e0);
    GV(u16, q_entry(e0)) = 0;
    k_qTail = (u16)e1;
    e1 &= 0xFFFF;
    if (how == 0) {
        u32 e2 = q_next(e1); k_qTail = (u16)e2; e2 &= 0xFFFF;
        u32 e3 = q_next(e2);
        GV(u16, q_entry(e1)) = 9;
        GV(u16, q_entry(e2)) = 0;
        k_cdMode = (u8)(k_cdMode & 0xBE);
        k_qTail = (u16)e3;
        GV(u16, q_entry(e3 & 0xFFFF)) = 4;
        GV(u32, q_entry(e3 & 0xFFFF) + 4) = k_cdMode;
    } else if (how == 1) {
        u32 e2 = q_next(e1); k_qTail = (u16)e2; e2 &= 0xFFFF;
        u32 e3 = q_next(e2);
        k_cdMode = (u8)(k_cdMode | 0x49);
        GV(u16, q_entry(e1)) = 4;
        GV(u32, q_entry(e1) + 4) = k_cdMode;
        GV(u16, q_entry(e2)) = 0;
        k_qTail = (u16)e3;
        GV(u16, q_entry(e3 & 0xFFFF)) = 0xB;
    }
    u32 e = q_next(k_qTail);
    k_qTail = (u16)e;
    V0 = e & 0xFFFF;
    RESTORE_S;
}

/* 80015D44 (dest, sectors, CD position): queue a read: wait, seek to the
 * position, read the sectors to dest. Returns the new free entry, -1
 * when full or no sectors. */
void n_cd_QueueRead(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x1C) = R[31];
    u32 dest = A0, n = A1, pos = A2;
    R[18] = dest; R[16] = n; R[17] = pos;
    if (JAL(0x80015D6Cu, 0x80014B14u, 3, A1, A2, A3) || (s32)n <= 0) {
        V0 = 0xFFFFFFFFu;
        RESTORE_S;
        return;
    }
    u32 e0 = k_qTail;
    u32 e1 = q_next(e0); k_qTail = (u16)e1; e1 &= 0xFFFF;
    u32 e2 = q_next(e1); k_qTail = (u16)e2; e2 &= 0xFFFF;
    u32 e3 = q_next(e2);
    GV(u16, q_entry(e0)) = 0;
    GV(u16, q_entry(e1)) = 3;
    for (int i = 0; i < 4; i++) GV(u8, q_entry(e1) + 8 + i) = GS(u8, pos + i);
    GV(u16, q_entry(e2)) = 2;
    GV(u32, q_entry(e2) + 4) = n;
    for (int i = 0; i < 4; i++) GV(u8, q_entry(e2) + 8 + i) = GS(u8, pos + i);
    GV(u32, q_entry(e2) + 0xC) = dest;
    k_qTail = (u16)e3;
    V0 = e3 & 0xFFFF;
    RESTORE_S;
}

/* Finish the command at the head (done, -1) and move on. */
static void q_done(u32 head, u32 next) {
    GV(u16, q_entry(head)) = 0xFFFF;
    k_qHead = (u16)next;
}

/* 800163E8: per frame (vertical-blank callback): run the queue: each
 * command either finishes now (then the next one runs too) or keeps
 * its step for the next frame. After an error (0x80062F48) only the
 * error handler runs. Returns the head, -1 without a queue. */
void n_cd_RunQueue(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x20) = R[31];
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x10) = in_s0;
    if (k_queue == 0) { V0 = 0xFFFFFFFFu; RESTORE_S; return; }
    R[18] = 0xFFFFFFFFu;
    if (GV(u16, 0x80062F48)) {
        JAL(0x80016428u, K_CD_ERROR, A0, A1, A2, A3);
        V0 = 0xFFFFFFFFu;
        RESTORE_S;
        return;
    }
    R[19] = 0xFFFFFFFFu;
    for (;;) {
        u32 head = k_qHead;
        u32 ent = q_entry(head);
        s32 cmd = GV(s16, ent);
        u32 go = 0;            /* $s1: the command finished, run the next */
        R[16] = (u32)cmd; R[17] = 0;
        u32 op = 0, arg = 0;   /* CdControlF (op, arg), when one is wanted */
        int ctl = 0, step = 0;
        switch (cmd) {
        case 0: {
            u32 r = JAL(0x80016570u, K_CD_SYNC, 1, 0x80062F10u, A2, A3);
            if (r == 2) {
                k_cmdStep = 0;
                go = 1; R[17] = 1;
                q_done(k_qHead, q_next(k_qHead));
            } else if (r == 5) {
                JAL(0x8001666Cu, K_CD_ERROR, A0, A1, A2, A3);
            }
            break;
        }
        case 2:
            if (k_cmdStep == 0) {
                u32 r = JAL(0x800165F4u, K_CD_SYNC, 1, 0, A2, A3);
                if (r == 2) {
                    JAL(0x8001661Cu, K_CD_CONTROL_F, 6, q_entry(k_qHead) + 8, A2, A3);
                    u32 e = q_entry(k_qHead);
                    u32 cnt = GV(u32, e + 4), dst = GV(u32, e + 0xC);
                    s32 st = k_cmdStep;
                    k_readStall = 0;
                    k_readLeft = (s32)cnt;
                    k_cmdStep = st + 1;
                    k_readDest = dst;
                    k_readLast = k_readLeft;
                } else if (r == 5) {
                    JAL(0x8001666Cu, K_CD_ERROR, A0, A1, A2, A3);
                }
            } else if (k_cmdStep == 1) {
                u32 nx = q_next(head);
                if (k_readLeft == 0) {
                    GV(u16, ent) = 0xFFFF;
                    k_cmdStep = 0;
                    k_qHead = (u16)nx;
                } else {
                    int fail = 0;
                    if (k_readLeft == -1) fail = 1;
                    else if (k_readLast != k_readLeft) {
                        k_readStall = 0;
                        k_readLast = k_readLeft;
                    } else {
                        k_readStall = k_readStall + 1;
                        if (k_readStall >= 300) { k_readLeft = -1; fail = 1; }
                    }
                    if (fail) {
                        JAL(0x8001671Cu, K_CD_ERROR, A0, A1, A2, A3);
                        k_cmdStep = 0;
                    }
                }
            }
            break;
        case 3: {
            GV(u32, K_CD_SEEK_POS) = GV(u32, ent + 8);
            k_readPos = (s32)JAL(0x8001682Cu, K_CD_POS_TO_INT, K_CD_SEEK_POS, A1, A2, A3);
            if (JAL(0x80016850u, K_CD_CONTROL, 2, q_entry(k_qHead) + 8, 0, A3) == 1)
                q_done(k_qHead, q_next(k_qHead));
            break;
        }
        case 4: ctl = 1; op = 0xE; arg = ent + 4; break;
        case 5: ctl = 1; op = 0x10; arg = ent + 0xC; break;
        case 6: ctl = 1; op = 0x15; break;
        case 7: ctl = 1; op = 0x16; break;
        case 8: ctl = 1; op = 3; break;
        case 0xA: ctl = 1; op = 8; break;
        case 0xB: ctl = 1; op = 0x1B; break;
        case 0xC: ctl = 1; op = 0xD; arg = ent + 8; break;
        case 9:
            if (k_cmdStep == 0) { ctl = 1; op = 9; break; }
            {
                u32 r = JAL(0x80016788u, K_CD_SYNC, 1, 0, A2, A3);
                if (r == 2) {
                    k_cmdStep = 0;
                    q_done(k_qHead, q_next(k_qHead));
                } else if (r != 0) {
                    k_cmdStep = k_cmdStep - 1;
                }
            }
            break;
        case 0x100:
            JAL(0x8001695Cu, 0x80016D00u, GV(u32, ent + 0xC), GV(u32, ent + 4), A2, A3);
            step = 1;
            break;
        case 0x101:
            JAL(0x80016970u, K_LOAD_IMAGE, ent + 4, GV(u32, ent + 0xC), A2, A3);
            step = 1;
            break;
        case 0x102:
            if (JAL(0x80016980u, 0x800193B8u, 0, A1, A2, A3) == 0) break;
            JAL(0x800169A4u, 0x80019358u, GV(u32, q_entry(k_qHead) + 0xC), A1, A2, A3);
            {
                u32 h = k_qHead, nx = q_next(h);
                GV(u16, q_entry(h)) = 0xFFFF;
                k_qHead = (u16)nx;
                u32 e = q_entry(nx & 0xFFFF);
                JAL(0x80016A10u, 0x800192F8u, GV(u32, e + 0xC), GV(u32, e + 4), A2, A3);
            }
            step = 1;
            break;
        case 0x104:
            if (JAL(0x80016A20u, 0x800193B8u, 0, A1, A2, A3) == 0) break;
            step = 1;
            break;
        case 0x200: {
            s32 n = GV(s32, ent + 4);
            GV(s32, ent + 4) = n - 1;
            if (n > 0) break;
            GV(u16, ent) = 0xFFFF;
            k_qHead = (u16)q_next(head);
            break;
        }
        case 0x202:
            JAL(0x80016AA4u, k_qCallback, GV(u32, ent + 4), A1, A2, A3);
            q_done(k_qHead, q_next(k_qHead));
            break;
        case 0x203:
            JAL(0x80016AF4u, K_CD_READY_CB, GV(u32, ent + 0xC), A1, A2, A3);
            step = 1;
            break;
        case 0x204: {
            u32 a = GV(u32, ent + 4), b = GV(u32, ent + 0xC);
            GV(u8, K_CD_PARAM + 0) = (u8)(a >> 16);
            GV(u8, K_CD_PARAM + 1) = (u8)a;
            GV(u8, K_CD_PARAM + 2) = (u8)(b >> 16);
            GV(u8, K_CD_PARAM + 3) = (u8)b;
            JAL(0x80016B2Cu, K_CD_MIX, K_CD_PARAM, A1, A2, A3);
            step = 1;
            break;
        }
        default:
            break;
        }
        if (ctl) {
            if (k_cmdStep != 0) {
                k_cmdStep = (s32)JAL(0x80016940u, 0x80016BA8u, (u32)k_cmdStep, arg, A2, A3);
            } else {
                JAL(0x80016920u, K_CD_CONTROL_F, op, arg, A2, A3);
                k_cmdStep = k_cmdStep + 1;
            }
        }
        if (step) {
            go = 1; R[17] = 1;
            q_done(k_qHead, q_next(k_qHead));
        }
        if (!go) break;
        LOOP_POLL(0x80016B80u);
    }
    V0 = k_qHead;
    RESTORE_S;
}

/* 80016BA8 (step): wait for a CdControlF command: done (then the next
 * entry, step 0), an error (the error handler, step 0), or still
 * running (the step stays). */
void n_cd_WaitCommand(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 step = A0;
    R[16] = step;
    u32 r = JAL(0x80016BC4u, K_CD_SYNC, 1, 0, A2, A3);
    if (r == 2) {
        q_done(k_qHead, q_next(k_qHead));
        V0 = 0;
    } else if (r == 5) {
        JAL(0x80016BE4u, K_CD_ERROR, A0, A1, A2, A3);
        V0 = 0;
    } else {
        V0 = step;
    }
    RESTORE_S;
}

/* 80016C54 (status): the CD ready callback during a read: each data
 * sector's header (its number checked against the one expected: the
 * error handler on a gap) then its 2048 bytes to the destination. Any
 * other status ends the read (-1). */
void n_cd_Sector(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if ((A0 & 0xFF) != 1) {
        k_readLeft = -1;
        V0 = 0xFFFFFFFFu;
        return;
    }
    if (k_readLeft <= 0) { V0 = (u32)k_readLeft; return; }
    JAL(0x80016C84u, K_CD_GET_SECTOR, k_readDest, 3, A2, A3);
    s32 pos = (s32)JAL(0x80016C90u, K_CD_POS_TO_INT, k_readDest, A1, A2, A3);
    if (pos != k_readPos) JAL(0x80016CA8u, K_CD_ERROR, A0, A1, A2, A3);
    else k_readPos = pos + 1;
    k_readLeft = k_readLeft - 1;
    JAL(0x80016CD0u, K_CD_GET_SECTOR, k_readDest, 0x200, A2, A3);
    k_readDest = k_readDest + 0x800;
    V0 = k_readDest;
}

/* 80016F78 (src, dest): unpack LZ data: a 32-bit size, then flag bytes
 * (bit set: a literal byte; clear: a 16-bit back reference, distance
 * high 11 bits, length low 5 + 3, bytes before dest read as 0). Returns
 * the size. */
void n_lz_Unpack(void) {
    u32 s = A0, d = A1, start = d;
    u32 size = GV(u8, s) + ((u32)GV(u8, s + 1) << 8) + ((u32)GV(u8, s + 2) << 16) + ((u32)GV(u8, s + 3) << 24);
    s += 4;
    u32 end = d + size, flags = 0;
    for (;;) {
        if (!(flags & 0x100)) flags = GV(u8, s++) | 0xFF00;
        if (flags & 1) {
            GV(u8, d) = GV(u8, s++);
            d++;
        } else {
            u32 lo = GV(u8, s++);
            u32 w = lo + ((u32)GV(u8, s) << 8);
            s++;
            u32 dist = (u32)((s32)w >> 5);
            s32 len = (s32)(w & 0x1F) + 2;
            if (d - dist < start) {
                for (s32 i = 0;;) {
                    u32 from = d - dist;
                    GV(u8, d) = from < start ? 0 : GV(u8, from);
                    i++;
                    d++;
                    if (len < i) break;
                    LOOP_POLL(0x80017050u);
                }
            } else {
                for (s32 i = 0;;) {
                    GV(u8, d) = GV(u8, d - dist);
                    i++;
                    d++;
                    if (len < i) break;
                    LOOP_POLL(0x80017084u);
                }
            }
        }
        flags >>= 1;
        if (!(d < end)) break;
        LOOP_POLL(0x800170A8u);
    }
    V0 = size;
}
