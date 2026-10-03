/*
 * Mods: new features kept apart from the port of the original game.
 *
 * Nothing here changes what the original code does. A mod is a small C
 * module under mods/<name>/ that the game runs hooks for, and only when
 * it is switched on at run time (wwf --mod NAME). With no mod enabled the
 * game behaves exactly as without this layer. See docs/MODS.md.
 *
 * A mod owns its own state and assets; it reads and draws through the same
 * wolf/video structures the game uses, and must not patch the recompiled
 * game code or the original files under orig/.
 */
#ifndef WWF_MODS_H
#define WWF_MODS_H

#include <stddef.h>

#define WWF_MAX_MODS 8

struct gsp_hw; /* the wolf machine (wolf/wolf.h) */

typedef struct wwf_mod {
    const char *name;        /* command line name, lowercase, e.g. "referee" */
    const char *description; /* one line for --list-mods */

    /* All hooks are optional. `state` is a per-mod pointer the mod may set. */

    /* Once, when the mod is enabled (after the game is initialized). Return
     * 0 to refuse (missing assets, ...) after filling `err`. */
    int (*init)(struct gsp_hw *w, void **state, char *err, size_t err_len);
    /* Every video frame, before the game code runs. */
    void (*frame_begin)(struct gsp_hw *w, void *state);
    /* Every video frame, after the game code ran and the display page is
     * final. Draw overlays here. */
    void (*frame_end)(struct gsp_hw *w, void *state);
    /* When the game shuts down. */
    void (*shutdown)(struct gsp_hw *w, void *state);

    /* An optional number the player can set (`--mod NAME=N`, left/right on the
     * mod's line in the F1 menu), for example how many extra opponents.
     * arg_max <= arg_min means the mod has none. Read it with mods_arg(). */
    int arg_min, arg_max, arg_default;
    const char *arg_label;    /* short, for the menu, e.g. "OPPONENTS" */
    /* 1 when the mod can be switched on and off while the game runs (it only reads what its hooks write each
     * frame and puts it back in `shutdown`); 0 when it builds something when a match or the game starts
     * (wrestlers, objects, the select screen, the ladder), so a change shows from the next match or restart. */
    int live;
} wwf_mod;

/* The mods compiled in (NULL-terminated), see mods/CMakeLists.txt. */
const wwf_mod *const *mods_builtin(void);
const wwf_mod *mods_find(const char *name);

/* Registers m and runs its init hook. Returns 0 with `err` set on failure
 * or when it is already enabled. */
int mods_add(struct gsp_hw *w, const wwf_mod *m, char *err, size_t err_len);
/* mods_add by name; "name=N" also sets the mod's number. */
int mods_enable(struct gsp_hw *w, const char *name, char *err, size_t err_len);

/* The mod's number (its default when it is not enabled), and setting it
 * (clamped to arg_min..arg_max). */
int mods_arg(const struct gsp_hw *w, const wwf_mod *m);
void mods_set_arg(struct gsp_hw *w, const wwf_mod *m, int value);

/* Whether m is enabled now, and disabling it (runs its shutdown hook). */
int mods_is_enabled(const struct gsp_hw *w, const wwf_mod *m);
void mods_remove(struct gsp_hw *w, const wwf_mod *m);

/* Hook dispatch, called by the machine. */
void mods_frame_begin(struct gsp_hw *w);
void mods_frame_end(struct gsp_hw *w);
void mods_shutdown(struct gsp_hw *w);

#endif
