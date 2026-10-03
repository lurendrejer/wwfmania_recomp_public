#include "wav.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/fsutil.h"

static int fail(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
    return 0;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

static int16_t s16(uint32_t v) { return (int16_t)((v & 0x8000) ? (int32_t)(v & 0xFFFF) - 0x10000 : (int32_t)(v & 0xFFFF)); }

static int16_t sample_at(const uint8_t *p, int fmt, int bits)
{
    if (fmt == 3) {   /* IEEE float */
        uint32_t u = rd32(p);
        float f;
        memcpy(&f, &u, sizeof f);
        f *= 32768.0f;
        return (int16_t)(f > 32767.0f ? 32767.0f : f < -32768.0f ? -32768.0f : f);
    }
    switch (bits) {
    case 8: return (int16_t)(((int)p[0] - 128) * 256);
    case 16: return s16(rd16(p));
    case 24: return s16(rd16(p + 1));
    default: return s16(rd16(p + 2));
    }
}

int wav_decode(const uint8_t *d, size_t n, wav_data *out, char *err, size_t err_len)
{
    memset(out, 0, sizeof *out);
    out->loop_start = -1;
    if (n < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0)
        return fail(err, err_len, "not a WAV file");
    int fmt = 0, bits = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    for (size_t p = 12; p + 8 <= n;) {
        uint32_t len = rd32(d + p + 4);
        const uint8_t *c = d + p + 8;
        if (len > n - p - 8)
            len = (uint32_t)(n - p - 8);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            fmt = (int)rd16(c);
            out->channels = (int)rd16(c + 2);
            out->rate = (int)rd32(c + 4);
            bits = (int)rd16(c + 14);
            if (fmt == 0xFFFE && len >= 26)   /* WAVE_FORMAT_EXTENSIBLE */
                fmt = (int)rd16(c + 24);
        } else if (!memcmp(d + p, "data", 4)) {
            data = c;
            data_len = len;
        } else if (!memcmp(d + p, "smpl", 4) && len >= 36 + 24 && rd32(c + 28) > 0) {
            uint32_t start = rd32(c + 36 + 8), end = rd32(c + 36 + 12);
            if (end >= start) {
                out->loop_start = (long)start;
                out->loop_len = (long)(end - start + 1);
            }
        }
        p += 8 + len + (len & 1);
    }
    if (!data || out->channels < 1 || out->channels > 2 || out->rate <= 0)
        return fail(err, err_len, "unsupported WAV layout");
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32)))
        return fail(err, err_len, "unsupported WAV sample format %d/%d bits", fmt, bits);
    size_t bps = (size_t)bits / 8, frame = bps * (size_t)out->channels;
    out->frames = data_len / frame;
    out->samples = malloc(out->frames * (size_t)out->channels * sizeof *out->samples + 1);
    if (!out->samples)
        return fail(err, err_len, "out of memory");
    for (size_t i = 0; i < out->frames * (size_t)out->channels; i++)
        out->samples[i] = sample_at(data + i * bps, fmt, bits);
    if (out->loop_start >= 0 && (size_t)(out->loop_start + out->loop_len) > out->frames)
        out->loop_start = -1;
    return 1;
}

int wav_load(const char *path, wav_data *out, char *err, size_t err_len)
{
    size_t n;
    uint8_t *d = fs_read_file(path, &n);
    if (!d) {
        memset(out, 0, sizeof *out);
        return fail(err, err_len, "cannot read %s", path);
    }
    int ok = wav_decode(d, n, out, err, err_len);
    free(d);
    return ok;
}

void wav_free(wav_data *w)
{
    free(w->samples);
    w->samples = NULL;
}
