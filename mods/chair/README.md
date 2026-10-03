# chair

The cut folding chair. `--mod chair`: at the left or right side of the ring (where he could climb in), **Down, Down,
Block** pulls a chair from under the ring and lifts it overhead. Walk around with it (into the ring too), and **any
button** brings it down on the opponent; then he lets go of it. Anything else (a hit, running, another move) and he
drops it. `--mod chair=1`: he keeps it after a swing, lifts it overhead again and can swing it again and again.

The original has most of it:

- the art: `CHAIR.IMG` (CHSWNG01-09, the chair turning in the air; CHSWNFL, lying flat; CHBRAK01-10, broken
  pieces) and its palette `CHAIR_P`, loaded by `MISC.LOD`
- a pick-up and swing script for every wrestler but Adam Bomb, commented out at the end of each `*SEQ3.ASM`
  ("CHAIR STUFF"). Each is a test loop, pick up and swing, again and again, with the chair attached to the
  wrestler (`ANI_ATTCHIMAGE2`) frame by frame, but no attack and no sound.
- Jerry Lawler's "Yes, the chair!" and "The chair, the chair!" (`L_YES_THE_CHAIR`, `L_THE_CHAIR`) and a chair
  crash sound (`triple_sndtab` 0C5h), which nothing plays

What the mod adds:

- `mkchair.py` turns the eight scripts into `asm/CHAIRSEQ.ASM`, each cut in two where the chair is first
  overhead:
  - `<w>_chair_up_anim`: bend down and lift it. It stays attached, overhead, when the script ends, and `chair_grab`
    marks it as held.
  - `<w>_chair_swing_anim`: the rest, bringing it down, then letting go, or, with the mod's number at 1
    (`chair_keep_status` sets `MODE_STATUS`, `ANI_IFSTATUS` branches), lifting it overhead again with the last two
    frames of the pick-up and holding it (`chair_grab`)
  - the loop is gone; 2 ticks per frame picking the chair up, 3 for the swing (the loops had 4)
  - an attack (`AMODE_HAYMAKER`, 23 damage) for three frames from where the chair comes down in front
  - a whoosh as it comes down; on a hit that was not blocked, the chair crash, a screen shake and one of Lawler's
    two lines, in turn
  Run it again (`python3 mods/chair/mkchair.py`) to change them.
- `asm/CHAIR.ASM`:
  - picking it up is a secret move (`chair_rec`, added to every wrestler's secret move table by `gen.txt`). It needs
    the mod on, the wrestler in normal mode and not in an uninterruptible animation, and at the left or right side of
    the ring, where the chairs are kept underneath (`chair_ringside`): the test the game makes before he climbs in
    from the side (`ck_climb_in_side`, `WRESTLE2.ASM`), outside the ring (`INRING`), between the posts (Z within
    0D8h of the ring's centre) and his collision box within 10 pixels of the mat's edge (`vln_left_matedge2`,
    `vln_right_matedge2`). Not at the top or bottom of the ring. Pushing on toward the ring there makes him climb
    in, as in the original, so he has to stop at the edge.
  - `chair_control` runs every tick, first in `check_secret_moves` (`WRESTLE.ASM`, one line in `gen.txt`), for a
    wrestler holding the chair. A new press of any button (`BUT_VAL_DOWN`) starts the swing and skips the secret moves
    that tick, so no other move takes the button too. Anything but standing or walking in normal mode lets go of the chair.
    The chair stays attached while he walks because the walk animations never touch the attached image. It is drawn
    at the offsets of the last pick-up frame, mirrored with him.
- The chair images' headers in the build have no palette: `MISC.LOD` lists them under a wrestler frame layout (see
  "Open questions" in `docs/IMG_FORMAT.md`). The attached image code (`ANIM.ASM`) reads one and stops at a `LOCKUP`
  without it. `chair_headers` copies the ten headers to RAM with `CHAIR_P` added when the move starts, and the
  `CHAIR_SWING` table the scripts use (commented out in `DNKSEQ3.ASM`) points at the copies.

The victim's reaction is the game's own for `AMODE_HAYMAKER`, which no move in the final game uses: `hit_haymaker`
(`REACT5.ASM`) makes him fall on his back, or flail if he blocks. A wrestler hit while he picks the chair up drops
it: the game clears the attached image on any hit (`REACT1.ASM`).

Tried (headless, two players, the second standing still): all eight wrestlers pick up the chair and swing it. The
opponent falls on his back every time, mirrored for the second player too. Picked up, carried over to the opponent
(overhead, while walking) and swung with Punch, it hits. The sound commands of the whoosh, the crash
and Lawler's line were sent. A computer opponent can hit the wrestler before the swing. Where (Bret, with `sansring`
to walk out at once, the second player walked along so the view follows): the chair comes up at the left side of the
ring, against the mat's edge; not in front of the ring nor away from it by the barrier. With the number at 1, a swing
with Punch and then one with Kick, the chair back overhead after each; at 0 he lets go after the first and the kick
is a kick.

Carrying it: the game draws a walking wrestler as two frames, his legs and, on the legs' secondary point, his torso
(`set_images`, `ANIM.ASM`). While he holds the chair, `chair_torso` draws the upper part of his holding frame (the
last pick-up frame, `chair_holds`) instead of the torso: an image header copied to RAM with fewer rows, spliced on
the legs the way the game splices a torso. The holding frame has no secondary point of its own, so its waist is taken as
where the wrestler's walking legs join on average (`chair_joints`, measured from the feet by `mkchair.py` from
`<W>_WLK.IMG`); that point goes on the legs' secondary point, so the upper body moves with the hips as a walking torso
does, and the frame is cut a few rows below it. Standing still, `chair_body`
draws the whole holding frame instead of his standing frame. The holding frame faces the screen, so his legs do too:
`change_walk_anim` (`WRESTLE.ASM`) picks the legs' walk from his move direction and the way he faces (toward the
opponent), and `chair_legs` turns an up facing (his back to the screen, below the opponent) into the down one, as if
he stood above the opponent. Tried: all eight wrestlers, standing and walking, hold
the chair overhead on top of their walking legs. The holding frames face sideways, so walking up or down the screen
shows a sideways torso.

Not done: the broken pieces (CHBRAK) and computer opponents using it. Unverified: the attack
box against every opponent, and the timing against the arcade (none of it was ever in the arcade game).
