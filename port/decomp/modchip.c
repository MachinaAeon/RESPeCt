/* The SCEA anti-modchip check's own CD-ROM driver (kernel, 0x800170B8-
 * 0x800183D4): it talks to the drive's registers directly, polled from a
 * state machine run every frame while the check is going on.
 *
 *   0x800596E4  pointer to the index/status register (0x1F801800)
 *   0x800596E8  pointer to the response FIFO (0x1F801801)
 *   0x800596EC  pointer to the interrupt enable register (0x1F801802)
 *   0x800596F0  pointer to the interrupt flag register (0x1F801803)
 *   0x800596AC  per command (4 bytes each): +2 response length, +3 how
 *               many interrupt polls to wait
 *   0x800596FC  the response
 *   0x80059708  the polls waited so far
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_modchk_CdPoll(void);
void n_modchk_CdSend(void);

#define MC_IDX   GV(u32, 0x800596E4u)
#define MC_RESP  GV(u32, 0x800596E8u)
#define MC_IEN   GV(u32, 0x800596ECu)
#define MC_IFLG  GV(u32, 0x800596F0u)
#define MC_WAIT  GV(u32, 0x80059708u)

/* 80017A5C (command): poll for the drive's answer to a command. 0 while
 * there is none (or not for long enough); once there is, the response
 * read and the interrupt acknowledged: -1 / -2 for an error interrupt
 * (-2 if the status says the shell is open), else 1 (-2 if the shell is
 * open, unless the command is 10). */
void n_modchk_CdPoll(void) {
    u32 cmd = A0;
    u32 need = GV(u8, 0x800596AFu + cmd * 4);
    MEM_W8(MC_IDX, 1);
    u32 x = MEM_R8(MC_IFLG), y = MEM_R8(MC_IFLG);
    u32 intr = x & 7;
    if (intr != (y & 7) || intr == 0) { V0 = 0; return; }
    MC_WAIT += intr;
    MEM_W8(MC_IDX, 1);
    MEM_W8(MC_IFLG, 7);
    for (u32 k = 0;;) {                         /* a delay, writing address 0 */
        GV(u32, 0) = k;
        k++;
        if (!((s32)k < 4)) break;
        LOOP_POLL(0x80017AECu);
    }
    if ((s32)MC_WAIT < (s32)need) { V0 = 0; return; }
    MC_WAIT = 0;
    if (intr == 5) {
        GV(u8, 0x800596FCu) = (u8)MEM_R8(MC_RESP);
        GV(u8, 0x800596FDu) = (u8)MEM_R8(MC_RESP);
        MEM_W8(MC_IDX, 1);
        MEM_W8(MC_IEN, 0x1F);
        V0 = (GV(u8, 0x800596FCu) & 0x10) ? 0xFFFFFFFEu : 0xFFFFFFFFu;
        return;
    }
    if (GV(u8, 0x800596AEu + cmd * 4) != 0) {
        for (u32 k = 0;;) {
            GV(u8, 0x800596FCu + k) = (u8)MEM_R8(MC_RESP);
            k++;
            if (!((s32)k < (s32)GV(u8, 0x800596AEu + cmd * 4))) break;
            LOOP_POLL(0x80017BD0u);
        }
    }
    MEM_W8(MC_IDX, 1);
    MEM_W8(MC_IEN, 0x1F);
    if (cmd == 10) { V0 = 1; return; }
    V0 = (GV(u8, 0x800596FCu) & 0x10) ? 0xFFFFFFFEu : 1;
}

/* 80017C30 (command): send a CD command: the interrupts acknowledged
 * (index 1, flags 7), the parameter FIFO cleared (0x18), the command's
 * parameters (0x800596AD + cmd * 4 of them, from 0x800596F4) written and
 * the command byte (0x800596AC + cmd * 4) issued. The short waits store
 * to address 0, as the original does. */
void n_modchk_CdSend(void) {
    const u32 cmd = A0;
    MEM_W8(GV(u32, 0x800596E4u), 1);
    MEM_W8(GV(u32, 0x800596F0u), 7);
    for (u32 i = 0;;) {
        MEM_W32(0, i);
        i++;
        if (!((s32)i < 4)) break;
        LOOP_POLL(0x80017C64u);
    }
    MEM_W8(GV(u32, 0x800596E4u), 1);
    MEM_W8(GV(u32, 0x800596ECu), 0x18);
    MEM_W8(GV(u32, 0x800596E4u), 0);
    const u32 np = 0x800596ADu + (cmd << 2);
    if (MEM_R8(np)) {
        for (s32 i = 0;;) {
            MEM_W8(GV(u32, 0x800596ECu), MEM_R8(0x800596F4u + (u32)i));
            i++;
            if (!(i < (s32)MEM_R8(np))) break;
            LOOP_POLL(0x80017CE4u);
        }
    }
    MEM_W8(GV(u32, 0x800596E4u), 0);
    const u32 c = MEM_R8(0x800596ACu + (cmd << 2));
    MEM_W8(GV(u32, 0x800596E8u), (u8)c);
    V0 = c;
}
