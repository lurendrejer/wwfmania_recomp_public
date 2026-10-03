/* Shared by imgview and `imgtool render`: draw one image centered on screen. */
#ifndef WWF_PREVIEW_H
#define WWF_PREVIEW_H

#include "video/video.h"

typedef struct {
    int fliph, flipv;
    int flash;         /* draw non-zero pixels in the constant color (hit flash) */
    uint16_t scale;    /* DMA 8.8 scale, DMA_SCALE_1X = original size */
} preview_opts;

/* Clears the screen, loads the image's palette into palette 1 and blits it. */
void preview_draw(video *v, const img_lib *lib, const gfx_image *gi, const preview_opts *o);

#endif
