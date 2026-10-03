/*
 * The DCS sound board, played from extracted files instead of emulated.
 *
 * tools/dcs/dcsrip turns the sound ROMs into WAV files and a table
 * (sounds.txt, see docs/SOUND.md) that says what every command does. This
 * module takes the bytes the game writes to the sound board and mixes those
 * files: five channels (0 = music, 1-4 = effects and speech), a channel
 * volume each and a master volume, as the board did.
 *
 * Any file can be replaced by putting a file with the same name in the
 * override directory (any rate, mono or stereo; a WAV 'smpl' loop, when
 * present, replaces the original loop points).
 */
#ifndef WWF_SOUND_H
#define WWF_SOUND_H

#include <stddef.h>
#include <stdint.h>

#define SND_CHANNELS 6   /* the board's 5 channels plus one for unknown ones */

typedef struct snd snd;

/*
 * Loads dir/sounds.txt. The WAV files are read when first played.
 * out_rate is the mixing rate. Returns NULL and fills err on failure.
 */
snd *snd_open(const char *dir, const char *override_dir, int out_rate, char *err, size_t err_len);
void snd_close(snd *s);

/* ---- the game's side of the board (the latches and the reset line) ---- */
void snd_reset_line(snd *s, int asserted);
void snd_write(snd *s, uint8_t byte);
int snd_reply_pending(const snd *s);
uint16_t snd_read(snd *s);

/* Mixes `frames` stereo frames (interleaved int16) and advances time. */
void snd_mix(snd *s, int16_t *out, int frames);

/* The listener's own volumes, 0..1 (default 1), on top of what the game sets: overall, the music
 * channel, the effects and speech channels, and (on top of the effects) the crowd sounds, which are the
 * commands 2048 to 2065. */
#define SND_MASTER_MAX 3.0f   /* the master gain may boost up to 300 percent (a soft limiter keeps the peaks in range) */
void snd_set_gains(snd *s, float master, float music, float effects, float crowd);

/* Reads every sound file now instead of when it is first played (no loading during the game; uses the memory of all
 * of them). Stops at `max_bytes` of decoded samples (0 = no limit). Returns the number of files in memory; *bytes gets
 * their size, *total the number of files there are. */
int snd_precache(snd *s, size_t max_bytes, size_t *bytes, int *total);

/* Counters for tests: commands seen, sounds started, codes without a sound. */
typedef struct {
    unsigned long commands, started, unknown;
} snd_stats;
snd_stats snd_get_stats(const snd *s);

#endif
