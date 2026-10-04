# Game options (F1 menu, wwf.cfg)

Options that change or skip parts of the original game. They live in the F1
menu under "GAME OPTIONS" and in `wwf.cfg`; none of them is on unless set
(except skipping the self test). `wwfrun` has `--skip-selftest`,
`--powerups N`, `--no-ringout-timer` and `--no-select-timer`, `--no-white-flash` and `--no-red-flash` for the same.

| Option | What it does | How |
|--------|--------------|-----|
| Skip self test | leaves out the power-up test (the ROM check with the chip map) | the board's own bypass switch: `READ_DIP` bit 6 (raw DIP bit 1) makes `WRESTLE.ASM` skip `POWERTST`; takes effect at the next start |
| No blocking, instant combos, ring out match, sans ring, move names, drone meters, hyper speed, buddy mode (as P1 BUDDY and P2 BUDDY, see below) | the game's own secret "powerups" (`GAME.EQU` `BLOCKING_OFF` ... `BUDDY_MODE`, bits 1 to 128) | the bits are set in `p1powerup_request` and `p2powerup_request` every frame, which is what the MAME cheats for this game do (`maincpu.pb@1116C60`/`1116C80`, bit addresses of the same variables) |
| Free roam (the key is still `no_ringout_timer`) | a wrestler outside the ring no longer loses health, and no "get back in" message | `RING_TIME` (`PLYR.EQU`, bit offset 0xC40 in the wrestler process) of every wrestler is set to 1 each frame, so `ARE_WE_IN_RING` (`SPECIAL.ASM`) never counts up the time outside |
| Disable low fidelity (on by default) | shadows (for example Undertaker's and Bam Bam's) also when there are more than two wrestlers in the ring | `reduce_bog` (`WRESTLE.ASM`, the number of wrestlers above two, set by `init_reduce_bog`) is held at 0 every frame and before each display; from the MAME cheat "Enable Shadows" (`maincpu.pb@10F43F0`, the same variable). While it is not 0 the game also leaves out other work: the crowd process then sleeps 0x7FFF ticks and the game wakes it (`PTIME` = 1) when it clears the variable itself, so the option does the same wake-up whenever it clears a non-zero value (without it the spectators stood still). Some animation steps come back too |
| No select timer | the character select clock (15 seconds, then the game picks for the players) never runs out, and its last-five-seconds digits stay hidden | `select_clock` (`SELECT.ASM`) ends its countdown with `dsj a9,#waitloop`; `src/wolf/core.gen.txt` puts a test of `no_select_timer` before it, and `wolf.c` sets that variable every frame. Checked: with only player 1 started and no button pressed the game was still on the select screen 2300 frames later, and without the option it had started a match |
| No white flashes, no red flashes | the full screen flashes of hard hits, the finish and the round end (white) and of the big hits that hurt (red) are left out; in a wide view or at high zoom they showed as a white or red rectangle in the middle | `flash_white` / `flash_red` (`WRESTLE2.ASM`) fill the game's 400 x 256 screen for a frame; `src/wolf/core.gen.txt` makes them return at once when `no_white_flash` / `no_red_flash` is set, which `wolf.c` does every frame. The variables were read back as 1 in a run; the effect on a hit was not compared frame by frame |

## Fixes (always on)

- **Wrestler number 7 (buddy mode, Doink).** Buddy mode can give one of its two
  drones wrestler number 7, which does not exist (6 is Doink); the game then
  jumps to address 0 ("stopped: jump to an address outside the recompiled code
  at 00000000"). The MAME cheat "Broken Doink - Crash patch" turns the 7 into a
  6 at `010E9DD0` and `010EB150`; those are `WRESTLERNUM` (`PLYR.EQU`, bit offset
  0x590) of wrestler processes 3 and 4 (process spacing 0x1380 bits), the buddy
  drones. Here the `WRESTLERNUM` of every wrestler process is checked through
  `process_ptrs` (and the two cheat addresses too), every frame and once more
  before each display. The result is a Doink in the palette the game has, not a
  seventh one. The cause (`choose_buddies`) was not read, and the crash itself was
  not reproduced here (the two-player buddy game could not be started from a
  script).

- **Random numbers (HCOUNT).** The game's random numbers (`RNDRNG0`, `RNDPER`, `UTIL.ASM`) mix in the board's horizontal pixel counter. The port's `HCOUNT` register used to be constant, so a number depended only on how many had been drawn before it, and a game started through the same menu steps always got the same ladder of opponents (in the world championship the first two drones were always the same). `HCOUNT` now follows the instructions used in the frame (`read16` in `wolf.c`). Checked: the first three ladder entries after picking a wrestler at frames 1000, 1001, 1002 and 1003 were identical before and all different after. Recordings and save states are as deterministic as before.

## Not verified

- **Disable low fidelity** (shadows with more than two wrestlers; formerly "Shadows with 4 wrestlers") was built and the variable is cleared every frame, but a four-wrestler match was not started here, so the shadows themselves were not seen.
- Only the self test skip and the start of a one-player match were run. The
  flags were seen in the request variables (`0xA9` with 0x89 requested; the game
  itself adds 0x20), but their effects in a match (no block, combos, hyper
  speed, ...) were not looked at.
- **P1 BUDDY / P2 BUDDY.** A computer buddy for each player separately (they replace the one BUDDY MODE; a saved
  `buddy_mode=1` turns both on). Both on is the game's own buddy mode (powerup bit 128). One alone (a buddy for one
  side against a single wrestler) needs the fourplayer mod, which makes the partners one at a time (`fp_has`, from
  `buddy_sides` in `wolf_set_options`: powerup bits 128 and 256); without the mod it does nothing.
- **Buddy mode** only exists in the head-to-head path of `WRESTLE.ASM`
  (`#2plyr`): in a one-player game against the ladder, no drones are added
  (`buddy_mode_on` stayed 0), so it needs a two-player game.
- **Sans ring** is unfinished in the original: `AWARD.ASM` has the check
  commented out "until we get blimp module". The flag exists and is set, but
  what is drawn (or not) with no ring was not looked at.
- The ring-out timer option was only built and run through a match without
  anyone leaving the ring; whether health still falls outside the ring
  (for example by `kill_when_hit_ground` in ring-out matches) was not tested.

## Leaving the ring with "free roam"

In the original a wrestler can only climb out of the ring (`ck_climb_out_top`,
`_bot`, `_side` in WRESTLE2.ASM) when an opponent is already outside
(`any_opp_outside`). With the option "free roam" the machine sets
`leave_ring` (src/wolf/core.gen.txt) and `any_opp_outside` says yes, so a
wrestler can go out any time, as if somebody were out there. Not tried in a
match: my test run did not reach a clean case (the computer opponent was out
of the ring by itself).

The ticks the stick has to be held against the ropes before the wrestler climbs out on his own (`idiot_check`,
`IDIOT_COUNT` 21) are doubled with the option: the elapsed count is halved before the compare
(`src/wolf/core.gen.txt`). A button press at the ropes still climbs out at once, as in the original.

## Free play

Free play is a dipswitch of the cabinet (DIP.EQU: `DPCOINAGE`, the coinage
switches all on, with `DPUSECMOS` off). The machine's switches are in
`wolf.c` (`IO_DIP`); with the setting the coinage switches read as free play.
F1 menu, game options, FREE PLAY; `--free-play` on the command line (also for
`wwfrun`); `free_play=1` in `wwf.cfg`.

The game reads the switches only when it starts (`SET_DIP_COINAGE`, DIAG.ASM)
and keeps the result in the CMOS file, so the setting takes effect at the
next start, and if the CMOS file is new the game picks it up at the start
after that (measured with `wwfrun`: the second start with the same CMOS shows
FREE PLAY and starts a game with no coins). Turning it off works the same way.

## Save state

F5 saves the machine to `wwf.state` in the working directory, F9 loads it. It is for the same build and
view size only (not a format to keep), and leaves the CMOS and the sound player alone. Checked with
`wwfrun --save-state FRAME PATH` / `--load-state FRAME PATH`: a run that loads a state and plays on gives
the same picture as the run that never stopped. Input recording is below.

## Game controllers

`src/platform/gamepad.c` uses SDL's game controller mapping. The first controller is player 1, the second
player 2; hot plugging works. The left stick always moves. The buttons are set in F1 > CONTROLS ("PAD ..."
lines, saved as `pad_*` in `wwf.cfg`, one set for both players); the defaults are the D-pad to move, A punch,
X kick, B block, Y super punch, right bumper super kick, left bumper run (punch + kick), Start start. Back is
coin. Their input is added to the keyboard's.

The menu is used from a controller too: the Guide button opens and closes it, the D-pad moves and changes
values, A selects, B goes back.

Unverified: no controller was available where this was written, so the mapping and the menu control
were not tried on real hardware.

## The F1 menu

The panel has one size on every page. Under the list there is a description of the selected line, wrapped at
word breaks into four rows (mods show their `description`); what does not fit is cut with "...".

## Sound volumes

F1 has VOLUME, MUSIC, EFFECTS and CROWD (0 to 100 %, LEFT/RIGHT in steps of 10; `vol_master`, `vol_music`,
`vol_effects`, `vol_crowd` in `wwf.cfg`). They are the listener's own gains (`snd_set_gains`, `src/sound`) on top
of what the game sets on the board's channels: MUSIC is channel 0, EFFECTS the other channels (effects and
speech), and CROWD applies on top of EFFECTS to the crowd sounds. Those are the board commands 2048 to 2065
(`CROWD_BOO` to `CROWD_BASIC` in `orig/SOUND.EQU`, sent as they are by `crowd_cheer`/`SNDSND`). Everything
applies to the extracted files and to replacements alike. Unverified: that all crowd noise is in that range (the
walk-in cheers may be music or announcer entries), which needs the extracted `sounds.txt` to check.

## Display and speed

F1 > DISPLAY (saved in `wwf.cfg`):

- `smooth` (default on): linear filtering, or sharp pixels.
- `integer_scale`: only whole-number magnification (SDL integer scale), the rest of the window stays black.
- `crt_aspect`: the arcade monitor showed the 400 x 254 picture at 4:3, so a pixel was 0.847 as wide as tall;
  on = that shape (the window is shown with those pixels, also in the wide view).
- `scanlines`: the lower third of every game row is dimmed.
- `speed`: 25 to 300 percent of the arcade's 54.7 frames per second (steps of 5). The sound is produced per game
  frame, so it speeds up and slows down with the game (pitch changes, like a tape). Holding Tab is fast forward
  (4x, no sound). The Tab key is fixed; it cannot be rebound.

Checked with screenshots (scanlines, sharp pixels); not compared against a real CRT.

## Recording and replay

F7 saves a state (to `wwf.state`, like F5) and starts recording the inputs of every frame from there; F7 again stops
and writes them to `wwf.rec`. F8 loads `wwf.state` and plays `wwf.rec` back, so the same moves are made again (the
keys are ignored until it ends; F9 or F8 again stops or restarts it). The machine is deterministic, so the replay
reproduces the recording exactly: `wwfrun --record FROM TO STATE REC` and `--replay FRAME STATE REC` showed identical
pictures at three points of a 300 frame recording with moves in it, and a run without those moves differs.
The recording is only good with the state file it started from, in the same build and view size, and with the same
mods on (a mod that acts on the game changes what happens).

## Display page (F1): size, art, GPU, precache and limits

The page DISPLAY holds everything about how the picture is made: smoothing, scaling, speed, the dynamic zoom,
WINDOW SIZE, RENDER SCALE, HD ART, GPU DRAWING and ASYNC COMPUTE (the first four used to be on the MODS page), DEBUG INFO, PRECACHE and
LIMITS (DEBUG INFO is on the DEBUG page). WINDOW SIZE, RENDER SCALE, HD ART, GPU DRAWING and PRECACHE take effect after APPLY AND RESTART.

- **DEBUG INFO** (`debug_overlay` in wwf.cfg) puts a few lines on the screen: frames per second and where the time goes
  (the game, the drawing), the bitmap size and render scale, whether the GPU draws or is paused, the HD art in memory
  and, on Android, the system's free memory.
- **ASYNC COMPUTE** (`async_compute=1`, `--async-compute`) with GPU DRAWING: the GPU textures of the HD art are made a few
  milliseconds per frame from a queue, not in the middle of a frame the first time an image is drawn. Until an image is
  ready it shows the original picture. Takes effect after APPLY AND RESTART; see docs/VIDEO.md, "Async compute".
- **HD ART LAYERS** (`art_off=` in wwf.cfg) opens a list of the parts of the game whose HD art can be switched on and off one at a
  time: the eight wrestlers, mugshots, crowd, menu screens, HUD and fonts, effects, ring and props, other, backgrounds. Takes
  effect at once (from the next picture after the menu closes). See docs/ASSET_OVERRIDES.md, "Layers".
- **PRECACHE** (`precache=1`) reads all HD art and all sounds at start and keeps them, instead of when they are first
  needed. It is used only where there is room: not on Android, and on a PC only with at least 6144 MB of RAM
  (`SDL_GetSystemRAM`); elsewhere the line says "(NOT HERE)" and LIMITS says why. The art is queued for the worker
  threads in the background (the game does not wait); the sounds are read before the first frame. Within 60 percent of the
  RAM; if that is not enough the rest is read when needed and LIMITS says so.
  With GPU DRAWING the precache also makes the GPU textures of every image it has read (about 3 ms of work per frame,
  `gpu_video_precache`), so the first time something is drawn there is nothing to upload. While it works the game runs and a box in
  the top left corner (under DEBUG INFO, if that is on) says `PRECACHING HD ART: READ n OF m`, `ON THE GPU n OF m`, with a
  progress bar; `HD ART READY` shows for a few seconds when it is done. Images the memory budget left out are read when drawn, as before.
- **LIMITS** lists what the game had to limit on this device (left/right shows the next): the bitmap capped at 16 Mpixel
  on Android (the dynamic zoom's far-out limit gives way), a render scale lowered because the bitmap would be too big, the
  HD art kept within a memory budget (and how many images were freed), low system memory, GPU drawing that could not be
  used, no worker threads for the art, precache that is not possible or stopped at its budget. With LIMIT WARNINGS on
  (DEBUG page, off by default, `limit_warnings` in wwf.cfg) a line at the bottom of the screen announces new limits for ten
  seconds.

## The menu pages

The main page lists, in alphabetical order, CONTROLS, DEBUG, DISPLAY, GAME OPTIONS, MODS and VOLUME (FULL SCREEN above them,
SAVE SETTINGS, APPLY AND RESTART and CLOSE MENU below). The game options and the mods are listed alphabetically too.
VOLUME holds the four volumes (overall up to 300 percent, music, effects, crowd; Enter = 100). DEBUG holds DEBUG INFO, LIMIT WARNINGS
(off by default) and RESET KEYS AND BUTTONS. The controls are listed up, down, left, right, punch, super punch, kick, super kick, run, block,
start (keys of every player and the pad buttons).
