/* Art overrides from a directory and/or zip files (artsrc.h). */
#include "artsrc.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ART_NAME_MAX 96

typedef struct {
    char name[ART_NAME_MAX]; /* file name inside the zip, lower case, no folders */
    int zip;                 /* index into the zip list */
    uint16_t method;         /* 0 stored, 8 deflate */
    uint32_t csize, usize, offset;
} art_entry;

static struct {
    int valid;
    char src[1024];
    char (*zips)[1024];
    int nzips;
    art_entry *e;
    int n, cap;
} idx;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void lower_copy(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < dst_len; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

void art_forget(void)
{
    free(idx.zips);
    free(idx.e);
    memset(&idx, 0, sizeof idx);
}

static int is_zip_file(const char *path)
{
    return str_iendswith(path, ".zip") && fs_file_exists(path);
}

static void add_entry(int zip, const char *name, size_t name_len, uint16_t method, uint32_t csize, uint32_t usize,
                      uint32_t offset)
{
    const char *base = name;
    for (size_t i = 0; i < name_len; i++)
        if (name[i] == '/' || name[i] == '\\')
            base = name + i + 1;
    size_t bl = name_len - (size_t)(base - name);
    if (bl == 0 || bl >= ART_NAME_MAX)
        return;
    if (idx.n == idx.cap) {
        int nc = idx.cap ? idx.cap * 2 : 1024;
        art_entry *ne = realloc(idx.e, (size_t)nc * sizeof *ne);
        if (!ne)
            return;
        idx.e = ne;
        idx.cap = nc;
    }
    art_entry *e = &idx.e[idx.n++];
    char tmp[ART_NAME_MAX];
    memcpy(tmp, base, bl);
    tmp[bl] = 0;
    lower_copy(e->name, sizeof e->name, tmp);
    e->zip = zip;
    e->method = method;
    e->csize = csize;
    e->usize = usize;
    e->offset = offset;
}

/* Reads the central directory of one zip into the index. */
static void index_zip(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    uint8_t *tail = NULL, *cd = NULL;
    if (fseek(f, 0, SEEK_END) != 0)
        goto done;
    long size = ftell(f);
    if (size < 22 || size >= LONG_MAX)
        goto done;
    long tail_len = size < 65557 ? size : 65557;
    tail = malloc((size_t)tail_len);
    if (!tail || fseek(f, size - tail_len, SEEK_SET) != 0 || fread(tail, 1, (size_t)tail_len, f) != (size_t)tail_len)
        goto done;
    long eocd = -1;
    for (long i = tail_len - 22; i >= 0; i--)
        if (rd32(tail + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        goto done;
    uint16_t count = rd16(tail + eocd + 10);
    uint32_t cd_size = rd32(tail + eocd + 12), cd_off = rd32(tail + eocd + 16);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu || (long)cd_off + (long)cd_size > size)
        goto done; /* zip64 or damaged */
    cd = malloc(cd_size ? cd_size : 1);
    if (!cd || fseek(f, (long)cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size)
        goto done;
    if (idx.nzips % 16 == 0) {
        void *nz = realloc(idx.zips, (size_t)(idx.nzips + 16) * sizeof idx.zips[0]);
        if (!nz)
            goto done;
        idx.zips = nz;
    }
    int zip = idx.nzips++;
    snprintf(idx.zips[zip], sizeof idx.zips[0], "%s", path);
    size_t p = 0;
    for (unsigned i = 0; i < count && p + 46 <= cd_size; i++) {
        const uint8_t *h = cd + p;
        if (rd32(h) != 0x02014b50u)
            break;
        uint16_t flags = rd16(h + 8), method = rd16(h + 10);
        uint32_t csize = rd32(h + 20), usize = rd32(h + 24), off = rd32(h + 42);
        size_t nl = rd16(h + 28), xl = rd16(h + 30), cl = rd16(h + 32);
        if (p + 46 + nl > cd_size)
            break;
        const char *name = (const char *)h + 46;
        int dir = nl > 0 && (name[nl - 1] == '/' || name[nl - 1] == '\\');
        if (!dir && !(flags & 1) && (method == 0 || method == 8) && csize != 0xFFFFFFFFu && usize != 0xFFFFFFFFu &&
            off != 0xFFFFFFFFu)
            add_entry(zip, name, nl, method, csize, usize, off);
        p += 46 + nl + xl + cl;
    }
done:
    free(tail);
    free(cd);
    fclose(f);
}

static int entry_cmp(const void *a, const void *b)
{
    const art_entry *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    if (c)
        return c;
    return x->zip - y->zip; /* earlier zips first */
}

typedef struct {
    const char *dir;
    fs_dir_cb cb;
    void *user;
    int count;
} list_ctx;

static void dir_cb(const char *name, void *user)
{
    list_ctx *c = user;
    if (str_iendswith(name, ".zip")) {
        char path[1024];
        if (fs_join(path, sizeof path, c->dir, name) && fs_file_exists(path))
            index_zip(path);
        return;
    }
    if (c->cb) {
        c->cb(name, c->user);
        c->count++;
    }
}

/* (Re)builds the zip index for src; lists the loose files through cb when src is a directory. */
static int build(const char *src, fs_dir_cb cb, void *user, int *loose)
{
    art_forget();
    snprintf(idx.src, sizeof idx.src, "%s", src);
    idx.valid = 1;
    *loose = 0;
    if (is_zip_file(src)) {
        index_zip(src);
    } else {
        list_ctx c = {src, cb, user, 0};
        if (fs_list_dir(src, dir_cb, &c) < 0)
            return -1;
        *loose = c.count;
    }
    if (idx.n)
        qsort(idx.e, (size_t)idx.n, sizeof *idx.e, entry_cmp);
    return 0;
}

int art_list(const char *src, fs_dir_cb cb, void *user)
{
    int loose;
    if (!src || !src[0] || build(src, cb, user, &loose) < 0)
        return -1;
    for (int i = 0; i < idx.n; i++)
        if (i == 0 || strcmp(idx.e[i].name, idx.e[i - 1].name) != 0)
            cb(idx.e[i].name, user);
    return loose + idx.n;
}

static const art_entry *find_entry(const char *file)
{
    art_entry key;
    lower_copy(key.name, sizeof key.name, file);
    key.zip = -1;
    /* the first entry with this name: bsearch for any, then step back */
    int lo = 0, hi = idx.n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(idx.e[mid].name, key.name) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < idx.n && strcmp(idx.e[lo].name, key.name) == 0 ? &idx.e[lo] : NULL;
}

static int load_entry(const art_ref *e, png_indexed *out, char *err, size_t err_len)
{
    FILE *f = fopen(e->path, "rb");
    uint8_t lh[30], *comp = NULL, *raw = NULL;
    int ok = 0;
    if (!f) {
        snprintf(err, err_len, "%s: cannot open", e->path);
        return 0;
    }
    if (fseek(f, (long)e->offset, SEEK_SET) != 0 || fread(lh, 1, sizeof lh, f) != sizeof lh ||
        rd32(lh) != 0x04034b50u || fseek(f, (long)(rd16(lh + 26) + rd16(lh + 28)), SEEK_CUR) != 0) {
        snprintf(err, err_len, "%s: %s: bad local header", e->path, e->name);
        goto done;
    }
    comp = malloc(e->csize ? e->csize : 1);
    if (!comp || fread(comp, 1, e->csize, f) != e->csize) {
        snprintf(err, err_len, "%s: %s: cannot read", e->path, e->name);
        goto done;
    }
    if (e->method == 8) {
        raw = malloc(e->usize ? e->usize : 1);
        if (!raw || raw_inflate(comp, e->csize, raw, e->usize) != (long)e->usize) {
            snprintf(err, err_len, "%s: %s: bad deflate data", e->path, e->name);
            goto done;
        }
    }
    ok = png_decode_indexed(raw ? raw : comp, raw ? e->usize : e->csize, out, err, err_len);
done:
    free(comp);
    free(raw);
    fclose(f);
    return ok;
}

int art_locate(const char *src, const char *file, art_ref *ref, char *err, size_t err_len)
{
    memset(ref, 0, sizeof *ref);
    if (!src || !src[0]) {
        snprintf(err, err_len, "no override source");
        return 0;
    }
    if (!is_zip_file(src)) {
        if (fs_resolve_ci(ref->path, sizeof ref->path, src, file))
            return 1;
    }
    if (!idx.valid || strcmp(idx.src, src) != 0) {
        int loose;
        if (build(src, NULL, NULL, &loose) < 0) {
            snprintf(err, err_len, "%s: cannot read", src);
            return 0;
        }
    }
    const art_entry *e = find_entry(file);
    if (!e) {
        snprintf(err, err_len, "%s: %s not found", src, file);
        return 0;
    }
    snprintf(ref->path, sizeof ref->path, "%s", idx.zips[e->zip]);
    snprintf(ref->name, sizeof ref->name, "%s", e->name);
    ref->zipped = 1;
    ref->method = e->method;
    ref->csize = e->csize;
    ref->usize = e->usize;
    ref->offset = e->offset;
    return 1;
}

int art_read(const art_ref *ref, png_indexed *out, char *err, size_t err_len)
{
    return ref->zipped ? load_entry(ref, out, err, err_len) : png_load_indexed(ref->path, out, err, err_len);
}

int art_load_png(const char *src, const char *file, png_indexed *out, char *err, size_t err_len)
{
    art_ref ref;
    return art_locate(src, file, &ref, err, err_len) && art_read(&ref, out, err, err_len);
}
