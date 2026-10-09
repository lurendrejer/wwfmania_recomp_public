# outsidedive

Dives to the outside. `--mod outsidedive` (or F1, TWEAKS).

In the original game a running attack stops at the ropes: the runner bounces off them, the flight of a flying attack is
held at the rope (`confine_wrestler`), and a leap at an opponent who is outside the ring is a hop in place
(`_ani_leapatopp`, `ANIM.ASM`). With the mod, a wrestler in the ring who runs at his target (`CLOSEST_NUM`) while the
target is outside the ring, and presses a button before he reaches the ropes, does his own running attack over the
ropes and comes down on him:

- on a standing opponent the attack for a standing one: the clotheslines (Bam Bam, Doink), the flying kicks, Lex Luger's
  flying punch, and the lunges that leave the mat a little (Undertaker's, Razor Ramon's and Yokozuna's running punches);
- on a lying one (`MODE_ONGROUND`, `MODE_DEAD`) the attack for a lying one, whatever the distance: belly flops, butt
  drops, elbow drops, stomps.

How (`asm/OUTSIDEDIVE.ASM`, `gen.txt`):

- `dive_pre` / `dive_post` go around the call of the button handler in every wrestler's `mode_running`. When the target
  is outside, ahead of him (the way he runs) and within 480 pixels in X and 220 in Z, and he lies down, the handler is
  shown him near (`CLOSEST_XDIST/ZDIST` 0), so the tables of the running attacks take the row for a near opponent
  (176 x 176 in most of them). `dive_xmid` lets Bam Bam's and Doink's clothesline start anywhere (the original wants
  the middle of the ring). A dive starts when the handler starts an animation or he is no longer running.
- `dive_tick`, at the start of `confine_wrestler` (twice a tick, every wrestler): once he has left the mat (within 20
  ticks, or it was no dive: a head butt, a slap that stays down) the ropes do not hold him, and his speed in X and Z is
  set every tick so that he is over the opponent when he comes down to the mat's height (standing opponent: there the
  original flight ends, and his feet are at the opponent's chest) or to the floor (lying opponent; the upward speed is
  at least 7 so that he goes over the top rope). Fastest: 20 pixels a tick in X, 10 in Z. The dive ends when he is
  back on the ground, his mode changes in the air (he was hit), or after 90 ticks.
- The attacks' hit boxes are on for the length of the original flight, which ends at the ropes. Until the attack has
  hit (`dive_hit`, called where `COLLIS.ASM` takes a hit), `dive_tick` keeps the hit box on while he is in the air, and
  turns it off on landing if it did not hit. Not for grabs (`AMODE_PUPPET` and the like): their victim waits for the
  attacker's animation, which has gone on to its miss by then, and would hang as a puppet.
- `calc_ground_y` makes him `INRING` 1 as he passes the mat's edge, and the floor his ground; the game's collisions
  need attacker and victim on the same side of it, which they are when he comes down.

Tried (`wwfrun`, P1 against P2, P2 put outside the ring at x 1530 with `--pokeat`, tracks and screenshots):

- Doink, punch at a standing Razor Ramon: over the top rope, clothesline, Razor goes down ("FIRST ATTACK").
- Doink at Razor lying outside (held down with GETUP_TIME, `--pokeat`): over the top rope, lands on him, he is hit.
- Every wrestler, punch, kick, super punch and super kick at a standing opponent outside: Doink, Bam Bam, Bret Hart, Lex
  Luger all four; Razor Ramon and Undertaker all four (the punches are their lunges); Shawn Michaels kick and super
  kick (his punch while running does nothing in the original either); Yokozuna punch and super punch.
- Every wrestler's attack on a lying opponent (P2 marked lying with `--pokeat` on PLYRMODE, so he stood): over the
  ropes, lands at x 1530 (Undertaker 1484, Shawn 1451: the standing body is in the way).
- With the mod off, and with the mod on and the target in the ring: as the original (the same track).

Not good yet:

- Grabs fly out and miss: Yokozuna's running kick (the scissor) and Shawn Michaels's super punch (the flip slam).
  Their hit box is on for 10 and 4 ticks while he is still over the ring; the victim is not held.
- The lunges (Undertaker, Razor, Yokozuna) go through the ropes, not over them; they leave the mat by some 25 pixels.
- Only the side ropes: running goes left and right, so an opponent outside at the top or bottom edge is not dived at
  (unless he is ahead in X as well).
- Not tried: computer players (they do not seem to use the running attacks), buddy and four-player matches (the target
  is still `CLOSEST_NUM`, which the game prefers inside the ring), on a device.
