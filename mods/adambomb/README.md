# adambomb

Adam Bomb, the wrestler Lex Luger replaced late in development, as wrestler number 7. `--mod adambomb`: the select
screen gets a square for him under the eight, and he can be chosen like the others and wrestle.

## What the original has of him

- his code, `orig/ADAM.ASM` and `ADMSEQ1-3.ASM` (September 1993), for an older engine: attack modes, animation
  commands, buttons and routines the final game no longer has (see docs/ADAMBOMB.md). It is not used.
- 643 frames in `IMG/ADAM.LOD` (`ADM_HIT/KIK/MSC/PNC/WLK.IMG`): standing, walking (legs and torso pieces), turning,
  running, bouncing off the ropes, punches, kicks, an uppercut, pushes, blocks, hits to the head and the body,
  losing balance, falling, lying, getting up, rolling, diving, a flex and an arms-up taunt. None of the frames of the
  final game's throws and holds, neither his side of them nor the victim's. Four of his standing frames are loaded by
  `DOINK.LOD` (`ADM_WLK2.IMG`).
- his mugshot, `ADMUG_A-H` in `WWFMUGS.IMG`, which `MISC.LOD` loads without it (`SELECT.ASM` has `ADAMMUG` commented
  out); his name for the life bar, `NAM_ADM` (`METERS.IMG`); his logo `ADM` and a bio card (`BIO1.IMG`: "Three Mile
  Island", "I'm ten megatons of terror"), loaded by no LOD. His crouton `!CRUT_AD` in `CRUTMUGS.IMG` is a copy of Bam
  Bam's.
- a row in every table by wrestler, mostly empty (`0`, `;7 spare`, `;Adam`, `#Adam .long 0`) or Doink's.

## How he is made

He wrestles with Razor Ramon's moves, with his own frames wherever he has a matching one and Razor's, recoloured to his
palette, where he has none. `gen/mkadam.py` (run by the generator, `script` in `gen.txt`) writes into `build/gen`:

- `ADAMB.ASM`, `ADBSEQ1-4.ASM`: `RAZOR.ASM` and `RZRSEQ1-4.ASM` with every name they define renamed (`rzr_` to
  `adm_`, `razor_` to `adam_`, `move_razor` to `move_adam`), `W_RAZOR` to `W_ADAM` (his row of the sound table, which
  plays the default sounds) and Razor's frame groups to Adam's (`R4ST4G` to `AB4ST4G`). Tables by victim keep Razor's
  entries for Razor; Razor's own voice lines (`DO_RAZOR_RUG_SPEECH`, `DO_RAZOR_PUSH`) are left out.
- `ADAMTBL.ASM`: the `AB` groups, as the image tools' `.SEQ` tables (a count, then the frames), filled from
  `gen/frames.txt`: which of Adam's groups stands for which of Razor's, picked by looking at the frames side by side.
  54 of the 88 groups come from his own art (walking, running, punches, kicks, blocks, hits, falls, getting up,
  rolling, diving, taunts). In 14 of them (in the air, falling, lying) his pictures sit up to 50 pixels elsewhere
  around their anchor than Razor's; for those (`align`) his frames get copies with the anchor moved so his body is
  where Razor's was (`AY...`). Also his other six palettes (blue, yellow, purple, orange, pink, grey: his red one with
  the singlet's ramp, indices 12-26, in another colour; red and green are in his IMG files).
- `lod/ADAMRZR.IMG`: the 229 Razor frames he has nothing for (throws, holds, being thrown, the Razor's Edge with its
  slash), recoloured to his palette (`AZ...`): Razor's trunks onto Adam's red, the rest to the nearest of Adam's
  colours. The head and the build stay Razor's.
- `lod/ADAMSEL.IMG`: his crouton (`CRUT_AB`: the face from his mugshot, in the frame of Razor's crouton), his name in
  the big letters of the end of a round (`BIGADM`, put together from the letters of YOKOZUNA, BRET, RAZOR RAMON,
  UNDERTAKER and BAM BAM) and his short name (`SHORTADM`, from `NAM_ADM`).

`img/ADAMB.LOD` loads those and the mugshot, the name and the logo from the shipped art. His square on the select
screen is square A of the machine's extra squares (`src/wolf/asm/XSQUARE.ASM`, docs/MODS.md), which `mod.c` fills:
under the eight in the middle, or on the left of mods/dink's. `asm/ADAMMOD.ASM` has his mugshot list and his
animations for the screens between matches.

`gen.txt` fills his row in every table by wrestler with Razor's, renamed to his copy where there is one:
`;7 spare/Adam/unused` lines, `;Adam` and `;spare` lines in the tables by victim, the `#Adam` blocks of every
wrestler's throws (Razor's lines with Adam's frames), the body point tables of `TABLES.ASM`, and entries without a
comment of their own (between Doink's and Lex's). Razor-as-victim animations that live in another wrestler's file get
a copy for Adam there (the Undertaker's choke, the standing up dizzy of `FINISEQ.ASM`). His palettes, his name, logo,
crouton and mugshot go where the others' are.

Wrestler 7 cannot be chosen in the original (eight squares), so none of this changes the game with the mod off. The
machine turns a wrestler number 7 into Doink's 6 (a crash fix for buddy mode); the mod sets `wolf.wrestler7` while it
is on, which leaves him alone.

With `mods/fourplayer`, the squares of players 3 and 4 move over the extra squares too.

## Tried

Headless (`wwfrun`), two players with random inputs, Adam as player 1 and as player 2 against each of the eight
others, 48 matches of 8000 to 12000 frames: no crash, and no frame of another wrestler drawn in Adam's palette (a
check of every drawn image against his palette, which found the Undertaker's choke before it was copied). The
images the DMA could not find were as many as with Razor in his place. One player against the computer through the
ladder (his logo on the versus screen, his name on the life bar, knocked down and back up). With fourplayer: player 3
takes him. The tests `adambomb_match` and `adambomb_fourplayer` (CMakeLists.txt) do the select screen and the start
of a match.

## Not done, unverified

- His own moves: he has Razor's special moves (with Razor's frames, recoloured), not those of his old code (a grab and
  fling, a 2nd wind) nor his finishing move from the bio card (the Atom Smasher). No art exists for them.
- No voice of his own: the announcer does not call his name on the select screen (no sample), and he has the default
  grunts. The music for him on the ladder is the tune of his row in `WHICH_MUSIC` (`PROGRESS.ASM`), 0; whether that
  was his is not known.
- The ladder of the one-player game has no place for him: he is never a computer opponent (the ladder packs
  wrestlers in three bits, 7 there is Lex).
- Poses mapped from Razor's frame by frame (frames.txt) were checked as still frames, not every move in motion.
  Throws where Adam is the attacker use his own frames for grabbing and Razor's offsets for the victim.
- The select screen's dark backing behind the squares (the background) is drawn for the eight; the extra squares
  are below it.
