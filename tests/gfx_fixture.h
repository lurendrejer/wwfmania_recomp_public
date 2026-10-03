/*
 * A tiny image library and override folder shared by test_gfx_async and test_gfx_worker: six 3x2 images IM0..IM5
 * (palette of two colors) and, in a folder of overrides at 2x:
 *   IM0, IM4, IM5  indexed PNGs         IM1  true-color PNG (RGBA, mapped onto the palette)
 *   IM2            wrong size (5x4)      IM3  no override
 */
#ifndef WWF_GFX_FIXTURE_H
#define WWF_GFX_FIXTURE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../tools/png_write.h"
#include "assets/catalog.h"
#include "util/fsutil.h"
#include "video/gfx.h"

#define FX_N 6

static void fx_w16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void fx_w32(uint8_t *p, unsigned long v)
{
    fx_w16(p, (unsigned)(v & 0xFFFF));
    fx_w16(p + 2, (unsigned)(v >> 16));
}

static int fx_write(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static size_t fx_build_lib(uint8_t *buf)
{
    static const uint8_t pixels[8] = {0, 1, 2, 9, 3, 0, 4, 9};
    memset(buf, 0, 1024);
    const size_t pal_off = 28, pix_off = pal_off + 4, oset = pix_off + 16;
    fx_w16(buf, FX_N);
    fx_w16(buf + 2, 4);
    fx_w32(buf + 4, oset);
    fx_w16(buf + 8, 0x634);
    fx_w16(buf + pal_off, 0);
    fx_w16(buf + pal_off + 2, 0x7FFF);
    memcpy(buf + pix_off, pixels, 8);
    for (int i = 0; i < FX_N; i++) {
        uint8_t *r = buf + oset + i * 50;
        char name[16];
        snprintf(name, sizeof name, "IM%d", i);
        memcpy(r, name, strlen(name));
        fx_w16(r + 22, 3);
        fx_w16(r + 24, 2);
        fx_w16(r + 26, 3);
        fx_w32(r + 28, pix_off);
        fx_w16(r + 46, 0xFFFF);
        fx_w16(r + 48, 0xFFFF);
    }
    uint8_t *p = buf + oset + FX_N * 50;
    memcpy(p, "P", 1);
    p[11] = 8;
    fx_w16(p + 12, 2);
    fx_w32(p + 14, pal_off);
    return oset + FX_N * 50 + 26;
}

/* Writes <tmp>/<sub>/T.IMG, T.LOD and the overrides in <tmp>/<sub>_hd (CMake makes the folders). Returns 1 on success. */
static int fx_make(const char *tmp, const char *sub, char *dir, size_t dir_n, char *ovr, size_t ovr_n)
{
    char path[1100], name[64];
    uint8_t buf[1024];
    snprintf(name, sizeof name, "%s", sub);
    if (!fs_join(dir, dir_n, tmp, name))
        return 0;
    snprintf(name, sizeof name, "%s_hd", sub);
    if (!fs_join(ovr, ovr_n, tmp, name))
        return 0;
    size_t len = fx_build_lib(buf);
    fs_join(path, sizeof path, dir, "T.IMG");
    if (!fx_write(path, buf, len))
        return 0;
    fs_join(path, sizeof path, dir, "T.LOD");
    if (!fx_write(path, "t.img\r\n", 7))
        return 0;
    uint8_t hi[6 * 4], rgba[6 * 4 * 4];
    uint32_t pal[2] = {0, 0xFFFFFFFFu};
    for (int i = 0; i < 24; i++) {
        hi[i] = (uint8_t)(i % 2);
        rgba[4 * i] = (uint8_t)(i * 9);
        rgba[4 * i + 1] = (uint8_t)(255 - i * 5);
        rgba[4 * i + 2] = (uint8_t)(i * 3);
        rgba[4 * i + 3] = (uint8_t)(i % 5 ? 255 : 0);
    }
    static const int indexed[3] = {0, 4, 5};
    for (int k = 0; k < 3; k++) {
        hi[0] = (uint8_t)k % 2;
        snprintf(name, sizeof name, "IM%d.png", indexed[k]);
        fs_join(path, sizeof path, ovr, name);
        if (!png_write_indexed(path, 6, 4, hi, 6, pal, 2))
            return 0;
    }
    fs_join(path, sizeof path, ovr, "IM1.png");
    if (!png_write_rgba(path, 6, 4, rgba))
        return 0;
    fs_join(path, sizeof path, ovr, "IM2.png");
    return png_write_indexed(path, 5, 4, hi, 6, pal, 2);
}

#endif
