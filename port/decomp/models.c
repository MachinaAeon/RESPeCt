/* Models (TEST overlay): binding HMD model data (libgs hierarchical
 * models) to a model record, and choosing the primitive drivers.
 *
 * An HMD file is a list of blocks; scanning a block (GsScanUnit) yields
 * (type, slot) pairs, and each slot gets the driver function for its
 * type: polygon drivers (category 0) from one of two tables (with and
 * without fog, 0x800B3CC2), shared-vertex drivers (2), animation (3),
 * MIMe (4), ground (5), envelope/camera (7). Animation blocks also link
 * the record's animation state (+0x1D0/+0x1D4).
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "libgs.h"
#include "poll.h"

void n_model_Bind(void);
void n_model_DriverEnvelope(void);
void n_model_DriverAnim(void);
void n_model_BindAnimDrivers(void);
void n_model_PolyDriver(void);
void n_model_DriverShared(void);
void n_model_DriverCat1(void);
void n_model_FindUnit(void);

#define F_DRIVER_CAT1   0x8006EB04u
#define F_DRIVER_CAT4   0x8006E984u
#define F_DRIVER_CAT5   0x8006E97Cu
#define F_HMD_MIME_A    0x8002FA28u
#define F_HMD_MIME_B    0x8002FB0Cu
#define F_HMD_MIME_C    0x8007EB5Cu

/* The two passes at the start of model_Bind: in each block, follow the
 * chain of units (+0 next, -1 = end) to the first one whose type byte
 * (+0xC, top byte) is `cat`, and give its data (+4) the record's
 * animation tables if it has none yet. Only the first block with such a
 * unit counts. */
static void bind_first_unit(u32 hmd, u32 rec, u32 cat) {
    s32 n = GV(s32, hmd + 0xC);
    for (s32 i = 0; i < n; i++) {
        u32 node = GV(u32, hmd + 0x10 + (u32)i * 4);
        if (node == 0) continue;
        for (;;) {
            if ((GV(u32, node + 0xC) & 0xFF000000u) == cat) break;
            if (GV(u32, node) == 0xFFFFFFFFu) { node = 0; break; }
            node = GV(u32, node);
        }
        if (node == 0) continue;
        u32 sub = GV(u32, node + 4);
        u32 anim = GV(u32, rec + 0x88C);           /* the record's animation set */
        if (cat == 0) {
            if (GV(u32, sub + 8) == 0) {
                u32 t = GV(u32, anim + 4);
                GV(u32, sub + 8) = t + GV(u32, t) * 4;
                GV(u32, sub + 0xC) = t + GV(u32, t + 4) * 4;
            }
        } else {
            if (GV(u32, sub + 0xC) == 0) {
                GV(u32, sub + 0xC) = GV(u32, anim + 8) + 4;
                GV(u32, sub + 0x10) = GV(u32, GV(u32, rec + 0x88C) + 0xC);
            }
        }
        return;
    }
}

/* 8006E304 (with animation, hmd, record): bind a model. Part i's entry
 * (+0x2C + 8i) gets block i (+0x30) and, when the model has its own
 * coordinates (+0x16C, 0x50 each), coordinate i-1; the root coordinate
 * is parented to the record's own (+0x180, scale +0x170). */
void n_model_Bind(void) {
    u32 anim = A0, hmd = A1, rec = A2;
    FRAME(0x40);
    u32 type_at = sp + 0x10, slot_at = sp + 0x14;     /* GsScanUnit's result */
    GCALL1(F_GS_MAP_UNIT, hmd);
    if ((u8)anim) {
        bind_first_unit(hmd, rec, 0);
        bind_first_unit(hmd, rec, 0x03000000u);
    }
    u32 nb = GV(u8, hmd + 0xC);
    GV(u32, rec + 0x16C) = 0;
    GV(u8, rec + 0x28) = (u8)nb;
    for (u32 b = 0; b < nb; b++) {
        u32 blk = GV(u32, hmd + 0x10 + b * 4);
        GV(u32, rec + 0x30 + b * 8) = blk;
        if (blk == 0) continue;
        GCALL4(F_GS_SCAN_UNIT, blk, 0, 0, 0);
        for (;;) {
            if (gcall(F_GS_SCAN_UNIT, 0, type_at, OT_SCREEN, SCRATCH_WORK) == 0) {
                GV(u32, rec + 0x2C + b * 8) = 0;
                break;
            }
            u32 cat = GS(u8, sp + 0x13);
            if (cat < 2 || cat == 5 || cat == 6) {
                if (GS(u32, type_at) & 0x800000) {          /* coordinates block */
                    GV(u32, rec + 0x16C) = GCALL2(F_GS_COORD_UNIT, hmd, GS(u32, slot_at));
                    GS(u32, type_at) &= 0xFF7FFFFFu;
                }
                cat = GS(u8, sp + 0x13);
            }
            if (cat >= 8) continue;
            u32 type = GS(u32, type_at);
            u32 v;
            switch (cat) {
            case 0:
                v = GCALL1(0x8006EB78u, type);
                GS(u32, GS(u32, slot_at)) = v;
                if ((type & 0x240000) == 0x40000) {
                    u32 p = GS(u32, slot_at);
                    GS(u32, p + 8) |= 0x1000000;
                }
                break;
            case 1:
                v = GCALL1(F_DRIVER_CAT1, type);
                GS(u32, GS(u32, slot_at)) = v;
                break;
            case 2:
                v = GCALL1(0x8006F16Cu, type);
                GS(u32, GS(u32, slot_at)) = v;
                break;
            case 3:
                if (nb >= 9) {
                    GV(u16, rec + 0x864) = GS(u16, GS(u32, slot_at) + 0xE);
                    GV(u16, rec + 0x84C) = (u16)b;
                    GV(u32, rec + 0x83C) = GV(u32, rec + 0x30 + b * 8);
                }
                v = GCALL1(0x8006E9FCu, type);
                GS(u32, GS(u32, slot_at)) = v;
                GV(u32, rec + 0x1D0) = GCALL2(F_GS_LINK_ANIM, rec + 0x1D4, GS(u32, slot_at));
                if (GCALL2(F_GS_SCAN_ANIM, GS(u32, slot_at), 0)) GCALL0(0x8006EA18u);
                break;
            case 4:
                if (GV(u32, rec + 0x35C) == 0) GV(u32, rec + 0x35C) = GS(u32, slot_at);
                v = GCALL1(F_DRIVER_CAT4, type);
                GS(u32, GS(u32, slot_at)) = v;
                type = GS(u32, type_at);
                if (type == 0x4010028) {
                    u32 h = GCALL0(F_GS_GET_HEAD);
                    GCALL2(F_HMD_MIME_A, GS(u32, slot_at), h);
                } else if (type == 0x4010029) {
                    u32 h = GCALL0(F_GS_GET_HEAD);
                    GCALL2(F_HMD_MIME_B, GS(u32, slot_at), h);
                } else if (type >= 0x4010020 && type <= 0x4010021) {
                    u32 h = GCALL0(F_GS_GET_HEAD);
                    GCALL2(F_HMD_MIME_C, GS(u32, type_at), h);
                }
                break;
            case 5:
                v = GCALL1(F_DRIVER_CAT5, type);
                GS(u32, GS(u32, slot_at)) = v;
                break;
            case 6:
                break;
            case 7:
                if ((type & 0xFFFF) == 0x100) D_800A8610 = 0;
                v = GCALL1(0x8006E890u, type);
                GS(u32, GS(u32, slot_at)) = v;
                break;
            }
        }
    }
    u32 coords = GV(u32, rec + 0x16C);
    if (coords)
        for (s32 i = 1; i < (s32)nb - 1; i++)
            GV(u32, rec + 0x2C + (u32)i * 8) = coords + (u32)(i - 1) * 0x50;
    u32 c = GV(u32, rec + 0x16C);
    u32 k = 0;
    while (GV(u32, c + 0x4C)) { c += 0x50; k++; }      /* the root (no parent) */
    GV(u32, GV(u32, rec + 0x16C) + k * 0x50 + 0x4C) = rec + 0x180;
    GCALL2(0x8006F69Cu, rec + 0x180, rec + 0x170);    /* coord_SetMatrix */
}

/* 8006E890 (type): category 7 drivers (envelope, camera). */
void n_model_DriverEnvelope(void) {
    switch (A0) {
    case 0x7000100: V0 = 0x8002FDE8u; break;
    case 0x7010100: V0 = 0x8002FE28u; break;
    case 0x7020100: V0 = 0x800303A8u; break;
    case 0x7030100: V0 = 0x80030988u; break;
    case 0x7000200: V0 = 0x80031BE8u; break;
    case 0x7010200: V0 = 0x80031C38u; break;
    case 0x7020200: V0 = 0x80031EB8u; break;
    default: V0 = 0xFFFFFFFFu; break;
    }
}

/* 8006E9FC (type): animation driver (only the one type the game uses). */
void n_model_DriverAnim(void) {
    V0 = A0 == 0x3800000 ? 0x8007E38Cu : 0xFFFFFFFFu;
}

/* 8006EA18: the drivers for each animation sequence type. */
void n_model_BindAnimDrivers(void) {
    FRAME(0x30);
    while (GCALL2(F_GS_SCAN_ANIM, 0, sp + 0x10)) {
        u32 type = GS(u32, sp + 0x10);
        u32 slot = GS(u32, sp + 0x14);
        switch (type) {
        case 0x3000111: GV(u32, slot) = 0x80032708u; break;
        case 0x3000011: GV(u32, slot) = 0x8007E724u; break;
        case 0x3002011: GV(u32, slot) = 0x80032118u; break;
        case 0x3002111: GV(u32, slot) = 0x80032BF8u; break;
        }
    }
}

/* 8006EB04 (type): category 1 drivers (types 0x1000000 + n). */
void n_model_DriverCat1(void) {
    switch (A0 - 0x1000000u) {
    case 0x00: V0 = 0x8002EB28u; break;
    case 0x0C: V0 = 0x8002EC58u; break;
    case 0x0D: V0 = 0x8002EEC8u; break;
    case 0x14: V0 = 0x8002F0F8u; break;
    case 0x15: V0 = 0x8002F548u; break;
    default: V0 = 0xFFFFFFFFu; break;
    }
}

/* 8006EB78 (type): polygon drivers, fogged or not. */
static const struct { u32 type, fog, plain; } poly_drivers[] = {
    {0x00008, 0x8004AE64u, 0x8003DF90u},
    {0x00009, 0x8004B034u, 0x8003E14Cu},
    {0x0000A, 0x8002E298u, 0x8002E298u},
    {0x0000C, 0x8004B24Cu, 0x8003E334u},
    {0x0000D, 0x8004B500u, 0x8003E530u},
    {0x00010, 0x8004B810u, 0x8003E764u},
    {0x00011, 0x8004BA2Cu, 0x8003E950u},
    {0x00012, 0x8002E448u, 0x8002E448u},
    {0x00014, 0x8004BC78u, 0x8003EB78u},
    {0x00015, 0x8004BFF0u, 0x8003EDE8u},
    {0x00048, 0x8002E648u, 0x8002E648u},
    {0x40048, 0x8004CF18u, 0x8003FC64u},
    {0x40049, 0x8004D8F0u, 0x80040668u},
    {0x4004C, 0x80030FD8u, 0x80030FD8u},
    {0x4004D, 0x8004EDECu, 0x80042058u},
    {0x40050, 0x800506F8u, 0x80043758u},
    {0x40051, 0x8005128Cu, 0x80044330u},
    {0x40054, 0x80052A88u, 0x80045B98u},
    {0x40055, 0x80053978u, 0x80046880u},
    {0x80010, 0x8004C344u, 0x8003F084u},
    {0x200008, 0x8002E7B8u, 0x8002E7B8u},
    {0x200011, 0x8002E948u, 0x8002E948u},
    {0x240049, 0x8005555Cu, 0x80048240u},
    {0x24004D, 0x80055EC8u, 0x80048BE8u},
    {0x240051, 0x80056B10u, 0x80049664u},
    {0x240055, 0x80057670u, 0x8004A1D8u},
    {0x480010, 0x8004C344u, 0x8003F084u},
};
void n_model_PolyDriver(void) {
    int fog = GV(s16, 0x800B3CC2) != 0;
    for (size_t i = 0; i < sizeof poly_drivers / sizeof poly_drivers[0]; i++)
        if (poly_drivers[i].type == A0) {
            V0 = fog ? poly_drivers[i].fog : poly_drivers[i].plain;
            return;
        }
    V0 = 0xFFFFFFFFu;
}

/* 8006F16C (type): shared-vertex drivers (set up in the scratchpad). */
void n_model_DriverShared(void) {
    u32 type = A0;
    FRAME(0x18);
    if (type == 0x2000000) {
        GCALL1(0x8002FC18u, SCRATCH_WORK);
        V0 = 0x8002FBF8u;
    } else if (type == 0x2000001) {
        GCALL1(0x8002FCA8u, SCRATCH_WORK);
        V0 = 0x8002FBF8u;
    } else {
        V0 = 0xFFFFFFFFu;
    }
}

/* 8007C558 (hmd, category): the first unit of a category (type byte at
 * +0xC) in the HMD's blocks, as model_Bind looks for them: each block's
 * chain of units (+0 the next one's word index from the HMD, -1 ends) is
 * followed until a match. The block's first unit only marks a candidate
 * (a later match in its chain replaces it); blocks are searched until
 * one gives a unit. The unit, or 0. */
void n_model_FindUnit(void) {
    u32 hmd = A0, cat = A1 << 24;
    u32 blk = hmd + 0x10;
    s32 n = GV(s32, hmd + 0xC);
    u32 found = 0;
    if (n > 0) {
        for (s32 i = 0;;) {
            u32 idx = GV(u32, blk);
            if (idx != 0) {
                u32 u = hmd + idx * 4;
                if ((GV(u32, u + 0xC) & 0xFF000000u) == cat) found = u;
                u32 next = GV(u32, u);
                while (next != 0xFFFFFFFFu) {
                    u = hmd + next * 4;
                    if ((GV(u32, u + 0xC) & 0xFF000000u) == cat) { found = u; break; }
                    next = GV(u32, u);
                    if (next == 0xFFFFFFFFu) break;
                    LOOP_POLL(0x8007C5E4u);
                }
            }
            if (++i >= n) break;
            blk += 4;
            if (found) break;
            LOOP_POLL(0x8007C5FCu);
        }
    }
    V0 = found;
}
