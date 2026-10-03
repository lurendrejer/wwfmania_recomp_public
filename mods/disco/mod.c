/*
 * disco - every colour on screen turns around the colour wheel (docs/MODS.md, mods/disco/README.md).
 * Only what is shown changes: after the game's frame the mod keeps a copy of the colour RAM and turns the
 * hue of every entry, and before the next frame it puts the copy back, so the game never reads a colour
 * it did not write.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolf/wolf.h"

extern const wwf_mod mod_disco;

typedef struct {
    uint16_t saved[VIDEO_COLORS];
    int have;
    double angle; /* radians */
} disco_state;

static int disco_init(wolf *w, void **state, char *err, size_t err_len)
{
    disco_state *s = calloc(1, sizeof *s);
    (void)w;
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return 0;
    }
    *state = s;
    return 1;
}

static void disco_frame_begin(wolf *w, void *state)
{
    disco_state *s = state;

    if (s->have)
        memcpy(w->v.colram, s->saved, sizeof s->saved);
    s->have = 0;
}

static int clamp31(double c)
{
    return c < 0 ? 0 : c > 31 ? 31 : (int)(c + 0.5);
}

/* A hue turn about the grey axis (the usual rotation matrix; greys stay grey). */
static void disco_frame_end(wolf *w, void *state)
{
    disco_state *s = state;
    double c = cos(s->angle), n = sin(s->angle);
    double m[3][3] = {
        {0.299 + 0.701 * c + 0.168 * n, 0.587 - 0.587 * c + 0.330 * n, 0.114 - 0.114 * c - 0.497 * n},
        {0.299 - 0.299 * c - 0.328 * n, 0.587 + 0.413 * c + 0.035 * n, 0.114 - 0.114 * c + 0.292 * n},
        {0.299 - 0.300 * c + 1.250 * n, 0.587 - 0.588 * c - 1.050 * n, 0.114 + 0.886 * c - 0.203 * n},
    };

    memcpy(s->saved, w->v.colram, sizeof s->saved);
    s->have = 1;
    for (int i = 0; i < VIDEO_COLORS; i++) {
        uint16_t v = s->saved[i];
        double r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        int nr = clamp31(m[0][0] * r + m[0][1] * g + m[0][2] * b);
        int ng = clamp31(m[1][0] * r + m[1][1] * g + m[1][2] * b);
        int nb = clamp31(m[2][0] * r + m[2][1] * g + m[2][2] * b);
        w->v.colram[i] = (uint16_t)((v & 0x8000) | (nr << 10) | (ng << 5) | nb);
    }
    s->angle += mods_arg(w, &mod_disco) * 0.02;
    if (s->angle > 6.283185307179586)
        s->angle -= 6.283185307179586;
}

static void disco_shutdown(wolf *w, void *state)
{
    disco_state *s = state;

    if (s->have)
        memcpy(w->v.colram, s->saved, sizeof s->saved);
    free(s);
}

const wwf_mod mod_disco = {
    "disco",
    "every colour on screen turns around the colour wheel; N is the speed",
    disco_init,
    disco_frame_begin,
    disco_frame_end,
    disco_shutdown,
    1, 20, 4, "SPEED", 1,
};
