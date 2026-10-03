# sansring

No front and back ropes. `--mod sansring`. The game has a "sans ring" powerup of its own (`NO_RING`, the bar SANSRING in
the powerup list), but it was never finished (`AWARD.ASM`: "until we get blimp module"); this mod does the
simple half of it.

- The front and back ropes are not drawn (the machine skips those rope objects, `wolf.hide_ropes`). The **side ropes are the
  game's own**: drawn and working as before, including the way out at the sides (a button press or pushing for a
  moment, with an opponent outside), because that part worked well.
- A wrestler who reaches the top or bottom edge of the ring is outside it and is not stopped there: walking, running, or
  pushed, kicked or thrown that way. The game calls `ck_climb_out_top` and `_bot` when a wrestler's position went past the
  edge, and then puts him back at the rope; with the mod `sans_exit` (`asm/SANSRING.ASM`) marks him as outside the ring
  (`INRING`) and the game goes on without stopping him, so his own movement carries him out. There is no climb-through
  animation and no waiting; nobody has to be outside already. Not while held or holding (`MODE_ATTACHED`) or in the game's
  own climb through the ropes.
- Nothing hurts outside the ring: `RING_TIME`, the count of ticks outside that finally does damage, is held at 1.

It is a first version. The corner posts with their turnbuckles, the mat and the apron are background and are
still drawn. Climbing back in uses the game's own climb-in animations, as before.

Tried (`wwfrun`, screenshots and positions): a one-player match where player 1 holds down or up: he passes the edge of the
ring and is stopped only by the arena's own limits (bottom: standing on the floor in front of the apron; top: at the
arena limit, z 0x250).
Not tried: a knock-back, kick or throw across the edge (same code, called from the same place), coming back in, a whole
match, buddy mode, a computer player, the side ropes with the mod on (they are not touched).
