# bottombuckles

Climb the near turnbuckles too. `--mod bottombuckles`.

The game only lets a wrestler climb the two turnbuckles at the far edge of the ring (`climb_turnbuckle` in
`WRESTLE2.ASM` wants Up + Left or Right at the top rope). With the mod, **Down + Left or Right at the near (bottom)
edge** climbs the near ones, with the same animations, and then the wrestler is on the turnbuckle like at the far
ones (the game's own moves from the turnbuckle: the leap at the opponent and the rest).

How: `gen.txt` makes `climb_turnbuckle` try the bottom edge when it is not at the top (`bb_check` in
`asm/BOTTOMBUCKLES.ASM`, the same tests: 5 pixels of leeway, up against the rope, the diagonal held), lets the game's
"near miss" step move the wrestler onto the bottom corner, and lets `tgt_tbukl` (the leap up in every wrestler's
climb animation) aim at the bottom corner of the rope table when the wrestler is at the bottom edge.

Tried (`wwfrun`, screenshots and positions): player 1 walked to the bottom edge (z 0x541) and held Down + Left at the
left post: his mode went to `MODE_CLIMBTURNBKL` (11) and he leapt at the opponent from the post.
Unverified: the right post; every wrestler (only Doink was tried); that the climb-up animation is drawn right from
the front (the animations were made for the far posts, seen from behind); the camera (the animations scroll it
for the far posts' height); computer players (they do not use `climb_turnbuckle`); coming down; the referee.

Standing on a near post, **Up** climbs down instead of Down (`bb_swap_ud`, applied in every wrestler's `mode_onturnbkl`):
Down is what climbs up there, and away from the ring. On a near post he also stands with his **back to the screen**:
14 ticks after landing (the game's landing, the corner shake and the offsets, take about that long) the wrestler's own
`stand8` animation (standing facing away) replaces the game's on-a-turnbuckle animation, which shows the front (it was drawn
for the far posts). The mode bits that keep him up there (no gravity, uninterruptable, ...) are set every tick, since the
stand animation sets the normal mode.

Tried (`wwfrun`, screenshots and positions, Doink): with a second player standing still he stays on the post (mode 6, y
0x9B) while Down + Left is held, shows his back, and Up takes him down into the ring. With a computer opponent close by he was
knocked off the post, as at the far posts. Not tried: the other wrestlers' stand8 (they are all in the table, none looked
at), whether the feet sit right on the post (the frames were not drawn for it), leaping from the post from the stand pose.

**Direction and position on the post.** When the stand animation takes over, the wrestler is mirrored (`M_FLIPH`) so that he
faces into the ring: without it he turned his body towards the outside at both posts. His place is set every tick while he
stands there (the game's rope line, the collision at 805 / 1348, is well inside the post it draws, and pulls him back): x
816 at the left post and 1336 at the right one (where the rope line holds him anyway), y 135 (the game's 155 for the far posts floats above the near ones), so that his feet
are on the turnbuckle pad. Found by looking at screenshots of Doink at both posts (`BB_XL`, `BB_XR`, `BB_Y` in
`asm/BOTTOMBUCKLES.ASM`); other wrestlers have other feet and were not looked at.

## The crowd fence's back corners

Outside the ring, the two back corners of the crowd fence (where the back rail meets the side rails, at the height of the
turnbuckles on the screen) can be climbed too: walk out of the ring to the corner and hold **Up + Left/Right** (Left at the
left corner, Right at the right one). The wrestler jumps up with his own climb animation and stands on the corner; **Down**
climbs down again, the game's other moves from a turnbuckle work as they do on the ring's.

How: the fence is the arena's outer limit (`ARENA_TOP`, `ARENA_TOP_LEFT`, `ARENA_TOP_RIGHT` in `RING.EQU`); `fc_check`
(`asm/BOTTOMBUCKLES.ASM`) is tried first in `climb_turnbuckle` (`gen.txt`): outside the ring (`INRING` 1), no more than 40
from the back rail and 60 from the corner, with the diagonal held. The game's climb-up animations move him up by fixed
offsets from where he stands, which for a wrestler on the floor gives a height above the fence, so `fc_stand` (called every
tick from `bb_swap_ud`, in `mode_onturnbkl`) brings him down to `FC_Y` over a few ticks and holds his X on the corner.

Tried (`wwfrun`, teleporting player 1 to the corner with `--pokeat`, wide view, Doink): jumps up, stands on the corner, climbs
down onto the floor. Not tried: the other wrestlers, the left corner, computer players, the leap at the opponent from the
fence, two wrestlers on the same side (the game's "only one on a side" test uses the ring's middle line).
`FC_Y` (62), the 8 pixels outward (`FC_XL-8`, `FC_XR+8`) and the size of the leaps were found by looking at screenshots of Doink
at the right corner.

### Leaps from the fence

The game's turnbuckle attacks pull a wrestler who is outside the ring into it when the attack starts and no opponent is out there
(`set_tbukl_confine`, `WRESTLE2.ASM`): from a fence corner that looked like a teleport into the ring and a hit after a short flight.
With the mod, a wrestler outside the ring is left where he is (`gen.txt` edit of `#clear_noconfine`) and the leap is made to reach:
`fc_leap` (for `ANI_LEAPATPOS`) and `fc_leapopp` (for `LEAPATOPP`, which copies the animation's numbers to `fc_leapbuf`) give it as
many ticks as the distance needs, 24 to 48 (a longer flight is a higher arc, the highest Y velocity is raised too), and lift the
limits on the distance. `fc_enter`, called every tick from `keep_onscreen`, puts a wrestler who is over the middle of the ring in
the ring (`INRING` 0, the mat is the ground) so that he does not land "outside" on the mat and get pushed out.

### The crowd fence's front corners

The two corners at the front (where the side rails meet the railing along the front of the picture, `ARENA_BOT_LEFT`,
`ARENA_BOT_RIGHT`, `ARENA_BOT`): **Down + Left/Right** there (Right at the right corner), outside the ring. He climbs like at the
ring's near posts (`bb_swap_ud`: Up climbs down, his own `stand8` animation with his back to the screen), and stands on the corner
(`FB_OUT`, `FB_OUTL`, `FB_Y`). The leap from there is the same as from the back corners.

Tried (`wwfrun`, Doink, wide view): both front corners climbed and stood on; climbing down from the right one; the leap from the back
right one into the ring and to the outside. Not tried: the other wrestlers, computer players, whether the hit lands.

### The number of the mod: FENCE LEAP

`--mod bottombuckles=2` (or the number in the MODS page of the F1 menu): a leap from a fence corner only flies at an opponent who is
outside the ring too (`fc_stay`, `fc_only_out`); when he is in the ring the wrestler hops on the spot (the distances of the leap are
set to 0) and lands on the floor. `1`, the default, flies at anyone.

Tried (`wwfrun`, Doink, back right corner): with 1 he flew into the ring; with 2 and an opponent in the ring he hopped up and stayed
above the corner. Not tried: with 2 and an opponent outside.
