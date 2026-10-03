# moongravity

Less gravity: longer, higher flights. `--mod moongravity=N`, in tenths of the original: 5 (default) is half the gravity,
10 is the original, 20 is double.

A wrestler's gravity is `OBJ_GRAVITY` (16.16), reset to `GRAVITY` (8000h, `GAME.EQU`) on every new animation by
`change_anim_anim` and `change_anim1a` (`ANIM.ASM`). `gen.txt` makes both use `mg_gravity` when it is not 0, and the
mod sets it to 8000h * N / 10.

- Moves that set a speed of their own (a jump, a throw, a bounce) fly as high as `1/N` times as far, for twice as long
  at half gravity.
- Leaps aimed at the opponent (`_ani_leapatopp`, `_ani_leapatpos`) work out the speed from the gravity, so they still
  land on target: the time in the air stays, and the arc is flatter.
- Sequences that set a gravity of their own (`ANI_SETLONG,OBJ_GRAVITY,...`) keep it.

Tried (headless, one CPU match, 6100 frames): the wrestlers' gravity read 4000h with N = 5 and 1999h with N = 2. They
were off the ground for 929 and 1473 frames, against 485 without the mod. The match ran to its end. Unverified: moves
that wait a fixed time rather than for the ground may be cut short in the air.
