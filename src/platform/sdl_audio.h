/* SDL2 audio output for the sound mixer (queued from the main loop). */
#ifndef WWF_SDL_AUDIO_H
#define WWF_SDL_AUDIO_H

#include <SDL.h>

typedef struct {
    SDL_AudioDeviceID dev;
    int rate;
} sdl_audio;

/* Opens the default output as 16-bit stereo. Returns 0 on failure. */
int sdl_audio_open(sdl_audio *sa, int rate);

/* Queues frames (interleaved stereo int16) for playback. */
void sdl_audio_queue(sdl_audio *sa, const int16_t *frames, int n);

/* Frames queued but not yet played. */
int sdl_audio_backlog(const sdl_audio *sa);

void sdl_audio_close(sdl_audio *sa);

#endif
