# KERNEL.BIN: game code and Sony SDK

KERNEL.BIN (`asm/1300.s`, `tools/dis.sh -m asm/1300.s ADDR`) is the
resident main program, loaded at 0x80010000 and never unloaded: the
game's engine core first, then the Psy-Q libraries it links statically.
The overlays (TEST.BIN and the stage / enemy programs) call into it.

The split below comes from the link order (game objects first, then the
libraries), the libraries' own debug strings (`tools/name_sdk_funcs.py`,
`symbol_addrs.txt`; the rodata strings at 0x80010000-0x80010AE0 are in the
same order as the code that prints them) and the call patterns into each
library. 1007 function entries in all.

| Range | What | Entries |
|---|---|---|
| 0x80010B00-0x800170B4 | **Game code** (the kernel proper, below) | 128 |
| 0x800170B8-0x800183D4 | SCEA anti-modchip check ("SOFTWARE TERMINATED / CONSOLE MAY HAVE BEEN MODIFIED / CALL 1-888-780-7690"; region byte at 0xBFC7FF52, CD registers poked directly) | 9 |
| 0x800183D8-0x800255F4 | libspu + libsnd ("SPU:T/O", "Can't Open Sequence data", "This is not SEQ Data"; SpuSetKey 0x800183D8, SsSeqCalledTbyT 0x80019868, SsVabOpenHead 0x80024488, SsVabTransBody 0x80024C58...) | |
| 0x800255F8-0x800262B4 | libetc (VSync 0x800255F8, intr.c "unexpected interrupt", DMA callbacks, GetVideoMode 0x8002629C) | |
| 0x800262B8-0x8002A34C | libcd + its streaming library (CdInit 0x800262E8, CdControl 0x800265E4, CdControlF 0x80026720, CdSync 0x8002657C, CdGetSector 0x800269C0, bios.c, StSetRing 0x800262B8, StGetNext 0x80029238, CdPosToInt 0x80029F0C) | |
| 0x8002A350-0x80034FF4 | libgte / libgs (InitGeom 0x8002A350, "ps_sort_sprite", light matrices, HMD primitive and animation drivers 0x8002E298-0x80032BF8) | |
| 0x80034FF8-0x80038484 | libgpu (ResetGraph ... sys.c, SetDefDrawEnv 0x80038088, SetDefDispEnv 0x80038148) | |
| 0x80038488-0x80038F74 | libpress (DecDCTReset 0x80038488, DecDCTout 0x8003865C, DecDCTin 0x80038B38, "MDEC_in_sync") | |
| 0x80038F78-0x8003A214 | libapi / libc / libcard: BIOS A/B/C call stubs (rand 0x800398A8, ChangeThread 0x800399C8, EnterCriticalSection 0x80039A08...), firstfile 0x80039C58 | |
| 0x8003A218-0x8003DF8C | libpad (PadInitDirect 0x8003C848, PadStartCom 0x8003A3A8, PadGetState 0x8003A434, PadSetAct 0x8003A7F4...) | |
| 0x8003DF90-0x8004ADxx | libgs TMD polygon drivers (GsTMDfast*, GsTMDdiv*; table in models.c) | |
| 0x8004AE64-0x8005841C+ | libgs fogged TMD drivers (the same set, with depth cueing) | |

Everything from 0x800170B8 on is Sony's. It stays translated except for
the library functions the game spends the most time in, decompiled for
speed: libgs's polygon drivers and subdivision (the subdividing F/G/FT/GT
triangle and quad drivers, the shared-vertex HMD drivers), the game's own
water-aware drivers (which find the floor height under the player and
splashes from the polygon they stand on), GsSortFastSprite, GsSortSprite
and the off-screen tests (gs_drivers.c); libgte's matrix maths, matrix
stack, rsin/rcos and SquareRoot0, and libgs's coordinate, OT, clip and
buffer helpers (GsGetLws, GsSortOt, GsSwapDispBuff...), the HMD
animation's coordinate interpolation, vertex mime and camera view,
VectorNormal, GsSetFlatLight and the CORDIC angle 8002A278 (gte_math.c);
also the fogged water triangle drivers, the lit flat triangle driver, the
subdividing flat quad driver and its G3 edge triangle, GsInitGraph, the
3D set-up and the TIM header reader (gs_drivers.c);
libgpu's primitive setup, command words, ClearOTag, DrawOTag, PutDrawEnv,
PutDispEnv, DrawSync and SetDispMask, ResetGraph and the driver's reset,
ClearImage and StoreImage, and libpress's MDEC transfers and
VLC decoder DecDCTvlc (lib_gpu.c); libpad's controller exchange and its
state machine's steps, PadInitDirect and the stop (lib_pad.c); libcd's interrupt reader, stream ring,
CdSync/CdReady (and their wait loops)/CdGetSector, the command issue
and CdControlF, the streaming sector handler and frame ring, the
sector DMA and CdIntToPos (lib_cd.c); libetc's
interrupt handler, VSync and vertical blank, InterruptCallback
(lib_etc.c); libspu's key and
voice bit registers, the voice manager flush, SpuSetVoiceAttr and the
transfer primitive _spu_t, SpuSetCommonAttr, SpuMalloc and its table
tidy, SpuSetReverbModeParam and the reverb registers (lib_spu.c);
libsnd's sequencer tick, event reader, note on and off, SsUtKeyOn,
SsUtKeyOnV, SsUtGetVagAtr, SsUtGetProgAtr, SsUtPitchBend and the voice
attribute setters, SsSeqOpen and the SEQ set-up, sequence and tone
volume, SsVabOpenHead, the control change dispatcher and NRPN 98, the
tick mode and start, the voice reset and the key bit helpers
(lib_snd.c). Not done: the NRPN data entry handler 8001B078 (passes
uninitialised stack bytes by value), the interrupt set-up 8002599C (its
setjmp context resumes inside it), and the continue screen's FT4 mesh
8002B338 (no scenario reaches it). One libapi function
is native for a port reason (`card_FirstFile`, savegame.c), not as
decompilation.

## The game code, 0x80010B00-0x800170B4

By subsystem (native name: decompiled into port/decomp; "not reached":
no scenario runs it, so it stays translated, untested). The functions
that run over many frames (`LIVE` >= 60: the main loop, the thread
runner, file loads, the movie player) are natives that keep save states
possible: they call on with `gcall_ss` and the original's frame and
registers, and their waits poll with `SAVE_POLL` (README, "Save states
and LIVE"):

**Main program, mode threads** (kernel_main.c, kernel_tasks.c).
kernel_Entry (0x80010B08, crt0), kernel_Start (0x80010BC4),
kernel_MainLoop (0x8001107C), thread_Run (0x80012E8C, the thread slots),
kernel_ModeLoad (0x80012FF4, a mode's overlay read and started),
kernel_ReadWait (0x80013208), kernel_Nothing, kernel_IdleThread,
kernel_ModeThread2/4/6/7/8, kernel_VSyncCallback, kernel_SetFrameCallback
(2), kernel_Random, kernel_ReadSectors, kernel_ModeSwitch; thread_InitSlots,
thread_Set, thread_ModeSwitch, thread_Exit, thread_Close, thread_Return,
thread_Hold, thread_Release. Not reached: 0x80010FF4, 0x800110D8 (GunCon
per frame) and the jump-table helpers at 0x80011298-0x80011548.

**Files**: kernel_LoadFile (0x800115C0, a file from the disc's
directory).

**Input** (kernel_input.c): input_MapButtons, pad_ChangeNone, pad_Init,
pad_MotorOff, pad_VibrationTick, pad_Vibrate, pad_VibrationOn/Off,
pad_Update, pad_GetButtons, pad_TypeBit, pad_AxesNone, pad_GetAxes. Not
reached: the mouse / neGcon / analog / GunCon / DualShock axes handlers
(0x80013BB4-0x80013CCC), 0x8001331C-0x80013364, 0x80013A3C, 0x80013D7C.

**Sound** (kernel_sound.c): sound_FileNames, sound_OpenBank,
sound_StopAll, sound_CloseBank, sound_CloseSeqData, sound_CloseSeq,
sound_FadeStart, sound_FadeStep, sound_SeqVolume, sound_SeqPlay,
sound_SeqStop, sound_SetListener, sound_UpdatePairs, sound_SetPanTables,
sound_PanVolumes, sound_PlayPair, sound_StopPair, sound_MovePair,
sound_PairStatus, sound_PairsOn, sound_Reset (0x80011994, with its 90
frames of silence), sound_LoadBank (0x80011CB4), sound_LoadBankHigh
(0x80011D24). Not reached: 0x80011FE8.

**Movies** (kernel_movie.c): movie_SetScreen, movie_SetFlag,
movie_StartStream, movie_SliceDone (MDEC callback), movie_DecodeNext,
movie_GetFrame, movie_WaitFrame, movie_Play (0x80014134, the player).

**CD queue, decompressor** (kernel_files.c): cd_Init, cd_QueueFull,
cd_QueueSpeed, cd_SetVolume, cd_QueuePlay, cd_QueueFilter, cd_QueueStop,
cd_QueueRead, cd_RunQueue, cd_WaitCommand, cd_Sector (CD ready callback),
lz_Unpack. Not reached: the other queue builders (0x80014A3C-0x80014F50,
0x80015144-0x8001558C, 0x80015970, 0x80015ED4, 0x8001614C), the TIM
upload 0x80016D00 and the error handler 0x80016DA8.

### Counts

The scenarios reach 563 KERNEL functions. 81 are game code, all
decompiled. 4 are the anti-modchip code (modchip.c): 0x800170B8,
0x80017A5C, 0x80017C30 decompiled; its state machine 0x80017124 (runs
during start-up) stays translated. The other 478 are SDK; of those the
scenarios reach, these stay translated: the NRPN data entry 0x8001B078
(passes uninitialised stack bytes by value) and the interrupt set-up's
setjmp pair 0x8002599C / 0x80025A04 (a context resumes inside them).

## Interrupt context

The vertical-blank callback (kernel_VSyncCallback and everything it calls:
pad_Update, sound_UpdatePairs, cd_RunQueue...), the MDEC callback
(movie_SliceDone) and the CD ready callback (cd_Sector) run inside
libetc's interrupt handler, on its stack in main RAM, which the RAM
comparison sees. So every native here keeps the original's frame
(FRAME), stores the registers the original saves at the same offsets,
sets $s0-$s7/$fp as the original has them at each call, and calls with
the original's return address (JAL, kernel.h). The thread functions need
the same for save states (a thread switched away resumes, in the
translation, from its saved $ra).
