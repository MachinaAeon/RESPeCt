/* Captions: words a mod puts over parts of the game's pictures (captions.c). */
#pragma once
#include <stdint.h>

typedef struct {
    float x, y, w, h;      /* on the picture shown, 0..1 */
    int bright;            /* 128: as the game draws it normally; 0: faded out */
    int r, g, b;           /* the picture's own background colour there (its commonest), for the box */
    int tr, tg, tb;        /* and its words' colour (the commonest one well apart from that) */
    const char *text;      /* UTF-8 */
} Caption;

void caption_add(int depth, int x0, int y0, int x1, int y1, uint32_t hash, const char *text);   /* texels from VRAM's corner; hash 0: any picture */
uint32_t caption_hash(int depth, int x0, int y0, int x1, int y1);
int caption_sources(void);
void caption_prim(int tx, int ty, int depth, int clut_x, int clut_y, int umin, int vmin, int umax, int vmax,
                  float x0, float y0, float x1, float y1, int bright, int da_x, int da_y, int disp_w, int disp_h);
void caption_display(int x, int y);
void caption_fill(int x, int y, int w, int h, int disp_w, int disp_h);
int caption_shown(int disp_x, int disp_y, const Caption **out);   /* the captions on the buffer shown */
