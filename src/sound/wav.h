/* WAV reader: PCM 8/16/24/32-bit or 32-bit float, any rate, 1 or 2 channels. */
#ifndef WWF_WAV_H
#define WWF_WAV_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t *samples;   /* interleaved, frames * channels */
    size_t frames;
    int channels, rate;
    long loop_start;    /* first 'smpl' loop in frames, or -1 */
    long loop_len;
} wav_data;

int wav_decode(const uint8_t *data, size_t size, wav_data *out, char *err, size_t err_len);
int wav_load(const char *path, wav_data *out, char *err, size_t err_len);
void wav_free(wav_data *w);

#endif
