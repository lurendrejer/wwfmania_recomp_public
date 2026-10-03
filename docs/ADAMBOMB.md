# Adam Bomb: porting notes

Adam Bomb is wrestler 7, the slot Lex Luger (8) took. He is playable with `--mod adambomb`
([mods/adambomb/README.md](../mods/adambomb/README.md)). These are the findings behind that mod.

## His own code is for an older engine

`orig/ADAM.ASM` and `ADMSEQ1-3.ASM` (Jamie Rivett, September 1993) were written for an earlier version of the
engine. Linked with the final game (a first attempt took the referee's way: `module ADAM` ... and `slot 7=6`), they
assemble but do not link:

- Undefined: `AMODE_GRAPPLE`, `AMODE_HIPTOSS` (attack modes removed from the final engine), `ANI_ENDMATCH`,
  `ANI_SLAVE` (removed animation commands), `B_GRAB`, `B_TURBO`, `PLAYER_TURBO_BIT` (old button names), and the
  routines `break_lockup`, `lineup_grapple`, `start_run`.
- `ADAM.ASM` includes neither `jjxm.h`, `damage.equ` nor `tmpdebug.h`, and calls the old per-wrestler hit code
  (`DAMAGE`, `adam_hit`), which the final engine replaced with REACT1/REACT2.
- `A4ST4J` is defined by both DOINKIMG and ADAMIMG (`DOINK.LOD` loads four of his standing frames).

Beyond linking, the final game's moves need far more than his scripts have: every throw and hold is a script with a
block per victim (`#Bret` ... `#Adam` ... `#Lex`) giving the victim's frames, and he has no victim frames at all.

## Whose moves he gets: Razor Ramon's

His art (643 frames, `IMG/ADAM.LOD`) covers about half of what a wrestler of the final game uses. Measured over the
animation groups each wrestler's scripts use (a group is one move from one side: `R4ST4G` is Razor standing,
`ST`, seen from side 4, variant G), the share whose move code Adam also has art for:

| | Bam Bam | Razor | Taker | Lex | Shawn | Yoko | Doink | Bret |
|---|---|---|---|---|---|---|---|---|
| groups | 56% | 51% | 50% | 50% | 48% | 47% | 46% | 43% |
| weighted by use in the scripts | 64% | 51% | 53% | 55% | 51% | 57% | 49% | 48% |

Bam Bam matches most codes, and his build is closest (a standing frame is 70 pixels wide, Adam's 66, Razor's 53).
Razor was chosen because of the rest: whoever it is, 230 to 310 frames have to be borrowed and recoloured to Adam's
palette, and recolouring cannot change shapes. Bam Bam wears a full black suit with flames and has a bald, tattooed
head; Razor is bare-chested in trunks and knee pads with dark hair, as Adam is (singlet, knee pads, long dark hair).
Recoloured, Razor's frames pass for Adam's much better. Looked at pair by pair, Adam's art also matched several of
Razor's groups under other codes (his `A3AE4A` for Razor's `R4AE4B`, `A3CP3A` for `R3AM3D` ...): 54 of the 88 groups
come from his own frames in the end (`mods/adambomb/gen/frames.txt`).

## Palettes

All of a wrestler's frames are drawn with the wrestler's palette (`OBJ_PAL`), one of eight chosen by the button held
on the select screen (`#wrestler_pal_table`, `WRESTLE2.ASM`); a frame's own palette in the IMG file does not count.
Razor's palettes differ only in indices 52-63 (his trunks, `WRESPAL.ASM`); Adam's `ADMRED_P` and `ADMGRN_P`
(`ADM_WLK.IMG`) only in 12-26 (singlet and knee pads). His IMG files have six more, `REDCYC1-6_P` and `GRNCYC1-6_P`,
each on one standing frame (`A2ST2D04`-`12`): presumably a glow cycled through for his radioactive look. Unverified;
nothing uses them.

## Other findings

- The machine turns a wrestler number 7 into 6 (`apply_fixes`, `src/wolf/wolf.c`, after the MAME cheat "Broken
  Doink - Crash patch": buddy mode can pick 7 at random for a drone). A mod that brings him must turn that off
  (`wolf.wrestler7`).
- Several places treat 7 as Lex on purpose: the one-player ladder packs wrestlers in three bits and promotes 7 to 8
  (`SORT_OUT_WRESTLER_NUM`, `PROGRESS.ASM`), the attract mode skips 7, the royal rumble's zombie code turns 7 into 8
  (`ANIM.ASM`), and the bodies of the 8-on-1 screen (`WHICH_WRESTLER_IMAGE`) have Lex at 7.
- His music on the ladder (`WHICH_MUSIC`, `PROGRESS.ASM`) is tune 0; whether it was his is not known (the sound ROMs
  are not in the source release).

## Open questions

- What the `REDCYC`/`GRNCYC` palettes were for (above).
- Whether `A3DC3B02` and `A4SW4A06`, whose pixels look like noise, are broken or meant for one of those palettes.
