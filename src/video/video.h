/*
 * Software model of the Wolf Unit video hardware: a 16-bit framebuffer of
 * color RAM indices, the color RAM itself and the DMA2 blitter.
 *
 * Each framebuffer entry is `palette << 8 | color`, exactly as on the
 * hardware, and the final RGB lookup happens when the frame is presented.
 * That is why palette effects (fades, per-wrestler colors, flashes) work
 * unchanged at any resolution.
 *
 * The framebuffer is `scale` times the original 400x254 screen. Callers
 * always pass original screen coordinates; the blitter draws each original
 * pixel as a scale x scale block, or samples a higher-resolution indexed
 * override (gfx.h) when one is available.
 *
 * DMA behavior (write modes, constant color, palette register masking) follows
 * MAME's midtunit/midwunit DMA emulation; see docs/VIDEO.md.
 */
#ifndef WWF_VIDEO_H
#define WWF_VIDEO_H

#include <stdint.h>

#include "gfx.h"

#define VIDEO_W 400
#define VIDEO_H 254
#define VIDEO_PALETTES 128
#define VIDEO_COLORS (VIDEO_PALETTES * 256)

/* DMA control word bits (orig/SYS.EQU, orig/DISPLAY.EQU). */
#define DMA_GO 0x8000u
#define DMA_WZ 0x0001u   /* write zero pixels as palette|0 */
#define DMA_WNZ 0x0002u  /* write non-zero pixels as palette|pixel */
#define DMA_CZ 0x0004u   /* write zero pixels as palette|constant */
#define DMA_CNZ 0x0008u  /* write non-zero pixels as palette|constant */
#define DMA_FLIPH 0x0010u
#define DMA_FLIPV 0x0020u

/* 8.8 scale factor for 1:1 (DMASCALEX/Y; larger values shrink). */
#define DMA_SCALE_1X 0x100u

struct video;

/* What video_dma worked out for one blit, in framebuffer pixels: the clipped destination rectangle and
 * where each of its pixels samples the source (docs/VIDEO.md, "GPU path"). */
typedef struct {
    const gfx_image *image;
    int use_hi;            /* sample the override (image->hi) instead of the original pixels */
    int k;                 /* source pixels per original pixel: 1, or the override's factor */
    int src_x, src_y;      /* the source rectangle inside the image, in source pixels (override pixels when use_hi) */
    int src_w, src_h;
    int x0, y0;            /* framebuffer position of the rectangle's top left (can lie outside the clip) */
    int xs, ys, xe, ye;    /* clipped destination, inclusive */
    uint32_t step_x, step_y; /* source pixels per framebuffer pixel, 16.16 */
    int flip_h, flip_v;
    uint16_t ctrl;         /* DMA_* write mode */
    uint16_t pal;          /* palette << 8, bit 15 clear */
    uint16_t cval;         /* palette | constant color */
    int detail;            /* the override carries detail words */
} video_blit_job;

/*
 * Optional sink that takes over the drawing (the GPU path, src/platform/gpu_video.c). While one is installed
 * and `cpu_auth` is 0, video_dma, video_clear, video_fill_rows and video_put_pixel hand their work to it
 * instead of writing `fb`/`detail`, and `fb` is stale. Anything that needs the pixels on the CPU
 * (video_get_pixel, video_to_argb_area, saving a state, a blit the sink refuses) calls video_sync_cpu(),
 * which asks the sink to read the picture back into fb/detail and makes the CPU copy the one that counts
 * (`cpu_auth` = 1) until the sink has taken it over again at the next present.
 * Without a sink every function behaves as before.
 */
typedef struct {
    void *user;
    /* returns 0 if it cannot draw this blit (video_dma then reads back and draws it on the CPU) */
    int (*blit)(void *user, struct video *v, const video_blit_job *j);
    /* rectangle in framebuffer pixels set to one value; the detail plane is cleared there */
    void (*fill)(void *user, struct video *v, int x, int y, int w, int h, uint16_t value);
    /* writes the picture into v->fb (and v->detail, allocating it if the sink holds detail) */
    void (*read_back)(void *user, struct video *v);
    /* one pixel of the picture (framebuffer pixel x, y) without reading everything back; -1 if it cannot say.
     * Optional. video_get_pixel uses it for the first few reads of a frame: the game reads video memory a few
     * times per frame by accident (the getup meter reads address 0), and a full read back each time costs more
     * than all the drawing. */
    int (*read_pixel)(void *user, struct video *v, int x, int y);
    /* Optional (NULL = always ready). Asked for each blit of an image that has an override: 0 means the sink has not
     * made its textures for the override yet (it has queued that work); video_dma then draws the original pixels of
     * the image this time, exactly as for an image without override. */
    int (*hi_ready)(void *user, const gfx_image *image);
} video_sink;

typedef struct video {
    int scale;             /* framebuffer pixels per original pixel */
    int bw, bh;            /* bitmap size in original pixels */
    int w, h;              /* framebuffer size: bw*scale x bh*scale */
    int view_x, view_y;    /* top-left of the displayed VIDEO_W x VIDEO_H area */
    int view_pad;          /* extra original pixels shown left and right of it */
    int view_pad_y;        /* extra original rows shown above and below it */
    uint16_t *fb;          /* palette << 8 | color */
    /* Parallel true-color detail words (gfx.h), allocated the first time a
     * true-color override is drawn. NULL until then. */
    uint32_t *detail;
    uint16_t colram[VIDEO_COLORS]; /* xRRRRRGGGGGBBBBB */
    int win_l, win_t, win_r, win_b; /* DMA clip window, bitmap coords, inclusive */
    uint32_t argb[VIDEO_COLORS];   /* scratch for video_to_argb */
    /* Pixels with bit 15 set (palette 128 and up) are replaced by this
     * color, like the Wolf Unit's VMUX palette match (black). */
    uint32_t high_pal_argb;
    const video_sink *sink; /* see video_sink; kept across video_init_bitmap */
    int cpu_auth;           /* 1: fb/detail are current and the sink's copy is stale */
    /* who asked for the last CPU access to the picture (a diagnostic for the log): the program counter of the game
     * code that read or wrote video memory (set by the machine), or 1 = a blit the sink refused, 2 = the picture
     * was converted on the CPU, 3 = a saved state */
    uint32_t sync_tag;
    int px_reads;           /* single pixel reads through the sink since the last present (the sink resets it) */
    int cpu_px_ops;         /* pixel reads and writes (video_get_pixel, video_put_pixel) since the front end last reset it:
                             * a screen that moves pixels one by one (the original's fade-in with small squares) shows here */
} video;

typedef struct {
    const gfx_image *image;
    int x, y;              /* top-left of the drawn rectangle, screen coords */
    uint16_t ctrl;         /* DMA_* write mode and flip bits */
    uint16_t pal;          /* palette register: palette number in bits 8-14 */
    uint16_t color;        /* constant color register (low 8 bits used) */
    uint16_t scale_x, scale_y; /* 8.8, DMA_SCALE_1X = original size */
    /* Optional source rectangle inside the image (all 0 = whole image),
     * used when the game clips an object at the window edge. */
    int src_x, src_y, src_w, src_h;
} dma_blit;

/* Bitmap the size of the screen (400x254), view at 0,0. */
int video_init(video *v, int scale);

/* Larger bitmap, e.g. the Wolf Unit's 512x512 with two display pages. */
int video_init_bitmap(video *v, int scale, int bw, int bh);
void video_free(video *v);

/* Fills the framebuffer with one color RAM index (the hardware autoerase). */
void video_clear(video *v, uint16_t value);

/* Sets the DMA clip window (screen coords, inclusive), clamped to the screen. */
void video_set_window(video *v, int left, int top, int right, int bottom);

/* Copies colors into color RAM starting at palette `palnum`, index 0. */
void video_set_palette(video *v, int palnum, const uint16_t *colors, int ncolors);

/* Draws an image through the blitter. */
void video_dma(video *v, const dma_blit *b);

/* Size in original pixels that a blit covers after DMA scaling. */
int dma_scaled_size(int size, uint16_t scale);

/* Size in framebuffer pixels of the displayed area: (VIDEO_W + 2 * view_pad)
 * x (VIDEO_H + 2 * view_pad_y), times scale. The pads enlarge the view
 * symmetrically around the original screen. */
int video_view_width(const video *v);
int video_view_height(const video *v);

/* Converts the displayed area to 0xAARRGGBB
 * (video_view_width x video_view_height entries). */
void video_to_argb(video *v, uint32_t *out);

/* Only the part (ax, ay, aw, ah) of that view (framebuffer pixels, from its
 * top left); out gets aw * ah entries. */
void video_to_argb_area(video *v, uint32_t *out, int ax, int ay, int aw, int ah);

/* Fills bitmap rows [row, row + count) with one value (page erase). */
void video_fill_rows(video *v, int row, int count, uint16_t value);

/* CPU access to one bitmap pixel (original coordinates). */
uint16_t video_get_pixel(video *v, int x, int y);

/* Installs (or, with NULL, removes) the sink. fb holds the picture and is handed to the sink at its next present. */
void video_set_sink(video *v, const video_sink *sink);
/* With a sink: makes fb/detail current (reads the sink's picture back unless that is done). */
void video_sync_cpu(video *v);
/* With a sink: the caller replaced the whole of fb (loading a state): the CPU copy is the one that counts. */
void video_fb_replaced(video *v);
void video_put_pixel(video *v, int x, int y, uint16_t value);

#endif
