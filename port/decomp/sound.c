/* Sound effects (TEST overlay): playing a sound from a bank at a volume,
 * enemy sounds placed in 3D (by distance and direction), and loading the
 * sound banks a stage and its enemies need.
 *
 * Sound banks are described by a table at 0x800637C0, 12 bytes per slot;
 * the s16 at +8 is the bank's handle once loaded (-1: not loaded). Sound
 * slots 3 and up belong to enemies: an enemy kind k uses slots 2k+3 (and
 * 2k+4).
 *
 * Volumes are given as percentages of the options' sound effect volume
 * (s16 at 0x80062DB6; streamed audio uses the one at 0x80062DB4).
 *
 * A sound's "nibbles" byte holds two parameters for the sound driver:
 * the high nibble and the low nibble.
 *
 * 3D sounds: up to three at a time, in slots 0-2 (busy bits in
 * 0x80062E0E); each remembers its enemy and the direction it came from
 * (0x800BB028, 8 bytes each) so 80081464 can update the panning as the
 * player turns, and its volume falls with the enemy's distance beyond
 * 1000 (divided by 0x80062E08).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_snd_Play(void);
void n_snd_PlayNote(void);
void n_snd_PlayEx(void);
void n_snd_Cue(void);
void n_snd_CueNote(void);
void n_snd_CueAtEnemy(void);
void n_snd_CueEnemyKind(void);
void n_snd_SetStreamVolume(void);
void n_snd_StopStreams(void);
void n_snd_LoadEnemyBanks(void);
void n_snd_QueueEnemyBanks(void);
void n_snd_ReloadBankSix(void);
void n_snd_StageBankFile(void);
void n_snd_LoadStageBank(void);
void n_snd_EnemySpatial(void);
void n_snd_EnemyVoiceSpatial(void);
void n_snd_UpdateSpatial(void);

#define SND_SLOTS        0x800637C0u   /* 12 bytes per slot */
#define SLOT_HANDLE(i)   GV(s16, SND_SLOTS + (u32)(i) * 12 + 8)
#define SE_VOL           GV(s16, 0x80062DB6)   /* sound effects volume */
#define MASTER_VOL       GV(s16, 0x80062DB4)   /* streamed audio volume */
#define g_sound3D        GV(u8, 0x80062D35)    /* 0: no 3D sound (mono) */
#define g_3dBusy         GV(s16, 0x80062E0E)   /* bit per 3D slot */
#define g_3dFalloff      GV(s32, 0x80062E08)   /* distance per volume step */
#define SND3D_PARAMS     0x80063478u           /* per slot, 10 bytes */
#define SND3D_SLOTS      0x800BB028u           /* per slot: enemy, s16 direction, u16 player yaw */
#define PLAYER_YAW_U     GV(u16, g_player + 0x46)

#define g_bankPick       GV(u32, 0x800BBB88)   /* stage data: which enemy banks (nibbles, 15 = random) */
#define BANK_FILES       0x800A22BCu           /* [0]: first enemy bank file; [k]: file for kind k */
#define BANK_CHANCES     0x800A2344u           /* 8 words: cumulative chances (%) */
#define BANK_BUFFER      0x800C3E50u
#define BANK_BUFFER2     0x80108000u
#define LOAD_QUEUE       0x800AF540u           /* 3 x {slot, file, buffer}, slot -1 ends */
#define STAGE_BANKS      0x800A2364u           /* per stage: bank file (-1 none) */
#define g_stage          GV(s32, 0x800B4BA0)
#define g_stageVariant   GV(s32, 0x800B3E20)
#define g_bankOverride   GV(u32, 0x800B3D50)   /* a buffer to load the stage bank to instead */
#define g_bankOverrideLast GV(u32, 0x800B3D54)

#define F_SPU_PLAY       0x8001EEA8u   /* (handle, hi, lo, a3, [4], [5], [6]) */
#define F_SPU_PLAY_NOTE  0x8001F348u   /* (key, handle, hi, lo, [4], [5], [6], [7]) */
#define F_PAN_FOR        0x800122C4u   /* (0, volume, &pan[2]) */
#define F_STREAM_ON      0x80019268u   /* (flag) */
#define F_STREAM_VOLUME  0x800206B4u   /* (channel, left, right) */
#define F_STREAM_STOP    0x8001F6A4u
#define F_BANK_FREE      0x80011DCCu   /* (slot) */
#define F_BANK_LOAD      0x80011CB4u   /* (slot, file, buffer) */
#define F_BANK_LOAD_TO   0x80011BBCu   /* (slot, file, buffer) */
#define F_BANK_OVERFLOW  0x800735E8u
#define F_RAND           0x80011570u
#define F_DIRECTION      0x800849D0u   /* (from position, to position) */
#define F_3D_START       0x80012524u   /* (slot, direction, volume, params) */
#define F_3D_UPDATE      0x80012AA4u   /* (slot, direction, volume) */
#define F_CUE_ENEMY_B    0x80080718u
#define F_ENEMY_DIST     0x8007F4E8u   /* (enemy): its distance */
#define F_SND_PLAY       0x8008038Cu
#define F_SND_PLAY_NOTE  0x80080454u
#define F_SND_CUE        0x800805F4u
#define F_SND_CUE_ENEMY  0x8008080Cu
#define F_STAGE_BANK_FILE 0x80080F9Cu

#define E_DISTANCE(e)    GV(s32, (e) + 0x868)

/* x * v / 100, as the game computes a percentage (32-bit product). */
static inline s32 pct(s32 x, s32 v) { return (s32)((u32)x * (u32)v) / 100; }

/* The volume step for an enemy further than 1000 away: 0-0xFF. */
static s32 falloff(u32 e) {
    s32 v = mips_div((s32)((u32)E_DISTANCE(e) - 1000), g_3dFalloff);
    return (s16)v < 0x100 ? v : 0xFF;
}

/* 8008038C (slot, nibbles, left, right): play a sound of a bank at
 * left/right volumes (percent of the sound effects volume). Returns the voice
 * (s16). */
void n_snd_Play(void) {
    FRAME(0x28);
    u32 slot = A0, nib = A1;
    s32 vol = SE_VOL;
    GS(s32, sp + 0x10) = 0x7F;
    GS(s32, sp + 0x14) = (s16)pct((s32)A2, vol);
    GS(s32, sp + 0x18) = (s16)pct((s32)A3, vol);
    u32 r = GCALL4(F_SPU_PLAY, (s32)SLOT_HANDLE(slot), (nib & 0xF0) >> 4, nib & 0xF, 0x3C);
    V0 = (u32)(s32)(s16)r;
}

/* 80080454 (slot, nibbles, key, left, [right]): the same through the
 * driver's keyed play. */
void n_snd_PlayNote(void) {
    FRAME(0x28);
    u32 slot = A0, nib = A1;
    s32 vol = SE_VOL;
    s32 right = (s32)GS(u32, sp + 0x38);
    GS(s32, sp + 0x10) = 0x3C;
    GS(s32, sp + 0x14) = 0x7F;
    GS(s32, sp + 0x18) = (s16)pct((s32)A3, vol);
    GS(s32, sp + 0x1C) = (s16)pct(right, vol);
    GCALL4(F_SPU_PLAY_NOTE, (s32)(s16)A2, (s32)SLOT_HANDLE(slot), (nib & 0xF0) >> 4, nib & 0xF);
}

/* 80080524 (slot, nibbles, a, b, [left], [right]): play with two more
 * driver parameters (16-bit). */
void n_snd_PlayEx(void) {
    FRAME(0x28);
    u32 slot = A0, nib = A1;
    s32 vol = SE_VOL;
    s32 left = (s32)GS(u32, sp + 0x38), right = (s32)GS(u32, sp + 0x3C);
    GS(s32, sp + 0x10) = (s16)A3;
    GS(s32, sp + 0x14) = (s16)pct(left, vol);
    GS(s32, sp + 0x18) = (s16)pct(right, vol);
    GCALL4(F_SPU_PLAY, (s32)SLOT_HANDLE(slot), (nib & 0xF0) >> 4, nib & 0xF, (s32)(s16)A2);
}

/* 800805F4 (slot, nibbles, left, right): snd_Play with the volumes scaled
 * by the sound effect volume first. Returns the voice (s16). */
void n_snd_Cue(void) {
    FRAME(0x18);
    s32 vol = SE_VOL;
    u32 r = GCALL4(F_SND_PLAY, A0, A1, pct((s32)A2, vol), pct((s32)A3, vol));
    V0 = (u32)(s32)(s16)r;
}

/* 80080670 (slot, nibbles, left, right, [key]): snd_PlayNote likewise,
 * if the bank is loaded. */
void n_snd_CueNote(void) {
    FRAME(0x20);
    s32 h = SLOT_HANDLE(A0);
    if (h < 0) { V0 = (u32)h; return; }
    s32 vol = SE_VOL;
    GS(s32, sp + 0x10) = pct((s32)A3, vol);
    GCALL4(F_SND_PLAY_NOTE, A0, A1, GS(u32, sp + 0x30), pct((s32)A2, vol));
}

/* 8008080C (enemy, slot, nibbles): an enemy's sound, quieter and panned
 * with distance beyond 1000. */
void n_snd_CueAtEnemy(void) {
    FRAME(0x28);
    u32 e = A0, slot = A1, nib = A2;
    if (E_DISTANCE(e) < 1001) {
        GCALL4(F_SND_CUE, slot, nib, 0x7F, 0x7F);
        return;
    }
    s32 v = falloff(e);
    GCALL3(F_PAN_FOR, 0, (s32)(s16)(0xFF - v), sp + 0x10);
    s32 left = GS(s16, sp + 0x10), right = GS(s16, sp + 0x12);
    GCALL4(F_SND_CUE, slot, nib, left / 64, right / 64);
}

/* 80080718 (enemy, sound): cue a sound of the enemy's kind's bank
 * (slot 2k + 3), placed by its distance (0x8007F4E8). */
void n_snd_CueEnemyKind(void) {
    FRAME(0x28);
    u32 e = A0, nib = A1;
    u32 slot = (u32)(s32)GV(s16, e + 0x84E) * 2 + 3;
    if ((s32)GCALL1(F_ENEMY_DIST, e) < 1001) {
        GCALL4(F_SND_CUE, slot, nib, 0x7F, 0x7F);
        return;
    }
    s32 v = mips_div((s32)(GCALL1(F_ENEMY_DIST, e) - 1000), g_3dFalloff);
    if (!((s16)v < 0x100)) v = 0xFF;
    GCALL3(F_PAN_FOR, 0, (s32)(s16)(0xFF - v), sp + 0x10);
    s32 left = GS(s16, sp + 0x10), right = GS(s16, sp + 0x12);
    GCALL4(F_SND_CUE, slot, nib, left / 64, right / 64);
}

/* 800808F4 (volume): set both streamed-audio channels (0x10, 0x11) that
 * are playing to volume percent of 0x80062DB4. */
void n_snd_SetStreamVolume(void) {
    FRAME(0x18);
    s32 vol = (s32)A0;
    if (GCALL1(F_STREAM_ON, 0x10000) != 0) {
        s32 v = (s16)pct(vol, MASTER_VOL);
        GCALL3(F_STREAM_VOLUME, 0x10, v, v);
    }
    if (GCALL1(F_STREAM_ON, 0x20000) != 0) {
        s32 v = (s16)pct(vol, MASTER_VOL);
        GCALL3(F_STREAM_VOLUME, 0x11, v, v);
    }
}

/* 800809B4: silence and stop the streamed audio, free banks 2-6. */
void n_snd_StopStreams(void) {
    FRAME(0x18);
    GCALL3(F_STREAM_VOLUME, 0x10, 0, 0);
    GCALL3(F_STREAM_VOLUME, 0x11, 0, 0);
    GCALL1(F_STREAM_STOP, 0x10);
    GCALL1(F_STREAM_STOP, 0x11);
    for (u32 b = 2; b <= 6; b++) GCALL1(F_BANK_FREE, b);
    GV(s16, 0x80062D4E) = 0;
    V0 = 0x80060000u;
}

/* An enemy bank picked by a nibble of the stage's choice; 15 = at random
 * by the chances table (the first entry whose 51/10 * chance reaches a
 * random byte). */
static u32 pick_bank(u32 n) {
    if (n != 15) return n;
    u32 r = GCALL0(F_RAND) & 0xFF;
    u32 i = 0;
    for (; i < 8; i++) {
        s32 w = GV(s32, BANK_CHANCES + i * 4);
        if (!((s32)((u32)w * 51) / 10 < (s32)r)) break;
    }
    return i;
}

/* 80080A28 (slot, kind): load the sound banks for enemy kind (< 0x22):
 * kinds under 0xB use the stage's two picks into slots slot+3 and slot+4
 * (bank 2 too, if it isn't loaded); other kinds their own bank into slot
 * 2*slot+3. Banks already loaded are kept. */
void n_snd_LoadEnemyBanks(void) {
    FRAME(0x20);
    s32 slot = (s32)A0, kind = (s32)A1;
    if (kind >= 0x22) return;
    if (kind < 0xB) {
        if (SLOT_HANDLE(2) == -1) GCALL3(F_BANK_LOAD, 2, 0x1A, BANK_BUFFER);
        if (SLOT_HANDLE(slot + 3) != -1) return;
        u32 n = pick_bank(GV(u8, g_bankPick) >> 4);
        GCALL3(F_BANK_LOAD, slot + 3, GV(u32, BANK_FILES) + n + 0x14, BANK_BUFFER);
        n = pick_bank(GV(u16, g_bankPick) & 0xF);
        GCALL3(F_BANK_LOAD, slot + 4, GV(u32, BANK_FILES) + n + 0x14, BANK_BUFFER);
    } else {
        s32 file = GV(s32, BANK_FILES + (u32)kind * 4);
        if (file < 0) return;
        s32 s = slot * 2 + 3;
        if (SLOT_HANDLE(s) != -1) return;
        GCALL3(F_BANK_LOAD, s, (u32)file + 0x14, BANK_BUFFER);
    }
}

static void queue_set(u32 i, s32 slot, u32 file) {
    u32 q = LOAD_QUEUE + i * 12;
    GV(s32, q) = slot;
    GV(u32, q + 8) = BANK_BUFFER2;
    GV(u32, q + 4) = file;
}

/* 80080C60 (slot, kind): the same banks as snd_LoadEnemyBanks, written as
 * a load list (0x800AF540) instead of loaded. */
void n_snd_QueueEnemyBanks(void) {
    FRAME(0x20);
    s32 slot = (s32)A0, kind = (s32)A1;
    u32 n = 0;
    GV(s32, LOAD_QUEUE) = -1;
    if (kind >= 0x22) return;
    if (kind < 0xB) {
        if (SLOT_HANDLE(2) == -1) {
            GV(s32, LOAD_QUEUE) = 2;
            GV(u32, LOAD_QUEUE + 4) = 0x1A;
            GV(u32, LOAD_QUEUE + 8) = BANK_BUFFER2;
            n = 1;
        }
        if (SLOT_HANDLE(slot + 3) == -1) {
            u32 b = pick_bank(GV(u8, g_bankPick) >> 4);
            queue_set(n, slot + 3, GV(u32, BANK_FILES) + b + 0x14);
            n++;
            b = pick_bank(GV(u16, g_bankPick) & 0xF);
            queue_set(n, slot + 4, GV(u32, BANK_FILES) + b + 0x14);
            n++;
        }
    } else {
        u32 fa = BANK_FILES + (u32)kind * 4;
        if (GV(s32, fa) >= 0) {
            s32 s = slot * 2 + 3;
            if (SLOT_HANDLE(s) == -1) {
                GV(s32, LOAD_QUEUE) = s;
                GV(u32, LOAD_QUEUE + 8) = BANK_BUFFER2;
                GV(u32, LOAD_QUEUE + 4) = GV(u32, fa) + 0x14;
                n = 1;
            }
        }
    }
    if (n < 3) GV(s32, LOAD_QUEUE + n * 12) = -1;
}

/* 80080F38 (n): reload bank 6 with file n + 0x3C. */
void n_snd_ReloadBankSix(void) {
    FRAME(0x18);
    u32 n = A0;
    GCALL1(F_BANK_FREE, 6);
    GCALL3(F_BANK_LOAD, 6, n + 0x3C, BANK_BUFFER2);
}

/* 80080F9C: the stage's sound bank file (by stage and variant). */
void n_snd_StageBankFile(void) {
    V0 = GV(u32, STAGE_BANKS + (u32)g_stage * 4) + (u32)g_stageVariant + 0x10E;
}

/* 80080FCC: load the stage's sound bank into slot 6 (if it has one): to
 * the usual buffer, or to the one asked for in 0x800B3D50 (remembered
 * unless it's past 0x8014FFFF, which is an error). */
void n_snd_LoadStageBank(void) {
    FRAME(0x18);
    if (GV(s32, STAGE_BANKS + (u32)g_stage * 4) == -1) return;
    GCALL1(F_BANK_FREE, 6);
    if (g_bankOverride == 0) {
        u32 f = GCALL0(F_STAGE_BANK_FILE);
        GCALL3(F_BANK_LOAD, 6, f, BANK_BUFFER);
        return;
    }
    u32 f = GCALL0(F_STAGE_BANK_FILE);
    GCALL3(F_BANK_LOAD_TO, 6, f, g_bankOverride);
    u32 to = g_bankOverride;
    if (to > 0x8014FFFFu) GCALL0(F_BANK_OVERFLOW);
    else g_bankOverrideLast = to;
    g_bankOverride = 0;
}

/* Start a 3D sound for enemy e from bank slot (nibbles) in the first free
 * 3D slot. V0 as the originals leave it: the slot's bit, or 0 if all
 * three are busy. */
static void start_3d(u32 e, u32 slot, u32 nib) {
    for (u32 i = 0; i < 3; i++) {
        if ((g_3dBusy >> i) & 1) continue;
        u32 p = SND3D_PARAMS + i * 10;
        u16 handle = GV(u16, SND_SLOTS + slot * 12 + 8);
        GV(u16, p + 4) = (u16)(nib & 0xF);
        GV(u16, p + 2) = (u16)((nib & 0xF0) >> 4);
        GV(u16, p + 6) = 0x3C;
        GV(u16, p + 8) = 0;
        GV(u16, p + 0) = handle;
        s32 v = falloff(e);
        u32 dir = GCALL2(F_DIRECTION, g_player, e + 0x198);
        GCALL4(F_3D_START, (s32)(s16)i, (s32)(s16)dir, (s32)(s16)(0xFF - v), p);
        u32 s = SND3D_SLOTS + i * 8;
        GV(u32, s) = e;
        GV(u16, s + 4) = (u16)dir;
        GV(u16, s + 6) = PLAYER_YAW_U & 0xFFF;
        g_3dBusy = (s16)(GV(u16, 0x80062E0E) | (1u << i));
        V0 = 1u << i;
        return;
    }
    V0 = 0;
}

/* 80081098 (enemy, slot, nibbles, nibbles2): an enemy's sound in 3D when
 * it's further than 1000 (and 3D sound is on); else snd_CueAtEnemy with
 * the second nibbles. */
void n_snd_EnemySpatial(void) {
    FRAME(0x30);
    u32 e = A0, slot = A1, nib = A2, nib2 = A3;
    if (g_sound3D != 0) {
        s32 h = SLOT_HANDLE(slot);
        if (h < 0) { V0 = (u32)h; return; }
        if (E_DISTANCE(e) >= 1001) { start_3d(e, slot, nib); return; }
    }
    GCALL3(F_SND_CUE_ENEMY, e, slot, nib2);
}

/* 8008127C (enemy, nibbles, b): the enemy's voice (its kind's bank,
 * 2 * +0x84E + 3) in 3D when further than 1000; else 80080718 (enemy, b). */
void n_snd_EnemyVoiceSpatial(void) {
    FRAME(0x30);
    u32 e = A0, nib = A1, b = A2;
    if (g_sound3D != 0 && E_DISTANCE(e) >= 1001) {
        s32 slot = (s32)GV(s16, e + 0x84E) * 2 + 3;
        s32 h = SLOT_HANDLE(slot);
        if (h < 0) { V0 = (u32)h; return; }
        start_3d(e, (u32)slot, nib);
        return;
    }
    GCALL2(F_CUE_ENEMY_B, e, b);
}

/* 80081464: once a frame, follow the 3D sounds playing: volume by the
 * enemy's distance, direction turned by how far the player has turned. */
void n_snd_UpdateSpatial(void) {
    FRAME(0x20);
    if (g_sound3D == 0 || g_3dBusy == 0) return;
    for (u32 i = 0; i < 3; i++) {
        if (!((g_3dBusy >> i) & 1)) continue;
        u32 s = SND3D_SLOTS + i * 8;
        u32 e = GV(u32, s);
        if (E_DISTANCE(e) < 1001) continue;
        s32 v = falloff(e);
        u16 yaw = PLAYER_YAW_U;
        if ((s32)(yaw & 0xFFF) != (s32)GV(s16, s + 6)) {
            GV(u16, s + 4) = (u16)(GV(u16, s + 4) + ((yaw - GV(u16, s + 6)) & 0xFFF));
            GV(u16, s + 6) = PLAYER_YAW_U & 0xFFF;
        }
        GCALL3(F_3D_UPDATE, (s32)(s16)i, (s32)GV(s16, s + 4), (s32)(s16)(0xFF - v));
    }
}
