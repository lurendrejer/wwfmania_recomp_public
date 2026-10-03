#include "png_read.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/fsutil.h"

/* ---- inflate (RFC 1951), structured after zlib's puff.c --------------------- */

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint8_t *out;
    size_t out_len, out_pos;
    uint32_t bitbuf;
    int bitcnt;
    int error;
} inflate_state;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huffman;

static int getbits(inflate_state *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) {
            s->error = 1;
            return 0;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int decode_sym(inflate_state *s, const huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= getbits(s, 1);
        if (s->error)
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Returns 0 for a complete code, >0 for incomplete, <0 for over-subscribed. */
static int construct(huffman *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (int sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0)
            h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(inflate_state *s, const huffman *lencode, const huffman *distcode)
{
    static const short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                    33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = decode_sym(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->out_pos >= s->out_len)
                return -1;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int len = lbase[sym] + getbits(s, lext[sym]);
            int dsym = decode_sym(s, distcode);
            if (dsym < 0 || dsym >= 30)
                return -1;
            size_t dist = (size_t)(dbase[dsym] + getbits(s, dext[dsym]));
            if (s->error || dist > s->out_pos || s->out_pos + (size_t)len > s->out_len)
                return -1;
            for (; len; len--, s->out_pos++)
                s->out[s->out_pos] = s->out[s->out_pos - dist];
        }
    }
}

static int stored(inflate_state *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->in_pos + 4 > s->in_len)
        return -1;
    unsigned len = s->in[s->in_pos] | (s->in[s->in_pos + 1] << 8);
    unsigned nlen = s->in[s->in_pos + 2] | (s->in[s->in_pos + 3] << 8);
    s->in_pos += 4;
    if (len != (~nlen & 0xFFFFu) || s->in_pos + len > s->in_len ||
        s->out_pos + len > s->out_len)
        return -1;
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int fixed(inflate_state *s)
{
    /* built on every call, not kept in statics: decoding runs on several threads (gfx_async) */
    huffman lencode, distcode;
    short lengths[FIXLCODES];
    int sym = 0;
    for (; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
    construct(&lencode, lengths, FIXLCODES);
    for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
    construct(&distcode, lengths, MAXDCODES);
    return codes(s, &lencode, &distcode);
}

static int dynamic(inflate_state *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[MAXLCODES + MAXDCODES];
    huffman lencode, distcode;
    int nlen = getbits(s, 5) + 257;
    int ndist = getbits(s, 5) + 1;
    int ncode = getbits(s, 4) + 4;
    if (s->error || nlen > MAXLCODES || ndist > MAXDCODES)
        return -1;
    int index = 0;
    for (; index < ncode; index++)
        lengths[order[index]] = (short)getbits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (s->error || construct(&lencode, lengths, 19) != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode_sym(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0)
                    return -1;
                len = lengths[index - 1];
                rep = 3 + getbits(s, 2);
            } else if (sym == 17) {
                rep = 3 + getbits(s, 3);
            } else {
                rep = 11 + getbits(s, 7);
            }
            if (s->error || index + rep > nlen + ndist)
                return -1;
            while (rep--)
                lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0)
        return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return -1;
    return codes(s, &lencode, &distcode);
}

static long inflate_from(const uint8_t *in, size_t in_len, size_t pos, uint8_t *out, size_t out_len)
{
    inflate_state s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.in_len = in_len;
    s.in_pos = pos;
    s.out = out;
    s.out_len = out_len;
    int last;
    do {
        last = getbits(&s, 1);
        int type = getbits(&s, 2);
        if (s.error)
            return -1;
        int err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err != 0 || s.error)
            return -1;
    } while (!last);
    return (long)s.out_pos;
}

long zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    if (in_len < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20))
        return -1;
    return inflate_from(in, in_len, 2, out, out_len);
}

long raw_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    return inflate_from(in, in_len, 0, out, out_len);
}

/* ---- PNG -------------------------------------------------------------------- */

static int fail(char *err, size_t err_len, const char *fmt, ...)
{
    if (err && err_len) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, err_len, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

int png_decode_indexed(const uint8_t *data, size_t size, png_indexed *out, char *err,
                       size_t err_len)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memset(out, 0, sizeof *out);
    if (size < 8 || memcmp(data, sig, 8) != 0)
        return fail(err, err_len, "not a PNG file");

    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, seen_ihdr = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0;
    size_t pos = 8;
    while (pos + 12 <= size) {
        uint32_t len = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;
        if (len > size - pos - 12) {
            free(idat);
            return fail(err, err_len, "truncated chunk");
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            w = be32(body);
            h = be32(body + 4);
            depth = body[8];
            ctype = body[9];
            if (body[10] != 0 || body[11] != 0) {
                free(idat);
                return fail(err, err_len, "unsupported compression/filter method");
            }
            if (body[12] != 0) {
                free(idat);
                return fail(err, err_len, "interlaced PNGs are not supported");
            }
            seen_ihdr = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *n = realloc(idat, idat_len + len + 1);
            if (!n) {
                free(idat);
                return fail(err, err_len, "out of memory");
            }
            idat = n;
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + (size_t)len;
    }
    if (!seen_ihdr || !idat) {
        free(idat);
        return fail(err, err_len, "missing IHDR or IDAT");
    }
    if (!(ctype == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) &&
        !((ctype == 0 || ctype == 2 || ctype == 6) && depth == 8)) {
        free(idat);
        return fail(err, err_len,
                    "color type %d, depth %d: overrides must be 8-bit indexed, grayscale, "
                    "RGB or RGBA PNGs",
                    ctype, depth);
    }
    const int bpp = ctype == 2 ? 3 : ctype == 6 ? 4 : 1; /* bytes per pixel, depth 8 */
    if (w == 0 || h == 0 || w > 16384 || h > 16384) {
        free(idat);
        return fail(err, err_len, "bad dimensions %ux%u", w, h);
    }

    size_t row_bytes = ((size_t)w * (size_t)depth * (size_t)bpp + 7) / 8;
    size_t raw_len = (row_bytes + 1) * h;
    uint8_t *raw = malloc(raw_len);
    if (!raw) {
        free(idat);
        return fail(err, err_len, "out of memory");
    }
    long got = zlib_inflate(idat, idat_len, raw, raw_len);
    free(idat);
    if (got != (long)raw_len) {
        free(raw);
        return fail(err, err_len, "corrupt image data");
    }

    /* Unfilter in place; the filter unit is one pixel (1 byte below 8 bits). */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (row_bytes + 1);
        uint8_t *cur = row + 1;
        const uint8_t *prev = y ? raw + (y - 1) * (row_bytes + 1) + 1 : NULL;
        int ft = row[0];
        for (size_t x = 0; x < row_bytes; x++) {
            int a = x >= (size_t)bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = prev && x >= (size_t)bpp ? prev[x - bpp] : 0;
            switch (ft) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default:
                free(raw);
                return fail(err, err_len, "bad filter type %d", ft);
            }
        }
    }

    if (bpp >= 3) {
        out->rgba = malloc((size_t)w * h * 4);
        if (!out->rgba) {
            free(raw);
            return fail(err, err_len, "out of memory");
        }
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *src = raw + y * (row_bytes + 1) + 1;
            uint8_t *dst = out->rgba + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; x++) {
                memcpy(dst + 4 * x, src + (size_t)bpp * x, 3);
                dst[4 * x + 3] = bpp == 4 ? src[4 * x + 3] : 255;
            }
        }
        free(raw);
        out->width = (int)w;
        out->height = (int)h;
        return 1;
    }

    out->indices = malloc((size_t)w * h);
    if (!out->indices) {
        free(raw);
        return fail(err, err_len, "out of memory");
    }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = raw + y * (row_bytes + 1) + 1;
        uint8_t *dst = out->indices + (size_t)y * w;
        if (depth == 8) {
            memcpy(dst, src, w);
        } else {
            int per_byte = 8 / depth, mask = (1 << depth) - 1;
            for (uint32_t x = 0; x < w; x++) {
                int shift = 8 - depth * (int)(x % per_byte + 1);
                dst[x] = (uint8_t)((src[x / per_byte] >> shift) & mask);
            }
        }
    }
    free(raw);
    out->width = (int)w;
    out->height = (int)h;
    return 1;
}

int png_load_indexed(const char *path, png_indexed *out, char *err, size_t err_len)
{
    size_t size;
    uint8_t *data = fs_read_file(path, &size);
    if (!data) {
        memset(out, 0, sizeof *out);
        return fail(err, err_len, "%s: cannot read file", path);
    }
    char inner[256];
    int ok = png_decode_indexed(data, size, out, inner, sizeof inner);
    free(data);
    if (!ok)
        return fail(err, err_len, "%s: %s", path, inner);
    return 1;
}

void png_indexed_free(png_indexed *img)
{
    free(img->indices);
    free(img->rgba);
    img->indices = NULL;
    img->rgba = NULL;
    img->width = img->height = 0;
}
