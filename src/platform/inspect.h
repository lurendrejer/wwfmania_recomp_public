/*
 * The image inspector (wwf --inspect, docs/ASSET_OVERRIDES.md): keeps the last few seconds of shown frames with the
 * list of images drawn in each, and on F6 freezes the game to step through them with an overlay that boxes and
 * names every image (green: drawn from a high-resolution override, orange: the original art, magenta: a fill).
 * Meant for finding a bad override (a white blink, a wrong frame) by its name.
 */
#ifndef WWF_INSPECT_H
#define WWF_INSPECT_H

#include <SDL.h>

#include "sdl_video.h"
#include "wolf/wolf.h"

typedef struct {
    uint32_t *px;          /* shown.w x shown.h pixels of the rendered view */
    size_t px_cap;
    SDL_Rect src;          /* where they were in the rendered view */
    int scale;
    int org_x, org_y;      /* the bitmap pixel at the rendered view's top-left (game pixels) */
    uint64_t frame;
    wolf_draw *draws;
    int ndraws, draws_cap;
} insp_snap;

typedef struct {
    int recording;         /* --inspect: frames are kept */
    int active;            /* frozen, overlay shown */
    insp_snap *ring;
    int cap, count, newest;
    int back;              /* frames back from the newest shown (0 = newest) */
    int sel;               /* index into the snapshot's draws, -1 for none */
    int show_bg, show_boxes;
    int step;              /* set: the main loop runs one frame, then stays frozen */
    sdl_video *sv;
    char msg[128];
    uint32_t msg_until;
} inspector;

void inspect_init(inspector *in, sdl_video *sv, int recording);
void inspect_free(inspector *in);

/* After each live present (when recording): keeps the frame shown and what was drawn. */
void inspect_capture(inspector *in, const wolf *w);

/* Freezes on the frame `back` frames before the newest and selects its first image (a test aid, --shot-inspect). */
void inspect_freeze(inspector *in, int back);

/* Handles F6 and, while frozen, the inspector's keys and mouse. Returns 1 when it used the event. */
int inspect_event(inspector *in, const SDL_Event *e);

/* While frozen: shows the selected kept frame (with the overlay, through the video's overlay hook). */
void inspect_present(inspector *in);

/* The overlay (call from the video's overlay hook; draws nothing unless frozen). */
void inspect_draw(inspector *in, SDL_Renderer *ren, int ow, int oh);

#endif
