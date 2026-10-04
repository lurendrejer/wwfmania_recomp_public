/*
 * imgtool - inspect and export the original IMG art.
 *
 *   imgtool info    <FILE.IMG>
 *   imgtool export  <FILE.IMG> <outdir> [--indexed]
 *   imgtool catalog <imgdir> [override_dir]
 *   imgtool find    <imgdir> <NAME> [override_dir]
 *   imgtool dump    <imgdir> <outdir> [--indexed]
 *   imgtool render  <imgdir> <NAME> <out.png> [--scale N] [--override DIR]
 *                   [--fliph] [--flipv] [--flash]
 *
 * `dump` writes every image the LOD scripts select as <outdir>/<NAME>.png
 * plus manifest.json. That folder is the starting point for high-res
 * replacements: upscale the PNGs, keep the names, point the game's
 * override directory at the result.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

#include "assets/catalog.h"
#include "assets/layers.h"
#include "assets/img.h"
#include "png_write.h"
#include "preview.h"
#include "util/fsutil.h"
#include "video/gfx.h"
#include "video/video.h"

static void usage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  imgtool info    <FILE.IMG>\n"
            "  imgtool export  <FILE.IMG> <outdir> [--indexed]\n"
            "  imgtool catalog <imgdir> [override_dir]\n"
            "  imgtool layers  <imgdir> [list]\n"
            "  imgtool find    <imgdir> <NAME> [override_dir]\n"
            "  imgtool dump    <imgdir> <outdir> [--indexed]\n"
            "  imgtool render  <imgdir> <NAME> <out.png> [--scale N] [--override DIR]\n"
            "                  [--fliph] [--flipv] [--flash]\n");
}

static void print_warn(const char *msg, void *user)
{
    (void)user;
    fprintf(stderr, "warning: %s\n", msg);
}

static const char *pal_name(const img_lib *lib, const img_image *im)
{
    return im->palette == IMG_NONE ? "-" : lib->palettes[im->palette].name;
}

static int export_image(const img_lib *lib, const img_image *im, const char *dir, int indexed)
{
    if (!im->width || !im->height)
        return 1;
    char file[IMG_NAME_MAX + 8], path[1024];
    char upper[IMG_NAME_MAX + 1];
    str_upper_copy(upper, sizeof upper, im->name);
    snprintf(file, sizeof file, "%s.png", upper);
    if (!fs_join(path, sizeof path, dir, file))
        return 0;

    if (indexed) {
        uint32_t argb[256];
        int n = 0;
        if (im->palette != IMG_NONE) {
            const img_palette *p = &lib->palettes[im->palette];
            n = p->ncolors > 256 ? 256 : p->ncolors;
            for (int i = 0; i < n; i++)
                argb[i] = img_color_argb(p->colors[i]);
        }
        return png_write_indexed(path, im->width, im->height, im->pixels, im->stride, argb, n);
    }
    uint8_t *rgba = malloc((size_t)im->width * im->height * 4);
    if (!rgba)
        return 0;
    img_image_to_rgba(lib, im, rgba);
    int ok = png_write_rgba(path, im->width, im->height, rgba);
    free(rgba);
    return ok;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', f);
        if ((unsigned char)*s >= 0x20)
            fputc(*s, f);
    }
    fputc('"', f);
}

static void json_image(FILE *f, const char *lib_file, const img_lib *lib, const img_image *im)
{
    char upper[IMG_NAME_MAX + 1];
    str_upper_copy(upper, sizeof upper, im->name);
    fputs("  {\"name\": ", f);
    json_str(f, upper);
    fputs(", \"lib\": ", f);
    json_str(f, lib_file);
    fprintf(f, ", \"width\": %d, \"height\": %d, \"anix\": %d, \"aniy\": %d", im->width,
            im->height, im->anix, im->aniy);
    fputs(", \"palette\": ", f);
    json_str(f, pal_name(lib, im));
    fprintf(f, ", \"flags\": %d}", im->flags);
}

static int cmd_info(const char *path)
{
    img_lib lib;
    char err[1024];
    if (!img_lib_load(&lib, path, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    printf("%s: version 0x%04x, %d images, %d palettes, %d point tables\n", path, lib.version,
           lib.nimages, lib.npalettes, lib.npttbls);
    printf("\npalettes:\n");
    for (int i = 0; i < lib.npalettes; i++) {
        const img_palette *p = &lib.palettes[i];
        printf("  %-10s %3d colors, %d bpp, flags 0x%02x\n", p->name, p->ncolors, p->bitspix,
               p->flags);
    }
    printf("\nimages:\n  %-16s %5s %5s %5s %5s  %-10s %6s %5s\n", "name", "w", "h", "anix",
           "aniy", "palette", "flags", "pttbl");
    for (int i = 0; i < lib.nimages; i++) {
        const img_image *im = &lib.images[i];
        printf("  %-16s %5d %5d %5d %5d  %-10s 0x%04x ", im->name, im->width, im->height,
               im->anix, im->aniy, pal_name(&lib, im), im->flags);
        if (im->pttbl == IMG_NONE)
            printf("%5s\n", "-");
        else
            printf("%5d\n", im->pttbl);
    }
    img_lib_free(&lib);
    return 0;
}

static int cmd_export(const char *path, const char *outdir, int indexed)
{
    img_lib lib;
    char err[1024];
    if (!img_lib_load(&lib, path, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    make_dir(outdir);
    int failed = 0;
    for (int i = 0; i < lib.nimages; i++)
        if (!export_image(&lib, &lib.images[i], outdir, indexed)) {
            fprintf(stderr, "failed to write %s\n", lib.images[i].name);
            failed++;
        }
    printf("exported %d images to %s\n", lib.nimages - failed, outdir);
    img_lib_free(&lib);
    return failed != 0;
}

static int cmd_catalog(const char *imgdir, const char *override_dir)
{
    catalog cat;
    if (!catalog_open(&cat, imgdir, override_dir, NULL, print_warn, NULL)) {
        fprintf(stderr, "cannot open catalog in %s\n", imgdir);
        return 1;
    }
    int loaded = 0, overrides = 0;
    for (int i = 0; i < cat.nlibs; i++)
        loaded += cat.libs[i].loaded;
    for (int i = 0; i < cat.nimages; i++)
        overrides += cat.images[i].has_override;
    printf("LOD scripts:      %d\n", cat.nlods);
    printf("IMG libraries:    %d referenced, %d loaded\n", cat.nlibs, loaded);
    printf("images:           %d\n", cat.nimages);
    printf("missing images:   %d\n", cat.missing_images);
    printf("duplicate names:  %d\n", cat.duplicate_names);
    if (override_dir)
        printf("overrides found:  %d\n", overrides);
    catalog_close(&cat);
    return 0;
}

/* The art layers (src/assets/layers.h): how many catalog images each has, or with `list` every label and its layer folder
 * (tab separated; tools/art_pack.py reads that). */
static int cmd_layers(const char *imgdir, int list)
{
    catalog cat;
    long count[ART_LAYER_COUNT] = {0};
    if (!catalog_open(&cat, imgdir, NULL, NULL, NULL, NULL)) {
        fprintf(stderr, "cannot open catalog in %s\n", imgdir);
        return 1;
    }
    for (int i = 0; i < cat.nimages; i++) {
        art_layer l = art_layer_of_image(&cat, &cat.images[i]);
        count[l]++;
        if (list)
            printf("%s\t%s\n", cat.images[i].name, art_layer_folder(l));
    }
    if (!list) {
        for (int l = 0; l < ART_LAYER_COUNT; l++)
            printf("%-22s %-28s %ld%s\n", art_layer_folder((art_layer)l), art_layer_title((art_layer)l), count[l],
                   l == ART_BACKGROUNDS ? "  (not in the catalog: <BDD>_<n>.png)" : "");
    }
    catalog_close(&cat);
    return 0;
}

static int cmd_find(const char *imgdir, const char *name, const char *override_dir)
{
    catalog cat;
    if (!catalog_open(&cat, imgdir, override_dir, NULL, NULL, NULL)) {
        fprintf(stderr, "cannot open catalog in %s\n", imgdir);
        return 1;
    }
    const cat_image *ci = catalog_find(&cat, name);
    if (!ci) {
        printf("%s: not in catalog\n", name);
        catalog_close(&cat);
        return 1;
    }
    const img_lib *lib = catalog_lib(&cat, ci);
    const img_image *im = catalog_image(&cat, ci);
    const lod_entry *e = &cat.lods[ci->lod].entries[ci->entry];
    printf("%s\n  library:  %s (image %d)\n  lod:      %s line %d -> %s\n", ci->name,
           cat.libs[ci->lib].file, ci->index, fs_basename(cat.lods[ci->lod].path), e->line,
           e->asm_table[0] ? e->asm_table : "-");
    printf("  size:     %dx%d, anchor (%d,%d), palette %s, flags 0x%04x\n", im->width,
           im->height, im->anix, im->aniy, pal_name(lib, im), im->flags);
    if (im->ani2x != -1 || im->ani2y != -1 || im->ani2z != -1)
        printf("  ani2:     (%d,%d,%d)\n", im->ani2x, im->ani2y, im->ani2z);
    const uint8_t *pt = img_image_pttbl(lib, im);
    if (pt) {
        printf("  pttbl:   ");
        for (int i = 0; i < IMG_PTTBL_SIZE; i++)
            printf("%s%02x", i % 4 ? "" : " ", pt[i]);
        printf("\n");
    }
    char path[1024];
    if (catalog_override_path(&cat, ci, path, sizeof path))
        printf("  override: %s\n", path);
    catalog_close(&cat);
    return 0;
}

static int cmd_dump(const char *imgdir, const char *outdir, int indexed)
{
    catalog cat;
    if (!catalog_open(&cat, imgdir, NULL, NULL, print_warn, NULL)) {
        fprintf(stderr, "cannot open catalog in %s\n", imgdir);
        return 1;
    }
    make_dir(outdir);
    char mpath[1024];
    fs_join(mpath, sizeof mpath, outdir, "manifest.json");
    FILE *mf = fopen(mpath, "w");
    if (!mf) {
        fprintf(stderr, "cannot write %s\n", mpath);
        catalog_close(&cat);
        return 1;
    }
    fputs("[\n", mf);
    int failed = 0;
    for (int i = 0; i < cat.nimages; i++) {
        const cat_image *ci = &cat.images[i];
        const img_lib *lib = catalog_lib(&cat, ci);
        const img_image *im = catalog_image(&cat, ci);
        if (!export_image(lib, im, outdir, indexed)) {
            fprintf(stderr, "failed to write %s\n", ci->name);
            failed++;
        }
        json_image(mf, cat.libs[ci->lib].file, lib, im);
        fputs(i + 1 < cat.nimages ? ",\n" : "\n", mf);
    }
    fputs("]\n", mf);
    fclose(mf);
    printf("exported %d images to %s\n", cat.nimages - failed, outdir);
    catalog_close(&cat);
    return failed != 0;
}

/* Duplicate-label notices are listed by `imgtool catalog`; skip them here. */
static void quiet_warn(const char *msg, void *user)
{
    (void)user;
    if (strncmp(msg, "duplicate image ", 16) != 0)
        fprintf(stderr, "warning: %s\n", msg);
}

/* Renders one image through the DMA renderer, like imgview, into a PNG. */
static int cmd_render(int argc, char **argv)
{
    const char *imgdir = argv[2], *name = argv[3], *out = argv[4];
    const char *override_dir = NULL;
    int scale = 1;
    preview_opts o = {0, 0, 0, DMA_SCALE_1X};
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--override") == 0 && i + 1 < argc)
            override_dir = argv[++i];
        else if (strcmp(argv[i], "--fliph") == 0)
            o.fliph = 1;
        else if (strcmp(argv[i], "--flipv") == 0)
            o.flipv = 1;
        else if (strcmp(argv[i], "--flash") == 0)
            o.flash = 1;
        else {
            usage();
            return 2;
        }
    }

    catalog cat;
    if (!catalog_open(&cat, imgdir, override_dir, NULL, quiet_warn, NULL))
        return 1;
    const cat_image *ci = catalog_find(&cat, name);
    if (!ci) {
        fprintf(stderr, "%s: not in catalog\n", name);
        catalog_close(&cat);
        return 1;
    }
    gfx_cache gc;
    video v;
    if (!gfx_cache_init(&gc, &cat, quiet_warn, NULL) || !video_init(&v, scale)) {
        fprintf(stderr, "initialization failed (scale must be 1-16)\n");
        catalog_close(&cat);
        return 1;
    }
    const gfx_image *gi = gfx_get(&gc, ci);
    preview_draw(&v, catalog_lib(&cat, ci), gi, &o);

    uint32_t *argb = malloc((size_t)v.w * v.h * sizeof *argb);
    uint8_t *rgba = malloc((size_t)v.w * v.h * 4);
    int ok = argb && rgba;
    if (ok) {
        video_to_argb(&v, argb);
        for (size_t i = 0; i < (size_t)v.w * v.h; i++) {
            rgba[i * 4] = (uint8_t)(argb[i] >> 16);
            rgba[i * 4 + 1] = (uint8_t)(argb[i] >> 8);
            rgba[i * 4 + 2] = (uint8_t)argb[i];
            rgba[i * 4 + 3] = 0xFF;
        }
        ok = png_write_rgba(out, v.w, v.h, rgba);
    }
    if (ok)
        printf("%s: %dx%d, %s\n", out, v.w, v.h,
               gi->hi_state == 1 ? "override" : "original pixels");
    else
        fprintf(stderr, "cannot write %s\n", out);
    free(argb);
    free(rgba);
    video_free(&v);
    gfx_cache_free(&gc);
    catalog_close(&cat);
    return !ok;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];
    int indexed = argc > 4 && strcmp(argv[4], "--indexed") == 0;
    if (strcmp(cmd, "info") == 0)
        return cmd_info(argv[2]);
    if (strcmp(cmd, "export") == 0 && argc >= 4)
        return cmd_export(argv[2], argv[3], indexed);
    if (strcmp(cmd, "catalog") == 0)
        return cmd_catalog(argv[2], argc > 3 ? argv[3] : NULL);
    if (strcmp(cmd, "layers") == 0 && argc >= 3)
        return cmd_layers(argv[2], argc > 3 && strcmp(argv[3], "list") == 0);
    if (strcmp(cmd, "find") == 0 && argc >= 4)
        return cmd_find(argv[2], argv[3], argc > 4 ? argv[4] : NULL);
    if (strcmp(cmd, "dump") == 0 && argc >= 4)
        return cmd_dump(argv[2], argv[3], indexed);
    if (strcmp(cmd, "render") == 0 && argc >= 5)
        return cmd_render(argc, argv);
    usage();
    return 2;
}
