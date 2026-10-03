/*
 * Reader for Midway ".IMG" image libraries (the art source files the original
 * build fed to the WWFLD/LOAD2 ROM builders). See docs/IMG_FORMAT.md.
 *
 * An IMG library holds 8-bit indexed images, 15-bit palettes and optional
 * per-image point tables. Pixel value 0 is transparent (the DMA blitter only
 * writes non-zero pixels in the modes the game uses).
 */
#ifndef WWF_IMG_H
#define WWF_IMG_H

#include <stddef.h>
#include <stdint.h>

#define IMG_NONE 0xFFFFu
#define IMG_NAME_MAX 16
#define IMG_PAL_NAME_MAX 10
#define IMG_PTTBL_SIZE 40

/*
 * Palette indices stored in the file count three built-in editor palettes
 * that are never written out; img_image.palette is already corrected for
 * that and indexes img_lib.palettes directly.
 */
#define IMG_BUILTIN_PALETTES 3

typedef struct {
    char name[IMG_NAME_MAX + 1];
    uint16_t flags;          /* raw editor flags, meaning partly unknown */
    int16_t anix, aniy;      /* animation (anchor) point, pixels from top-left */
    uint16_t width, height;
    uint16_t stride;         /* bytes per pixel row in the file: (width+3)&~3 */
    uint16_t palette;        /* index into img_lib.palettes or IMG_NONE */
    int16_t ani2x, ani2y, ani2z; /* secondary anim point, -1 when unused */
    uint16_t frame;
    uint16_t pttbl;          /* index into img_lib point tables or IMG_NONE */
    uint16_t opals;          /* alternate palette index (raw) or IMG_NONE */
    const uint8_t *pixels;   /* height rows of `stride` bytes, values are palette indices */
} img_image;

typedef struct {
    char name[IMG_PAL_NAME_MAX + 1];
    uint8_t flags;
    uint8_t bitspix;         /* bits per pixel the ROM builder packs images to */
    uint16_t ncolors;
    uint16_t *colors;        /* ncolors entries, xRRRRRGGGGGBBBBB */
} img_palette;

typedef struct {
    char path[512];
    uint16_t version;        /* 0 for the old layout, 0x6xx otherwise */
    uint8_t *data;           /* whole file; image pixels point into it */
    size_t size;

    img_image *images;
    int nimages;

    img_palette *palettes;
    int npalettes;

    const uint8_t *pttbls;   /* npttbls * IMG_PTTBL_SIZE raw bytes */
    int npttbls;
} img_lib;

/* Loads and validates a library. On failure returns 0 and writes err. */
int img_lib_load(img_lib *lib, const char *path, char *err, size_t err_len);

/*
 * Loads a background data file (.BDD) as a library of unnamed images:
 * a count line, then per image "index width height palette" and width*height
 * pixel bytes (stride = width). Palettes are not read (BGNDPAL.ASM has them).
 */
int img_lib_load_bdd(img_lib *lib, const char *path, char *err, size_t err_len);
void img_lib_free(img_lib *lib);

/* Returns the index of the named image (case-insensitive) or -1. */
int img_lib_find(const img_lib *lib, const char *name);

/* Returns the point table bytes for an image, or NULL. */
const uint8_t *img_image_pttbl(const img_lib *lib, const img_image *img);

/* Converts a 15-bit Midway color to 0xAARRGGBB with full alpha. */
uint32_t img_color_argb(uint16_t c);

/*
 * Mask the ROM builder applied when it packed pixels to the palette's
 * bitspix. A few original images contain stray indices above the palette
 * size (e.g. 64 in a 64-color, 6 bpp palette); on the hardware those lose
 * their high bits, so apply this before looking a pixel up.
 */
uint8_t img_palette_mask(const img_palette *pal);

/*
 * Expands an image to RGBA8888 (width*height*4 bytes, tightly packed).
 * Pixels are masked with img_palette_mask; index 0 becomes fully
 * transparent. Indices still outside the palette are drawn magenta.
 */
void img_image_to_rgba(const img_lib *lib, const img_image *img, uint8_t *out);

#endif
