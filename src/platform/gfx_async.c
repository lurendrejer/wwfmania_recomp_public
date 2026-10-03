/* Worker threads for the high-resolution art (gfx_async.h). */
#include "gfx_async.h"

#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#define MAX_THREADS 8
#define PREFETCH_QUEUE_MAX 24   /* prefetch jobs waiting at once: the plan feeds more as they start */
#define DONE_MAX 32             /* finished jobs the main thread has not taken: prefetch waits beyond this */

typedef struct node {
    gfx_job job;
    gfx_hi hi;
    char err[256];
    int ok;
    double ms;
    struct node *next;
} node;

typedef struct {
    node *head, *tail;
    int n;
} queue;

struct gfx_async {
    gfx_cache *gc;
    SDL_mutex *m;
    SDL_cond *work;         /* a job was queued, or a result was taken, or stop */
    SDL_cond *idle;         /* a job finished */
    SDL_Thread *thr[MAX_THREADS];
    int nthr;
    int quit;
    int delay_ms;           /* WWF_ART_DELAY_MS: each job takes this much longer (a test aid: a slow device) */
    queue demand, prefetch, done;
    const gfx_image *running[MAX_THREADS];   /* the image each worker is loading (NULL: idle) */
    unsigned running_epoch[MAX_THREADS];
    int nrunning;
    unsigned seen_epoch;
};

static void q_push(queue *q, node *n)
{
    n->next = NULL;
    if (q->tail)
        q->tail->next = n;
    else
        q->head = n;
    q->tail = n;
    q->n++;
}

static node *q_pop(queue *q)
{
    node *n = q->head;
    if (n) {
        q->head = n->next;
        if (!q->head)
            q->tail = NULL;
        q->n--;
    }
    return n;
}

static void q_free(queue *q)
{
    node *n;
    while ((n = q_pop(q)) != NULL) {
        gfx_hi_free(&n->hi);
        free(n);
    }
}

/* Finds a queued job for the image and takes it out of its queue. */
static node *q_take(queue *q, const gfx_image *gi, unsigned epoch)
{
    node *prev = NULL;
    for (node *n = q->head; n; prev = n, n = n->next)
        if (n->job.gi == gi && n->job.epoch == epoch) {
            if (prev)
                prev->next = n->next;
            else
                q->head = n->next;
            if (q->tail == n)
                q->tail = prev;
            q->n--;
            return n;
        }
    return NULL;
}

static int q_has(const queue *q, const gfx_image *gi, unsigned epoch)
{
    for (const node *n = q->head; n; n = n->next)
        if (n->job.gi == gi && n->job.epoch == epoch)
            return 1;
    return 0;
}

/* gfx_submit_fn: the game thread */
static int submit(void *user, const gfx_job *job)
{
    gfx_async *a = user;
    int ok = 1;
    SDL_LockMutex(a->m);
    node *n;
    if (!job->prefetch && (n = q_take(&a->prefetch, job->gi, job->epoch)) != NULL) {
        n->job.prefetch = 0;                 /* the game wants it now: to the front of the line */
        q_push(&a->demand, n);
    } else if (q_has(&a->demand, job->gi, job->epoch) || q_has(&a->prefetch, job->gi, job->epoch)) {
        /* already queued */
    } else {
        int running = 0;
        for (int i = 0; i < a->nthr; i++)
            running |= a->running[i] == job->gi && a->running_epoch[i] == job->epoch;
        if (!running) {
            if (job->prefetch && a->prefetch.n >= PREFETCH_QUEUE_MAX) {
                ok = 0;
            } else if ((n = calloc(1, sizeof *n)) == NULL) {
                ok = 0;
            } else {
                n->job = *job;
                q_push(job->prefetch ? &a->prefetch : &a->demand, n);
            }
        }
    }
    if (ok)
        SDL_CondSignal(a->work);
    SDL_UnlockMutex(a->m);
    return ok;
}

static int worker(void *arg)
{
    gfx_async *a = arg;
    int slot = -1;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);   /* the game thread comes first on a small CPU */
    SDL_LockMutex(a->m);
    for (int i = 0; i < a->nthr; i++)
        if (SDL_ThreadID() == SDL_GetThreadID(a->thr[i]))
            slot = i;
    for (;;) {
        node *n = NULL;
        while (!a->quit && !(n = q_pop(&a->demand)) &&
               !(a->done.n < DONE_MAX && (n = q_pop(&a->prefetch)) != NULL))
            SDL_CondWait(a->work, a->m);
        if (a->quit) {
            if (n)
                q_push(&a->demand, n);
            break;
        }
        if (slot >= 0)
            a->running[slot] = n->job.gi, a->running_epoch[slot] = n->job.epoch;
        a->nrunning++;
        SDL_UnlockMutex(a->m);

        Uint64 t0 = SDL_GetPerformanceCounter();
        n->ok = gfx_job_run(&n->job, &n->hi, n->err, sizeof n->err);
        if (a->delay_ms > 0)
            SDL_Delay((Uint32)a->delay_ms);
        n->ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();

        SDL_LockMutex(a->m);
        if (slot >= 0)
            a->running[slot] = NULL;
        a->nrunning--;
        q_push(&a->done, n);
        SDL_CondBroadcast(a->idle);
    }
    SDL_UnlockMutex(a->m);
    return 0;
}

gfx_async *gfx_async_start(gfx_cache *gc, int nthreads, long prefetch_cap)
{
    gfx_async *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    if (nthreads < 1)
        nthreads = 1;
    if (nthreads > MAX_THREADS)
        nthreads = MAX_THREADS;
    a->gc = gc;
    if (SDL_getenv("WWF_ART_DELAY_MS"))
        a->delay_ms = atoi(SDL_getenv("WWF_ART_DELAY_MS"));
    a->m = SDL_CreateMutex();
    a->work = SDL_CreateCond();
    a->idle = SDL_CreateCond();
    if (!a->m || !a->work || !a->idle)
        goto fail;
    a->seen_epoch = gc->epoch;
    gfx_cache_set_async(gc, submit, a, prefetch_cap);
    /* the threads read a->thr to find their slot: hold the lock until all exist */
    SDL_LockMutex(a->m);
    for (int i = 0; i < nthreads; i++) {
        a->thr[i] = SDL_CreateThread(worker, "wwf-art", a);
        if (!a->thr[i])
            break;
        a->nthr++;
    }
    SDL_UnlockMutex(a->m);
    if (a->nthr == 0) {
        gfx_cache_set_async(gc, NULL, NULL, 0);
        goto fail;
    }
    return a;
fail:
    if (a->idle)
        SDL_DestroyCond(a->idle);
    if (a->work)
        SDL_DestroyCond(a->work);
    if (a->m)
        SDL_DestroyMutex(a->m);
    free(a);
    return NULL;
}

void gfx_async_poll(gfx_async *a)
{
    node *list;
    SDL_LockMutex(a->m);
    if (a->seen_epoch != a->gc->epoch) {     /* the cache was reloaded: jobs not started are of no use */
        a->seen_epoch = a->gc->epoch;
        queue *qs[2] = {&a->demand, &a->prefetch};
        for (int i = 0; i < 2; i++) {
            node *n;
            queue keep = {0, 0, 0};
            while ((n = q_pop(qs[i])) != NULL) {
                if (n->job.epoch == a->gc->epoch) {
                    q_push(&keep, n);
                } else {
                    gfx_hi_free(&n->hi);
                    free(n);
                }
            }
            *qs[i] = keep;
        }
    }
    int was_full = a->done.n >= DONE_MAX;
    list = a->done.head;
    memset(&a->done, 0, sizeof a->done);
    if (was_full)
        SDL_CondBroadcast(a->work);
    SDL_UnlockMutex(a->m);
    while (list) {
        node *n = list;
        list = n->next;
        gfx_job_done(a->gc, &n->job, n->ok ? &n->hi : NULL, n->err, n->ms);
        free(n);
    }
    gfx_prefetch_pump(a->gc);
}

void gfx_async_counts(gfx_async *a, int *queued, int *running)
{
    SDL_LockMutex(a->m);
    *queued = a->demand.n + a->prefetch.n;
    *running = a->nrunning;
    SDL_UnlockMutex(a->m);
}

void gfx_async_wait_idle(gfx_async *a)
{
    for (;;) {
        gfx_async_poll(a);
        SDL_LockMutex(a->m);
        int idle = !a->demand.n && !a->prefetch.n && !a->nrunning && !a->done.n && a->gc->plan_n == 0;
        if (!idle && !a->done.n)
            SDL_CondWaitTimeout(a->idle, a->m, 5);
        SDL_UnlockMutex(a->m);
        if (idle)
            return;
    }
}

void gfx_async_stop(gfx_async *a)
{
    if (!a)
        return;
    SDL_LockMutex(a->m);
    a->quit = 1;
    SDL_CondBroadcast(a->work);
    SDL_UnlockMutex(a->m);
    for (int i = 0; i < a->nthr; i++)
        SDL_WaitThread(a->thr[i], NULL);
    gfx_cache_set_async(a->gc, NULL, NULL, 0);   /* pending images go back to "not tried" */
    q_free(&a->demand);
    q_free(&a->prefetch);
    q_free(&a->done);
    SDL_DestroyCond(a->idle);
    SDL_DestroyCond(a->work);
    SDL_DestroyMutex(a->m);
    free(a);
}
