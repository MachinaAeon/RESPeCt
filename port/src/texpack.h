/* HD texture packs (texpack.c). */
#ifndef TEXPACK_H
#define TEXPACK_H

#include <stddef.h>
#include <stdint.h>

typedef struct HdImg { int w, h; uint32_t *px; } HdImg;   /* RGBA, 8 bits each */

struct RPrim;
int texpack_open(const char *dir);                        /* a pack folder: 1 if it has images */
int texpack_open_listing(const char *list, const char *dir);
int texpack_loaded(void);
void texpack_upload(int x, int y, int w, int h);          /* VRAM written by the CPU */
void texpack_fill(int x, int y, int w, int h);
void texpack_copy(int sx, int sy, int dx, int dy, int w, int h);
void texpack_frame(void);                                 /* nothing queued for drawing */
void texpack_wait(void);
void texpack_reload(void);
extern int g_hd_textures;                                 /* setting */
extern size_t g_hd_budget;

#endif
