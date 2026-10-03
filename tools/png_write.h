/* Minimal dependency-free PNG writer (uncompressed deflate blocks). */
#ifndef WWF_PNG_WRITE_H
#define WWF_PNG_WRITE_H

#include <stdint.h>

/* Writes RGBA8888 pixels (w*h*4 bytes). Returns 1 on success. */
int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba);

/*
 * Writes an 8-bit indexed PNG. `rows` has `stride` bytes per row, `argb`
 * holds `ncolors` palette entries (0xAARRGGBB). Index 0 is marked fully
 * transparent through a tRNS chunk.
 */
int png_write_indexed(const char *path, int w, int h, const uint8_t *rows, int stride,
                      const uint32_t *argb, int ncolors);

#endif
