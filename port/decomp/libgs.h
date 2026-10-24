/* Shared by the TEST-overlay frame, model, shooting and game-flow code
 * (render.c, models.c, shooting.c, gameflow.c, checkpoint.c):
 * the Psy-Q library calls they make (libgs / libgte / libgpu, identified
 * by what they do; names marked "probably" are inferred), the ordering
 * tables, and helpers for the game stack.
 *
 * Game stack: these natives reproduce the original's stack frame exactly
 * (FRAME(size), decomp.h), so callees see the same stack addresses, and
 * locals passed to callees sit where the original had them. Much of this
 * code runs with the stack moved to the scratchpad (0x1F800400, see
 * render_SortFrame / shot_Update), so anything on the stack, or behind a
 * pointer argument that may point at a caller's local, is accessed with
 * GS (through gp()), never GV. */
#ifndef LIBGS_H
#define LIBGS_H

#include "game_vars.h"

/* Copy n words between game addresses (either may be on the stack). */
static inline void copy_words(u32 dst, u32 src, u32 n) {
    for (u32 i = 0; i < n; i++) GS(u32, dst + i * 4) = GS(u32, src + i * 4);
}

/* Call game code with $ra as the original's jal leaves it (the address
 * after its delay slot). Needed where the callee keeps $ra somewhere
 * compared: in a global (0x8002A350), or in a stack frame that isn't in
 * the main stack (render_Flip runs on the frame callback's stack, in
 * game data at 0x8005Bxxx). */
static inline u32 gcall_ra(u32 ra, u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    u32 old = R[31];
    R[31] = ra;
    u32 v = gcall(addr, a0, a1, a2, a3);
    R[31] = old;
    return v;
}

/* ---- kernel ---- */
#define F_GET_SP            0x80039AC8u   /* returns sp */
#define F_SET_SP            0x80039A28u   /* (sp): switch stacks */
#define F_RAND              0x800398A8u   /* BIOS rand() */
#define F_MEMSET            0x80039898u   /* (dst, byte, n) */
#define F_VSYNC             0x800255F8u
#define F_FILE_LOAD         0x800115C0u   /* (dir, table, list, file, [dest]) */
#define F_RANDOM_BYTE       0x80011570u

/* ---- libgpu ---- */
#define F_LOAD_IMAGE        0x80035620u
#define F_DRAW_SYNC         0x80035374u
#define F_CLEAR_IMAGE       0x800354F8u
#define F_PUT_DISP_ENV      0x80035BA4u
#define F_PUT_DRAW_ENV      0x800359D8u
#define F_SET_DISP_MASK     0x800352DCu
#define F_DRAW_PRIM         0x8003590Cu
#define F_SET_DEF_DISP_ENV  0x80038148u   /* (env, x, y, w, [h]) */
#define F_SET_DEF_DRAW_ENV  0x80038088u
#define F_SET_DRAW_MODE     0x80038388u   /* (p, dfe, dtd, tpage, [tw]) */
#define F_SET_TILE          0x80038328u   /* probably setTile */
#define F_SET_POLY_F4       0x800382A8u
#define F_SET_POLY_FT4      0x800382C8u
#define F_SET_SEMI_TRANS    0x80038248u
#define F_GET_TPAGE         0x80038188u   /* (tp, abr, x, y) */
#define F_GET_CLUT          0x800381C8u   /* (x, y) */
#define F_ADD_PRIM          0x800381E8u   /* (ot, prim) */
#define F_PRIM_INIT_383B8   0x800383B8u   /* (prim, n): primitive set-up */

/* ---- libgte ---- */
#define F_ROT_MATRIX        0x8002AA68u
#define F_ROT_MATRIX_Y      0x8002ACF8u
#define F_SCALE_MATRIX      0x8002A638u
#define F_SET_ROT_MATRIX    0x8002A778u
#define F_SET_TRANS_MATRIX  0x8002A7A8u
#define F_ROT_TRANS         0x8002A958u   /* (v, out, flag) */
#define F_ROT_TRANS_PERS4   0x8002A9E8u   /* (v0..v3, [sxy0..3, p, flag]) */
#define F_SET_FOG_NEAR_FAR  0x8002A168u
#define F_SET_FAR_COLOR     0x8002A8D8u

/* ---- libgs ---- */
#define F_GS_SET_WORKBASE   0x8002E208u   /* packet area */
#define F_GS_CLEAR_OT       0x8002DED8u   /* (offset, point, ot) */
#define F_GS_SORT_OT        0x8002DF38u   /* (src, dst) */
#define F_GS_DRAW_OT        0x8002DEA8u
#define F_GS_SORT_CLEAR     0x8002CA50u   /* (r, g, b, ot) */
#define F_GS_GET_ACTIVE_BUFF 0x8002CB88u
#define F_GS_SWAP_DISP_BUFF 0x8002CDA8u   /* probably */
#define F_GS_SET_DRAW_OFFSETS 0x8002E218u /* (x0, y0, x1, y1): draw/display offsets */
#define F_GS_SET_CLIP       0x8002CD28u   /* (rect) */
#define F_GS_INIT_3D        0x8002D798u   /* probably GsInit3D */
#define F_GS_GET_LWS        0x80033CB8u   /* (coord, lw, ls) */
#define F_GS_GET_LS         0x800339E8u   /* (coord, ls) */
#define F_GS_SET_LS_MATRIX  0x8002CF14u
#define F_GS_SET_LIGHT_MATRIX 0x8002CF40u
#define F_GS_SORT_OBJECT    0x800333D8u   /* (object, ot, scratch work area) */
#define F_GS_SET_AMBIENT    0x8002DE58u
#define F_GS_SET_PROJECTION 0x8002D818u
#define F_GS_SET_FLAT_LIGHT 0x8002D838u
#define F_GS_SET_LIGHT_MODE 0x8002DDD8u
#define F_GS_SET_REF_VIEW2  0x80033FD8u
#define F_GS_SORT_SPRITE    0x8002C4B8u
#define F_GS_GET_TIM_INFO   0x8002E048u
#define F_GS_MAP_UNIT       0x80033508u   /* HMD: offsets to pointers */
#define F_GS_SCAN_UNIT      0x80033208u   /* HMD: next primitive block (type, ptr) */
#define F_GS_SCAN_ANIM      0x8002F938u
#define F_GS_LINK_ANIM      0x8002F9E0u
#define F_GS_COORD_UNIT     0x800336A8u   /* HMD: coordinates block */
#define F_GS_GET_HEAD       0x800333C0u

#define SCRATCH_WORK        0x1F800000u   /* GsSortObject's work area */
#define SCRATCH_STACK       0x1F800400u

/* ---- the frame ---- */
#define BUF_INDEX     GV(u8, 0x80062D1C)   /* double buffer: 0 or 1 */
#define PACKET_AREA   0x800C3E50u          /* two 0x17700-byte packet buffers */
/* Ordering tables: a GsOT (20 bytes) per buffer in each of four arrays. */
#define OT_SCREEN     0x800AFC88u          /* the one drawn; the others sort into it */
#define OT_WORLD      0x800B3D70u          /* room and models */
#define OT_FRONT      0x800B4BB8u          /* drawn over the world */
#define OT_OVERLAY    0x800B4D40u          /* the reticle, flagged models */
#define OT(base)      ((base) + (u32)BUF_INDEX * 20)

#define IDENTITY_MATRIX 0x80065B90u        /* GsIDMATRIX (8 words) */

/* ---- enemies ---- */
#define ENEMIES       GV(u32, 0x800AB510)  /* array of 0x8C4-byte records */
#define ENEMY_COUNT   GV(s32, 0x800AF530)
#define ENEMY_SIZE    0x8C4u
#define g_inputLocked GV(s16, 0x800B3D2C)
#define D_800A8610    GV(u32, 0x800A8610)  /* 3D scene active */

#endif
