/*
 * Input recording and replay. A recording is the player inputs of every frame from a save state on;
 * replaying loads that state and feeds the inputs back, which reproduces the game exactly because the
 * machine is deterministic (checked in docs/OPTIONS.md).
 */
#ifndef WWF_REPLAY_H
#define WWF_REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "wolf.h"

typedef struct {
    uint8_t player[4];
    uint16_t coin_bits, extra_player[2];
} replay_frame;

typedef struct {
    enum { REPLAY_IDLE, REPLAY_RECORDING, REPLAY_PLAYING } mode;
    replay_frame *frames;
    size_t count, cap, pos;
} replay;

/* Recording starts empty. */
void replay_start_record(replay *r);
/* Ends a recording and writes it to path. Returns 0 if it could not be written. */
int replay_stop_record(replay *r, const char *path);
/* Reads path and starts playing it. Returns 0 if the file is not a recording. */
int replay_start_play(replay *r, const char *path);
/* Once per frame, after the inputs were read and before the frame runs: records them, or replaces
 * them with the recorded ones. Playing ends by itself after the last frame (mode IDLE). */
void replay_frame_inputs(replay *r, wolf *w);
void replay_free(replay *r);

#endif
