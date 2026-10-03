#include "platform/sdl_audio.h"

#include <stdio.h>

int sdl_audio_open(sdl_audio *sa, int rate)
{
    sa->dev = 0;
    sa->rate = rate;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    sa->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!sa->dev) {
        fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(sa->dev, 0);
    return 1;
}

void sdl_audio_queue(sdl_audio *sa, const int16_t *frames, int n)
{
    if (sa->dev && n > 0)
        SDL_QueueAudio(sa->dev, frames, (Uint32)n * 4);
}

int sdl_audio_backlog(const sdl_audio *sa)
{
    return sa->dev ? (int)(SDL_GetQueuedAudioSize(sa->dev) / 4) : 0;
}

void sdl_audio_close(sdl_audio *sa)
{
    if (sa->dev)
        SDL_CloseAudioDevice(sa->dev);
    sa->dev = 0;
}
