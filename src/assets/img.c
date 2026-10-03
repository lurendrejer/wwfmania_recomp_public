#include "img.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/fsutil.h"

/* Library header (little-endian, as written by the DOS tools). */
#define HDR_SIZE 28
#define HDR_IMGCNT 0
#define HDR_PALCNT 2
#define HDR_OSET 4
#define HDR_VERSION 8
#define HDR_SEQCNT 10
#define HDR_SCRCNT 12

#define IMGREC_SIZE_V0 42
#define IMGREC_SIZE 50
#define PALREC_SIZE 26

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void copy_name(char *dst, const uint8_t *src, size_t max)
{
    size_t i = 0;
    for (; i < max && src[i]; i++)
        dst[i] = (char)src[i];
    dst[i] = 0;
}

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

int img_lib_load(img_lib *lib, const char *path, char *err, size_t err_len)
{
    memset(lib, 0, sizeof *lib);
    snprintf(lib->path, sizeof lib->path, "%s", path);

    lib->data = fs_read_file(path, &lib->size);
    if (!lib->data)
        return fail(err, err_len, "%s: cannot read file", path);
    const uint8_t *d = lib->data;
    size_t size = lib->size;

    if (size < HDR_SIZE) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: file too small (%zu bytes)", path, size);
    }

    int nimg = rd16(d + HDR_IMGCNT);
    int palcnt = rd16(d + HDR_PALCNT);
    size_t oset = rd32(d + HDR_OSET);
    lib->version = rd16(d + HDR_VERSION);
    size_t recsize = lib->version == 0 ? IMGREC_SIZE_V0 : IMGREC_SIZE;
    int npal = palcnt > IMG_BUILTIN_PALETTES ? palcnt - IMG_BUILTIN_PALETTES : 0;

    size_t palrec_off = oset + (size_t)nimg * recsize;
    size_t pttbl_off = palrec_off + (size_t)npal * PALREC_SIZE;
    if (oset > size || pttbl_off > size) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: record tables out of bounds", path);
    }

    lib->images = calloc(nimg ? (size_t)nimg : 1, sizeof *lib->images);
    lib->palettes = calloc(npal ? (size_t)npal : 1, sizeof *lib->palettes);
    if (!lib->images || !lib->palettes) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: out of memory", path);
    }

    for (int i = 0; i < npal; i++) {
        const uint8_t *r = d + palrec_off + (size_t)i * PALREC_SIZE;
        img_palette *p = &lib->palettes[i];
        copy_name(p->name, r, IMG_PAL_NAME_MAX);
        p->flags = r[10];
        p->bitspix = r[11];
        p->ncolors = rd16(r + 12);
        size_t coff = rd32(r + 14);
        if (coff + (size_t)p->ncolors * 2 > oset) {
            lib->npalettes = i;
            img_lib_free(lib);
            return fail(err, err_len, "%s: palette %d colors out of bounds", path, i);
        }
        p->colors = malloc((p->ncolors ? p->ncolors : 1) * sizeof *p->colors);
        if (!p->colors) {
            lib->npalettes = i;
            img_lib_free(lib);
            return fail(err, err_len, "%s: out of memory", path);
        }
        for (int c = 0; c < p->ncolors; c++)
            p->colors[c] = rd16(d + coff + (size_t)c * 2);
    }
    lib->npalettes = npal;

    int max_pttbl = -1;
    for (int i = 0; i < nimg; i++) {
        const uint8_t *r = d + oset + (size_t)i * recsize;
        img_image *im = &lib->images[i];
        copy_name(im->name, r, IMG_NAME_MAX);
        uint16_t palind;
        size_t pix_off;
        if (lib->version == 0) {
            im->flags = 0;
            im->anix = (int16_t)rd16(r + 16);
            im->aniy = (int16_t)rd16(r + 18);
            im->width = rd16(r + 20);
            im->height = rd16(r + 22);
            palind = rd16(r + 24);
            pix_off = rd32(r + 26);
            im->ani2x = im->ani2y = im->ani2z = -1;
            im->frame = IMG_NONE;
            im->pttbl = IMG_NONE;
            im->opals = IMG_NONE;
        } else {
            im->flags = rd16(r + 16);
            im->anix = (int16_t)rd16(r + 18);
            im->aniy = (int16_t)rd16(r + 20);
            im->width = rd16(r + 22);
            im->height = rd16(r + 24);
            palind = rd16(r + 26);
            pix_off = rd32(r + 28);
            /* r+32: runtime data pointer, r+36: lib index (both editor-only) */
            im->ani2x = (int16_t)rd16(r + 38);
            im->ani2y = (int16_t)rd16(r + 40);
            im->ani2z = (int16_t)rd16(r + 42);
            im->frame = rd16(r + 44);
            im->pttbl = rd16(r + 46);
            im->opals = rd16(r + 48);
        }
        im->stride = (uint16_t)((im->width + 3u) & ~3u);
        if (palind >= IMG_BUILTIN_PALETTES && palind - IMG_BUILTIN_PALETTES < npal)
            im->palette = (uint16_t)(palind - IMG_BUILTIN_PALETTES);
        else
            im->palette = IMG_NONE;
        if (pix_off + (size_t)im->stride * im->height > oset) {
            lib->nimages = i;
            img_lib_free(lib);
            return fail(err, err_len, "%s: image %d pixels out of bounds", path, i);
        }
        im->pixels = d + pix_off;
        if (im->pttbl != IMG_NONE && (int)im->pttbl > max_pttbl)
            max_pttbl = im->pttbl;
    }
    lib->nimages = nimg;

    /* Sequence and script chunks (editor data the game does not use: its
     * animation scripts live in the .ASM files) follow the palette records,
     * each a 16-byte name, a word, a count n at +18 and 98 + 18 * n bytes in
     * all. The point tables come after them and run to the end of the file. */
    int nchunks = rd16(d + HDR_SEQCNT) + rd16(d + HDR_SCRCNT);
    for (int i = 0; i < nchunks && lib->version != 0; i++) {
        if (pttbl_off + 20 > size) {
            img_lib_free(lib);
            return fail(err, err_len, "%s: sequence data out of bounds", path);
        }
        pttbl_off += 98 + 18 * (size_t)rd16(d + pttbl_off + 18);
    }
    lib->npttbls = max_pttbl + 1;
    if (pttbl_off + (size_t)lib->npttbls * IMG_PTTBL_SIZE > size) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: point tables out of bounds", path);
    }
    lib->pttbls = lib->npttbls ? d + pttbl_off : NULL;
    return 1;
}

void img_lib_free(img_lib *lib)
{
    if (lib->palettes)
        for (int i = 0; i < lib->npalettes; i++)
            free(lib->palettes[i].colors);
    free(lib->palettes);
    free(lib->images);
    free(lib->data);
    lib->palettes = NULL;
    lib->images = NULL;
    lib->data = NULL;
    lib->nimages = lib->npalettes = lib->npttbls = 0;
    lib->pttbls = NULL;
}

int img_lib_find(const img_lib *lib, const char *name)
{
    for (int i = 0; i < lib->nimages; i++)
        if (str_ieq(lib->images[i].name, name))
            return i;
    return -1;
}

const uint8_t *img_image_pttbl(const img_lib *lib, const img_image *img)
{
    if (img->pttbl == IMG_NONE || (int)img->pttbl >= lib->npttbls)
        return NULL;
    return lib->pttbls + (size_t)img->pttbl * IMG_PTTBL_SIZE;
}

uint32_t img_color_argb(uint16_t c)
{
    uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint8_t img_palette_mask(const img_palette *pal)
{
    if (!pal || pal->bitspix == 0 || pal->bitspix >= 8)
        return 0xFF;
    return (uint8_t)((1u << pal->bitspix) - 1);
}

void img_image_to_rgba(const img_lib *lib, const img_image *img, uint8_t *out)
{
    const img_palette *pal = img->palette != IMG_NONE ? &lib->palettes[img->palette] : NULL;
    uint8_t mask = img_palette_mask(pal);
    for (int y = 0; y < img->height; y++) {
        const uint8_t *src = img->pixels + (size_t)y * img->stride;
        uint8_t *dst = out + (size_t)y * img->width * 4;
        for (int x = 0; x < img->width; x++, dst += 4) {
            uint8_t idx = src[x] & mask;
            if (idx == 0) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
                continue;
            }
            uint32_t argb = pal && idx < pal->ncolors ? img_color_argb(pal->colors[idx]) : 0xFFFF00FFu;
            dst[0] = (uint8_t)(argb >> 16);
            dst[1] = (uint8_t)(argb >> 8);
            dst[2] = (uint8_t)argb;
            dst[3] = 0xFF;
        }
    }
}

/* ---- background data files ---------------------------------------------------- */

static int bdd_line(const uint8_t *d, size_t size, size_t *pos, char *out, size_t out_len)
{
    size_t n = 0;
    while (*pos < size && d[*pos] != '\n') {
        if (n + 1 < out_len)
            out[n++] = (char)d[*pos];
        (*pos)++;
    }
    if (*pos >= size)
        return 0;
    (*pos)++;
    out[n] = 0;
    return 1;
}

int img_lib_load_bdd(img_lib *lib, const char *path, char *err, size_t err_len)
{
    memset(lib, 0, sizeof *lib);
    snprintf(lib->path, sizeof lib->path, "%s", path);
    lib->data = fs_read_file(path, &lib->size);
    if (!lib->data)
        return fail(err, err_len, "%s: cannot read file", path);
    size_t pos = 0;
    char line[128];
    int count = 0;
    if (!bdd_line(lib->data, lib->size, &pos, line, sizeof line) || sscanf(line, "%d", &count) != 1 ||
        count <= 0 || count > 4096) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: bad header", path);
    }
    lib->images = calloc((size_t)count, sizeof *lib->images);
    if (!lib->images) {
        img_lib_free(lib);
        return fail(err, err_len, "%s: out of memory", path);
    }
    for (int i = 0; i < count; i++) {
        unsigned idx;
        int w, h;
        if (!bdd_line(lib->data, lib->size, &pos, line, sizeof line) ||
            sscanf(line, "%x %d %d", &idx, &w, &h) != 3 || w <= 0 || h <= 0 ||
            pos + (size_t)w * h > lib->size) {
            lib->nimages = i;
            img_lib_free(lib);
            return fail(err, err_len, "%s: bad image %d", path, i);
        }
        img_image *im = &lib->images[i];
        snprintf(im->name, sizeof im->name, "#%d", i);
        im->width = (uint16_t)w;
        im->height = (uint16_t)h;
        im->stride = (uint16_t)w;
        im->palette = IMG_NONE;
        im->ani2x = im->ani2y = im->ani2z = -1;
        im->frame = im->pttbl = im->opals = IMG_NONE;
        im->pixels = lib->data + pos;
        pos += (size_t)w * h;
    }
    lib->nimages = count;
    lib->version = 0xBDD;
    return 1;
}
