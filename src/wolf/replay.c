#include "replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "WWFR1"
#define REC_BYTES 10

void replay_free(replay *r)
{
    free(r->frames);
    memset(r, 0, sizeof *r);
}

void replay_start_record(replay *r)
{
    replay_free(r);
    r->mode = REPLAY_RECORDING;
}

int replay_stop_record(replay *r, const char *path)
{
    FILE *f;
    int ok;

    r->mode = REPLAY_IDLE;
    f = fopen(path, "wb");
    if (!f)
        return 0;
    ok = fwrite(MAGIC, 1, 5, f) == 5;
    for (size_t i = 0; ok && i < 4; i++)
        ok = fputc((int)((r->count >> (8 * i)) & 0xFF), f) != EOF;
    for (size_t i = 0; ok && i < r->count; i++) {
        const replay_frame *fr = &r->frames[i];
        uint8_t b[REC_BYTES] = {fr->player[0], fr->player[1], fr->player[2], fr->player[3],
                                (uint8_t)fr->coin_bits, (uint8_t)(fr->coin_bits >> 8),
                                (uint8_t)fr->extra_player[0], (uint8_t)(fr->extra_player[0] >> 8),
                                (uint8_t)fr->extra_player[1], (uint8_t)(fr->extra_player[1] >> 8)};
        ok = fwrite(b, 1, sizeof b, f) == sizeof b;
    }
    return fclose(f) == 0 && ok;
}

int replay_start_play(replay *r, const char *path)
{
    FILE *f = fopen(path, "rb");
    char magic[5];
    uint8_t n[4];
    size_t count;

    replay_free(r);
    if (!f)
        return 0;
    if (fread(magic, 1, 5, f) != 5 || memcmp(magic, MAGIC, 5) || fread(n, 1, 4, f) != 4) {
        fclose(f);
        return 0;
    }
    count = (size_t)n[0] | (size_t)n[1] << 8 | (size_t)n[2] << 16 | (size_t)n[3] << 24;
    if (count > 10u * 60 * 60 * 60) {           /* ten hours of frames: not a recording */
        fclose(f);
        return 0;
    }
    r->frames = calloc(count ? count : 1, sizeof *r->frames);
    if (!r->frames) {
        fclose(f);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        uint8_t b[REC_BYTES];

        if (fread(b, 1, sizeof b, f) != sizeof b) {
            fclose(f);
            replay_free(r);
            return 0;
        }
        memcpy(r->frames[i].player, b, 4);
        r->frames[i].coin_bits = (uint16_t)(b[4] | b[5] << 8);
        r->frames[i].extra_player[0] = (uint16_t)(b[6] | b[7] << 8);
        r->frames[i].extra_player[1] = (uint16_t)(b[8] | b[9] << 8);
    }
    fclose(f);
    r->count = count;
    r->pos = 0;
    r->mode = REPLAY_PLAYING;
    return 1;
}

void replay_frame_inputs(replay *r, wolf *w)
{
    if (r->mode == REPLAY_RECORDING) {
        replay_frame *fr;

        if (r->count == r->cap) {
            size_t cap = r->cap ? r->cap * 2 : 4096;
            replay_frame *n = realloc(r->frames, cap * sizeof *n);

            if (!n)
                return;
            r->frames = n;
            r->cap = cap;
        }
        fr = &r->frames[r->count++];
        memcpy(fr->player, w->player, 4);
        fr->coin_bits = w->coin_bits;
        memcpy(fr->extra_player, w->extra_player, sizeof fr->extra_player);
    } else if (r->mode == REPLAY_PLAYING) {
        if (r->pos >= r->count) {
            r->mode = REPLAY_IDLE;
            return;
        }
        memcpy(w->player, r->frames[r->pos].player, 4);
        w->coin_bits = r->frames[r->pos].coin_bits;
        memcpy(w->extra_player, r->frames[r->pos].extra_player, sizeof w->extra_player);
        r->pos++;
    }
}
