/*
 * Asset loader tests.
 *
 *   test_assets <tmpdir> [imgdir]
 *
 * The synthetic tests always run. When imgdir (orig/IMG) is given, every
 * original IMG library and LOD script is loaded and checked too; the
 * expected counts are pinned to the vendored copy in orig/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/assets/catalog.h"
#include "../src/assets/img.h"
#include "../src/assets/layers.h"
#include "../src/assets/lod.h"
#include "../src/util/fsutil.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

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

/* Builds a v0x634 library: one 3x2 image with one palette and a point table. */
static size_t build_synthetic(uint8_t *buf)
{
    memset(buf, 0, 512);
    const size_t pal_off = 28, pix_off = pal_off + 3 * 2, oset = pix_off + 4 * 2;
    w16(buf + 0, 1);      /* images */
    w16(buf + 2, 4);      /* palettes incl. 3 built-ins */
    w32(buf + 4, oset);
    w16(buf + 8, 0x634);
    w16(buf + 16, 0xABCD);
    w16(buf + pal_off + 0, 0x0000);
    w16(buf + pal_off + 2, 0x7C00); /* red */
    w16(buf + pal_off + 4, 0x001F); /* blue */
    const uint8_t px[8] = {0, 1, 2, 0xEE, 2, 1, 0, 0xEE}; /* stride 4 */
    memcpy(buf + pix_off, px, sizeof px);

    uint8_t *r = buf + oset;
    memcpy(r, "TESTIMG\0junkjunk", 16);
    w16(r + 16, 0x0004);
    w16(r + 18, (unsigned)-1 & 0xFFFF); /* anix -1 */
    w16(r + 20, 2);
    w16(r + 22, 3);
    w16(r + 24, 2);
    w16(r + 26, 3);                      /* first real palette */
    w32(r + 28, pix_off);
    w16(r + 38, 0xFFFF);
    w16(r + 40, 0xFFFF);
    w16(r + 42, 0xFFFF);
    w16(r + 44, 0);
    w16(r + 46, 0);                      /* point table 0 */
    w16(r + 48, 0xFFFF);

    uint8_t *p = r + 50;
    memcpy(p, "TESTPAL\0\0\0", 10);
    p[10] = 0;
    p[11] = 2;
    w16(p + 12, 3);
    w32(p + 14, pal_off);

    uint8_t *pt = p + 26;
    for (int i = 0; i < 40; i++)
        pt[i] = (uint8_t)i;
    return (size_t)(pt + 40 - buf);
}

static void test_synthetic(const char *tmpdir)
{
    uint8_t buf[512];
    size_t len = build_synthetic(buf);
    char path[1024], lodpath[1024];
    fs_join(path, sizeof path, tmpdir, "SYNTH.IMG");
    CHECK(write_file(path, buf, len));

    img_lib lib;
    char err[512];
    CHECK(img_lib_load(&lib, path, err, sizeof err));
    CHECK(lib.nimages == 1 && lib.npalettes == 1 && lib.npttbls == 1);
    const img_image *im = &lib.images[0];
    CHECK(strcmp(im->name, "TESTIMG") == 0);
    CHECK(im->width == 3 && im->height == 2 && im->stride == 4);
    CHECK(im->anix == -1 && im->aniy == 2);
    CHECK(im->palette == 0 && strcmp(lib.palettes[0].name, "TESTPAL") == 0);
    CHECK(im->pixels[4 + 0] == 2);
    CHECK(img_lib_find(&lib, "testimg") == 0);
    const uint8_t *pt = img_image_pttbl(&lib, im);
    CHECK(pt && pt[39] == 39);

    uint8_t rgba[3 * 2 * 4];
    img_image_to_rgba(&lib, im, rgba);
    CHECK(rgba[3] == 0);                                     /* index 0 transparent */
    CHECK(rgba[4] == 0xFF && rgba[5] == 0 && rgba[7] == 0xFF); /* red */
    CHECK(rgba[8 + 2] == 0xFF && rgba[8] == 0);             /* blue */
    img_lib_free(&lib);

    /* Truncated file must fail cleanly. */
    CHECK(write_file(path, buf, 40));
    CHECK(!img_lib_load(&lib, path, err, sizeof err));

    /* LOD parsing: CRLF, lowercase file names, toggles, selection lists. */
    static const char lod_text[] =
        "; comment\r\n"
        "IHDR SIZX:W,SIZY:W\r\n"
        "ASM> test.tbl\r\n"
        "ZON>\r\n"
        "synth.img\r\n"
        "COF>\r\n"
        "---> TESTIMG, NOPE\r\n"
        "BBB> SOMEBG\r\n"
        "other.img\r\n";
    fs_join(lodpath, sizeof lodpath, tmpdir, "TEST.LOD");
    CHECK(write_file(lodpath, lod_text, sizeof lod_text - 1));
    lod_script lod;
    CHECK(lod_load(&lod, lodpath, err, sizeof err));
    CHECK(lod.nentries == 3);
    if (lod.nentries == 3) {
        CHECK(lod.entries[0].kind == LOD_IMG && strcmp(lod.entries[0].file, "synth.img") == 0);
        CHECK(lod.entries[0].nnames == 2 && strcmp(lod.entries[0].names[1], "NOPE") == 0);
        CHECK(lod.entries[0].toggles == LOD_T_Z);
        CHECK(strcmp(lod.entries[0].asm_table, "test.tbl") == 0);
        CHECK(strcmp(lod.entries[0].ihdr, "SIZX:W,SIZY:W") == 0);
        CHECK(lod.entries[1].kind == LOD_BBB && strcmp(lod.entries[1].file, "SOMEBG") == 0);
        CHECK(lod.entries[2].nnames == 0);
    }
    lod_free(&lod);

    /* Catalog over the temp dir: rewrite the good library first. */
    CHECK(write_file(path, buf, len));
    char png[1024];
    fs_join(png, sizeof png, tmpdir, "testimg.png");
    CHECK(write_file(png, "x", 1));
    static const char *const test_lods[] = {"test.lod", NULL};
    catalog cat;
    CHECK(catalog_open(&cat, tmpdir, tmpdir, test_lods, NULL, NULL));
    CHECK(cat.nimages == 1);
    CHECK(cat.missing_images == 1); /* NOPE */
    CHECK(cat.missing_libs == 1);   /* other.img */
    const cat_image *ci = catalog_find(&cat, "TestImg");
    CHECK(ci != NULL);
    if (ci) {
        CHECK(ci->has_override);
        char out[1024];
        CHECK(catalog_override_path(&cat, ci, out, sizeof out));
        CHECK(catalog_image(&cat, ci)->width == 3);
    }
    CHECK(catalog_find(&cat, "NOPE") == NULL);
    catalog_close(&cat);
    remove(png);
    remove(lodpath);
    remove(path);

    /* A mod's LOD, given with its directory (gen/lod), finds a library next to it that the image
     * directory does not have (one the mod's generator made, tools/gsp/extras.py 'script'). The
     * CMake test setup makes <tmpdir>/modlod. */
    char moddir[1024], modimg[1024], modlod[1024];
    fs_join(moddir, sizeof moddir, tmpdir, "modlod");
    fs_join(modimg, sizeof modimg, moddir, "MODOWN.IMG");
    fs_join(modlod, sizeof modlod, moddir, "MODOWN.LOD");
    static const char mod_lod_text[] = "IHDR SIZX:W,SIZY:W\nmodown.img\n---> TESTIMG\n";
    if (write_file(modimg, buf, len) && write_file(modlod, mod_lod_text, sizeof mod_lod_text - 1)) {
        const char *const own_lods[] = {modlod, NULL};
        CHECK(catalog_open(&cat, tmpdir, NULL, own_lods, NULL, NULL));
        CHECK(cat.nimages == 1 && cat.missing_libs == 0 && catalog_find(&cat, "TESTIMG") != NULL);
        catalog_close(&cat);
        /* by name only (an original LOD) the library is looked for in the image directory alone */
        char plain[1024];
        fs_join(plain, sizeof plain, tmpdir, "MODOWN.LOD");
        CHECK(write_file(plain, mod_lod_text, sizeof mod_lod_text - 1));
        static const char *const plain_lods[] = {"modown.lod", NULL};
        CHECK(catalog_open(&cat, tmpdir, NULL, plain_lods, NULL, NULL));
        CHECK(cat.nimages == 0 && cat.missing_libs == 1);
        catalog_close(&cat);
        remove(plain);
    } else {
        fprintf(stderr, "note: %s missing, mod LOD test skipped\n", moddir);
    }
    remove(modimg);
    remove(modlod);
}

/* ---- original data --------------------------------------------------------- */

typedef struct {
    const char *dir;
    int files, loaded, failed, images, bad_index;
} orig_stats;

static void orig_cb(const char *name, void *user)
{
    orig_stats *s = user;
    if (!str_iendswith(name, ".IMG"))
        return;
    char path[1024], err[1024];
    fs_join(path, sizeof path, s->dir, name);
    s->files++;
    img_lib lib;
    if (!img_lib_load(&lib, path, err, sizeof err)) {
        s->failed++;
        return;
    }
    s->loaded++;
    s->images += lib.nimages;
    for (int i = 0; i < lib.nimages; i++) {
        const img_image *im = &lib.images[i];
        if (im->palette == IMG_NONE) {
            s->bad_index++;
            continue;
        }
        const img_palette *p = &lib.palettes[im->palette];
        uint8_t mask = img_palette_mask(p);
        for (int y = 0; y < im->height; y++)
            for (int x = 0; x < im->width; x++)
                if ((im->pixels[(size_t)y * im->stride + x] & mask) >= p->ncolors) {
                    s->bad_index++;
                    y = im->height;
                    break;
                }
    }
    img_lib_free(&lib);
}

static void test_original(const char *imgdir)
{
    orig_stats s = {imgdir, 0, 0, 0, 0, 0};
    CHECK(fs_list_dir(imgdir, orig_cb, &s) > 0);
    printf("original IMG: %d files, %d loaded, %d failed, %d images, %d images with "
           "out-of-palette pixels\n",
           s.files, s.loaded, s.failed, s.images, s.bad_index);
    CHECK(s.failed == 4);        /* the four zero-length files */
    CHECK(s.images == 17514);    /* 17406 current-format + 108 in TROGF15.IMG (v0) */

    catalog cat;
    CHECK(catalog_open(&cat, imgdir, NULL, NULL, NULL, NULL));
    printf("catalog: %d LODs, %d libs, %d images, %d missing images, %d missing libs, "
           "%d duplicates\n",
           cat.nlods, cat.nlibs, cat.nimages, cat.missing_images, cat.missing_libs,
           cat.duplicate_names);
    CHECK(cat.nlods == 10);
    CHECK(cat.missing_libs == 0);
    CHECK(cat.missing_images == 0);
    CHECK(cat.nimages == 8085);
    CHECK(cat.duplicate_names == 73); /* see docs/IMG_FORMAT.md, "Open questions" */

    const cat_image *ci = catalog_find(&cat, "D4BK3A06");
    CHECK(ci != NULL);
    if (ci) {
        CHECK(strcmp(cat.libs[ci->lib].file, "DNK_HIT.IMG") == 0);
        const img_image *im = catalog_image(&cat, ci);
        CHECK(im->palette != IMG_NONE);
        CHECK(strcmp(catalog_lib(&cat, ci)->palettes[im->palette].name, "DNKBLU_P") == 0);
    }
    CHECK(catalog_find(&cat, "A4AH4C01") == NULL); /* Adam Bomb: cut, ADAM.LOD not built */
    /* YOK_WLK.IMG has sequence/script chunks before its point tables: the
     * stance frame's collision box (bytes 36-39) must be Yokozuna's */
    ci = catalog_find(&cat, "Y2ST2Z02");
    CHECK(ci != NULL);
    if (ci) {
        const uint8_t *pt = img_image_pttbl(catalog_lib(&cat, ci), catalog_image(&cat, ci));
        CHECK(pt && pt[36] == 2 && pt[37] == 0 && pt[38] == 35 && pt[39] == 110);
    }
    ci = catalog_find(&cat, "FON151"); /* v0 library via MAIN.LOD */
    CHECK(ci != NULL && catalog_image(&cat, ci)->width == 6);

    /* the art layers (src/assets/layers.h): every image has one; counts pinned to the vendored commit */
    static const int layer_count[ART_LAYER_COUNT] = {642, 681, 632, 627, 684, 719, 669, 669, 91, 123, 137, 1018, 941, 433, 19, 0};
    int by_layer[ART_LAYER_COUNT] = {0};
    for (int i = 0; i < cat.nimages; i++) {
        art_layer l = art_layer_of_image(&cat, &cat.images[i]);
        CHECK(l >= 0 && l < ART_LAYER_COUNT);
        if (l >= 0 && l < ART_LAYER_COUNT)
            by_layer[l]++;
    }
    for (int l = 0; l < ART_LAYER_COUNT; l++) {
        if (by_layer[l] != layer_count[l])
            fprintf(stderr, "layer %s: %d images, expected %d\n", art_layer_folder((art_layer)l), by_layer[l], layer_count[l]);
        CHECK(by_layer[l] == layer_count[l]);
    }
    ci = catalog_find(&cat, "D4BK3A06");               /* DNK_HIT.IMG, from DOINK.LOD */
    CHECK(ci && art_layer_of_image(&cat, ci) == ART_DOINK);
    ci = catalog_find(&cat, "Y2ST2Z02");               /* YOK_WLK.IMG */
    CHECK(ci && art_layer_of_image(&cat, ci) == ART_YOKOZUNA);
    CHECK(art_layer_of_lib(&cat, "crowd.img", 0) == ART_CROWD);
    CHECK(art_layer_of_lib(&cat, "NEWRING.BDD", 1) == ART_BACKGROUNDS);
    CHECK(art_layer_of_lib(&cat, "SOMETHING.IMG", 0) == ART_OTHER);
    for (int a = 0; a < ART_LAYER_COUNT; a++) {
        CHECK(art_layer_folder((art_layer)a)[0] && art_layer_title((art_layer)a)[0]);
        for (int b = a + 1; b < ART_LAYER_COUNT; b++)
            CHECK(strcmp(art_layer_folder((art_layer)a), art_layer_folder((art_layer)b)) != 0);
    }
    catalog_close(&cat);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_assets <tmpdir> [imgdir]\n");
        return 2;
    }
    test_synthetic(argv[1]);
    if (argc > 2)
        test_original(argv[2]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all asset tests passed\n");
    return 0;
}
