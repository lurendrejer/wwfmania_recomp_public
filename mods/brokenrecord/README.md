# brokenrecord

The commentators only ever say one line. `--mod brokenrecord=N`:

| N | line |
|---|------|
| 0 | the first line Vince or Jerry says after the game starts (default) |
| 1 | "Hello!" |
| 2 | "Oh my!" |
| 3 | "Goodnight!" |
| 4 | "To the face!" |
| 5 | "What a blow!" |

Every line is played by `announcer_sound` or `triple_sound` (`DCSSOUND.ASM`), whether it comes off the announcer
queue or is played directly. `gen.txt` puts a check at the start of both: while `brec_on` is set, a commentator line
(Vince and Jerry: `triple_sndtab` 0E0h-1FAh and 2B0h up) is swapped for `brec_line`. Howard Finkel (1FBh-1FFh), the
wrestlers' own voices (200h-2AFh), the crowd and the hit sounds are left alone. With N = 0, `brec_line` starts at 0 and
the first commentator line takes its place; the game clears its variables when it starts, so each game has a new line.

The line keeps its own length, so the announcer's timing follows the new line.

Tried (headless, one CPU match, sound commands logged): with N = 2 the commentators said "oh my" 20 times and nothing
else. With N = 0 the first line was repeated 11 times. Unverified: how it sounds with the real board timing.
