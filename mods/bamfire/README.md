# bamfire

Bam Bam's unused fireball throw. The original has the pieces but never joined
them:
- `IMG/BAMSPEC.IMG` holds `BAMBOWL01`-`13`, a low underhand throw, that no
  LOD lists and no animation script uses;
- `SPECIAL.ASM` has the projectile (`bam_fireball`, `fireball_anim`) and a
  fire splat (`firesplat_anim`); the only start of it is commented out in
  `BAMSEQ2.ASM`.

`gen.txt` adds `img/BAMFIRE.LOD` (the images), `asm/BAMFIRE.ASM` (the
animation script and the secret move) and a few in-memory edits: the move is
put in Bam's secret move table, the fireball gets its own hit id (3) with the
fire splat and hits like the Undertaker's reaper, and it is thrown low
(y offset 24 instead of 97; the unused pie has the same line).

**Move:** Down, Toward, Punch. `--mod bamfire` sets `bowl_enabled`, which the
move checks; without it nothing changes.

Unverified: damage balance (it uses the reaper's -3 and knock away), the
sound (none is played), blocking, and the fireball against the referee.
