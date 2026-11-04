/* Authored PC raster messages. No mod: all lookups are inert. */
#ifndef MODFS_PC_TEXT_H
#define MODFS_PC_TEXT_H
/* Host decoded picture storage, separate from original PS1 RAM/SPU.
 * The measured complete diary/pickup/message families exceed128MiB.
 * Keep a finite256MiB ceiling; decode and per-resource limits still apply. */
#define MODFS_PC_PICTURE_BUDGET (UINT64_C(256) << 20)
#include <stdint.h>
#include <stddef.h>
#define MODFS_PC_DIARIES 1
#define MODFS_PC_DIARY_KEYS 1
#define MODFS_PC_BOOK 1
int modfs_pc_book_object(uint32_t index);
typedef struct { uint32_t org[2], words[2], tags[2][2048]; } ModPcBookDraw;
int modfs_pc_book_begin(ModPcBookDraw *draw);
void modfs_pc_book_end(const ModPcBookDraw *draw);
/* Suppress only the draw emission of validated, replaced diary key panels. */
int modfs_pc_diary_panel(uint32_t panel);
typedef struct { uint32_t version, doc, page; } ModPcDiaryState;
int modfs_pc_has_diaries(void);
uint32_t modfs_pc_diary_pages(uint32_t doc);
int modfs_pc_diary_load(uint32_t doc, uint32_t page);
void modfs_pc_diary_get_state(ModPcDiaryState *state);
void modfs_pc_diary_set_state(const ModPcDiaryState *state);
int modfs_pc_diary_check_state(const ModPcDiaryState *state, const void *ram, size_t bytes);
#define MODFS_PC_CAPTIONS 1
typedef struct { uint32_t version, pickup, caption_block, caption_index, caption_offset; } ModPcBitmapState;
void modfs_pc_get_state(ModPcBitmapState *state);
int modfs_pc_valid_state(const ModPcBitmapState *state);
void modfs_pc_set_state(const ModPcBitmapState *state);
void modfs_pc_entry(uint32_t text);
void modfs_pc_caption(uint32_t index, uint32_t offset);
int modfs_pc_caption_message(int x, int y, int w, int h);
int modfs_pc_has_bitmaps(void);
void modfs_pc_pickup(uint32_t item);
uint32_t modfs_pc_pickup_state(void);
void modfs_pc_restore_pickup(uint32_t item);
/* Logical screen coordinates: the central original 320 x 240 picture. */
int modfs_pc_message(uint32_t text, int x, int y, int w, int h);
void modfs_pc_composite(uint32_t *out, int w, int h, int native_w, int native_h);
#endif
