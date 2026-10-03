# fourplayer

Four people in one match. `--mod fourplayer`.

Four people, each a player of their own. The game has two players (the left and right side) and,
in **buddy mode**, a computer partner for each. The mod gives each person one fixed place, whoever presses **start** first (player 3 or 4: key 3 or 4, or a controller's start button):

- Players 1 and 2 are the game's two players (left and right side) with all of the game's own rules (credits,
  select screen, challenger, continue, game over).
- Player 3 is the partner on player 1's side and player 4 the partner on player 2's. A partner joins after a
  player of the game has started (a start before that is not kept). Buddy mode is asked for only then, and only the partners someone plays are
  made: three people are two against one (no computer partner for the one), four are two against two. A partner who joins during a match
  without partners stops it, as a second player coming in does in the original, and the game goes to the
  select screen with the new partner in.
- Someone who has not pressed start plays nothing. Players 1 and 2 keep their own places while nobody else
  holds them, so the attract mode, the menus and a game without players 3 and 4 are as in the original.
- A side is given up when the game has been without its player for 15 seconds (game over, or a continue not
  taken); the partners when neither side is in.

**Background.** With the mod on, the select screen has no big blue and red panels (they were made for one
large mugshot each): the mugshots stand on the original background's WWF stone pattern (its own blocks
WWFSELBK #0 and #1 laid over the whole screen), with the frames of the wrestler grid as before.

**Cooperative.** In the original's cooperative game (the royal rumble) the left side's player and the right
side's player fight together on the left against a line of eight wrestlers that come in one at a time, two at once.
With the mod the two wrestlers on the right are played by the partners (the third and fourth person to press
start), each new one that comes in taking the place of the one who went out; without partners they are the
computer's, as in the original. Buddy mode is never asked for in it (with three wrestlers on the left the game
crashes when the first one goes out, also in the original).

**Who is who.** Each mugshot on the select screen has a label ("P1" to "P4") with the person who plays it. Buddy mode is the mod's: it is asked for
exactly while someone plays a partner, and a request left from an earlier game (or the game's own buddy code)
is taken back otherwise.

**Character select.** The screen has four mugshots: the left side and its partner in the left panel (one above the other),
the right side and its partner in the right, each at half size, with no name text (the big names of the original would land on the other mugshots). A partner who has joined also gets a square
on the grid of the wrestlers: in team colours: the left side red (its player) and a lighter red (its partner), the right side blue and a lighter blue (the original has player 1 blue and player 2 red), flashing
white in the same rhythm as their cursors until the wrestler is chosen. Until a partner has joined, "PRESS START"
stands under its mugshot place. The stick moves the square (the mugshot and name follow), any button chooses, and the
choice becomes that side's partner. The select screen waits up to ten seconds for a player who has joined and not yet
chosen. When the time runs out, a partner who is in gets the wrestler under their square, as players 1 and 2 do. A partner nobody plays is not in the match. The same wrestler can be chosen
twice.

**Sounds.** The partners get the sounds players 1 and 2 have, with the same codes the original code uses: the start sound
(`triple_sound` 49h, from WRESTLE.ASM) when a partner joins, the cursor sound (0C8h for the left partner, 0C7h for the right one)
when the square moves, and when the wrestler is chosen the select sound (0CBh, 0CCh) and the announcer saying his name
(SELECT.ASM's `call_wrestler_name`, in a process of its own; the announcer's queue can skip a name when another is
still being said, as in the original). A side taken by person 3 or 4 goes through the game's own start code and has
all of these already. The tests `fourplayer_sound_*` watch the calls of `triple_sound` and `call_wrestler_name`.

**Fly-ins.** With partners in the match the fly-in messages (high risk, 2x combo, reversal, first attack) are off: they would land behind the bottom bars.

**Life bars.** In the match the life bars of players 1 and 2 are along the top as always, and those of the partners
along the bottom (left side's partner left, right side's right) with the wrestler's name. The top bars stay with the two sides' players
(they do not switch to the partner when the player is knocked out, as they do in buddy mode without the mod). A side without a partner has no bottom bar. The
bottom bars have no combo ("super") meter, win boxes or flashing at low life.

**Camera.** With four wrestlers in the match the camera follows all of them (the average of their places, as in the
royal rumble), not only players 1 and 2. Nothing zooms out, so the outermost can still be at the edge of the picture. With three people one wrestler slot is empty; the camera
skips it (the royal rumble code, made for four, read its data at address 0, the top of the video bitmap, and the camera
ran off when the picture there changed; test fourplayer_camera).

## Keys and controllers

Players 3 and 4 have their own lines on the **Controls** page of the F1 menu (P3 and P4, with
the same actions as players 1 and 2; their start joins the game). Enter,
then press the new key. They are saved in `wwf.cfg` as `key.p3.*` and `key.p4.*`. The defaults:

| | player 3 | player 4 |
|---|---|---|
| Move (up, down, left, right) | keypad 8 5 4 6 | Home, End, Delete, Page Down |
| Punch, block, super punch, kick, super kick | keypad 7 9 1 3 0 | `[` `]` `-` `=` Backspace |
| Start (join) | 3 | 4 |
| Run (punch + kick) | keypad + | Page Up |

Game controllers: the first four controllers that are connected play players 1 to 4, with the
buttons of the Controls page ("PAD ..." lines, the same for every player).

Test aids: `--input FRAME,LEN,x3|x4,BITS` in `wwf` and `wwfrun`; BITS is the stick in bits 0 to 3
(1 up, 2 down, 4 left, 8 right) and then 16 punch, 32 block, 64 super punch, 128 kick, 256 super kick, 512 start.

## How it works

A computer wrestler is controlled by two words, `DRN_JOY` and `DRN_BUT`, that the drone code
(`drone_main`, `DRONE.ASM`) fills in each frame and then turns into the button and stick
transitions the wrestler code reads. `gen.txt` makes `drone_main` start by asking `fp_input`
(`asm/FOURPLAYER.ASM`): for the wrestler processes 2 and 3 (the buddies) while the mod is on, it
sets the two words from `fp_in` and skips the computer's own choices. The other edits in `gen.txt`:
`fp_select` (a process the select screen creates) draws the squares, the mugshots and names of 3 and 4
and fills `fp_pick3` / `fp_pick4` (`fp_mug1` and `fp_name1` shrink and move the mugshots and names of 1 and 2, and
`fp_scale` does the half-size placing: each piece at half its size rounded up so no seam is left, and the flipped
right-hand ones allowing for how the display code and the DMA differ), `choose_buddies` uses them, `meters` makes
and updates the two bottom bars (`fp_mtr_init`, `fp_mtr_update`), the select screen waits on `fp_wait`, and `scroll_world` takes the
royal rumble branch (camera on all four) when the mod and buddy mode are on. `mod.c` keeps who holds which place and routes each person's controls there every frame (the game's
players through `player[]` and the start bits of `coin_bits`, the partners through `fp_in`), and writes `fp_on` from `wolf.extra_player[]`, which the front end fills from the keys and
controllers of players 3 and 4 (`src/platform/main_game.c`). Nothing reads them without the mod.

Player 3 and 4 are drones as far as the rest of the game can see: the game treats them like
the original buddies.

## State

Tried (`wwfrun`, screenshots, tests `fourplayer_select` and `fourplayer_match`): player 1 against player 3 (one-on-one, no partners, player 3 moves the right-hand wrestler); four
people (two against two, player 3 moves their wrestler); player 3 alone from the attract mode; the select screen with four mugshots (both squares move and
choose, the chosen wrestlers are the partners in the match; the mod off leaves the select screen alone); the four life
bars in a match, a hit partner's bar going down; the camera (with both partners walking
right the picture scrolls further than without the mod's camera); a buddy match with the mod on, no keys for 3 and 4, where the partners
stand still; with keys, where partner 3 walks right, and partner 4 walks left and attacks; with the
mod off the partners fight on their own as before.

Unverified: how the sounds sound (the sound ROMs are not in the repository, so the tests only see the codes
sent to the sound board); that the announcer says each name fully (its hold time is 60 ticks, set by the mod: the original passes
the other player's cursor index there, which looks like a slip, and the length of each name is in WHICH_SPEECH);
the sounds when a partner joins during a match (`fp_input`; the start sound is played when the join is first seen).

Unverified: a whole match; what happens when a partner is knocked out (the game may turn a
defeated drone into a zombie rolling around on its own); whether the drones' damage
adjustments (`damage_mod_table`) make the partners take more or less damage than players; the special
moves and the run of the partners (only the stick and a punch were tried); the partners have no getup
meter, like the original ones; a stick held toward the ropes at a turn buckle.

Unverified: the bottom bars in a match where a partner is knocked out and in the later rounds (they were not checked);
a start of player 4 alone from the attract mode (only 3 was tried; the code is the same); with credits (not free play)
the start of players 1 and 2 take their credits; the half-size mugshots with high-resolution art overrides.

**Computer buddies.** The settings P1 BUDDY and P2 BUDDY (F1 menu, game options) give player 1's side and player 2's side
a computer partner each, also one alone; a person who plays a partner place takes it over. Without this mod only both
together work (the game's own buddy mode).
