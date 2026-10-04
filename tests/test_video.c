/*
 * Tests for the PNG reader, the DMA renderer and override loading.
 *
 *   test_video <tmpdir> [imgdir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../tools/png_write.h"
#include "assets/artsrc.h"
#include "assets/png_read.h"
#include "util/fsutil.h"
#include "video/gfx.h"
#include "video/video.h"
#include "png_vectors.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

/* ---- PNG ------------------------------------------------------------------- */

static int matches_pattern(const png_indexed *p)
{
    if (p->width != PNGVEC_W || p->height != PNGVEC_H)
        return 0;
    for (int y = 0; y < PNGVEC_H; y++)
        for (int x = 0; x < PNGVEC_W; x++)
            if (p->indices[y * PNGVEC_W + x] != PNGVEC_PIXEL(x, y))
                return 0;
    return 1;
}

static void test_png(const char *tmpdir)
{
    char err[256];
    png_indexed p;
    CHECK(png_decode_indexed(pngvec_idx8_dynamic, sizeof pngvec_idx8_dynamic, &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);
    CHECK(png_decode_indexed(pngvec_gray8_fixed, sizeof pngvec_gray8_fixed, &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);
    CHECK(png_decode_indexed(pngvec_idx4, sizeof pngvec_idx4, &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);

    /* Corrupt data must fail, not crash. */
    uint8_t bad[sizeof pngvec_idx8_dynamic];
    memcpy(bad, pngvec_idx8_dynamic, sizeof bad);
    for (size_t i = 60; i < sizeof bad - 20; i += 7)
        bad[i] ^= 0x5A;
    if (png_decode_indexed(bad, sizeof bad, &p, err, sizeof err))
        png_indexed_free(&p);
    CHECK(!png_decode_indexed(bad, 40, &p, err, sizeof err));

    /* Round trip through our own writer (stored deflate blocks). */
    enum { W = 300, H = 250 }; /* > 64 KiB of scanlines: several stored blocks */
    uint8_t *rows = malloc((size_t)W * H);
    uint32_t pal[4] = {0, 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu};
    for (int i = 0; i < W * H; i++)
        rows[i] = (uint8_t)((i * 31 / 7) & 3);
    char path[1024];
    fs_join(path, sizeof path, tmpdir, "roundtrip.png");
    CHECK(png_write_indexed(path, W, H, rows, W, pal, 4));
    CHECK(png_load_indexed(path, &p, err, sizeof err));
    CHECK(p.width == W && p.height == H && memcmp(p.indices, rows, (size_t)W * H) == 0);
    png_indexed_free(&p);

    /* RGBA decodes to a true-color buffer, not indices. */
    uint8_t rgba[4 * 3] = {1, 2, 3, 4, 250, 251, 252, 253, 9, 8, 7, 0};
    CHECK(png_write_rgba(path, 3, 1, rgba));
    CHECK(png_load_indexed(path, &p, err, sizeof err));
    CHECK(p.indices == NULL && p.rgba != NULL && p.width == 3 && p.height == 1);
    CHECK(memcmp(p.rgba, rgba, sizeof rgba) == 0);
    png_indexed_free(&p);
    remove(path);
    free(rows);
}

/* ---- renderer -------------------------------------------------------------- */

/* 3x2 test image: rows {0,1,2} and {3,0,4}, stride 4. */
static const uint8_t test_pixels[8] = {0, 1, 2, 9, 3, 0, 4, 9};

static void make_image(img_image *im, gfx_image *gi)
{
    memset(im, 0, sizeof *im);
    strcpy(im->name, "T");
    im->width = 3;
    im->height = 2;
    im->stride = 4;
    im->palette = IMG_NONE;
    im->pixels = test_pixels;
    memset(gi, 0, sizeof *gi);
    gi->img = im;
    gi->mask = 0xFF;
    gi->hi_state = -1;
}

static uint16_t px(const video *v, int x, int y) { return v->fb[y * v->w + x]; }

static void test_truecolor(void)
{
    /* Palette: index n has color n * 0x0421 (grey ramp), 5 colors. */
    uint16_t cols[5];
    for (int i = 0; i < 5; i++)
        cols[i] = (uint16_t)(i * 0x0421 * 4);
    img_palette pal;
    memset(&pal, 0, sizeof pal);
    pal.ncolors = 5;
    pal.bitspix = 8;
    pal.colors = cols;

    img_image im;
    gfx_image gi;
    make_image(&im, &gi);
    gi.pal = &pal;

    /* 2x override (6x4): opaque everywhere, but the original's zero pixels
     * (0,0), (1,1) are transparent. Original pixel (1,0) = index 1 is
     * repainted slightly redder than palette color 1. */
    uint32_t base1 = img_color_argb(cols[1]);
    int r1 = (int)(base1 >> 16 & 255), g1 = (int)(base1 >> 8 & 255), b1 = (int)(base1 & 255);
    uint8_t px_rgba[6 * 4 * 4];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 6; x++) {
            uint8_t *q = px_rgba + 4 * (y * 6 + x);
            int o = (im.pixels[(y / 2) * 4 + x / 2]);
            uint32_t c = img_color_argb(cols[o]);
            q[0] = (uint8_t)(c >> 16);
            q[1] = (uint8_t)(c >> 8);
            q[2] = (uint8_t)c;
            q[3] = o ? 255 : 0;
        }
    uint8_t *red = px_rgba + 4 * (0 * 6 + 2);
    red[0] = (uint8_t)(r1 + 10);
    png_indexed png = {6, 4, NULL, malloc(sizeof px_rgba)};
    memcpy(png.rgba, px_rgba, sizeof px_rgba);
    char err[256];
    CHECK(gfx_attach_override(&gi, &png, err, sizeof err));
    png_indexed_free(&png);
    CHECK(gi.hi_state == 1 && gi.hi_factor == 2 && gi.hi_detail != NULL);
    CHECK(gi.hi[0] == 0 && gi.hi[2] == 1 && gi.hi[2 * 6 + 0] == 3);
    CHECK(gi.hi_detail[0] == 0 && gi.hi_detail[2] != 0 && (int8_t)(gi.hi_detail[2] & 255) == 10);

    video v;
    CHECK(video_init(&v, 2));
    video_set_palette(&v, 5, cols, 5);
    video_clear(&v, 0);
    dma_blit b = {&gi, 10, 20, DMA_WNZ, 0x0500, 0, 0, 0, 0, 0, 0, 0};
    video_dma(&v, &b);
    CHECK(v.detail != NULL);
    uint32_t *out = malloc((size_t)VIDEO_W * 2 * VIDEO_H * 2 * sizeof *out);
    video_to_argb(&v, out);
    uint32_t got = out[(size_t)(20 * 2) * VIDEO_W * 2 + (10 + 1) * 2]; /* original (1,0) */
    CHECK((int)(got >> 16 & 255) == r1 + 10 && (int)(got >> 8 & 255) == g1 &&
          (int)(got & 255) == b1);

    /* A fade to black removes the detail along with the color. */
    uint16_t black[5] = {0, 0, 0, 0, 0};
    video_set_palette(&v, 5, black, 5);
    video_to_argb(&v, out);
    got = out[(size_t)(20 * 2) * VIDEO_W * 2 + (10 + 1) * 2];
    CHECK((got & 0xFFFFFF) == 0);

    /* A constant-color blit over it drops the detail. */
    video_set_palette(&v, 5, cols, 5);
    b.ctrl = DMA_WNZ | DMA_CNZ;
    b.color = 2;
    video_dma(&v, &b);
    video_to_argb(&v, out);
    got = out[(size_t)(20 * 2) * VIDEO_W * 2 + (10 + 1) * 2];
    CHECK(got == img_color_argb(cols[2]));

    /* Wrong size is rejected and leaves the PNG to the caller. */
    png_indexed bad = {5, 4, NULL, calloc(5 * 4, 4)};
    gfx_image_free_override(&gi);
    CHECK(!gfx_attach_override(&gi, &bad, err, sizeof err));
    png_indexed_free(&bad);
    free(out);
    video_free(&v);
}

static void test_dma(void)
{
    img_image im;
    gfx_image gi;
    make_image(&im, &gi);
    video v;
    CHECK(video_init(&v, 1));
    video_clear(&v, 0xEEEE);

    dma_blit b = {&gi, 10, 20, DMA_WNZ, 0x0505, 0, 0, 0, 0, 0, 0, 0};
    video_dma(&v, &b);
    CHECK(px(&v, 10, 20) == 0xEEEE);      /* zero pixel skipped */
    CHECK(px(&v, 11, 20) == 0x0501);      /* palette 5 (bits 8-14) | pixel */
    CHECK(px(&v, 12, 20) == 0x0502);
    CHECK(px(&v, 10, 21) == 0x0503);
    CHECK(px(&v, 12, 21) == 0x0504);
    CHECK(px(&v, 13, 20) == 0xEEEE);      /* row padding never drawn */

    video_clear(&v, 0xEEEE);
    b.ctrl = DMA_WNZ | DMA_WZ | DMA_FLIPH;
    video_dma(&v, &b);
    CHECK(px(&v, 10, 20) == 0x0502 && px(&v, 11, 20) == 0x0501 && px(&v, 12, 20) == 0x0500);
    CHECK(px(&v, 10, 21) == 0x0504 && px(&v, 12, 21) == 0x0503);

    video_clear(&v, 0xEEEE);
    b.ctrl = DMA_WNZ | DMA_FLIPV;
    video_dma(&v, &b);
    CHECK(px(&v, 10, 20) == 0x0503 && px(&v, 11, 21) == 0x0501);

    /* Constant color: non-zero pixels become palette|constant. */
    video_clear(&v, 0xEEEE);
    b.ctrl = DMA_WNZ | DMA_CNZ;
    b.color = 0x12FF;                      /* only the low byte is used */
    video_dma(&v, &b);
    CHECK(px(&v, 10, 20) == 0xEEEE && px(&v, 11, 20) == 0x05FF && px(&v, 12, 21) == 0x05FF);
    b.ctrl = DMA_CZ;
    video_dma(&v, &b);
    CHECK(px(&v, 10, 20) == 0x05FF && px(&v, 11, 21) == 0x05FF);

    /* Window clipping and the screen edge. */
    video_clear(&v, 0xEEEE);
    b.ctrl = DMA_WNZ;
    b.color = 0;
    video_set_window(&v, 11, 0, 11, VIDEO_H - 1);
    video_dma(&v, &b);
    CHECK(px(&v, 11, 20) == 0x0501 && px(&v, 12, 20) == 0xEEEE && px(&v, 10, 21) == 0xEEEE);
    video_set_window(&v, 0, 0, VIDEO_W - 1, VIDEO_H - 1);
    b.x = -2;
    b.y = VIDEO_H - 1;
    video_dma(&v, &b);
    CHECK(px(&v, 0, VIDEO_H - 1) == 0x0502);

    /* DMA scaling: 0x80 doubles, 0x200 halves. */
    video_clear(&v, 0xEEEE);
    b.x = 100;
    b.y = 100;
    b.scale_x = b.scale_y = 0x80;
    video_dma(&v, &b);
    CHECK(px(&v, 102, 100) == 0x0501 && px(&v, 103, 101) == 0x0501 && px(&v, 105, 103) == 0x0504);
    CHECK(px(&v, 106, 100) == 0xEEEE);
    CHECK(dma_scaled_size(3, 0x200) == 1 && dma_scaled_size(100, 0x180) == 66);

    /* Source sub-rectangle (clipped object): columns 1-2 of row 1 only. */
    video_clear(&v, 0xEEEE);
    b.scale_x = b.scale_y = 0;
    b.src_x = 1;
    b.src_y = 1;
    b.src_w = 2;
    b.src_h = 1;
    video_dma(&v, &b);
    CHECK(px(&v, 100, 100) == 0xEEEE && px(&v, 101, 100) == 0x0504 && px(&v, 102, 100) == 0xEEEE);
    b.src_x = b.src_y = b.src_w = b.src_h = 0;
    video_free(&v);

    /* Two-page bitmap with a view offset; row fill for page erase. */
    CHECK(video_init_bitmap(&v, 2, 512, 512));
    video_clear(&v, 0);
    video_fill_rows(&v, 256, 256, 0x0001);
    CHECK(video_get_pixel(&v, 10, 255) == 0 && video_get_pixel(&v, 10, 256) == 1);
    video_put_pixel(&v, 60, 300, 0x0102);
    v.view_x = 56;
    v.view_y = 256;
    v.colram[0x0102] = 0x7C00;
    {
        uint32_t *argb = malloc((size_t)VIDEO_W * VIDEO_H * 4 * sizeof *argb);
        video_to_argb(&v, argb);
        CHECK(argb[(size_t)(44 * 2) * VIDEO_W * 2 + 4 * 2] == 0xFFFF0000u);
        free(argb);

        /* view_pad widens the view symmetrically: the same pixel moves right
         * by the pad, and the row gets 2 * pad * scale entries longer. */
        v.view_pad = 30;
        CHECK(video_view_width(&v) == (VIDEO_W + 60) * 2 && video_view_height(&v) == VIDEO_H * 2);
        argb = malloc((size_t)video_view_width(&v) * video_view_height(&v) * sizeof *argb);
        video_to_argb(&v, argb);
        CHECK(argb[(size_t)(44 * 2) * video_view_width(&v) + (4 + 30) * 2] == 0xFFFF0000u);
        free(argb);
        v.view_pad = 0;
    }
    video_free(&v);

    /* Scale-3 framebuffer: each original pixel is a 3x3 block. */
    CHECK(video_init(&v, 3));
    video_clear(&v, 0);
    b.x = 1;
    b.y = 1;
    b.scale_x = b.scale_y = 0;
    video_dma(&v, &b);
    CHECK(px(&v, 6, 3) == 0x0501 && px(&v, 8, 5) == 0x0501 && px(&v, 9, 3) == 0x0502);
    CHECK(px(&v, 3, 3) == 0);

    /* Override at k=3 on a scale-3 framebuffer is copied 1:1. */
    uint8_t hi[9 * 6];
    for (int i = 0; i < 9 * 6; i++)
        hi[i] = (uint8_t)(i + 1);
    gi.hi = hi;
    gi.hi_factor = 3;
    gi.hi_state = 1;
    video_clear(&v, 0);
    video_dma(&v, &b);
    CHECK(px(&v, 3, 3) == 0x0501 && px(&v, 4, 3) == 0x0502 && px(&v, 3, 4) == 0x050A);
    CHECK(px(&v, 11, 8) == 0x0500 + 54);

    /* An art layer switched off (hi_off): drawn from the original pixels, as for an image without an override. */
    gi.hi_off = 1;
    video_clear(&v, 0);
    video_dma(&v, &b);
    CHECK(px(&v, 6, 3) == 0x0501 && px(&v, 8, 5) == 0x0501 && px(&v, 9, 3) == 0x0502 && px(&v, 3, 3) == 0);   /* the scale 3 blocks again */
    gi.hi_off = 0;
    video_clear(&v, 0);
    video_dma(&v, &b);
    CHECK(px(&v, 3, 3) == 0x0501 && px(&v, 4, 3) == 0x0502 && px(&v, 3, 4) == 0x050A);   /* switched on: the override */

    /* k=6 on scale 3: every second override pixel (centers 1, 3, 5, ...). */
    uint8_t hi6[18 * 12];
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 18; x++)
            hi6[y * 18 + x] = (uint8_t)(x + 1);
    gi.hi = hi6;
    gi.hi_factor = 6;
    video_clear(&v, 0);
    video_dma(&v, &b);
    CHECK(px(&v, 3, 3) == 0x0502 && px(&v, 4, 3) == 0x0504 && px(&v, 11, 3) == 0x0512);
    b.ctrl = DMA_WNZ | DMA_FLIPH;
    video_clear(&v, 0);
    video_dma(&v, &b);
    CHECK(px(&v, 3, 3) == 0x0511 && px(&v, 11, 3) == 0x0501);

    /* Color RAM lookup. */
    uint16_t cols[2] = {0x0000, 0x7C00};
    video_set_palette(&v, 5, cols, 2);
    uint32_t *argb = malloc((size_t)v.w * v.h * sizeof *argb);
    video_clear(&v, 0x0501);
    video_to_argb(&v, argb);
    CHECK(argb[0] == 0xFFFF0000u);
    free(argb);
    video_free(&v);
}

/* ---- overrides through the catalog ----------------------------------------- */

static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, unsigned long v)
{
    w16(p, (unsigned)(v & 0xFFFF));
    w16(p + 2, (unsigned)(v >> 16));
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

/* Library with two 3x2 images, GOOD and BADSIZE, sharing one palette. */
static size_t build_lib(uint8_t *buf)
{
    memset(buf, 0, 512);
    const size_t pal_off = 28, pix_off = pal_off + 4, oset = pix_off + 16;
    w16(buf, 2);
    w16(buf + 2, 4);
    w32(buf + 4, oset);
    w16(buf + 8, 0x634);
    w16(buf + pal_off, 0);
    w16(buf + pal_off + 2, 0x7FFF);
    memcpy(buf + pix_off, test_pixels, 8);
    memcpy(buf + pix_off + 8, test_pixels, 8);
    const char *names[2] = {"GOOD", "BADSIZE"};
    for (int i = 0; i < 2; i++) {
        uint8_t *r = buf + oset + i * 50;
        memcpy(r, names[i], strlen(names[i]));
        w16(r + 22, 3);
        w16(r + 24, 2);
        w16(r + 26, 3);
        w32(r + 28, pix_off + (size_t)i * 8);
        w16(r + 46, 0xFFFF);
        w16(r + 48, 0xFFFF);
    }
    uint8_t *p = buf + oset + 100;
    memcpy(p, "P", 1);
    p[11] = 8;
    w16(p + 12, 2);
    w32(p + 14, pal_off);
    return oset + 100 + 26;
}

static int warnings;
static void count_warn(const char *msg, void *user)
{
    (void)msg;
    (void)user;
    warnings++;
}

static void test_overrides(const char *tmpdir)
{
    char dir[1024], ovr[1024], path[1024];
    fs_join(dir, sizeof dir, tmpdir, "gfx");
    fs_join(ovr, sizeof ovr, tmpdir, "gfx_hd");
    /* Directories are created by CMake (see CMakeLists.txt). */
    uint8_t buf[512];
    size_t len = build_lib(buf);
    fs_join(path, sizeof path, dir, "T.IMG");
    CHECK(write_file(path, buf, len));
    fs_join(path, sizeof path, dir, "T.LOD");
    CHECK(write_file(path, "t.img\r\n", 7));

    uint8_t hi[6 * 4];
    for (int i = 0; i < 24; i++)
        hi[i] = (uint8_t)(i % 2);
    uint32_t pal[2] = {0, 0xFFFFFFFFu};
    fs_join(path, sizeof path, ovr, "GOOD.png");
    CHECK(png_write_indexed(path, 6, 4, hi, 6, pal, 2));
    fs_join(path, sizeof path, ovr, "badsize.png");
    CHECK(png_write_indexed(path, 5, 4, hi, 6, pal, 2));

    static const char *const lods[] = {"T.LOD", NULL};
    catalog cat;
    CHECK(catalog_open(&cat, dir, ovr, lods, NULL, NULL));
    gfx_cache gc;
    CHECK(gfx_cache_init(&gc, &cat, count_warn, NULL));
    const gfx_image *good = gfx_find(&gc, "GOOD");
    const gfx_image *bad = gfx_find(&gc, "BADSIZE");
    CHECK(good && good->hi_state == 1 && good->hi_factor == 2 && good->hi[1] == 1);
    CHECK(bad && bad->hi_state == -1);
    CHECK(warnings == 1);

    gc.use_overrides = 0;
    gfx_cache_reload(&gc);
    good = gfx_find(&gc, "GOOD");
    CHECK(good && good->hi_state == -1);
    gfx_cache_free(&gc);
    catalog_close(&cat);
}

/* ---- original data ---------------------------------------------------------- */

static void test_original(const char *imgdir)
{
    catalog cat;
    CHECK(catalog_open(&cat, imgdir, NULL, NULL, NULL, NULL));
    gfx_cache gc;
    CHECK(gfx_cache_init(&gc, &cat, NULL, NULL));
    video v;
    CHECK(video_init(&v, 2));
    /* Every catalog image draws, flipped and scaled, without touching
     * memory it should not (run under ASan to make this meaningful). */
    for (int i = 0; i < cat.nimages; i++) {
        dma_blit b = {gfx_get(&gc, &cat.images[i]), -20, -10, DMA_WNZ | DMA_FLIPH, 0x0101, 0,
                      0x0C0, 0x140, 0, 0, 0, 0};
        video_dma(&v, &b);
    }
    video_free(&v);
    gfx_cache_free(&gc);
    catalog_close(&cat);
}

/* ---- overrides in zip files ---------------------------------------------- */

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

/* Writes a zip with `n` entries (CRCs left 0: the reader does not check them). With deflate, each entry is one
 * stored deflate block (method 8). */
static int write_zip(const char *path, const char *const *names, const uint8_t *data, size_t len, int n, int deflate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    uint8_t cd[4096];
    size_t cdn = 0;
    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        uint8_t lh[30] = {0}, blk[5];
        size_t nl = strlen(names[i]), clen = len + (deflate ? 5 : 0);
        put32(lh, 0x04034b50u);
        put16(lh + 4, 20);
        put16(lh + 8, deflate ? 8 : 0);
        put32(lh + 18, (uint32_t)clen);
        put32(lh + 22, (uint32_t)len);
        put16(lh + 26, (unsigned)nl);
        fwrite(lh, 1, sizeof lh, f);
        fwrite(names[i], 1, nl, f);
        if (deflate) {
            blk[0] = 1; /* final, stored */
            put16(blk + 1, (unsigned)len);
            put16(blk + 3, (unsigned)~len & 0xFFFF);
            fwrite(blk, 1, 5, f);
        }
        fwrite(data, 1, len, f);
        uint8_t *c = cd + cdn;
        memset(c, 0, 46);
        put32(c, 0x02014b50u);
        put16(c + 4, 20);
        put16(c + 6, 20);
        put16(c + 10, deflate ? 8 : 0);
        put32(c + 20, (uint32_t)clen);
        put32(c + 24, (uint32_t)len);
        put16(c + 28, (unsigned)nl);
        put32(c + 42, off);
        memcpy(c + 46, names[i], nl);
        cdn += 46 + nl;
        off += (uint32_t)(30 + nl + clen);
    }
    fwrite(cd, 1, cdn, f);
    uint8_t eocd[22] = {0};
    put32(eocd, 0x06054b50u);
    put16(eocd + 8, (unsigned)n);
    put16(eocd + 10, (unsigned)n);
    put32(eocd + 12, (uint32_t)cdn);
    put32(eocd + 16, off);
    fwrite(eocd, 1, sizeof eocd, f);
    return fclose(f) == 0;
}

static int count_cb_n;
static int count_cb_zipimg;
static void count_cb(const char *name, void *user)
{
    (void)user;
    count_cb_n++;
    if (str_ieq(name, "zipimg.png"))
        count_cb_zipimg++;
}

static void test_zip_overrides(const char *tmpdir)
{
    char zip1[1024], zip2[1024], err[256];
    png_indexed p;
    fs_join(zip1, sizeof zip1, tmpdir, "zipart_stored.zip");
    static const char *const stored[] = {"hd/ZipImg.png", "folder/"};
    static const char *const deflated[] = {"Deflated.png", "ZipImg.png"};
    CHECK(write_zip(zip1, stored, pngvec_idx8_dynamic, sizeof pngvec_idx8_dynamic, 1, 0));

    /* a single zip given instead of a directory */
    count_cb_n = count_cb_zipimg = 0;
    CHECK(art_list(zip1, count_cb, NULL) == 1 && count_cb_zipimg == 1);
    CHECK(art_load_png(zip1, "ZIPIMG.PNG", &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);
    CHECK(!art_load_png(zip1, "missing.png", &p, err, sizeof err) && err[0]);

    /* a directory with a deflated zip: both names listed once, both load */
    fs_join(zip2, sizeof zip2, tmpdir, "zipart_deflated.zip");
    CHECK(write_zip(zip2, deflated, pngvec_idx4, sizeof pngvec_idx4, 2, 1));
    count_cb_n = count_cb_zipimg = 0;
    CHECK(art_list(zip2, count_cb, NULL) == 2 && count_cb_zipimg == 1);
    CHECK(art_load_png(zip2, "deflated.png", &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);

    /* the directory holding both zips: their entries are listed, the first zip in the listing wins for ZipImg */
    count_cb_n = count_cb_zipimg = 0;
    CHECK(art_list(tmpdir, count_cb, NULL) > 0 && count_cb_zipimg == 1);
    CHECK(art_load_png(tmpdir, "Deflated.png", &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);
    CHECK(art_load_png(tmpdir, "zipimg.png", &p, err, sizeof err));
    CHECK(matches_pattern(&p));
    png_indexed_free(&p);
    art_forget();
    remove(zip1);
    remove(zip2);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_video <tmpdir> [imgdir]\n");
        return 2;
    }
    test_png(argv[1]);
    test_dma();
    test_truecolor();
    test_overrides(argv[1]);
    test_zip_overrides(argv[1]);
    if (argc > 2)
        test_original(argv[2]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all video tests passed\n");
    return 0;
}
