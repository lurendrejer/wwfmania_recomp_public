/*
 * data_extract: copies a bundle described by filelist.txt to a directory, once per stamp.
 *
 *   test_dataextract <tmpdir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/dataextract.h"
#include "util/fsutil.h"

static int failures;
#define CHECK(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while (0)

/* the "bundle" is a table in memory */
static const struct { const char *name, *data; } bundle[] = {
    {"filelist.txt", "IMG/A.IMG\ngen/symbols.txt\nsounds/deep/dir/x.wav\n"},
    {"data.stamp", "v1"},
    {"IMG/A.IMG", "image-a"},
    {"gen/symbols.txt", "0 X\n"},
    {"sounds/deep/dir/x.wav", "wav"},
};
static const char *stamp_text = "v1";
static int reads;

static uint8_t *rd(void *user, const char *rel, size_t *n)
{
    (void)user;
    reads++;
    for (size_t i = 0; i < sizeof bundle / sizeof bundle[0]; i++)
        if (!strcmp(bundle[i].name, rel)) {
            const char *d = !strcmp(rel, "data.stamp") ? stamp_text : bundle[i].data;
            size_t len = strlen(d);
            uint8_t *p = malloc(len ? len : 1);
            memcpy(p, d, len);
            *n = len;
            return p;
        }
    return NULL;
}

static uint8_t *rd_empty(void *user, const char *rel, size_t *n)
{
    (void)user; (void)rel; (void)n;
    return NULL;
}

int main(int argc, char **argv)
{
    char dest[512], p[1024], err[256] = "";
    int copied = -1;
    size_t n = 0;
    data_source s = {NULL, rd, NULL};
    data_source none = {NULL, rd_empty, NULL};

    if (argc < 2) {
        fprintf(stderr, "usage: test_dataextract <tmpdir>\n");
        return 2;
    }
    snprintf(dest, sizeof dest, "%s/extract_out", argv[1]);
    CHECK(data_extract(&none, dest, &copied, err, sizeof err) == 0 && copied == 0);   /* nothing bundled */
    CHECK(data_extract(&s, dest, &copied, err, sizeof err) == 2 && copied == 3);
    fs_join(p, sizeof p, dest, "sounds/deep/dir/x.wav");
    uint8_t *f = fs_read_file(p, &n);
    CHECK(f && n == 3 && !memcmp(f, "wav", 3));
    free(f);
    fs_join(p, sizeof p, dest, "IMG/A.IMG");
    f = fs_read_file(p, &n);
    CHECK(f && n == 7 && !memcmp(f, "image-a", 7));
    free(f);

    reads = 0;
    CHECK(data_extract(&s, dest, &copied, err, sizeof err) == 1 && copied == 0);      /* same stamp: not again */
    CHECK(reads == 2);                                                                /* only filelist and stamp read */

    stamp_text = "v2";                                                                /* a new version: copied again */
    CHECK(data_extract(&s, dest, &copied, err, sizeof err) == 2 && copied == 3);
    fs_join(p, sizeof p, dest, "data.stamp");
    f = fs_read_file(p, &n);
    CHECK(f && n == 2 && !memcmp(f, "v2", 2));
    free(f);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("dataextract: ok\n");
    return 0;
}
