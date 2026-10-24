/* Placing a room's enemies (TEST overlay), and their per-room specials.
 *
 * The room's event file lists its enemies (0x800BBB78: 0x18-byte entries,
 * -1 ends): s16 type (bit 0x4000: placed asleep), s16 room flag (killed),
 * pos x/y/z, rot x/y/z (u16). Type 0x11 is a random zombie. Each one is
 * loaded (model file by type from the enemy file table 0x800A3EDC, into
 * 0x800C3E50, decompressed, its textures uploaded), made from the record
 * template at 0x800B42D0, and listed in the room's enemy slots
 * (0x800BBC18: 0x14 bytes: s16 type, s16 flag, record, model data,
 * s16 rot y, flags (per type, 0x800A4060; 0x10/0x20: asleep, 2: the
 * fire-hit sparkle, 4: spinning)).
 *
 * Per type there are two program slots (0x800BB020), each loading its
 * enemy program overlay; 0x8008DE84 calls a record's program.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"
#include "game_stack.h"

void n_enemy_ProgramSlot(void);
void n_enemy_CallProgram(void);
void n_enemy_PlaceAll(void);
void n_enemy_ReloadTextures(void);
void n_enemy_ItemFiles(void);
void n_enemy_FileTable(void);
void n_enemy_TypeFlags(void);
void n_enemy_RoomSpecials(void);
void n_enemy_ResetMarkers(void);

#define SLOTS            0x800BBC18u   /* the room's enemies, 0x14 bytes */
#define ENEMY_FILES      0x800A3EDCu
#define TYPE_FLAGS       0x800A4060u
#define LOAD_BUF         0x800C3E50u
#define FLAGS_ROOM       0x80062FA0u
#define FLAGS_GAME       0x80062FC0u
#define F_FLAG_TEST      0x8008F64Cu
#define F_PROG_LOAD      0x80076B14u   /* (dir, table, file, dest): an overlay */
#define F_DECOMPRESS     0x80016F78u   /* (src, dst) */
#define F_UPLOAD_TIMS    0x8006F370u
#define F_MODEL_TOP      0x800735A4u   /* free model memory */
#define F_MODEL_TAKE     0x800735B4u   /* (words) */
#define F_SHOTS_CLEAR    0x8008E4A0u
#define F_RANDOM_BYTE_   0x80011570u
#define F_MODEL_INIT     0x8006E304u   /* (0, model, record) */
#define F_OBJ_MATRIX2    0x80082760u   /* (coord, scale) */
#define F_ENEMY_BY_ID    0x800892D8u   /* (id): record index, or negative */
#define F_FX_HIT         0x80099068u   /* (pos, a, b, c, [kind]) */
#define F_ENEMY_TICK     0x8007F404u
#define F_SQRT           0x8002A3D8u
#define ROOM_OBJECTS     GV(u32, 0x800AB52C)   /* the room's objects: count s16 +0x1C, 0x24 each */

/* 8008DC94 (record, &type, overlay): the program slot (0 or 1) for an
 * enemy type, loading its program into a free one (as an overlay, or as
 * a file to the slot's address); the slot, or -1. Some types share
 * another's program. */
void n_enemy_ProgramSlot(void) {
    FRAME(0x20);
    u32 rec = A0, tp = A1, ovl = A2;
    if (GV(u8, rec + 0x315) >= 0x32) { V0 = (u32)-1; return; }
    if (GS(s32, tp) == 0x11) GS(s32, tp) = 0x10;
    if (GS(s32, tp) == 0xE) GS(s32, tp) = 0xD;
    if (GS(s32, tp) == 0x17) GS(s32, tp) = 0x16;
    if (GS(s32, tp) < 0xB) GS(s32, tp) = 0;
    s32 type = GS(s32, tp);
    const u32 slots = 0x800BB020u;
    for (s32 i = 1; i >= 0; i--) {
        u32 s = slots + (u32)i * 4;
        if (GV(s16, s) == type) {
            GV(u16, s + 2) = 1;
            GV(s16, rec + 0x84E) = (s16)i;
            V0 = (u32)i;
            return;
        }
    }
    s32 n = -1;
    if (GV(s16, slots + 6) == 0) n = 1;
    else if (GV(s16, slots + 2) == 0) n = 0;
    if (ovl) {
        if (n == 0) GCALL4(F_PROG_LOAD, 0x80058554u, 0x800A31C4u, (u32)GS(s32, tp), GV(u32, 0x80010048));
        else if (n == 1) GCALL4(F_PROG_LOAD, 0x80058554u, 0x800A31C4u, (u32)GS(s32, tp) + 0x28, GV(u32, 0x8001004C));
    } else {
        if (n == 0) {
            GS(u32, sp + 0x10) = GV(u32, 0x80010048);
            GCALL4(F_FILE_LOAD, 0x8006C690u, 0x80058554u, 0x800A31C4u, (u32)GS(s32, tp));
        } else if (n == 1) {
            GS(u32, sp + 0x10) = GV(u32, 0x8001004C);
            GCALL4(F_FILE_LOAD, 0x8006C690u, 0x80058554u, 0x800A31C4u, (u32)GS(s32, tp) + 0x28);
        }
    }
    V0 = (u32)n;
}

/* 8008DE84 (record, entry): call entry `entry` of the record's program
 * (slot +0x84E); it gets a pointer to the slot number and the record. */
void n_enemy_CallProgram(void) {
    FRAME(0x20);
    u32 rec = A0, entry = A1;
    s32 slot = GV(s16, rec + 0x84E);
    GS(s32, sp + 0x10) = slot;
    if (GV(u8, rec + 0x315) >= 0x32) return;
    u32 tbl;
    if (slot == 0) tbl = 0x800A3AF8u;
    else if (slot == 1) tbl = 0x800A3B80u;
    else return;
    u32 fn = GV(u32, tbl + entry * 4);
    A0 = sp + 0x10;
    A1 = rec;
    A2 = rec;
    jalr_call(fn);
}

/* Load an enemy model file into the load buffer and decompress it; the
 * decompressed data. */
static u32 load_model(s32 type) {
    GS(u32, R[29] + 0x10) = LOAD_BUF;
    u32 r = GCALL4(F_FILE_LOAD, 0x8006C6A8u, 0x8005855Cu, ENEMY_FILES, (u32)type);
    u32 d = LOAD_BUF + r * 4;
    GCALL2(F_DECOMPRESS, LOAD_BUF, d);
    return d;
}

/* 8008E4DC: place the room's enemies (those whose flag isn't set). */
void n_enemy_PlaceAll(void) {
    FRAME(0x40);
    u32 mem = GCALL0(F_MODEL_TOP);
    GCALL0(F_SHOTS_CLEAR);
    u32 e = GV(u32, 0x800BBB78);
    u32 slot = SLOTS;
    for (; GV(s16, e) >= 0; e += 0x18) {
        if (GCALL2(F_FLAG_TEST, FLAGS_ROOM, (u32)(s32)GV(s16, e + 2))) continue;
        int asleep = 0;
        if (GV(s16, e) == 0x11) {                         /* a random zombie */
            asleep = 1;
            u32 r = GCALL0(F_RANDOM_BYTE_) & 0xFF;
            GV(s16, e) = r < 0x34 ? 0xD : r < 0x48 ? 0xE : r < 0x5C ? 0xF : 0xB;
        }
        if (GV(u16, e) & 0x4000) {
            asleep = 2;
            GV(u16, e) &= 0xBFFF;
        }
        s32 type = GV(s16, e);
        u32 d = load_model(type);
        type = GV(s16, e);
        if (type >= 0x5D || type < 0x5B) GCALL1(F_UPLOAD_TIMS, d + GV(u32, d + 8));
        /* the model's data, copied to model memory */
        u32 dst = mem;
        for (s32 i = 0; i < (GV(s32, d + 8) - GV(s32, d + 4)) / 4; i++, dst += 4)
            GV(u32, dst) = GV(u32, d + (u32)(GV(s32, d + 4) / 4 * 4) + (u32)i * 4);
        s32 size = GV(s32, d + 8) - GV(s32, d + 4);
        GV(u32, slot + 8) = mem;
        u32 rec = ENEMIES + (u32)ENEMY_COUNT * ENEMY_SIZE;
        for (u32 k = 0; k < ENEMY_SIZE; k += 4) GV(u32, rec + k) = GV(u32, 0x800B42D0u + k);
        GCALL3(F_MODEL_INIT, 0, mem, rec);
        GV(u32, rec + 0x170) = 0x1000;
        GV(u32, rec + 0x174) = 0x1000;
        GV(u32, rec + 0x178) = 0x1000;
        GCALL2(F_OBJ_MATRIX2, rec + 0x180, rec + 0x170);
        GV(u8, rec + 0x2A) = 0;
        u8 cnt = GV(u8, 0x800AF530);
        GV(u8, rec + 0x29) = 1;
        GV(u32, rec + 0x8C0) = 0x190;
        GV(u8, rec + 0x314) = cnt;
        for (s32 i = 0; i < GV(u8, rec + 0x28); i++) GV(u8, rec + (u32)i) = 1;
        mem += (u32)(size / 4 * 4);
        GCALL1(F_MODEL_TAKE, (u32)(size / 4));
        ENEMY_COUNT++;
        GV(u32, rec + 0x198) = GV(u32, e + 4);
        GV(u32, rec + 0x19C) = GV(u32, e + 8);
        GV(u32, rec + 0x1A0) = GV(u32, e + 0xC);
        GV(u16, rec + 0x1C4) = GV(u16, e + 0x10);
        GV(u16, rec + 0x1C6) = GV(u16, e + 0x12);
        GV(u16, rec + 0x1C8) = GV(u16, e + 0x14);
        GV(u8, rec + 0x315) = GV(u8, e);
        GV(u16, slot) = GV(u16, e);
        GV(u16, slot + 2) = GV(u16, e + 2);
        GV(u16, rec + 0x850) = GV(u16, e + 2);
        GV(u32, slot + 4) = rec;
        GV(s32, slot + 0xC) = GV(s16, e + 0x12);
        if (GV(u16, e) < 0x5A) GV(u32, slot + 0x10) = GV(u8, TYPE_FLAGS + (u32)(s32)GV(s16, slot));
        else GV(u32, slot + 0x10) = 0;
        if (GV(u32, slot + 0x10) & 8) GV(u8, rec + 0x29) = 0;
        if (asleep == 1) {
            GV(u32, slot + 0x10) |= 0x10;
            GV(u8, rec + 0x29) = 0;
        }
        if (asleep == 2) {
            GV(u32, slot + 0x10) |= 0x20;
            GV(u8, rec + 0x29) = 0;
        }
        slot += 0x14;
    }
}

/* 8008E9C0: upload the room's enemies' textures again (after something
 * else used the texture pages). */
void n_enemy_ReloadTextures(void) {
    FRAME(0x28);
    for (u32 e = GV(u32, 0x800BBB78); GV(s16, e) != -1; e += 0x18) {
        s32 type = GV(s16, e);
        if (type < 0x5D && type >= 0x5B) continue;
        u32 d = load_model(type);
        GCALL1(F_UPLOAD_TIMS, d + GV(u32, d + 8));
    }
}

/* 8008EA80 (file): the item model files to load: this one if it isn't
 * empty (kind 2), then -1. */
void n_enemy_ItemFiles(void) {
    u32 n = A0;
    s32 k = 0;
    if ((s32)(GV(u32, ENEMY_FILES + (n + 1) * 4) - GV(u32, ENEMY_FILES + n * 4)) > 0) {
        k = 1;
        GV(u32, 0x800AF540) = n;
        GV(u32, 0x800AF544) = 2;
    }
    GV(s32, 0x800AF540 + (u32)k * 12) = -1;
    V0 = (u32)-1;
}

/* 8008EAF4: the enemy / item file table. */
void n_enemy_FileTable(void) { V0 = ENEMY_FILES; }

/* 8008EB00 (type): the type's flags. */
void n_enemy_TypeFlags(void) { V0 = GV(u8, TYPE_FLAGS + A0); }

/* 8008F014: no markers (0x800BBC68: four of 0x18 bytes: panel, distance). */
void n_enemy_ResetMarkers(void) {
    for (int i = 0; i < 4; i++) {
        u32 m = 0x800BBC68u + (u32)i * 0x18;
        GV(s32, m) = -1;
        GV(s16, m + 0xE) = -1;
    }
    V0 = 0;
}

/* 8008EB18: once a frame: the room's enemies' specials (the type 0x24
 * waiting by enemy 0x69, type 0x27's flag, sparkles, spinning, sleepers
 * waking), each enemy's tick, and the markers' distances to the player. */
void n_enemy_RoomSpecials(void) {
    FRAME(0x30);
    if ((u32)(GV(u16, 0x800B3D2C) - 1) < 0xC) return;
    for (u32 s = SLOTS; s < SLOTS + 0x50; s += 0x14) {
        u32 rec = GV(u32, s + 4);
        if (rec == 0) continue;
        s16 type = GV(s16, s);
        if (type == 0x24) {
            if (GCALL2(F_FLAG_TEST, FLAGS_GAME, 0x25) == 0) {
                s32 k = (s32)GCALL1(F_ENEMY_BY_ID, 0x69);
                if (k >= 0 && (GV(u32, ENEMIES + (u32)k * ENEMY_SIZE + 0x320) & 1)) {
                    u32 other = ENEMIES;
                    u32 objs = ROOM_OBJECTS;
                    if (ENEMY_COUNT > 0) {
                        while (GV(s16, other + 0x850) != 0x69) other += ENEMY_SIZE;
                        if (0 < ENEMY_COUNT) {
                            u32 me = GV(u32, s + 4);
                            GV(u32, me + 0x198) = GV(u32, other + 0x198);
                            GV(s32, GV(u32, s + 4) + 0x19C) = -0x1F4;
                            GV(u32, GV(u32, s + 4) + 0x1A0) = GV(u32, other + 0x1A0);
                            s32 n = GV(s16, objs + 0x1C);
                            for (s32 i = 0; i < n; i++) {
                                u32 o = objs + (u32)i * 0x24;
                                if (GV(u32, o) != 0x10000 || GV(s16, o + 0x20) != 0x25) continue;
                                GV(u16, o) = 3;
                                GV(u32, o + 4) = GV(u32, other + 0x198);
                                GV(u32, o + 8) = GV(u32, other + 0x19C);
                                GV(u32, o + 0xC) = GV(u32, other + 0x1A0);
                            }
                        }
                    }
                }
            }
        } else if (type == 0x27) {
            if (GCALL2(F_FLAG_TEST, FLAGS_GAME, 6)) {
                GV(u32, s + 0x10) |= 2;
            } else {
                GV(u32, s + 0x10) &= ~2u;
                GV(u8, GV(u32, s + 4) + 0x29) = 0;
            }
        }
        if (GV(u32, s + 0x10) & 2) {                       /* sparkle every 32 frames */
            u16 c = (u16)(GV(u16, rec + 0x866) + 1);
            GV(u16, rec + 0x866) = c;
            if ((c & 0x1F) == 0 && !GCALL2(F_FLAG_TEST, FLAGS_ROOM, (u32)(s32)GV(s16, s + 2))) {
                s16 t = GV(s16, s);
                if (t == 0x36) {
                    GS(u32, sp + 0x10) = 6;
                    GCALL4(F_FX_HIT, rec + 0x180, 0xC8, 0, 0);
                } else if (D_800BB19C != 3 && GV(u8, rec + 0x29)) {
                    GS(u32, sp + 0x10) = 6;
                    if (t >= 0xD && t < 0x10) GCALL4(F_FX_HIT, rec + 0x180, 0xC8, (u32)-0xFA, 0);
                    else GCALL4(F_FX_HIT, rec + 0x180, 0xC8, 0, 0);
                }
            }
        }
        if (GV(u32, s + 0x10) & 4) GV(u16, rec + 0x1C6) = (u16)((GV(u16, rec + 0x1C6) + 0x14) & 0xFFF);
        if (GV(u32, s + 0x10) & 0x10) {                    /* asleep */
            s16 st = GV(s16, rec + 0x31C);
            if (st == 1) {
                if (GV(u32, ENEMIES + GV(u32, rec + 0x35C) * ENEMY_SIZE + 0x320) & 2) {
                    GV(u8, rec + 0x29) = 1;
                    GV(s32, rec + 0x19C) = -0x1F4;
                    GV(u16, rec + 0x31C) = (u16)(GV(u16, rec + 0x31C) + 1);
                }
            } else if (st == 2) {
                u32 objs = ROOM_OBJECTS;
                s32 n = GV(s16, objs + 0x1C);
                for (s32 i = 0; i < n; i++) {
                    u32 o = objs + (u32)i * 0x24;
                    if (GV(s16, o) != 0 || GV(s16, o + 2) != GV(s16, s + 2)) continue;
                    GV(u16, o) = 1;
                    GV(u32, o + 8) = 0;
                    GV(u32, o + 4) = GV(u32, rec + 0x198);
                    GV(u32, o + 0xC) = GV(u32, rec + 0x1A0);
                }
                GV(u16, rec + 0x31C) = (u16)(GV(u16, rec + 0x31C) + 1);
            }
        }
        GCALL1(F_ENEMY_TICK, rec);
    }
    /* markers: distance from the player to their panel */
    for (int i = 0; i < 4; i++) {
        u32 m = 0x800BBC68u + (u32)i * 0x18;
        s32 v = GV(s32, m);
        if (v < 0) return;
        u32 p = 0x800C09D8u + (u32)v * 0x44;
        s32 dx = GV(s32, g_player + 0x18) - GV(s16, p + 0x10) * 4;
        s32 dz = GV(s32, g_player + 0x20) - GV(s16, p + 0x18) * 16;
        GV(u32, m + 4) = GCALL1(F_SQRT, (u32)dx * (u32)dx + (u32)dz * (u32)dz);
    }
}
