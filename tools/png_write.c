#include "png_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
    static int done;
    if (done)
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    done = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
           fwrite(tail, 1, 4, f) == 4;
}

/* Wraps raw scanlines in a zlib stream made of stored deflate blocks. */
static uint8_t *zlib_store(const uint8_t *raw, size_t len, size_t *out_len)
{
    size_t nblocks = len / 65535 + 1;
    size_t total = 2 + len + nblocks * 5 + 4;
    uint8_t *out = malloc(total), *p = out;
    if (!out)
        return NULL;
    *p++ = 0x78;
    *p++ = 0x01;
    size_t pos = 0;
    do {
        size_t n = len - pos > 65535 ? 65535 : len - pos;
        *p++ = pos + n == len ? 1 : 0;
        *p++ = (uint8_t)n;
        *p++ = (uint8_t)(n >> 8);
        *p++ = (uint8_t)~n;
        *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + pos, n);
        p += n;
        pos += n;
    } while (pos < len);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32(p, (b << 16) | a);
    p += 4;
    *out_len = (size_t)(p - out);
    return out;
}

static int write_png(const char *path, int w, int h, int color_type, const uint8_t *raw,
                     size_t raw_len, const uint8_t *plte, size_t plte_len, const uint8_t *trns,
                     size_t trns_len)
{
    crc_init();
    size_t z_len;
    uint8_t *z = zlib_store(raw, raw_len, &z_len);
    if (!z)
        return 0;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(z);
        return 0;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = (uint8_t)color_type;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    int ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13);
    if (ok && plte)
        ok = write_chunk(f, "PLTE", plte, plte_len);
    if (ok && trns)
        ok = write_chunk(f, "tRNS", trns, trns_len);
    ok = ok && write_chunk(f, "IDAT", z, z_len) && write_chunk(f, "IEND", NULL, 0);
    free(z);
    return fclose(f) == 0 && ok;
}

int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba)
{
    size_t row = (size_t)w * 4 + 1;
    uint8_t *raw = malloc(row * (size_t)h + 1);
    if (!raw)
        return 0;
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    int ok = write_png(path, w, h, 6, raw, row * (size_t)h, NULL, 0, NULL, 0);
    free(raw);
    return ok;
}

int png_write_indexed(const char *path, int w, int h, const uint8_t *rows, int stride,
                      const uint32_t *argb, int ncolors)
{
    /* Always emit 256 entries so out-of-range indices still decode. */
    uint8_t plte[256 * 3], trns[1] = {0};
    for (int i = 0; i < 256; i++) {
        uint32_t c = i < ncolors ? argb[i] : 0xFFFF00FFu;
        plte[i * 3] = (uint8_t)(c >> 16);
        plte[i * 3 + 1] = (uint8_t)(c >> 8);
        plte[i * 3 + 2] = (uint8_t)c;
    }
    size_t row = (size_t)w + 1;
    uint8_t *raw = malloc(row * (size_t)h + 1);
    if (!raw)
        return 0;
    for (int y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, rows + (size_t)y * stride, (size_t)w);
    }
    int ok = write_png(path, w, h, 3, raw, row * (size_t)h, plte, sizeof plte, trns, sizeof trns);
    free(raw);
    return ok;
}
