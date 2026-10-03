/*
 * The GPU path against the CPU blitter (src/platform/gpu_video.c, docs/VIDEO.md "GPU path").
 *
 *   test_gpu
 *
 * Needs an OpenGL ES 2 renderer from SDL (the offscreen video driver and Mesa's llvmpipe will do). Where there
 * is none the test exits with 77, which ctest reports as skipped, never as a failure.
 *
 *  1. gpu_video_selftest: random blits (all write modes, flips, scales, clip windows, source rectangles,
 *     masked pixels, overrides with and without detail), fills, pixels, palettes and CPU reads, compared bit for
 *     bit with video.c on the CPU: framebuffer, detail plane and converted picture, at render scales 1 to 4.
 *  2. The whole present path (sdl_video_present with and without the GPU, with zoom, aspect, scanlines and
 *     smoothing): the pictures read back from the renderer must be identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/gpu_video.h"
#include "platform/sdl_video.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

#define SKIP 77

static int open_gl_renderer_driver(SDL_Window **win, SDL_Renderer **ren)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return 0;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    *win = SDL_CreateWindow("test_gpu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64, SDL_WINDOW_HIDDEN);
    *ren = *win ? SDL_CreateRenderer(*win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, NULL);
    if (!*ren) {
        if (*win)
            SDL_DestroyWindow(*win);
        *win = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 0;
    }
    return 1;
}

/* The offscreen video driver needs no display; the default driver is tried when it has no GL ES 2. */
static int open_gl_renderer(SDL_Window **win, SDL_Renderer **ren)
{
    if (!SDL_getenv("SDL_VIDEODRIVER")) {
        SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
        if (open_gl_renderer_driver(win, ren))
            return 1;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_setenv("SDL_VIDEODRIVER", "", 1);
    }
    return open_gl_renderer_driver(win, ren);
}

static void selftests(void)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    video v;
    char err[400], msg[400];

    if (!open_gl_renderer(&win, &ren)) {
        printf("test_gpu: no OpenGL ES 2 renderer (%s): skipped\n", SDL_GetError());
        exit(SKIP);
    }
    video_init(&v, 1);
    gpu_video *g = gpu_video_create(ren, &v, err, sizeof err);
    if (!g) {
        printf("test_gpu: the GPU path cannot start here (%s): skipped\n", err);
        video_free(&v);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        exit(SKIP);
    }
    printf("GL: %s\n", gpu_video_info(g));
    for (int scale = 1; scale <= 4; scale++) {
        int ok = gpu_video_selftest(g, 40, scale, 777u + (unsigned)scale, msg, sizeof msg);
        if (!ok)
            fprintf(stderr, "selftest at scale %d: %s\n", scale, msg);
        CHECK(ok);
    }
    for (unsigned seed = 1; seed <= 6; seed++) {
        int ok = gpu_video_selftest(g, 25, 3, seed * 7919u, msg, sizeof msg);
        if (!ok)
            fprintf(stderr, "selftest seed %u: %s\n", seed, msg);
        CHECK(ok);
    }
    gpu_video_destroy(g, &v);
    video_free(&v);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* ---- the present path ----------------------------------------------------------------------- */

typedef struct {
    uint32_t *px;
    int w, h;
} grab;

static void grab_overlay(SDL_Renderer *ren, int w, int h, void *user)
{
    grab *gr = user;
    free(gr->px);
    gr->px = malloc((size_t)w * h * 4);
    gr->w = w;
    gr->h = h;
    if (gr->px && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, gr->px, w * 4) != 0) {
        free(gr->px);
        gr->px = NULL;
    }
}

static unsigned lcg(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

/* The same scene on any video: three images, many blits, a palette. */
static void scene(video *v, unsigned seed)
{
    static uint8_t pix[3][40 * 30];
    static img_image im[3];
    static gfx_image gi[3];
    unsigned s = seed;
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 40 * 30; k++) {
            unsigned r = lcg(&s);
            pix[i][k] = (r & 3) == 0 ? 0 : (uint8_t)(r >> 4);
        }
        memset(&im[i], 0, sizeof im[i]);
        im[i].width = 40;
        im[i].height = 30;
        im[i].stride = 40;
        im[i].palette = IMG_NONE;
        im[i].pixels = pix[i];
        gfx_image_from_img(&gi[i], NULL, &im[i]);
    }
    for (int i = 0; i < VIDEO_COLORS; i++)
        v->colram[i] = (uint16_t)(lcg(&s) & 0x7FFF);
    static const uint16_t sc[] = {0x100, 0x100, 0x80, 0xC0, 0x155, 0x300, 0x1FF};
    for (int n = 0; n < 400; n++) {
        dma_blit b;
        memset(&b, 0, sizeof b);
        b.image = &gi[lcg(&s) % 3];
        b.x = (int)(lcg(&s) % 460) - 20;
        b.y = (int)(lcg(&s) % 290) - 10;
        b.ctrl = (uint16_t)(lcg(&s) & 15);
        if (lcg(&s) % 3 == 0)
            b.ctrl |= DMA_FLIPH;
        b.pal = (uint16_t)(lcg(&s) & 0x7F00);
        b.color = (uint16_t)lcg(&s);
        b.scale_x = sc[lcg(&s) % 7];
        b.scale_y = sc[lcg(&s) % 7];
        video_dma(v, &b);
    }
}

static int present_once(int gpu, int variant, int pause, grab *out, int *used_gpu)
{
    sdl_video sv;
    video v;
    char why[300];
    sdl_video_request_gpu(gpu);
    video_init_bitmap(&v, 2, 440, 270);
    v.view_x = 20;
    v.view_y = 8;
    v.view_pad = 20;
    v.view_pad_y = 8;
    if (!sdl_video_open(&sv, "test_gpu", &v)) {
        video_free(&v);
        return 0;
    }
    *used_gpu = 0;
    if (gpu) {
        *used_gpu = sdl_video_attach_gpu(&sv, &v, why, sizeof why);
        if (!*used_gpu)
            fprintf(stderr, "no GPU path: %s\n", why);
    }
    scene(&v, 4242u + (unsigned)variant);
    SDL_SetWindowSize(sv.win, 801, 517);
    sv.smooth = variant & 1;
    sv.crt_aspect = (variant >> 1) & 1;
    sv.scanlines = (variant >> 2) & 1;
    sv.integer_scale = (variant >> 3) & 1;
    sv.zoom = 1.0 + 0.3 * (variant % 3);
    sv.zmin = 0.5;
    sv.base_w = VIDEO_W;
    sv.base_h = VIDEO_H;
    sv.overlay = grab_overlay;
    sv.overlay_user = out;
    sdl_video_present(&sv, &v);
    /* a second frame with more drawn over it and a changed palette (the lookup happens at present) */
    for (int i = 0; i < 4096; i++)
        v.colram[i] = (uint16_t)(v.colram[i] ^ 0x2D6B);
    if (pause && sv.gpu)                 /* the GPU path paused while the CPU draws, as for the pixel-moving effects */
        gpu_video_suspend(sv.gpu, &v);
    scene(&v, 99u + (unsigned)variant);
    sdl_video_present(&sv, &v);
    if (pause && sv.gpu) {
        CHECK(gpu_video_suspended(sv.gpu));
        gpu_video_resume(sv.gpu, &v);
        CHECK(!gpu_video_suspended(sv.gpu));
    }
    /* a third frame: with the GPU path back, drawn over what the CPU drew */
    scene(&v, 7u + (unsigned)variant);
    sdl_video_present(&sv, &v);
    sdl_video_close(&sv);
    video_free(&v);
    return out->px != NULL;
}

static void present_tests(void)
{
    for (int variant = 0; variant < 16; variant += 3) {
        grab a = {NULL, 0, 0}, b = {NULL, 0, 0};
        int ga = 0, gb = 0;
        CHECK(present_once(0, variant, 0, &a, &ga));
        CHECK(present_once(1, variant, variant % 2, &b, &gb));
        CHECK(gb == 1);
        if (!a.px || !b.px || !gb)
            continue;
        CHECK(a.w == b.w && a.h == b.h);
        size_t diff = 0;
        for (size_t i = 0; a.w == b.w && a.h == b.h && i < (size_t)a.w * a.h; i++)
            diff += a.px[i] != b.px[i];
        if (diff)
            fprintf(stderr, "present variant %d: %zu of %d pixels differ\n", variant, diff, a.w * a.h);
        CHECK(diff == 0);
        free(a.px);
        free(b.px);
    }
}

int main(void)
{
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    selftests();
    present_tests();
    if (failures) {
        fprintf(stderr, "test_gpu: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_gpu: all passed\n");
    return 0;
}
