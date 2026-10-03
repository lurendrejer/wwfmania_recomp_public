/*
 * The worker threads for the HD art (src/platform/gfx_async.c) with the real gfx_cache: the images the game asks
 * for come back as the same pixels a synchronous load gives, each is read once, a reload drops what is out, and
 * stopping with jobs pending is clean (run it under ThreadSanitizer for the rest).
 *
 *   test_gfx_worker <tmpdir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "gfx_fixture.h"
#include "platform/gfx_async.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

static int warnings;
static void count_warn(const char *msg, void *user)
{
    (void)msg;
    (void)user;
    warnings++;
}

/* polls until nothing is pending (the main loop does this once a frame) */
static int settle(gfx_async *a, gfx_cache *gc)
{
    for (int i = 0; i < 2000; i++) {
        gfx_async_poll(a);
        if (gc->npending == 0 && gc->plan_n == 0)
            return 1;
        SDL_Delay(5);
    }
    return 0;
}

static void request_all(gfx_cache *gc)
{
    for (int i = 0; i < gc->cat->nimages; i++)
        gfx_get(gc, &gc->cat->images[i]);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_gfx_worker <tmpdir>\n");
        return 2;
    }
    char dir[1100], ovr[1100];
    CHECK(fx_make(argv[1], "gfx_worker", dir, sizeof dir, ovr, sizeof ovr));
    static const char *const lods[] = {"T.LOD", NULL};
    catalog cat;
    CHECK(catalog_open(&cat, dir, ovr, lods, NULL, NULL));

    gfx_cache ref;
    CHECK(gfx_cache_init(&ref, &cat, NULL, NULL));
    for (int i = 0; i < FX_N; i++)
        gfx_get(&ref, &cat.images[i]);

    {
        long n;
        double ms;
        gfx_load_stats(&n, &ms);        /* (the reference loads were counted) */
    }
    gfx_cache gc;
    CHECK(gfx_cache_init(&gc, &cat, count_warn, NULL));
    gfx_async *a = gfx_async_start(&gc, 2, 0);
    CHECK(a != NULL);
    if (!a)
        return 1;

    /* 1. the game draws everything: the overrides arrive, each exactly once, however often it asks */
    request_all(&gc);
    for (int i = 0; i < FX_N; i++)
        CHECK(gc.images[i].hi_state == (gc.images[i].entry->has_override ? 2 : -1));
    CHECK(settle(a, &gc));
    for (int round = 0; round < 50; round++)
        request_all(&gc);
    CHECK(settle(a, &gc));
    for (int i = 0; i < FX_N; i++) {
        const gfx_image *g = &gc.images[i], *r = &ref.images[i];
        CHECK(g->hi_state == r->hi_state);
        if (r->hi_state == 1) {
            CHECK(g->hi_factor == r->hi_factor && memcmp(g->hi, r->hi, 24) == 0);
            CHECK((g->hi_detail == NULL) == (r->hi_detail == NULL));
            if (r->hi_detail)
                CHECK(memcmp(g->hi_detail, r->hi_detail, 24 * 4) == 0);
            CHECK(g->gen == 1);                         /* attached once: the GPU rebuilds its textures once */
        }
    }
    CHECK(warnings == 1);                               /* IM2, the wrong size */
    {
        long n;
        double ms;
        int pend;
        gfx_cache_load_stats(&gc, &n, &ms, &pend);
        CHECK(n == 5 && pend == 0);                     /* IM0, 1, 2, 4, 5 read once each (IM3 has no file) */
    }

    /* 2. prefetch through the workers */
    gfx_cache_reload(&gc);
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM4"));
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM5"));
    CHECK(settle(a, &gc));
    CHECK(gc.images[4].hi_state == 1 && gc.images[5].hi_state == 1 && gc.prefetch_bytes == 48);
    {
        long n;
        double ms;
        int pend;
        gfx_cache_load_stats(&gc, &n, &ms, &pend);
        CHECK(n == 2);
    }

    /* 3. a reload while jobs are out, then asking again: it all comes back, nothing stale is attached */
    for (int round = 0; round < 20; round++) {
        gfx_cache_reload(&gc);
        request_all(&gc);
        gfx_cache_reload(&gc);
        CHECK(gc.npending == 0);
        request_all(&gc);
        CHECK(settle(a, &gc));
        CHECK(gc.images[0].hi_state == 1 && gc.images[1].hi_state == 1 && gc.images[4].hi_state == 1);
        CHECK(memcmp(gc.images[0].hi, ref.images[0].hi, 24) == 0);
    }

    /* 4. wait_idle runs the plan to the end */
    gfx_cache_reload(&gc);
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM0"));
    gfx_prefetch_add(&gc, catalog_find(&cat, "IM1"));
    gfx_async_wait_idle(a);
    CHECK(gc.images[0].hi_state == 1 && gc.images[1].hi_state == 1 && gc.npending == 0);

    /* 5. stopping with jobs pending and results not taken: the images are "not tried", the cache is usable,
     * the workers can be started again */
    for (int round = 0; round < 40; round++) {
        gfx_cache_reload(&gc);
        request_all(&gc);
        gfx_async_stop(a);
        CHECK(gc.submit == NULL && gc.npending == 0);
        for (int i = 0; i < FX_N; i++)
            CHECK(gc.images[i].hi_state == 0 || gc.images[i].hi_state == -1);
        a = gfx_async_start(&gc, 1 + round % 3, 0);
        CHECK(a != NULL);
        if (!a)
            return 1;
    }
    CHECK(settle(a, &gc));
    request_all(&gc);
    CHECK(settle(a, &gc));
    CHECK(gc.images[1].hi_state == 1);

    gfx_async_stop(a);
    /* after stop the cache loads synchronously again */
    gfx_cache_reload(&gc);
    CHECK(gfx_get(&gc, &cat.images[0])->hi_state == 1);

    gfx_cache_free(&gc);
    gfx_cache_free(&ref);
    catalog_close(&cat);
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    else
        printf("gfx worker: ok\n");
    return failures ? 1 : 0;
}
