#include "preview.h"

#define PREVIEW_PAL 1
#define PREVIEW_BG 0x1084     /* dark gray */
#define PREVIEW_FLASH 255     /* constant color index, set to white */

void preview_draw(video *v, const img_lib *lib, const gfx_image *gi, const preview_opts *o)
{
    const img_image *im = gi->img;
    v->colram[0] = PREVIEW_BG;
    video_clear(v, 0);
    if (im->palette != IMG_NONE) {
        const img_palette *p = &lib->palettes[im->palette];
        video_set_palette(v, PREVIEW_PAL, p->colors, p->ncolors);
    }
    v->colram[PREVIEW_PAL * 256 + PREVIEW_FLASH] = 0x7FFF;

    dma_blit b = {0};
    b.image = gi;
    b.scale_x = b.scale_y = o->scale ? o->scale : DMA_SCALE_1X;
    b.x = (VIDEO_W - dma_scaled_size(im->width, b.scale_x)) / 2;
    b.y = (VIDEO_H - dma_scaled_size(im->height, b.scale_y)) / 2;
    b.ctrl = DMA_WNZ | (o->fliph ? DMA_FLIPH : 0) | (o->flipv ? DMA_FLIPV : 0) |
             (o->flash ? DMA_CNZ : 0);
    b.pal = PREVIEW_PAL * 0x101;
    b.color = PREVIEW_FLASH;
    video_dma(v, &b);
}
