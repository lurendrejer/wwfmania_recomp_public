# dink

Dink, Doink the Clown's little sidekick, as a tenth character. `--mod dink`: the select screen gets an extra square
for him, and he wrestles as Doink at half the size.

He is Doink, wrestler 6, in every table: his moves, his frames, his palettes, his sounds. Nothing in the original
has a small wrestler, so this mod adds three things:

- **His square.** Square B of the select screen's extra squares (`src/wolf/asm/XSQUARE.ASM`, see docs/MODS.md):
  wrestler 6, marked small. A player who takes it gets his bit in `xsq_smallp` (by player number); one who takes any
  other square loses it. The bits of players 3 and up (computer partners and opponents, and mods/fourplayer's
  players) are cleared at every select screen. With mods/adambomb as well, the two extra squares stand side by side
  under the eight (Adam left, Dink right), so the grid is two by five.
- **Drawing him smaller.** The game draws a wrestler as one or two images (his frame, and on a walking frame the
  torso on the legs' secondary point) and his shadow, each placed by its offset from his position
  (`#plot_object` and `set_images` in `ANIM.ASM`). For a player marked small, `gen.txt` halves those offsets and
  gives his objects a DMA scale of one half (`OSCALE`, which the display code already passes to the DMA for every
  object, `DISPLAY.ASM`): he stands on the same spot, half the size. While the mod is on every wrestler's objects get
  their scale set each frame (half or 1:1), as the objects of a player number pass from one wrestler to the next.
- **His name and pictures.** `gen/mkdink.py` makes them from Doink's at generation time (`gen/lod/DINK.IMG`): the
  crouton and the mugshot with the picture smaller inside the frame, and his name in the three lettering the game
  shows names in (life bar `NAM_DINK`, short `SHORTDINK`, end of a round `BIGDINK`): Doink's without the O.
  `gen.txt` makes the life bar and the end of a round show them for a player marked small.

`dink_enabled` is the switch for all of the drawing and naming; the mod sets it every frame.

## Tried

Headless, two players with random inputs, Dink as player 1 and as player 2 against each of the other eight and
against himself (19 matches of 8000 frames, with mods/adambomb on as well): no crash. Against Bam Bam (one player,
through the ladder): walking, being hit, thrown over the shoulder and out of the ring, at half size with the torso on
the legs and a shadow of his size; Bam Bam at full size. The tests `dink_match` and `adambomb_dink_match`
(CMakeLists.txt) choose him and check that a frame of Doink's is drawn at half his height.

## Not done, unverified

- Only the picture is smaller. Where he hits and where he can be hit (the collision boxes of the frames, and the
  attack boxes of the moves) are Doink's, so his punches reach as far as Doink's and the opponent's land on the air
  above him. Throws place him (or his opponent) by Doink's offsets.
- The trail of after-images some moves leave (`shadow_gen`, `ANIM.ASM`) and images attached to a wrestler are drawn
  at full size.
- Near the edge of the screen the display code clips an object by its full size (`dma_objlst2d`), so he may be cut a
  little early there.
- On the versus screen he has Doink's logo, and the announcer calls him Doink on the select screen.
