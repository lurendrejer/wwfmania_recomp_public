/*
 * Minimal PNG reader for indexed art overrides. Dependency-free: carries its
 * own inflate.
 *
 * Accepted:
 *   - color type 3 (indexed), bit depth 1/2/4/8, and color type 0 (8-bit
 *     grayscale): the values are original palette indices -> `indices`
 *   - color type 2/6 (8-bit RGB/RGBA): true-color art -> `rgba`, which the
 *     renderer maps back to palette indices plus a color detail layer
 * Interlaced images are rejected.
 */
#ifndef WWF_PNG_READ_H
#define WWF_PNG_READ_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width, height;
    uint8_t *indices; /* width*height bytes, one palette index per pixel; NULL for RGB(A) */
    uint8_t *rgba;    /* width*height*4 bytes for color type 2/6 PNGs, else NULL */
} png_indexed;

/* Decodes a PNG held in memory. Returns 0 and fills err on failure. */
int png_decode_indexed(const uint8_t *data, size_t size, png_indexed *out, char *err,
                       size_t err_len);

/* Reads and decodes a file. */
int png_load_indexed(const char *path, png_indexed *out, char *err, size_t err_len);

void png_indexed_free(png_indexed *img);

/*
 * Raw zlib stream decoder (exposed for tests). Returns the number of bytes
 * written to out, or -1 on malformed data or if out_len is too small.
 */
long zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);

/* The same for a raw deflate stream without the zlib header (ZIP entries). */
long raw_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);

#endif
