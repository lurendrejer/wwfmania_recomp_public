#include "layers.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char *folder;
    const char *title;
    const char *lod;       /* wrestlers: the LOD script of the wrestler, else NULL */
    const char *libs;      /* other layers: the libraries (lowercase, without .img), space separated */
} layer_info;

static const layer_info info[ART_LAYER_COUNT] = {
    [ART_BRET] = {"wrestlers/bret", "BRET HART", "BRET.LOD", NULL},
    [ART_RAZOR] = {"wrestlers/razor", "RAZOR RAMON", "RAZOR.LOD", NULL},
    [ART_UNDERTAKER] = {"wrestlers/undertaker", "UNDERTAKER", "TAKER.LOD", NULL},
    [ART_YOKOZUNA] = {"wrestlers/yokozuna", "YOKOZUNA", "YOKO.LOD", NULL},
    [ART_SHAWN] = {"wrestlers/shawn", "SHAWN MICHAELS", "SHAWN.LOD", NULL},
    [ART_BAMBAM] = {"wrestlers/bambam", "BAM BAM BIGELOW", "BAM.LOD", NULL},
    [ART_DOINK] = {"wrestlers/doink", "DOINK", "DOINK.LOD", NULL},
    [ART_LEX] = {"wrestlers/lex", "LEX LUGER", "LEX.LOD", NULL},
    [ART_MUGSHOTS] = {"mugshots", "MUGSHOTS AND NAMES", NULL, "wwfmugs newicons names nuchoice wwficon"},
    [ART_CROWD] = {"crowd", "CROWD", NULL, "crowd"},
    [ART_SCREENS] = {"screens", "MENU SCREENS", NULL,
                     "ladder vs_bk lillogo awwftit wwfselbk gamewin2 wwfinfo mkvs credit dcslogo nlogos4 choicebk ntrnsplt "
                     "diagp wwfourm3"},
    [ART_HUD] = {"hud", "HUD AND FONTS", NULL,
                 "robotron osgemd winfont fnt9 wsfnt14 wsfnt10 wgsfnt18 wgsfnt14 wgsfnt20 wgsfnt22 wgsfnt24 sgmd8 ogmd10 "
                 "trogf15 trogf7 statusd sportlo8 meters attbars pwrbarsn barbutt skil2 tipstuff arrow10 wmatch crut2 "
                 "roundplt"},
    [ART_EFFECTS] = {"effects", "EFFECTS", NULL,
                     "flash sparks2 sparkle explode xplosion fireball firewrk4 bladesp glovehit hitstuff sweat dizzy ghost "
                     "perfect combo glowpals special mkpower sungls jamie tony powerups"},
    [ART_RING] = {"ring", "RING AND PROPS", NULL, "ropeshad ropestuf casket chair glove stands dnkball rckchips wwfstuf"},
    [ART_OTHER] = {"other", "OTHER", NULL, NULL},
    [ART_BACKGROUNDS] = {"backgrounds", "BACKGROUNDS", NULL, NULL},
};

const char *art_layer_folder(art_layer l)
{
    return l >= 0 && l < ART_LAYER_COUNT ? info[l].folder : "other";
}

const char *art_layer_title(art_layer l)
{
    return l >= 0 && l < ART_LAYER_COUNT ? info[l].title : "OTHER";
}

/* "dir/Name.IMG" -> "name" */
static void stem(const char *file, char *out, size_t n)
{
    const char *b = file;
    for (const char *p = file; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    size_t i = 0;
    for (; b[i] && b[i] != '.' && i + 1 < n; i++)
        out[i] = (char)tolower((unsigned char)b[i]);
    out[i] = 0;
}

static int in_list(const char *list, const char *word)
{
    size_t wl = strlen(word);
    for (const char *p = list; p && *p;) {
        const char *e = strchr(p, ' ');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == wl && strncmp(p, word, l) == 0)
            return 1;
        p = e ? e + 1 : p + l;
    }
    return 0;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* The wrestler whose LOD script includes this library, or -1. */
static int wrestler_of_lib(const catalog *cat, const char *lib)
{
    for (int l = 0; l < cat->nlods; l++) {
        const char *path = cat->lods[l].path, *b = path;
        for (const char *p = path; *p; p++)
            if (*p == '/' || *p == '\\')
                b = p + 1;
        for (int w = ART_BRET; w <= ART_LEX; w++) {
            if (!ieq(b, info[w].lod))
                continue;
            for (int e = 0; e < cat->lods[l].nentries; e++) {
                const lod_entry *en = &cat->lods[l].entries[e];
                char name[64];
                if (en->kind == LOD_IMG) {
                    stem(en->file, name, sizeof name);
                    if (!strcmp(name, lib))
                        return w;
                }
            }
        }
    }
    return -1;
}

art_layer art_layer_of_lib(const catalog *cat, const char *lib_file, int is_bdd)
{
    char lib[64];
    if (is_bdd)
        return ART_BACKGROUNDS;
    stem(lib_file, lib, sizeof lib);
    int w = wrestler_of_lib(cat, lib);
    if (w >= 0)
        return (art_layer)w;
    for (int l = ART_MUGSHOTS; l < ART_OTHER; l++)
        if (in_list(info[l].libs, lib))
            return (art_layer)l;
    return ART_OTHER;
}

art_layer art_layer_of_image(const catalog *cat, const cat_image *ci)
{
    if (ci->lib < 0 || ci->lib >= cat->nlibs)
        return ART_OTHER;
    return art_layer_of_lib(cat, cat->libs[ci->lib].file, 0);
}
