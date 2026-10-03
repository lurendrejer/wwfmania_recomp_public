#include "catalog.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/fsutil.h"
#include "artsrc.h"

static void warnf(catalog *cat, const char *fmt, ...)
{
    if (!cat->warn)
        return;
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    cat->warn(msg, cat->warn_user);
}

static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 32);
        h = (h ^ c) * 16777619u;
    }
    return h;
}

const char *const catalog_default_lods[] = {
    "MAIN.LOD", "BAM.LOD", "BRET.LOD", "DOINK.LOD", "LEX.LOD",
    "RAZOR.LOD", "SHAWN.LOD", "TAKER.LOD", "YOKO.LOD", "MISC.LOD", NULL,
};

/* ---- libraries ---------------------------------------------------------- */

/* `own_dir`: for a LOD a mod carries, its directory, searched when the library is not in the image
 * directory (an IMG library the mod's generator made, tools/gsp/extras.py 'script'). */
static int get_lib(catalog *cat, const char *file, const char *own_dir)
{
    char upper[128];
    str_upper_copy(upper, sizeof upper, fs_basename(file));
    for (int i = 0; i < cat->nlibs; i++)
        if (strcmp(cat->libs[i].file, upper) == 0)
            return i;

    cat_lib *nl = realloc(cat->libs, (size_t)(cat->nlibs + 1) * sizeof *nl);
    if (!nl)
        return -1;
    cat->libs = nl;
    cat_lib *l = &cat->libs[cat->nlibs];
    memset(l, 0, sizeof *l);
    snprintf(l->file, sizeof l->file, "%s", upper);

    char path[1024], err[1024];
    if (!fs_resolve_ci(path, sizeof path, cat->img_dir, file) &&
        (!own_dir || !fs_resolve_ci(path, sizeof path, own_dir, file))) {
        warnf(cat, "missing IMG library %s", upper);
        cat->missing_libs++;
    } else if (!img_lib_load(&l->lib, path, err, sizeof err)) {
        warnf(cat, "%s", err);
        cat->missing_libs++;
    } else {
        l->loaded = 1;
    }
    return cat->nlibs++;
}

/* ---- name index ---------------------------------------------------------- */

static int index_insert(catalog *cat, int image)
{
    uint32_t mask = (uint32_t)cat->nslots - 1;
    uint32_t h = hash_name(cat->images[image].name) & mask;
    while (cat->slots[h] >= 0) {
        if (str_ieq(cat->images[cat->slots[h]].name, cat->images[image].name))
            return 0;
        h = (h + 1) & mask;
    }
    cat->slots[h] = image;
    return 1;
}

static int add_image(catalog *cat, int *cap, int lib, int index, int lod, int entry)
{
    if (cat->nimages == *cap) {
        int ncap = *cap ? *cap * 2 : 1024;
        cat_image *ni = realloc(cat->images, (size_t)ncap * sizeof *ni);
        if (!ni)
            return 0;
        cat->images = ni;
        *cap = ncap;
    }
    cat_image *ci = &cat->images[cat->nimages];
    memset(ci, 0, sizeof *ci);
    str_upper_copy(ci->name, sizeof ci->name, cat->libs[lib].lib.images[index].name);
    ci->lib = lib;
    ci->index = index;
    ci->lod = lod;
    ci->entry = entry;
    cat->nimages++;
    return 1;
}

static int build_index(catalog *cat)
{
    size_t n = 1;
    while (n < (size_t)cat->nimages * 2 + 16)
        n <<= 1;
    free(cat->slots);
    cat->slots = malloc(n * sizeof *cat->slots);
    if (!cat->slots)
        return 0;
    cat->nslots = n;
    for (size_t i = 0; i < n; i++)
        cat->slots[i] = -1;

    /* Drop duplicates so every name maps to its first selection. */
    int out = 0;
    for (int i = 0; i < cat->nimages; i++) {
        cat->images[out] = cat->images[i];
        if (index_insert(cat, out)) {
            out++;
        } else {
            const cat_image *d = &cat->images[i];
            warnf(cat, "duplicate image %s (%s in %s), keeping first", d->name,
                  cat->libs[d->lib].file, fs_basename(cat->lods[d->lod].path));
            cat->duplicate_names++;
        }
    }
    cat->nimages = out;
    return 1;
}

/* ---- public API --------------------------------------------------------- */

int catalog_open(catalog *cat, const char *img_dir, const char *override_dir,
                 const char *const *lods, catalog_warn_fn warn, void *warn_user)
{
    memset(cat, 0, sizeof *cat);
    snprintf(cat->img_dir, sizeof cat->img_dir, "%s", img_dir);
    if (override_dir)
        snprintf(cat->override_dir, sizeof cat->override_dir, "%s", override_dir);
    cat->warn = warn;
    cat->warn_user = warn_user;

    if (!lods)
        lods = catalog_default_lods;
    int nlods = 0;
    while (lods[nlods])
        nlods++;
    cat->lods = calloc(nlods ? (size_t)nlods : 1, sizeof *cat->lods);
    if (!cat->lods)
        goto fail;

    int cap = 0;
    for (int f = 0; f < nlods; f++) {
        char path[1024], err[1024];
        lod_script *lod = &cat->lods[cat->nlods];
        /* a name with a directory in it is a path of its own (a mod's LOD) */
        int has_dir = strchr(lods[f], '/') || strchr(lods[f], '\\');
        if (has_dir)
            snprintf(path, sizeof path, "%s", lods[f]);
        if (!has_dir && !fs_resolve_ci(path, sizeof path, img_dir, lods[f])) {
            warnf(cat, "missing LOD script %s", lods[f]);
            continue;
        }
        if (!lod_load(lod, path, err, sizeof err)) {
            warnf(cat, "%s", err);
            continue;
        }
        int lod_index = cat->nlods++;
        char own_dir[1024] = "";
        if (has_dir) {
            snprintf(own_dir, sizeof own_dir, "%s", path);
            own_dir[fs_basename(own_dir) - own_dir] = 0;
        }

        for (int e = 0; e < lod->nentries; e++) {
            const lod_entry *en = &lod->entries[e];
            if (en->kind != LOD_IMG)
                continue; /* backgrounds (BBB) are handled separately later */
            int li = get_lib(cat, en->file, has_dir ? own_dir : NULL);
            if (li < 0)
                goto fail;
            if (!cat->libs[li].loaded)
                continue;
            const img_lib *lib = &cat->libs[li].lib;
            if (en->nnames == 0) {
                for (int i = 0; i < lib->nimages; i++)
                    if (!add_image(cat, &cap, li, i, lod_index, e))
                        goto fail;
            } else {
                for (int n = 0; n < en->nnames; n++) {
                    int i = img_lib_find(lib, en->names[n]);
                    if (i < 0) {
                        warnf(cat, "%s:%d: image %s not found in %s", fs_basename(lod->path),
                              en->line, en->names[n], cat->libs[li].file);
                        cat->missing_images++;
                        continue;
                    }
                    if (!add_image(cat, &cap, li, i, lod_index, e))
                        goto fail;
                }
            }
        }
    }
    if (!build_index(cat))
        goto fail;
    catalog_rescan_overrides(cat);
    return 1;

fail:
    warnf(cat, "out of memory");
    catalog_close(cat);
    return 0;
}

void catalog_close(catalog *cat)
{
    for (int i = 0; i < cat->nlods; i++)
        lod_free(&cat->lods[i]);
    free(cat->lods);
    for (int i = 0; i < cat->nlibs; i++)
        if (cat->libs[i].loaded)
            img_lib_free(&cat->libs[i].lib);
    free(cat->libs);
    free(cat->images);
    free(cat->slots);
    memset(cat, 0, sizeof *cat);
}

const cat_image *catalog_find(const catalog *cat, const char *name)
{
    if (!cat->nslots)
        return NULL;
    uint32_t mask = (uint32_t)cat->nslots - 1;
    uint32_t h = hash_name(name) & mask;
    while (cat->slots[h] >= 0) {
        const cat_image *ci = &cat->images[cat->slots[h]];
        if (str_ieq(ci->name, name))
            return ci;
        h = (h + 1) & mask;
    }
    return NULL;
}

const img_lib *catalog_lib(const catalog *cat, const cat_image *ci)
{
    return &cat->libs[ci->lib].lib;
}

const img_image *catalog_image(const catalog *cat, const cat_image *ci)
{
    return &cat->libs[ci->lib].lib.images[ci->index];
}

int catalog_override_path(const catalog *cat, const cat_image *ci, char *out, size_t out_len)
{
    if (!ci->has_override || !cat->override_dir[0])
        return 0;
    char file[IMG_NAME_MAX + 8];
    snprintf(file, sizeof file, "%s.png", ci->name);
    return fs_resolve_ci(out, out_len, cat->override_dir, file);
}

static void override_cb(const char *name, void *user)
{
    catalog *cat = user;
    if (!str_iendswith(name, ".png"))
        return;
    char label[IMG_NAME_MAX + 1];
    size_t len = strlen(name) - 4;
    if (len > IMG_NAME_MAX)
        return;
    memcpy(label, name, len);
    label[len] = 0;
    cat_image *ci = (cat_image *)catalog_find(cat, label);
    if (ci)
        ci->has_override = 1;
}

void catalog_rescan_overrides(catalog *cat)
{
    for (int i = 0; i < cat->nimages; i++)
        cat->images[i].has_override = 0;
    if (cat->override_dir[0])
        art_list(cat->override_dir, override_cb, cat);
}
