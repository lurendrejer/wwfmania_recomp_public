/*
 * Image catalog: the port's replacement for the image tables the original
 * ROM builder generated (adamimg.tbl, crowdimg.tbl, ...).
 *
 * It reads every .LOD script in the IMG directory, loads the IMG libraries
 * they reference and indexes each selected image by its label name (the
 * name the assembly code refers to, e.g. A4AH4C01). Game code looks images
 * up by that name instead of by ROM address, which is what makes the art
 * swappable: if <override_dir>/<NAME>.png exists, the renderer can use it
 * in place of the indexed IMG pixels (see docs/ASSET_OVERRIDES.md).
 */
#ifndef WWF_CATALOG_H
#define WWF_CATALOG_H

#include <stddef.h>

#include "img.h"
#include "lod.h"

typedef struct {
    char name[IMG_NAME_MAX + 1]; /* uppercase label */
    int lib;                     /* index into catalog.libs */
    int index;                   /* image index inside that library */
    int lod;                     /* LOD script that selected it */
    int entry;                   /* entry inside that script */
    int has_override;            /* <override_dir>/<name>.png exists */
} cat_image;

typedef struct {
    char file[128];              /* uppercase IMG basename */
    img_lib lib;
    int loaded;                  /* 0 if the file was missing or broken */
} cat_lib;

typedef void (*catalog_warn_fn)(const char *msg, void *user);

typedef struct {
    char img_dir[512];
    char override_dir[512];

    lod_script *lods;
    int nlods;

    cat_lib *libs;
    int nlibs;

    cat_image *images;
    int nimages;

    int *slots;                  /* hash of images by name, -1 = empty */
    size_t nslots;

    /* Diagnostics gathered while opening. */
    int missing_libs;            /* referenced IMG files that failed to load */
    int missing_images;          /* LOD names not present in their library */
    int duplicate_names;         /* names selected more than once (first wins) */

    catalog_warn_fn warn;
    void *warn_user;
} catalog;

/*
 * The LOD scripts the shipped game was built from, in build order (see
 * orig/LD.BAT). ADAM, REF, HART, FONTS, FRAME and TEMP are leftovers that
 * the final build did not load. NULL-terminated.
 */
extern const char *const catalog_default_lods[];

/*
 * Loads the given LOD scripts from img_dir (NULL-terminated list, or NULL
 * for catalog_default_lods) and everything they reference. override_dir
 * may be NULL. warn, if set, receives one message per problem found.
 * Missing files are only warnings; returns 0 (with cat closed) on OOM.
 */
int catalog_open(catalog *cat, const char *img_dir, const char *override_dir,
                 const char *const *lods, catalog_warn_fn warn, void *warn_user);
void catalog_close(catalog *cat);

/* Finds an image by label, case-insensitive. NULL if unknown. */
const cat_image *catalog_find(const catalog *cat, const char *name);

const img_lib *catalog_lib(const catalog *cat, const cat_image *ci);
const img_image *catalog_image(const catalog *cat, const cat_image *ci);

/* Writes the override PNG path for an image; returns 0 if it has none. */
int catalog_override_path(const catalog *cat, const cat_image *ci, char *out, size_t out_len);

/* Rescans override_dir (e.g. after assets were added while running). */
void catalog_rescan_overrides(catalog *cat);

#endif
