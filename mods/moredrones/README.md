# moredrones

More computer opponents at the same time. `--mod moredrones=N`, N from 1 to 4
(default 3); in the F1 menu, left/right on the line changes N.

Works in a **one-player match** (against one opponent, or the ladder's own 2 or
3) and in the **cooperative mode** (two players on one team; the game calls
it royal rumble). Two-player matches, attract mode and buddy mode are not
touched.

The game has been made for 1 against 3: the final battle of the world title
ladder is that, and everything looks at `NUM_OPPS` for it: the life bar follows
the closest opponent (`rewire_monitor`), the camera height, the clock (slower
in 1 against 3), the sweat and the audit. The mod raises `NUM_OPPS` to N when
`WRESTLE.ASM` creates the wrestlers, gives the extra ones a random wrestler each
(the ladder only names the first; `more_fill_lineup` in `asm/MOREDRONES.ASM`),
and puts `NUM_OPPS` back when the match is over, since the progress screens
after it expect the ladder's number. The teams have three starting places; the
mod adds a fourth, so 4 opponents fit.

The slots: player processes 0 and 1, drones 2 to 5. The referee is process 6.

Tried: N = 4 in a one-player match and N = 3 in the cooperative mode (two
players vs three), each for a short time in a screenshot: the right number of
wrestlers is there, and the game keeps running.
Unverified: a whole match; the progress picture before the match (its extra
opponents are the ladder's bytes after the first, that is Bret Hart's face);
that more than 3 is fair (the clock is only slowed for exactly 3, and the
sweat is only off for exactly 3); several opponents that are the same wrestler
as the player.
