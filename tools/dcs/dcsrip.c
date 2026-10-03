/*
 * dcsrip - extract the sounds of the DCS sound board to WAV files.
 *
 *   dcsrip <roms> <outdir> [--codes FIRST-LAST] [--max SECONDS] [--names ASM] [-v]
 *
 * <roms> is a directory or a MAME zip holding the sound ROMs (*.u2 .. *.u5).
 * --names (default orig/DCSSOUND.ASM, if present) adds the names from the
 * game's sound table to sounds.txt as comments.
 *
 * The board's own program runs on an ADSP-2105 emulation, once per sound
 * code, and whatever reaches the DAC is recorded. The result goes to
 * <outdir>:
 *   sounds.txt   what every code does (format in docs/SOUND.md)
 *   XXXX.wav     one file per distinct sound, named after the first code
 *                that plays it (31250 Hz, mono, 16 bit; loops also carry a
 *                'smpl' chunk)
 *   boot.wav     the power-up tone
 *
 * The game itself does not emulate the board; it plays these files.
 *
 * Loops are found exactly: after every block of output the board's RAM is
 * hashed (minus a few free-running counters, found while idle). When a
 * state comes back, everything from then on repeats. A repeat of silence
 * means the sound has ended.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcs.h"
#include "romset.h"

#define RATE 31250
#define SILENT 2            /* |sample| <= this counts as silence */
#define MAX_CODE 0x1FFF

static dcs board, snap;     /* the live board; the state after boot */
static int verbose;

/* ---- recording ------------------------------------------------------------------- */

typedef struct {
    int16_t *s;
    size_t n, cap;
} pcm;

static void pcm_add(pcm *p, const int16_t *s, size_t n)
{
    if (!n)
        return;
    if (p->n + n > p->cap) {
        size_t c = p->cap ? p->cap : 1 << 16;
        while (c < p->n + n)
            c *= 2;
        int16_t *q = realloc(p->s, c * sizeof *q);
        if (!q) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        p->s = q;
        p->cap = c;
    }
    memcpy(p->s + p->n, s, n * sizeof *s);
    p->n += n;
}

static pcm rec;             /* everything the DAC produced since restore() */

static void collect(void)
{
    size_t n;
    const int16_t *s = dcs_take_samples(&board, &n);
    pcm_add(&rec, s, n);
}

static void run(int64_t cycles)
{
    dcs_run(&board, cycles);
    collect();
}

/* Runs to the end of the next block of output. */
static void run_block(void)
{
    if (board.ab_on && board.ab_next > board.now)
        run(board.ab_next - board.now);
    else
        run(DCS_CLOCK / 1000);
}

static void restore(void)
{
    int16_t *out = board.out;
    size_t cap = board.cap;
    board = snap;
    board.out = out;
    board.cap = cap;
    board.nout = 0;
    rec.n = 0;
}

static void send_byte(int b)
{
    dcs_write(&board, (uint8_t)b);
    for (int i = 0; i < 1000 && !(dcs_status(&board) & 0x0800); i++)
        run(50);
}

static void send(int code)
{
    send_byte(code >> 8);
    send_byte(code & 0xFF);
}

static void set_volume(int cmd, int v)
{
    send(cmd);
    send(v << 8 | (~v & 0xFF));
}

/* ---- state hashing ----------------------------------------------------------------- */

static uint8_t clock_word[0x800];   /* data RAM words that tick on their own */

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

static uint64_t state_hash(void)
{
    uint16_t lo[0x800];
    for (int i = 0; i < 0x800; i++)
        lo[i] = clock_word[i] ? 0 : board.dm_lo[i];
    uint64_t h = fnv(0xCBF29CE484222325ull, lo, sizeof lo);
    h = fnv(h, board.dm_hi, 0x200 * sizeof board.dm_hi[0]);
    h = fnv(h, board.cpu.pm + 0x800, 0x1800 * sizeof board.cpu.pm[0]);
    return fnv(h, &board.bank, sizeof board.bank);
}

/* Words of data RAM that change while the board sits idle are clocks. */
static int find_clock_words(void)
{
    static uint16_t first[0x800];
    int n = 0;
    memcpy(first, board.dm_lo, sizeof first);
    for (int f = 0; f < 400; f++) {
        run_block();
        for (int i = 0; i < 0x800; i++)
            if (board.dm_lo[i] != first[i] && !clock_word[i]) {
                clock_word[i] = 1;
                n++;
            }
    }
    return n;
}

#define HT_BITS 18
static struct {
    uint64_t hash;
    uint32_t gen, frame;
} ht[1u << HT_BITS];
static uint32_t ht_gen;

/* Returns the frame that had this state, or -1 (and remembers this one). */
static long ht_check(uint64_t h, uint32_t frame)
{
    uint32_t i = (uint32_t)(h >> (64 - HT_BITS));
    for (;;) {
        if (ht[i].gen != ht_gen) {
            ht[i].gen = ht_gen;
            ht[i].hash = h;
            ht[i].frame = frame;
            return -1;
        }
        if (ht[i].hash == h)
            return ht[i].frame;
        i = (i + 1) & ((1u << HT_BITS) - 1);
    }
}

/* ---- rendering one code ------------------------------------------------------------ */

typedef struct {
    size_t first;          /* first audible sample */
    size_t end;            /* one past the last sample to keep */
    long loop_start;       /* -1: plays once */
    long loop_len;
    int timed_out;
} take_info;

static int silent(const int16_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] > SILENT || s[i] < -SILENT)
            return 0;
    return 1;
}

/* Plays code from the snapshot until it ends or loops. Audio is in rec. */
static void render(int code, double max_s, take_info *t)
{
    static size_t *mark;
    static size_t mark_cap;
    restore();
    ht_gen++;
    send(code);
    memset(t, 0, sizeof *t);
    t->loop_start = -1;
    size_t max_n = (size_t)(max_s * RATE);
    for (uint32_t f = 0;; f++) {
        run_block();
        if (f >= mark_cap) {
            mark_cap = mark_cap ? mark_cap * 2 : 1 << 16;
            mark = realloc(mark, mark_cap * sizeof *mark);
            if (!mark)
                exit(1);
        }
        mark[f] = rec.n;
        long prev = ht_check(state_hash(), f);
        if (prev >= 0) {
            size_t a = mark[prev], b = mark[f];
            if (silent(rec.s + a, b - a)) {
                t->end = a;
            } else {
                t->loop_start = (long)a;
                t->loop_len = (long)(b - a);
                t->end = b;
            }
            break;
        }
        if (rec.n >= max_n) {
            t->timed_out = 1;
            t->end = rec.n;
            break;
        }
    }
    /* trim silence at both ends (a loop keeps its whole body) */
    size_t first = 0;
    while (first < t->end && rec.s[first] <= SILENT && rec.s[first] >= -SILENT)
        first++;
    if (t->loop_start >= 0 && (size_t)t->loop_start < first)
        first = (size_t)t->loop_start;
    if (t->loop_start < 0)
        while (t->end > first && rec.s[t->end - 1] <= SILENT && rec.s[t->end - 1] >= -SILENT)
            t->end--;
    t->first = first;
}

/* Loudness of samples [a, b) after sending code, with one channel muted
 * (volume changes ramp, so the window should start a little late). */
static double level_after(int code, size_t a, size_t b, int mute_channel)
{
    restore();
    if (mute_channel >= 0)
        set_volume(0x55AB + mute_channel, 0);
    size_t start = rec.n;
    send(code);
    while (rec.n < start + b)
        run_block();
    double e = 0;
    for (size_t i = start + a; i < start + b; i++)
        e += (double)rec.s[i] * rec.s[i];
    return b > a ? sqrt(e / (double)(b - a)) : 0;
}

/* ---- output ----------------------------------------------------------------------- */

static void put16(FILE *f, unsigned v) { fputc((int)(v & 0xFF), f); fputc((int)((v >> 8) & 0xFF), f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static int write_wav(const char *path, const int16_t *s, size_t n, long loop_start, long loop_len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    uint32_t data = (uint32_t)(n * 2), smpl = loop_start >= 0 ? 36 + 24 : 0;
    fwrite("RIFF", 1, 4, f);
    put32(f, 4 + (8 + 16) + (8 + data) + (smpl ? 8 + smpl : 0));
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, RATE);
    put32(f, RATE * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, data);
    for (size_t i = 0; i < n; i++)
        put16(f, (uint16_t)s[i]);
    if (smpl) {
        fwrite("smpl", 1, 4, f);
        put32(f, smpl);
        put32(f, 0);                                   /* manufacturer */
        put32(f, 0);                                   /* product */
        put32(f, (uint32_t)(1000000000.0 / RATE));     /* sample period (ns) */
        put32(f, 60);                                  /* MIDI unity note */
        put32(f, 0);                                   /* pitch fraction */
        put32(f, 0);                                   /* SMPTE format */
        put32(f, 0);                                   /* SMPTE offset */
        put32(f, 1);                                   /* loops */
        put32(f, 0);                                   /* sampler data */
        put32(f, 0);                                   /* cue id */
        put32(f, 0);                                   /* forward loop */
        put32(f, (uint32_t)loop_start);
        put32(f, (uint32_t)(loop_start + loop_len - 1)); /* inclusive end */
        put32(f, 0);
        put32(f, 0);                                   /* play count: forever */
    }
    return fclose(f) == 0;
}

static void join(char *out, size_t len, const char *dir, const char *name)
{
    snprintf(out, len, "%s/%s", dir, name);
}

/* ---- names from DCSSOUND.ASM ------------------------------------------------------------ */

/* Lines like  .word sp_smack|17,>80 ; 1 = face hit #0  name code 0x80. */
static char *names[0x10000];

static void load_names(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *w = strstr(line, ".word");
        char *semi = strchr(line, ';');
        if (!w || !semi || semi < w || !strstr(w, "sp_"))
            continue;
        char *comma = strchr(w, ',');
        if (!comma || comma > semi)
            continue;
        char *p = comma + 1, *end;
        while (*p == ' ' || *p == '\t')
            p++;
        long code;
        if (*p == '>') {
            code = strtol(p + 1, &end, 16);
        } else {
            code = strtol(p, &end, 10);
            if (*end == 'h' || *end == 'H')
                code = strtol(p, &end, 16);
        }
        if (end == p || code <= 0 || code > 0xFFFF || names[code])
            continue;
        /* the comment, without its "index =" prefix */
        char *c = strchr(semi, '=');
        c = c ? c + 1 : semi + 1;
        while (*c == ' ' || *c == '\t')
            c++;
        size_t n = strcspn(c, "\r\n");
        while (n && (c[n - 1] == ' ' || c[n - 1] == '\t'))
            n--;
        if (!n)
            continue;
        names[code] = malloc(n + 1);
        if (names[code]) {
            memcpy(names[code], c, n);
            names[code][n] = 0;
        }
    }
    fclose(f);
}

/* Effect codes come in fours (one per channel); the table names the first. */
static const char *name_of(int code)
{
    if (names[code])
        return names[code];
    return code >= 0x80 ? names[code & ~3] : NULL;
}

/* ---- main --------------------------------------------------------------------------- */

typedef struct {
    int code, channel, file;   /* file: index of the code whose WAV is shared */
    uint64_t hash;
    size_t delay, len;
    long loop_start, loop_len;
} sound;

static double rms(const int16_t *s, size_t n)
{
    double e = 0;
    for (size_t i = 0; i < n; i++)
        e += (double)s[i] * s[i];
    return n ? sqrt(e / (double)n) : 0;
}

/* Gain of each volume setting (0-255) of command cmd, relative to 255. */
static void measure_volume(int cmd, int code, double gain[256])
{
    size_t skip = RATE / 3, win = RATE / 4;
    double ref = 0;
    for (int v = 255; v >= 0; v--) {
        restore();
        set_volume(cmd, v);
        size_t start = rec.n;
        send(code);
        while (rec.n < start + skip + win)
            run_block();
        double r = rms(rec.s + start + skip, win);
        if (v == 255)
            ref = r;
        gain[v] = ref > 0 ? r / ref : 0;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: dcsrip <roms dir or zip> <outdir> [--codes FIRST-LAST] "
                        "[--max SECONDS] [--names ASM] [-v]\n");
        return 2;
    }
    const char *roms = argv[1], *outdir = argv[2], *names_path = "orig/DCSSOUND.ASM";
    int lo = 1, hi = MAX_CODE;
    double max_s = 600;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--codes") && i + 1 < argc) {
            if (sscanf(argv[++i], "%i-%i", &lo, &hi) != 2)
                lo = hi = (int)strtol(argv[i], NULL, 0);
        } else if (!strcmp(argv[i], "--max") && i + 1 < argc) {
            max_s = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--names") && i + 1 < argc) {
            names_path = argv[++i];
        } else if (!strcmp(argv[i], "-v")) {
            verbose = 1;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    load_names(names_path);
    dcs_romset rs;
    char err[512];
    if (!dcs_romset_load(&rs, roms, err, sizeof err)) {
        fprintf(stderr, "dcsrip: %s\n", err);
        return 1;
    }
    if (!rs.known)
        fprintf(stderr, "warning: these are not the WWF WrestleMania L1 sound ROMs "
                        "(CRC %08X %08X %08X %08X)\n", rs.crc[0], rs.crc[1], rs.crc[2], rs.crc[3]);
    if (!dcs_init(&board, (const uint8_t *const *)rs.data, DCS_NROMS)) {
        fprintf(stderr, "dcsrip: cannot set up the board\n");
        return 1;
    }
    dcs_romset_free(&rs);

    char path[1024];
    join(path, sizeof path, outdir, "sounds.txt");
    FILE *txt = fopen(path, "w");
    if (!txt) {
        fprintf(stderr, "dcsrip: cannot write %s (does the directory exist?)\n", path);
        return 1;
    }

    /* power up: the board checks its ROMs, reports, and plays a tone */
    dcs_reset_line(&board, 1);
    dcs_reset_line(&board, 0);
    int replies[8], nreplies = 0;
    size_t reply_at[8];
    rec.n = 0;
    while (board.now < 8LL * DCS_CLOCK) {
        run_block();
        if (!(dcs_status(&board) & 0x0400) && nreplies < 8) {
            reply_at[nreplies] = rec.n;
            replies[nreplies++] = dcs_read(&board);
        }
    }
    {
        size_t a = 0, b = rec.n;
        while (a < b && silent(rec.s + a, 1)) a++;
        while (b > a && silent(rec.s + b - 1, 1)) b--;
        join(path, sizeof path, outdir, "boot.wav");
        write_wav(path, rec.s + a, b - a, -1, 0);
    }
    rec.n = 0;
    int nclock = find_clock_words();
    if (verbose)
        printf("boot: %d replies, %d clock words\n", nreplies, nclock);
    set_volume(0x55AA, 255);
    run(DCS_CLOCK / 10);
    snap = board;
    snap.out = NULL;
    snap.cap = snap.nout = 0;

    /* the revision request */
    restore();
    send(999);
    int rev[4], nrev = 0;
    while (rec.n < RATE / 5) {
        run_block();
        if (!(dcs_status(&board) & 0x0400) && nrev < 4)
            rev[nrev++] = dcs_read(&board);
    }

    fprintf(txt, "# WWF WrestleMania DCS sounds, extracted by tools/dcs/dcsrip.\n"
                 "# Format: docs/SOUND.md. Times and lengths are samples at `rate`.\n");
    fprintf(txt, "rate %d\n", RATE);
    fprintf(txt, "boot boot.wav\n");
    fprintf(txt, "reply reset");
    for (int i = 0; i < nreplies; i++)
        fprintf(txt, " %zu:%04X", reply_at[i], (unsigned)replies[i]);
    fprintf(txt, "\nreply 999");
    for (int i = 0; i < nrev; i++)
        fprintf(txt, " %04X", (unsigned)rev[i]);
    fprintf(txt, "\n");

    /* every code */
    sound *snd = calloc((size_t)(hi - lo + 1), sizeof *snd);
    int nsnd = 0, group_code = -1;
    pcm group = {NULL, 0, 0};   /* the start of the first sound of a group of four */
    size_t total = 0;
    for (int code = lo; code <= hi; code++) {
        if (code >= 990 && code <= 999)
            continue;   /* commands (stop, revision) */
        sound *s = &snd[nsnd];
        take_info t;
        int reused = 0;
        /* Effects come in fours, one per channel, normally the same sound:
         * when a variant starts exactly like the first one, reuse it. */
        if (code >= 0x80 && (code & 3) && group.n && group_code == (code & ~3) && nsnd &&
            snd[nsnd - 1].code == code - 1) {
            restore();
            send(code);
            while (rec.n < group.n)
                run_block();
            if (!memcmp(rec.s, group.s, group.n * sizeof *group.s)) {
                *s = snd[nsnd - 1];
                s->code = code;
                reused = 1;
                t.first = s->delay;
            }
        }
        if (!reused) {
            render(code, max_s, &t);
            if ((code & 3) == 0)
                group.n = 0;
            if (t.end <= t.first)
                continue;
            if ((code & 3) == 0 && code >= 0x80) {
                group.n = 0;
                pcm_add(&group, rec.s, t.end < t.first + RATE ? t.end : t.first + RATE);
                group_code = code;
            }
            s->code = code;
            s->delay = t.first;
            s->len = t.end - t.first;
            s->loop_start = t.loop_start >= 0 ? t.loop_start - (long)t.first : -1;
            s->loop_len = t.loop_len;
            s->hash = fnv(fnv(0xCBF29CE484222325ull, rec.s + t.first, s->len * 2), &s->loop_start,
                          sizeof s->loop_start);
            s->file = nsnd;
            for (int k = 0; k < nsnd; k++)
                if (snd[k].hash == s->hash && snd[k].len == s->len) {
                    s->file = snd[k].file;
                    break;
                }
            if (s->file == nsnd) {
                char name[32];
                snprintf(name, sizeof name, "%04X.wav", code);
                join(path, sizeof path, outdir, name);
                if (!write_wav(path, rec.s + t.first, s->len, s->loop_start, s->loop_len)) {
                    fprintf(stderr, "dcsrip: cannot write %s\n", path);
                    return 1;
                }
                total += s->len;
            }
            if (t.timed_out)
                fprintf(stderr, "warning: code %04X neither ended nor looped within %.0f s\n", code, max_s);
        }

        /* which channel: muting it silences the sound */
        size_t a = t.first + (s->len > RATE / 10 ? RATE / 20 : 0);
        size_t b = t.first + (s->len < RATE / 3 ? s->len : RATE / 3);
        double base = level_after(code, a, b, -1);
        int order[6] = {code < 0x80 ? 0 : (code & 3) + 1, 0, 1, 2, 3, 4};
        s->channel = -1;
        for (int k = 0; k < 6 && s->channel < 0 && base > 0; k++)
            if (k == 0 || order[k] != order[0])
                if (level_after(code, a, b, order[k]) < base * 0.05)
                    s->channel = order[k];
        nsnd++;
        if (verbose)
            printf("%04X ch %d  %6.2f s%s%s\n", code, s->channel, (double)s->len / RATE,
                   s->loop_start >= 0 ? "  loop" : "", s->file != nsnd - 1 ? "  (same as before)" : "");
        else if ((code & 0xFF) == 0)
            fprintf(stderr, "code %04X...\n", code);
    }

    /* the stop commands: which channels each one silences. Each channel is
     * tried with its longest sound, which must still play without the
     * command (the control run). */
    int probe[5];
    for (int ch = 0; ch <= 4; ch++) {
        probe[ch] = -1;
        for (int k = 0; k < nsnd; k++)
            if (snd[k].channel == ch &&
                (probe[ch] < 0 || snd[k].loop_start >= 0 ||
                 (snd[probe[ch]].loop_start < 0 && snd[k].len > snd[probe[ch]].len)))
                probe[ch] = k;
    }
    fprintf(txt, "# stop CODE CHANNELS... (0 is always \"stop everything\")\n");
    for (int cmd = 990; cmd <= 999; cmd++) {
        int mask = 0;
        for (int ch = 0; ch <= 4; ch++) {
            if (probe[ch] < 0)
                continue;
            int stopped = 1;
            for (int control = 1; control >= 0; control--) {
                restore();
                send(snd[probe[ch]].code);
                while (rec.n < snd[probe[ch]].delay + RATE / 10)
                    run_block();
                if (!control)
                    send(cmd);
                size_t a = rec.n + RATE / 20;
                while (rec.n < a + RATE / 5)
                    run_block();
                int quiet = silent(rec.s + a, RATE / 5);
                if (control && quiet) {
                    stopped = 0;   /* the sound is over by then anyway */
                    break;
                }
                if (!control)
                    stopped = quiet;
            }
            if (stopped)
                mask |= 1 << ch;
        }
        if (mask) {
            fprintf(txt, "stop %d", cmd);
            for (int ch = 0; ch <= 4; ch++)
                if (mask & (1 << ch))
                    fprintf(txt, " %d", ch);
            fprintf(txt, "\n");
        }
    }

    /* volume curves: master (55AA) and channel (55AB + channel) */
    int ref = -1;
    for (int k = 0; k < nsnd; k++)
        if (snd[k].channel == 0 && snd[k].loop_start >= 0) {
            ref = snd[k].code;
            break;
        }
    if (ref >= 0) {
        double g[256];
        measure_volume(0x55AA, ref, g);
        fprintf(txt, "# gain of volume settings 0..255 (x 65536)\nvolume master");
        for (int v = 0; v < 256; v++)
            fprintf(txt, " %ld", lround(g[v] * 65536));
        measure_volume(0x55AB, ref, g);
        fprintf(txt, "\nvolume channel");
        for (int v = 0; v < 256; v++)
            fprintf(txt, " %ld", lround(g[v] * 65536));
        fprintf(txt, "\n");
    }

    fprintf(txt, "# code channel file delay loop_start loop_length\n");
    for (int k = 0; k < nsnd; k++) {
        const sound *s = &snd[k];
        fprintf(txt, "sound %04X %d %04X.wav %zu %ld %ld", (unsigned)s->code, s->channel,
                (unsigned)snd[s->file].code, s->delay, s->loop_start, s->loop_len);
        const char *nm = name_of(s->code);
        fprintf(txt, nm ? "  # %s\n" : "\n", nm);
    }
    fclose(txt);
    int files = 0;
    for (int k = 0; k < nsnd; k++)
        files += snd[k].file == k;
    printf("dcsrip: %d codes, %d files, %.1f minutes of audio\n", nsnd, files, (double)total / RATE / 60);
    free(snd);
    free(group.s);
    free(rec.s);
    dcs_free(&board);
    return 0;
}
