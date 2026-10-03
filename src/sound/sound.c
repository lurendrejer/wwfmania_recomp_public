/*
 * The sound board as a file player (see sound.h and docs/SOUND.md).
 *
 * Commands arrive as 16-bit words, high byte first:
 *   0000             stop everything
 *   55AA, vvXX       master volume vv (XX = ~vv)
 *   55AB+c, vvXX     volume of channel c
 *   `stop` codes     stop the channels listed in sounds.txt
 *   999              revision request (replies as listed)
 *   `sound` codes    play a file on its channel, replacing what was there
 */
#include "sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound/wav.h"
#include "util/fsutil.h"

#define BYPASS_MS 270   /* a byte this soon after reset skips the ROM test */
#define BOOT_TONE_MS 2600

typedef struct {
    char name[32];
    int state;          /* 0 = not loaded, 1 = loaded, -1 = failed */
    wav_data w;
} snd_file;

typedef struct {
    uint16_t code;
    int8_t channel;
    int file;
    uint32_t delay;     /* samples at the table rate */
    long loop_start, loop_len;
} snd_entry;

typedef struct {
    const snd_file *f;
    double pos, step;
    long delay;         /* output frames before it starts */
    long loop_start, loop_end;   /* file frames; loop_start < 0: no loop */
    int crowd;          /* one of the crowd sounds (SND_CROWD_FIRST..LAST) */
} voice;

struct snd {
    float user_master, user_music, user_fx, user_crowd; /* snd_set_gains */
    char dir[512], override_dir[512];
    int rate, out_rate;

    snd_entry *entries;
    int nentries;
    snd_file *files;
    int nfiles, boot_file;
    uint8_t stop_mask[1000];     /* channels stopped by command 0..999 */
    int32_t vol_master[256], vol_channel[256];
    uint16_t reset_reply[8], rev_reply[8];
    int nreset_reply, nrev_reply;

    voice v[SND_CHANNELS];
    int master, chanvol[SND_CHANNELS];

    /* latches */
    int have_high, expect_volume;
    uint8_t high;
    uint16_t reply[16];
    int nreply;
    int in_reset;
    long since_reset;            /* output frames since reset was released, -1 = long ago */
    int bypassed;

    snd_stats stats;
};

/* ---- the table -------------------------------------------------------------------------- */

static int find_file(snd *s, const char *name)
{
    for (int i = 0; i < s->nfiles; i++)
        if (!strcmp(s->files[i].name, name))
            return i;
    snd_file *n = realloc(s->files, (size_t)(s->nfiles + 1) * sizeof *n);
    if (!n)
        return -1;
    s->files = n;
    memset(&n[s->nfiles], 0, sizeof n[0]);
    snprintf(n[s->nfiles].name, sizeof n[0].name, "%s", name);
    return s->nfiles++;
}

static int cmp_entry(const void *a, const void *b)
{
    return (int)((const snd_entry *)a)->code - (int)((const snd_entry *)b)->code;
}

static int parse_volume(const char *p, int32_t *g)
{
    for (int v = 0; v < 256; v++) {
        char *end;
        long x = strtol(p, &end, 10);
        if (end == p)
            return 0;
        g[v] = (int32_t)x;
        p = end;
    }
    return 1;
}

static int load_table(snd *s, char *err, size_t err_len)
{
    char path[1024], line[4096];
    fs_join(path, sizeof path, s->dir, "sounds.txt");
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_len, "cannot read %s (extract the sounds with dcsrip first)", path);
        return 0;
    }
    int cap = 0;
    s->boot_file = -1;
    s->rate = 31250;
    for (int v = 0; v < 256; v++)   /* until the table says otherwise: linear */
        s->vol_master[v] = s->vol_channel[v] = v * 65536 / 255;
    while (fgets(line, sizeof line, f)) {
        char name[64];
        unsigned code;
        int ch, n;
        long delay, ls, ll;
        if (line[0] == '#')
            continue;
        if (sscanf(line, "sound %x %d %63s %ld %ld %ld", &code, &ch, name, &delay, &ls, &ll) == 6) {
            if (s->nentries == cap) {
                cap = cap ? cap * 2 : 1024;
                snd_entry *e = realloc(s->entries, (size_t)cap * sizeof *e);
                if (!e)
                    break;
                s->entries = e;
            }
            snd_entry *e = &s->entries[s->nentries++];
            e->code = (uint16_t)code;
            e->channel = (int8_t)(ch >= 0 && ch < SND_CHANNELS - 1 ? ch : SND_CHANNELS - 1);
            e->file = find_file(s, name);
            e->delay = (uint32_t)(delay > 0 ? delay : 0);
            e->loop_start = ls;
            e->loop_len = ll;
        } else if (sscanf(line, "rate %d", &s->rate) == 1) {
        } else if (sscanf(line, "boot %63s", name) == 1) {
            s->boot_file = find_file(s, name);
        } else if (!strncmp(line, "stop ", 5)) {
            char *p = line + 5, *end;
            long c = strtol(p, &end, 10);
            if (end != p && c >= 0 && c < 1000)
                for (p = end;; p = end) {
                    long k = strtol(p, &end, 10);
                    if (end == p)
                        break;
                    if (k >= 0 && k < SND_CHANNELS)
                        s->stop_mask[c] |= (uint8_t)(1 << k);
                }
        } else if (!strncmp(line, "volume master", 13)) {
            parse_volume(line + 13, s->vol_master);
        } else if (!strncmp(line, "volume channel", 14)) {
            parse_volume(line + 14, s->vol_channel);
        } else if (!strncmp(line, "reply reset", 11)) {
            char *p = line + 11;
            unsigned long at;
            unsigned r;
            while (s->nreset_reply < 8 && sscanf(p, " %lu:%x%n", &at, &r, &n) == 2) {
                s->reset_reply[s->nreset_reply++] = (uint16_t)r;
                p += n;
            }
        } else if (!strncmp(line, "reply 999", 9)) {
            char *p = line + 9;
            unsigned r;
            while (s->nrev_reply < 8 && sscanf(p, " %x%n", &r, &n) == 1) {
                s->rev_reply[s->nrev_reply++] = (uint16_t)r;
                p += n;
            }
        }
    }
    fclose(f);
    if (!s->nentries) {
        snprintf(err, err_len, "%s lists no sounds", path);
        return 0;
    }
    qsort(s->entries, (size_t)s->nentries, sizeof *s->entries, cmp_entry);
    return 1;
}

static const snd_entry *lookup(const snd *s, uint16_t code)
{
    snd_entry key;
    key.code = code;
    return bsearch(&key, s->entries, (size_t)s->nentries, sizeof key, cmp_entry);
}

/* An override with the same name wins over the extracted file. */
static const snd_file *get_file(snd *s, int i)
{
    if (i < 0 || i >= s->nfiles)
        return NULL;
    snd_file *f = &s->files[i];
    if (f->state == 0) {
        char path[1024], err[256];
        f->state = -1;
        if (s->override_dir[0] && fs_resolve_ci(path, sizeof path, s->override_dir, f->name) &&
            wav_load(path, &f->w, err, sizeof err))
            f->state = 2;
        else if (fs_join(path, sizeof path, s->dir, f->name) && wav_load(path, &f->w, err, sizeof err))
            f->state = 1;
        else
            fprintf(stderr, "sound: %s\n", err);
    }
    return f->state > 0 ? f : NULL;
}

int snd_precache(snd *s, size_t max_bytes, size_t *bytes, int *total)
{
    int n = 0;
    size_t b = 0;
    *total = s->nfiles;
    for (int i = 0; i < s->nfiles; i++) {
        const snd_file *f = get_file(s, i);
        if (!f)
            continue;
        size_t sz = f->w.frames * (size_t)f->w.channels * sizeof(int16_t);
        if (max_bytes && b + sz > max_bytes)
            break;
        b += sz;
        n++;
    }
    *bytes = b;
    return n;
}

/* ---- playing ------------------------------------------------------------------------------ */

/* The crowd sounds are the board commands 2048 to 2065 (orig/SOUND.EQU CROWD_BOO..CROWD_BASIC). */
#define SND_CROWD_FIRST 2048
#define SND_CROWD_LAST 2065

static void start(snd *s, int ch, int file, uint32_t delay, long loop_start, long loop_len, int crowd)
{
    const snd_file *f = get_file(s, file);
    voice *v = &s->v[ch];
    memset(v, 0, sizeof *v);
    if (!f || !f->w.frames)
        return;
    v->f = f;
    v->crowd = crowd;
    v->step = (double)f->w.rate / s->out_rate;
    v->delay = (long)((double)delay * s->out_rate / s->rate);
    v->loop_start = -1;
    if (f->state == 2 && f->w.loop_start >= 0) {          /* override with its own loop */
        v->loop_start = f->w.loop_start;
        v->loop_end = f->w.loop_start + f->w.loop_len;
    } else if (loop_start >= 0) {                          /* table loop, in file time */
        double k = (double)f->w.rate / s->rate;
        v->loop_start = lround(loop_start * k);
        v->loop_end = lround((loop_start + loop_len) * k);
        if ((size_t)v->loop_end > f->w.frames)
            v->loop_end = (long)f->w.frames;
        if (v->loop_end <= v->loop_start)
            v->loop_start = -1;
    }
    s->stats.started++;
}

static void stop_channels(snd *s, unsigned mask)
{
    for (int c = 0; c < SND_CHANNELS; c++)
        if (mask & (1u << c))
            s->v[c].f = NULL;
}

static void command(snd *s, uint16_t w)
{
    s->stats.commands++;
    if (s->expect_volume != -2) {
        int v = w >> 8;
        if ((w & 0xFF) == (~v & 0xFF)) {
            if (s->expect_volume < 0)
                s->master = v;
            else if (s->expect_volume < SND_CHANNELS)
                s->chanvol[s->expect_volume] = v;
        }
        s->expect_volume = -2;
        return;
    }
    if (w == 0x55AA) {
        s->expect_volume = -1;
        return;
    }
    if (w >= 0x55AB && w < 0x55AB + 8) {
        s->expect_volume = w - 0x55AB;
        return;
    }
    if (w == 0) {
        stop_channels(s, 0xFFFF);
        return;
    }
    if (w < 1000 && s->stop_mask[w])
        stop_channels(s, s->stop_mask[w]);
    if (w == 999) {
        for (int i = 0; i < s->nrev_reply && s->nreply < 16; i++)
            s->reply[s->nreply++] = s->rev_reply[i];
        return;
    }
    if (w < 1000 && s->stop_mask[w])
        return;
    const snd_entry *e = lookup(s, w);
    if (!e) {
        s->stats.unknown++;
        return;
    }
    start(s, e->channel, e->file, e->delay, e->loop_start, e->loop_len,
          e->code >= SND_CROWD_FIRST && e->code <= SND_CROWD_LAST);
}

void snd_reset_line(snd *s, int asserted)
{
    if (asserted) {
        stop_channels(s, 0xFFFF);
        s->in_reset = 1;
        return;
    }
    if (!s->in_reset)
        return;
    /* the board comes up: ROM test result, then (unless bypassed) a tone */
    s->in_reset = 0;
    s->have_high = 0;
    s->expect_volume = -2;
    s->master = 255;
    for (int c = 0; c < SND_CHANNELS; c++)
        s->chanvol[c] = 255;
    s->nreply = 0;
    for (int i = 0; i < s->nreset_reply && i < 16; i++)
        s->reply[s->nreply++] = s->reset_reply[i];
    s->since_reset = 0;
    s->bypassed = 0;
}

void snd_write(snd *s, uint8_t byte)
{
    if (s->in_reset)
        return;
    if (s->since_reset >= 0 && s->since_reset < (long)s->out_rate * BYPASS_MS / 1000 && !s->bypassed) {
        /* the first byte after a reset skips the diagnostics (no replies, no tone) */
        s->bypassed = 1;
        s->nreply = 0;
        return;
    }
    if (!s->have_high) {
        s->high = byte;
        s->have_high = 1;
        return;
    }
    s->have_high = 0;
    command(s, (uint16_t)(s->high << 8 | byte));
}

int snd_reply_pending(const snd *s) { return s->nreply > 0; }

uint16_t snd_read(snd *s)
{
    if (!s->nreply)
        return 0xFFFF;
    uint16_t r = s->reply[0];
    memmove(s->reply, s->reply + 1, (size_t)--s->nreply * sizeof s->reply[0]);
    return r;
}

/* ---- mixing -------------------------------------------------------------------------------- */

static float frame_at(const voice *v, long i, int c)
{
    const wav_data *w = &v->f->w;
    if (v->loop_start >= 0 && i >= v->loop_end)
        i = v->loop_start + (i - v->loop_end);
    if (i < 0 || (size_t)i >= w->frames)
        return 0;
    return w->samples[(size_t)i * (size_t)w->channels + (size_t)(c < w->channels ? c : 0)];
}

void snd_set_gains(snd *s, float master, float music, float effects, float crowd)
{
    if (!s)
        return;
    s->user_master = master < 0 ? 0 : master > SND_MASTER_MAX ? SND_MASTER_MAX : master;   /* above 1 boosts */
    s->user_music = music < 0 ? 0 : music > 1 ? 1 : music;
    s->user_fx = effects < 0 ? 0 : effects > 1 ? 1 : effects;
    s->user_crowd = crowd < 0 ? 0 : crowd > 1 ? 1 : crowd;
}

/* Up to +-24576 the mix is untouched; above that it is squeezed towards +-32767 (slope 1 at the knee, no libm), so
 * that a master gain above 1 does not clip hard. */
static float soft_limit(float x)
{
    const float knee = 24576.0f, room = 32767.0f - 24576.0f;
    float a = x < 0 ? -x : x;

    if (a <= knee)
        return x;
    a -= knee;
    a = knee + room * a / (room + a);
    return x < 0 ? -a : a;
}

void snd_mix(snd *s, int16_t *out, int frames)
{
    for (int k = 0; k < frames; k++) {
        float l = 0, r = 0;
        if (s->since_reset >= 0) {
            s->since_reset++;
            if (s->since_reset == (long)s->out_rate * BOOT_TONE_MS / 1000) {
                if (!s->bypassed && s->boot_file >= 0 && !s->in_reset)
                    start(s, SND_CHANNELS - 1, s->boot_file, 0, -1, 0, 0);
                s->since_reset = -1;
            }
        }
        for (int c = 0; c < SND_CHANNELS; c++) {
            voice *v = &s->v[c];
            if (!v->f)
                continue;
            if (v->delay > 0) {
                v->delay--;
                continue;
            }
            long i = (long)v->pos;
            float t = (float)(v->pos - (double)i);
            float g = (float)s->vol_channel[s->chanvol[c]] / 65536.0f * (c == 0 ? s->user_music : s->user_fx) * (v->crowd ? s->user_crowd : 1.0f);
            l += g * ((1 - t) * frame_at(v, i, 0) + t * frame_at(v, i + 1, 0));
            r += g * ((1 - t) * frame_at(v, i, 1) + t * frame_at(v, i + 1, 1));
            v->pos += v->step;
            if (v->loop_start >= 0) {
                while (v->pos >= (double)v->loop_end)
                    v->pos -= (double)(v->loop_end - v->loop_start);
            } else if (v->pos >= (double)v->f->w.frames) {
                v->f = NULL;
            }
        }
        float gm = (float)s->vol_master[s->master] / 65536.0f * s->user_master;
        l = soft_limit(l * gm);
        r = soft_limit(r * gm);
        out[2 * k] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * k + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}

snd_stats snd_get_stats(const snd *s) { return s->stats; }

/* ---- setup --------------------------------------------------------------------------------- */

snd *snd_open(const char *dir, const char *override_dir, int out_rate, char *err, size_t err_len)
{
    snd *s = calloc(1, sizeof *s);
    if (!s) {
        snprintf(err, err_len, "out of memory");
        return NULL;
    }
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    if (override_dir)
        snprintf(s->override_dir, sizeof s->override_dir, "%s", override_dir);
    s->out_rate = out_rate > 0 ? out_rate : 48000;
    s->expect_volume = -2;
    s->user_master = s->user_music = s->user_fx = s->user_crowd = 1;
    s->master = 255;
    for (int c = 0; c < SND_CHANNELS; c++)
        s->chanvol[c] = 255;
    s->since_reset = -1;
    if (!load_table(s, err, err_len)) {
        snd_close(s);
        return NULL;
    }
    return s;
}

void snd_close(snd *s)
{
    if (!s)
        return;
    for (int i = 0; i < s->nfiles; i++)
        wav_free(&s->files[i].w);
    free(s->files);
    free(s->entries);
    free(s);
}
