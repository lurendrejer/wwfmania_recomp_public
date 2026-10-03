#include "romset.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/png_read.h"
#include "dcs.h"
#include "util/fsutil.h"

/* MAME's wwfmania set: wwf_music-spch_l1.u2 .. u5 */
static const uint32_t known_crc[DCS_NROMS] = {0xa9acb250, 0x9442b6c9, 0xcee78fac, 0x5b31fd40};

static int fail(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
    return 0;
}

uint32_t dcs_crc32(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* Which ROM a file name is: .u2 -> 0 ... .u5 -> 3, else -1. */
static int rom_index(const char *name)
{
    static const char *const ext[DCS_NROMS] = {".u2", ".u3", ".u4", ".u5"};
    for (int i = 0; i < DCS_NROMS; i++)
        if (str_iendswith(name, ext[i]))
            return i;
    return -1;
}

static void take(dcs_romset *rs, int i, uint8_t *data)
{
    free(rs->data[i]);
    rs->data[i] = data;
}

/* ---- directory ------------------------------------------------------------------ */

typedef struct {
    dcs_romset *rs;
    const char *dir;
} dir_ctx;

static void dir_cb(const char *name, void *user)
{
    dir_ctx *c = user;
    int i = rom_index(name);
    if (i < 0)
        return;
    char path[1024];
    size_t size;
    fs_join(path, sizeof path, c->dir, name);
    uint8_t *data = fs_read_file(path, &size);
    if (data && size == DCS_ROM_BYTES)
        take(c->rs, i, data);
    else
        free(data);
}

/* ---- zip -------------------------------------------------------------------------- */

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static int load_zip(dcs_romset *rs, const uint8_t *z, size_t n, char *err, size_t err_len)
{
    /* end of central directory: last 22+ bytes, signature PK\5\6 */
    size_t eocd = n;
    for (size_t p = n >= 22 ? n - 22 : 0; p + 22 <= n; p--) {
        if (le32(z + p) == 0x06054b50u) {
            eocd = p;
            break;
        }
        if (p == 0 || n - p > 22 + 65535)
            break;
    }
    if (eocd == n)
        return fail(err, err_len, "not a zip file");
    uint32_t count = le16(z + eocd + 10), cd = le32(z + eocd + 16);
    for (uint32_t k = 0; k < count; k++) {
        if ((size_t)cd + 46 > n || le32(z + cd) != 0x02014b50u)
            return fail(err, err_len, "bad zip central directory");
        uint32_t method = le16(z + cd + 10), csize = le32(z + cd + 20), usize = le32(z + cd + 24);
        uint32_t nlen = le16(z + cd + 28), xlen = le16(z + cd + 30), clen = le16(z + cd + 32);
        uint32_t lho = le32(z + cd + 42);
        char name[256];
        size_t l = nlen < sizeof name - 1 ? nlen : sizeof name - 1;
        if ((size_t)cd + 46 + nlen > n)
            return fail(err, err_len, "bad zip central directory");
        memcpy(name, z + cd + 46, l);
        name[l] = 0;
        cd += 46 + nlen + xlen + clen;
        int i = rom_index(name);
        if (i < 0 || usize != DCS_ROM_BYTES)
            continue;
        if ((size_t)lho + 30 > n || le32(z + lho) != 0x04034b50u)
            return fail(err, err_len, "bad zip entry %s", name);
        size_t data = lho + 30 + le16(z + lho + 26) + le16(z + lho + 28);
        if (data + csize > n)
            return fail(err, err_len, "truncated zip entry %s", name);
        uint8_t *out = malloc(DCS_ROM_BYTES);
        if (!out)
            return fail(err, err_len, "out of memory");
        long got = -1;
        if (method == 0 && csize == DCS_ROM_BYTES) {
            memcpy(out, z + data, DCS_ROM_BYTES);
            got = DCS_ROM_BYTES;
        } else if (method == 8) {
            got = raw_inflate(z + data, csize, out, DCS_ROM_BYTES);
        }
        if (got != DCS_ROM_BYTES) {
            free(out);
            return fail(err, err_len, "cannot unpack %s (method %u)", name, (unsigned)method);
        }
        take(rs, i, out);
    }
    return 1;
}

int dcs_romset_load(dcs_romset *rs, const char *path, char *err, size_t err_len)
{
    memset(rs, 0, sizeof *rs);
    if (str_iendswith(path, ".zip")) {
        size_t n;
        uint8_t *z = fs_read_file(path, &n);
        if (!z)
            return fail(err, err_len, "cannot read %s", path);
        int ok = load_zip(rs, z, n, err, err_len);
        free(z);
        if (!ok) {
            dcs_romset_free(rs);
            return 0;
        }
    } else {
        dir_ctx c = {rs, path};
        if (fs_list_dir(path, dir_cb, &c) < 0)
            return fail(err, err_len, "cannot list %s", path);
    }
    rs->known = 1;
    for (int i = 0; i < DCS_NROMS; i++) {
        if (!rs->data[i]) {
            dcs_romset_free(rs);
            return fail(err, err_len, "%s: no 1 MB file ending in .u%d", path, i + 2);
        }
        rs->crc[i] = dcs_crc32(rs->data[i], DCS_ROM_BYTES);
        if (rs->crc[i] != known_crc[i])
            rs->known = 0;
    }
    return 1;
}

void dcs_romset_free(dcs_romset *rs)
{
    for (int i = 0; i < DCS_NROMS; i++) {
        free(rs->data[i]);
        rs->data[i] = NULL;
    }
}
