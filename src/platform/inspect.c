/* The image inspector (inspect.h). */
#include "inspect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

#define HISTORY_BYTES (512u * 1024 * 1024)   /* memory for kept frames */
#define HISTORY_MIN 30
#define HISTORY_MAX 330                       /* about 6 seconds */

void inspect_init(inspector *in, sdl_video *sv, int recording)
{
    memset(in, 0, sizeof *in);
    in->sv = sv;
    in->recording = recording;
    in->sel = -1;
    in->show_boxes = 1;
}

void inspect_free(inspector *in)
{
    for (int i = 0; i < in->cap; i++) {
        free(in->ring[i].px);
        free(in->ring[i].draws);
    }
    free(in->ring);
    in->ring = NULL;
    in->cap = in->count = 0;
}

static void say(inspector *in, const char *msg)
{
    snprintf(in->msg, sizeof in->msg, "%s", msg);
    in->msg_until = SDL_GetTicks() + 3000;
}

void inspect_capture(inspector *in, const wolf *w)
{
    const sdl_video *sv = in->sv;
    if (!in->recording || in->active || sv->shown.w <= 0 || sv->shown.h <= 0)
        return;
    size_t npx = (size_t)sv->shown.w * sv->shown.h;
    if (!in->ring) {
        size_t per = npx * 4 + 64 * 1024;
        int cap = (int)(HISTORY_BYTES / per);
        cap = cap < HISTORY_MIN ? HISTORY_MIN : cap > HISTORY_MAX ? HISTORY_MAX : cap;
        in->ring = calloc((size_t)cap, sizeof *in->ring);
        if (!in->ring) {
            in->recording = 0;
            return;
        }
        in->cap = cap;
        in->newest = -1;
    }
    int slot = (in->newest + 1) % in->cap;
    insp_snap *s = &in->ring[slot];
    if (s->px_cap < npx) {
        uint32_t *p = realloc(s->px, npx * sizeof *p);
        if (!p)
            return;
        s->px = p;
        s->px_cap = npx;
    }
    memcpy(s->px, sv->argb, npx * sizeof *s->px);
    s->src = sv->shown;
    s->scale = w->v.scale;
    s->org_x = w->v.view_x - w->v.view_pad;
    s->org_y = w->v.view_y - w->v.view_pad_y;
    s->frame = w->frames;
    int n;
    const wolf_draw *dl = wolf_shown_draws(w, &n);
    if (n > s->draws_cap) {
        wolf_draw *d = realloc(s->draws, (size_t)n * sizeof *d);
        if (!d)
            n = 0;
        else {
            s->draws = d;
            s->draws_cap = n;
        }
    }
    if (n > 0)
        memcpy(s->draws, dl, (size_t)n * sizeof *dl);
    s->ndraws = n;
    in->newest = slot;
    if (in->count < in->cap)
        in->count++;
}

static insp_snap *current(inspector *in)
{
    if (!in->count)
        return NULL;
    int back = in->back < in->count ? in->back : in->count - 1;
    return &in->ring[((in->newest - back) % in->cap + in->cap) % in->cap];
}

static int shown_draw(const inspector *in, const wolf_draw *d)
{
    return in->show_bg || !d->background;
}

/* The draw's rectangle in the window's output pixels. */
static SDL_Rect out_rect(const inspector *in, const insp_snap *s, const wolf_draw *d, int ow, int oh)
{
    int x0, y0, x1, y1;
    sdl_video_to_output(in->sv, ow, oh, (double)(d->x - s->org_x) * s->scale, (double)(d->y - s->org_y) * s->scale,
                        &x0, &y0);
    sdl_video_to_output(in->sv, ow, oh, (double)(d->x - s->org_x + d->w) * s->scale,
                        (double)(d->y - s->org_y + d->h) * s->scale, &x1, &y1);
    SDL_Rect r = {x0, y0, x1 - x0 > 1 ? x1 - x0 : 1, y1 - y0 > 1 ? y1 - y0 : 1};
    return r;
}

static void select_step(inspector *in, int dir)
{
    insp_snap *s = current(in);
    if (!s || !s->ndraws)
        return;
    int i = in->sel;
    for (int tries = 0; tries < s->ndraws; tries++) {
        i = i < 0 ? (dir > 0 ? 0 : s->ndraws - 1) : (i + dir + s->ndraws) % s->ndraws;
        if (shown_draw(in, &s->draws[i]))
            break;
    }
    in->sel = i;
}

/* The smallest shown image under a window point (output pixels). */
static void select_at(inspector *in, int x, int y, int ow, int oh)
{
    insp_snap *s = current(in);
    if (!s)
        return;
    int best = -1;
    long best_area = 0;
    for (int i = 0; i < s->ndraws; i++) {
        if (!shown_draw(in, &s->draws[i]))
            continue;
        SDL_Rect r = out_rect(in, s, &s->draws[i], ow, oh);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && (best < 0 || (long)r.w * r.h < best_area)) {
            best = i;
            best_area = (long)r.w * r.h;
        }
    }
    in->sel = best;
}

static void copy_selected(inspector *in)
{
    insp_snap *s = current(in);
    if (!s || in->sel < 0 || in->sel >= s->ndraws || !s->draws[in->sel].name[0])
        return;
    SDL_SetClipboardText(s->draws[in->sel].name);
    printf("inspector: %s (frame %lu)\n", s->draws[in->sel].name, (unsigned long)s->frame);
    fflush(stdout);
    char m[64];
    snprintf(m, sizeof m, "COPIED %s", s->draws[in->sel].name);
    say(in, m);
}

void inspect_freeze(inspector *in, int back)
{
    if (!in->count)
        return;
    in->active = 1;
    in->back = back < in->count ? back : in->count - 1;
    in->sel = -1;
    select_step(in, 1);
}

int inspect_event(inspector *in, const SDL_Event *e)
{
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F6) {
        if (!in->recording) {
            printf("inspector: start the game with --inspect to keep frames for it\n");
            return 1;
        }
        if (!in->active && !in->count)
            return 1;
        in->active = !in->active;
        in->back = 0;
        in->sel = -1;
        in->step = 0;
        return 1;
    }
    if (!in->active)
        return 0;
    if (e->type == SDL_KEYDOWN) {
        int many = (e->key.keysym.mod & KMOD_SHIFT) ? 10 : 1;
        switch (e->key.keysym.sym) {
        case SDLK_LEFT:
            in->back = in->back + many < in->count ? in->back + many : in->count - 1;
            in->sel = -1;
            break;
        case SDLK_RIGHT:
            if (in->back == 0)
                in->step = many;                     /* past the newest: run the game a frame (or ten) */
            else
                in->back = in->back > many ? in->back - many : 0;
            in->sel = -1;
            break;
        case SDLK_UP:
            select_step(in, -1);
            break;
        case SDLK_DOWN:
            select_step(in, 1);
            break;
        case SDLK_c:
            copy_selected(in);
            break;
        case SDLK_b:
            in->show_bg = !in->show_bg;
            in->sel = -1;
            break;
        case SDLK_h:
            in->show_boxes = !in->show_boxes;
            break;
        case SDLK_ESCAPE:
            in->active = 0;
            break;
        default:
            break;
        }
        return 1;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int ww = 1, wh = 1, ow = 1, oh = 1;
        SDL_GetWindowSize(in->sv->win, &ww, &wh);
        SDL_GetRendererOutputSize(in->sv->ren, &ow, &oh);
        select_at(in, e->button.x * ow / (ww > 0 ? ww : 1), e->button.y * oh / (wh > 0 ? wh : 1), ow, oh);
        return 1;
    }
    return e->type == SDL_KEYUP || e->type == SDL_MOUSEBUTTONUP || e->type == SDL_MOUSEMOTION;
}

void inspect_present(inspector *in)
{
    insp_snap *s = current(in);
    if (s)
        sdl_video_present_argb(in->sv, s->px, s->src, s->scale);
}

static void frame_rect(SDL_Renderer *ren, SDL_Rect r, int t)
{
    for (int i = 0; i < t; i++) {
        SDL_Rect q = {r.x - i, r.y - i, r.w + 2 * i, r.h + 2 * i};
        SDL_RenderDrawRect(ren, &q);
    }
}

static void label(SDL_Renderer *ren, int x, int y, int fs, const char *str, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect bg = {x - fs, y - fs, (int)strlen(str) * 6 * fs + fs, 9 * fs};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 190);
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    font_text(ren, x, y, fs, str);
}

void inspect_draw(inspector *in, SDL_Renderer *ren, int ow, int oh)
{
    if (!in->active)
        return;
    insp_snap *s = current(in);
    if (!s)
        return;
    int fs = ow / 600 > 1 ? ow / 600 : 1;    /* font pixel size: a line of about 95 characters fits */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int nshown = 0, nhd = 0;
    for (int i = 0; i < s->ndraws; i++) {
        const wolf_draw *d = &s->draws[i];
        if (!shown_draw(in, d))
            continue;
        nshown++;
        nhd += d->hd;
        if (!in->show_boxes && i != in->sel)
            continue;
        SDL_Rect r = out_rect(in, s, d, ow, oh);
        Uint8 cr = 255, cg = 150, cb = 0;              /* original art: orange */
        if (d->hd) {
            cr = 60;                                     /* override: green */
            cg = 230;
            cb = 60;
        } else if (!d->name[0]) {
            cr = 255;                                    /* a fill: magenta */
            cg = 0;
            cb = 255;
        }
        SDL_SetRenderDrawColor(ren, cr, cg, cb, i == in->sel ? 255 : 170);
        frame_rect(ren, r, i == in->sel ? 3 * fs : 1);
        if (in->show_boxes && !d->background && r.w > 20 * fs)
            label(ren, r.x + fs, r.y + fs, fs, d->name[0] ? d->name : "FILL", cr, cg, cb);
    }
    char line[160];
    snprintf(line, sizeof line, "INSPECTOR  FRAME %lu (%d BACK OF %d)  %d IMAGES, %d HIGH-RES%s", (unsigned long)s->frame,
             in->back, in->count, nshown, nhd, in->show_bg ? " (WITH BACKGROUND)" : "");
    label(ren, 4 * fs, 4 * fs, fs, line, 255, 255, 255);
    label(ren, 4 * fs, 14 * fs, fs, "< > STEP (SHIFT: 10; > AT THE NEWEST RUNS THE GAME ON)", 200, 200, 200);
    label(ren, 4 * fs, 24 * fs, fs, "UP/DOWN OR CLICK: SELECT   C: COPY NAME   B: BACKGROUND   H: BOXES   F6: RESUME",
          200, 200, 200);
    if (in->sel >= 0 && in->sel < s->ndraws) {
        const wolf_draw *d = &s->draws[in->sel];
        snprintf(line, sizeof line, "%s  %s  X %d Y %d  %dX%d  PAL %04X", d->name[0] ? d->name : "FILL",
                 d->hd ? "HIGH-RES" : d->name[0] ? "ORIGINAL" : "COLOUR", d->x, d->y, d->w, d->h, d->pal);
        if (!d->name[0])
            snprintf(line + strlen(line), sizeof line - strlen(line), "  COLOUR %04X", d->color);
        label(ren, 4 * fs, 34 * fs, fs, line, 255, 255, 0);
    }
    if (in->msg[0] && SDL_GetTicks() < in->msg_until)
        label(ren, 4 * fs, 44 * fs, fs, in->msg, 120, 255, 120);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}
