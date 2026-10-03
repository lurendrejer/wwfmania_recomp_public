/*
 * Sound player tests with a synthetic table and WAV files.
 *
 *   test_sound <tmpdir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound/sound.h"
#include "sound/wav.h"
#include "util/fsutil.h"

static int failures;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

/* Builds a WAV in buf; returns its size. */
static size_t make_wav(uint8_t *buf, int fmt, int bits, int channels, int rate, const uint8_t *data,
                       size_t data_len, long loop_start, long loop_len)
{
    size_t n = 0;
    memcpy(buf, "RIFF", 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    put32(buf + 16, 16);
    put16(buf + 20, (unsigned)fmt);
    put16(buf + 22, (unsigned)channels);
    put32(buf + 24, (uint32_t)rate);
    put32(buf + 28, (uint32_t)(rate * channels * bits / 8));
    put16(buf + 32, (unsigned)(channels * bits / 8));
    put16(buf + 34, (unsigned)bits);
    memcpy(buf + 36, "data", 4);
    put32(buf + 40, (uint32_t)data_len);
    memcpy(buf + 44, data, data_len);
    n = 44 + data_len + (data_len & 1);
    if (loop_start >= 0) {
        memcpy(buf + n, "smpl", 4);
        put32(buf + n + 4, 60);
        memset(buf + n + 8, 0, 60);
        put32(buf + n + 8 + 28, 1);
        put32(buf + n + 8 + 36 + 8, (uint32_t)loop_start);
        put32(buf + n + 8 + 36 + 12, (uint32_t)(loop_start + loop_len - 1));
        n += 68;
    }
    put32(buf + 4, (uint32_t)(n - 8));
    return n;
}

static int write_file(const char *dir, const char *name, const void *data, size_t len)
{
    char path[1024];
    fs_join(path, sizeof path, dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static void test_wav(void)
{
    static uint8_t buf[4096], data[1024];
    wav_data w;
    char err[256];

    /* 8-bit mono */
    for (int i = 0; i < 4; i++)
        data[i] = (uint8_t)(128 + i);
    size_t n = make_wav(buf, 1, 8, 1, 8000, data, 4, -1, 0);
    CHECK(wav_decode(buf, n, &w, err, sizeof err));
    CHECK(w.frames == 4 && w.channels == 1 && w.rate == 8000 && w.samples[3] == 3 * 256);
    CHECK(w.loop_start == -1);
    wav_free(&w);

    /* 24-bit stereo with a loop */
    for (int i = 0; i < 6 * 4; i++)
        data[i] = (uint8_t)i;
    n = make_wav(buf, 1, 24, 2, 44100, data, 6 * 4, 1, 2);
    CHECK(wav_decode(buf, n, &w, err, sizeof err));
    CHECK(w.frames == 4 && w.channels == 2);
    CHECK(w.samples[0] == 0x0201 && w.samples[1] == 0x0504);
    CHECK(w.loop_start == 1 && w.loop_len == 2);
    wav_free(&w);

    /* 32-bit float */
    float f[2] = {0.5f, -1.0f};
    memcpy(data, f, sizeof f);
    n = make_wav(buf, 3, 32, 1, 48000, data, sizeof f, -1, 0);
    CHECK(wav_decode(buf, n, &w, err, sizeof err));
    CHECK(w.frames == 2 && w.samples[0] == 16384 && w.samples[1] == -32768);
    wav_free(&w);

    CHECK(!wav_decode((const uint8_t *)"RIFF0000WAVX", 12, &w, err, sizeof err));
}

static void write_board(const char *dir)
{
    static uint8_t buf[8192], data[4096];
    /* a.wav: 100 frames of 1000; loop.wav: 0, 1, 2, ... 49 */
    for (int i = 0; i < 100; i++)
        put16(data + 2 * i, 1000);
    size_t n = make_wav(buf, 1, 16, 1, 1000, data, 200, -1, 0);
    CHECK(write_file(dir, "a.wav", buf, n));
    for (int i = 0; i < 50; i++)
        put16(data + 2 * i, (unsigned)i);
    n = make_wav(buf, 1, 16, 1, 1000, data, 100, -1, 0);
    CHECK(write_file(dir, "loop.wav", buf, n));

    char text[8192];
    size_t len = (size_t)snprintf(text, sizeof text,
                                  "# test\nrate 1000\nreply reset 0:0079 0:0001\nreply 999 0001\n"
                                  "stop 993 0\nstop 994 1\nvolume master");
    for (int v = 0; v < 256; v++)
        len += (size_t)snprintf(text + len, sizeof text - len, " %d", v * 256);
    len += (size_t)snprintf(text + len, sizeof text - len,
                            "\nsound 0001 0 loop.wav 0 10 30\nsound 0080 1 a.wav 5 -1 0\nsound 0800 2 a.wav 5 -1 0\n");
    CHECK(write_file(dir, "sounds.txt", text, len));
}

static void word(snd *s, uint16_t w)
{
    snd_write(s, (uint8_t)(w >> 8));
    snd_write(s, (uint8_t)(w & 0xFF));
}

static void test_player(const char *dir)
{
    write_board(dir);
    char err[256];
    snd *s = snd_open(dir, NULL, 1000, err, sizeof err);
    CHECK(s != NULL);
    if (!s) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    int16_t out[2 * 200];

    /* reset: the ROM test result is waiting; a quick byte bypasses it */
    snd_reset_line(s, 1);
    snd_reset_line(s, 0);
    CHECK(snd_reply_pending(s));
    CHECK(snd_read(s) == 0x79 && snd_read(s) == 0x01 && !snd_reply_pending(s));
    snd_reset_line(s, 1);
    snd_reset_line(s, 0);
    snd_write(s, 0);
    CHECK(!snd_reply_pending(s));

    /* an effect on channel 1, after its start delay; stop 994 ends it */
    word(s, 0x0080);
    snd_mix(s, out, 10);
    CHECK(out[0] == 0 && out[2 * 4] == 0);
    CHECK(out[2 * 5] == 1000 * 255 / 256 && out[2 * 5 + 1] == out[2 * 5]);
    word(s, 994);
    snd_mix(s, out, 2);
    CHECK(out[0] == 0);

    /* master volume */
    word(s, 0x55AA);
    word(s, 0x807F);
    word(s, 0x0080);
    snd_mix(s, out, 6);
    CHECK(out[2 * 5] == 1000 * 128 / 256);
    word(s, 0x55AA);
    word(s, 0xFF00);
    word(s, 994);

    /* a loop: 0..39, then 10..39 again */
    word(s, 0x0001);
    snd_mix(s, out, 100);
    CHECK(out[2 * 39] == 39 * 255 / 256 && out[2 * 40] == 10 * 255 / 256);
    CHECK(out[2 * 70] == out[2 * 40]);
    word(s, 993);
    snd_mix(s, out, 1);
    CHECK(out[0] == 0 && out[1] == 0);

    /* the listener's volumes: effects halved, then everything muted */
    word(s, 0x0080);
    snd_set_gains(s, 1.0f, 1.0f, 0.5f, 1.0f);
    snd_mix(s, out, 6);
    CHECK(out[2 * 5] == 1000 * 255 / 256 / 2 || out[2 * 5] == 1000 * 255 / 256 / 2 - 1);
    snd_set_gains(s, 0.0f, 1.0f, 1.0f, 1.0f);
    snd_mix(s, out, 2);
    CHECK(out[0] == 0 && out[1] == 0);
    snd_set_gains(s, 1.0f, 1.0f, 1.0f, 1.0f);
    word(s, 994);
    snd_mix(s, out, 1);

    /* the crowd (commands 2048 to 2065) has its own volume on top of the effects */
    word(s, 0x0800);
    snd_set_gains(s, 1.0f, 1.0f, 1.0f, 0.5f);
    snd_mix(s, out, 6);
    CHECK(out[2 * 5] == 1000 * 255 / 256 / 2 || out[2 * 5] == 1000 * 255 / 256 / 2 - 1);
    snd_set_gains(s, 1.0f, 1.0f, 1.0f, 1.0f);

    /* revision request, unknown codes, stop all */
    word(s, 999);
    CHECK(snd_read(s) == 0x0001);
    word(s, 0x1234);
    word(s, 0x0001);
    word(s, 0x0000);
    snd_mix(s, out, 1);
    CHECK(out[0] == 0);
    snd_stats st = snd_get_stats(s);
    CHECK(st.unknown == 1 && st.started == 6);
    snd_close(s);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_sound <tmpdir>\n");
        return 2;
    }
    test_wav();
    test_player(argv[1]);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all sound tests passed\n");
    return 0;
}
