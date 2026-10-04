/*
 * The art layers: which part of the game an image belongs to, so that its high-resolution override can be
 * switched on and off as a group (the F1 menu, DISPLAY, HD ART LAYERS) and so that an art set can be kept in one
 * folder per layer (tools/art_pack.py; the zip reader only looks at file names, so any folder layout works).
 *
 * The layer of an image comes from the IMG library it is drawn from and, for a wrestler, from the LOD script that
 * selected it (docs/ASSET_OVERRIDES.md, "Layers"). The libraries of the other layers are listed in layers.c by
 * name; that grouping is a judgment from the image labels, not something the game's data says.
 */
#ifndef WWF_LAYERS_H
#define WWF_LAYERS_H

#include "catalog.h"

typedef enum {
    ART_BRET, ART_RAZOR, ART_UNDERTAKER, ART_YOKOZUNA, ART_SHAWN, ART_BAMBAM, ART_DOINK, ART_LEX,   /* WRESTLE2.ASM order */
    ART_MUGSHOTS,      /* wrestler pictures and names on the select screens */
    ART_CROWD,
    ART_SCREENS,       /* menus, logos, the ladder, the versus screen */
    ART_HUD,           /* fonts, meters, plates */
    ART_EFFECTS,       /* flashes, sparks, explosions, sweat, hits, power-ups */
    ART_RING,          /* ropes, chairs, props, shadows */
    ART_OTHER,         /* everything the lists above do not name */
    ART_BACKGROUNDS,   /* the arena pieces (.BDD): they are not in the catalog */
    ART_LAYER_COUNT
} art_layer;

/* The layer of an image drawn from the IMG library `lib_file` (basename, any case); `is_bdd` for a background piece. */
art_layer art_layer_of_lib(const catalog *cat, const char *lib_file, int is_bdd);
art_layer art_layer_of_image(const catalog *cat, const cat_image *ci);

/* "wrestlers/bret", "mugshots", ...: the folder of the layer in an art set. */
const char *art_layer_folder(art_layer l);
/* "BRET HART", "MUGSHOTS AND NAMES", ...: for the menu. */
const char *art_layer_title(art_layer l);

#endif
