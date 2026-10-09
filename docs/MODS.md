# Mods

New features live apart from the port of the original game, so the port can
keep being compared with `orig/` and checked for fidelity.

## The rule

- The port (`src/`, and the code generated from `orig/`) is the original
  game. Features that were not in it do not go there.
- A mod is a directory `mods/<name>/` with its own code, assets and README.
- A mod is **off unless enabled at run time** with `wwf --mod <name>` (or
  `wwfrun ... --mod <name>`). With no `--mod` the game runs exactly as
  without this layer.
- A mod uses only the hooks below and the public structures (`wolf`,
  `video`, `catalog`). It never edits `orig/` or the recompiled code.
- A mod loads its own assets (its own `catalog`, its own files) and leaves the
  game's catalog alone.
- Mods are C11, warning-free like the rest, and have tests in `tests/`.

## Hooks (`src/mods/mods.h`)

| Hook          | When                                                        |
|---------------|-------------------------------------------------------------|
| `init`        | once, when enabled; return 0 with a message to refuse       |
| `frame_begin` | each video frame, before the game code runs                 |
| `frame_end`   | each video frame, after it ran and the display page is final |
| `shutdown`    | when the game exits (reverse order of enabling)             |

All are optional. Each mod gets a `void *state` of its own.

## Switching mods on and off

The F1 menu has a "MODS" page listing the mods built in (`wwf --list-mods`):
Enter switches one on or off while the game runs (its `init` or `shutdown`
hook runs at once; a mod that cannot start shows why on the last line). Save
settings writes them as `mod=<name>` lines in `wwf.cfg`, and they are started
with the game next time. `--mod NAME` still starts one for a single run, without
saving it. Switching a mod off does not undo what it already changed in the
game's memory unless its `shutdown` does.

## A mod's number

A mod can have one number the player sets: `arg_min`, `arg_max`, `arg_default` and
`arg_label` in `wwf_mod`. `--mod NAME=N` sets it, left/right on the mod's line in the F1 menu
changes it (the line shows `<N>`), Save keeps it (`modarg=NAME=N` in `wwf.cfg`), and the mod
reads it with `mods_arg(w, &mod_NAME)`, typically every frame.

## Adding a mod

1. Create `mods/<name>/CMakeLists.txt` with `wwf_add_mod(<name> mod.c ...)`.
2. In `mod.c`, define `const wwf_mod mod_<name> = { "<name>", "one line", ... };`
3. Re-run CMake. The list of built-in mods (`mods_builtin.c`) is generated
   from `mods/*/`, so nothing else needs editing.
4. `wwf --list-mods` shows it. Add a README in the mod's directory and a test.

`-DWWF_MODS=OFF` leaves the mods out of the build. The hook dispatch stays,
with an empty list.

A mod can also carry a LOD script of its own (`lod NAME` with `srcdir` holding
`NAME.LOD`, as `mods/bamfire` does for images no original LOD lists); the
generator copies it to `gen/lod/` and the catalog reads it from there.

A mod can also have a generator: `script FILE` in its `gen.txt` is run by `tools/gsp/genimg.py` before anything
else, as `FILE --src orig --out build/gen`. It can write modules into `build/gen` (searched before `orig/`) and IMG
libraries into `build/gen/lod`, where the image tables and, for a LOD the mod carries, the runtime catalog look for
libraries that `orig/IMG` does not have. `mods/adambomb` makes a whole wrestler that way (`gen/mkadam.py`).

A mod that brings a wrestler of its own gets a square on the select screen from the machine: up to two extra
squares (`src/wolf/asm/XSQUARE.ASM`, edits in `src/wolf/core.gen.txt`). The mod fills its square's entries every
frame from C (`xsq_on` bit, wrestler number, a "small" flag, crouton, mugshot pieces and name image, found with
`wolf_symbol_addr`). One extra square stands under the eight in the middle, two stand side by side; with none the
screen is the original's. Square A is `mods/adambomb`'s, B `mods/dink`'s. The choice of a square marked small sets
the player's bit in `xsq_smallp`. `mods/fourplayer` moves its squares over them too.

The code and data of all mods go to a ROM of their own, the megabyte below the program ROM (0xFF000000-0xFF7FFFFF,
`EXT_ROM_BASE` in `tools/gsp/gspasm.py`): the original's megabyte has only some 30 KB free, and the original modules
keep their addresses.

## Limits (unverified)

- An edit that adds a test of a mod's switch to code the game runs (`move @x_enabled,a14` / `jrz`) costs a few
  instructions even with the mod off. The machine's timing follows the instructions run, and the game's random numbers
  follow the timing, so a build with mods may scatter sparks and pick at random differently from one without, from
  the first screen that runs such an edit (checked: the select screen, with the mods of this repository). The game is
  otherwise the same.
- Hooks see the machine between frames only. A mod that must change what the
  recompiled game does *inside* a frame (for example spawning a process) has
  no hook for that yet; it would need one added to the machine, kept as small
  as possible and off when no mod is enabled.
- Whether a mod can find game state (wrestler positions and so on) by symbol
  name from `build/gen/symbols.txt` has not been tried.

## Mods

| Mod       | State |
|-----------|-------|
| `referee` | the unused referee walks around the ring; see [mods/referee/README.md](../mods/referee/README.md) |
| `bamfire` | Bam Bam's unused fireball throw, Down + Toward + Punch; see [mods/bamfire/README.md](../mods/bamfire/README.md) |
| `easymoves` | every special move as Toward, Toward + button; see [mods/easymoves/README.md](../mods/easymoves/README.md) |
| `moredrones` | 1–4 computer opponents at once in a one-player or cooperative match, `--mod moredrones=N`; see [mods/moredrones/README.md](../mods/moredrones/README.md) |
| `morebuddies` | a second buddy for each player in buddy mode; see [mods/morebuddies/README.md](../mods/morebuddies/README.md) |
| `doinkpie` | Doink throws his unused pie, Down + Down + kick; see [mods/doinkpie/README.md](../mods/doinkpie/README.md) |
| `fourplayer` | the two computer partners of buddy mode played by a third and a fourth person; see [mods/fourplayer/README.md](../mods/fourplayer/README.md) |
| `training` | practice mode: held clock, refill or infinite life and turbo, collision boxes drawn; see [mods/training/README.md](../mods/training/README.md) |
| `rounds` | Wins needed to win a match; see [mods/rounds/README.md](../mods/rounds/README.md) |
| `cpuskill` | CPU difficulty; see [mods/cpuskill/README.md](../mods/cpuskill/README.md) |
| `damage` | A factor on all damage; see [mods/damage/README.md](../mods/damage/README.md) |
| `matchtime` | The length of a round in seconds; see [mods/matchtime/README.md](../mods/matchtime/README.md) |
| `nodebris` | No blood and no debris.; see [mods/nodebris/README.md](../mods/nodebris/README.md) |
| `sansring` | no ropes; walk out of the ring at once; see [mods/sansring/README.md](../mods/sansring/README.md) |
| `bottombuckles` | climb the near turnbuckles too; see [mods/bottombuckles/README.md](../mods/bottombuckles/README.md) |
| `brokenrecord` | the commentators only ever say one line; see [mods/brokenrecord/README.md](../mods/brokenrecord/README.md) |
| `wrongnames` | the announcer calls every wrestler by someone else's name; see [mods/wrongnames/README.md](../mods/wrongnames/README.md) |
| `chatterbox` | the commentators comment on everything; see [mods/chatterbox/README.md](../mods/chatterbox/README.md) |
| `moongravity` | less gravity, longer flights; see [mods/moongravity/README.md](../mods/moongravity/README.md) |
| `disco` | every colour turns around the colour wheel; see [mods/disco/README.md](../mods/disco/README.md) |
| `adambomb` | Adam Bomb, cut for Lex Luger, on a select square of his own and in the ring, with Razor Ramon's moves and as much of his own art as there is; see [mods/adambomb/README.md](../mods/adambomb/README.md) |
| `dink` | Dink, Doink's little sidekick: Doink at half the size, on a select square of his own; see [mods/dink/README.md](../mods/dink/README.md) |
| `chair` | the cut folding chair: Down + Down + Block at the side of the ring picks it up, walk with it, any button swings it; see [mods/chair/README.md](../mods/chair/README.md) |
| `coopladder` | two people play the one-player championship ladder together: COOP starts it, `--mod coopladder=N` (0 rumble, 1 Intercontinental, 2 WWF, 3 ask); see [mods/coopladder/README.md](../mods/coopladder/README.md) |
| `outsidedive` | running attacks at an opponent outside the ring go over the ropes and land on him (standing or lying); see [mods/outsidedive/README.md](../mods/outsidedive/README.md) |

## Switching mods while the game runs

The F1 menu has two mod pages, TWEAKS (the mods that can be switched at any time) and MODS (the others, which show from
the next match or start). Both switch a mod on or off at once, ENTER = on/off, LEFT/RIGHT = the mod's number, shown as
words (`mod_value_text` in `src/platform/menu.c`, by the mod's name; a new mod with a number needs a line there, or the
number is shown as it is). SAVE keeps what is on for the next start. Which page a mod is on is the mod's `live` field (`wwf_mod`, `src/mods/mods.h`):

- `live = 1`: the mod only reads and writes what its hooks set every frame and `shutdown` puts it back, so it can be
  switched at any time, also in a match: damage, matchtime, cpuskill, moongravity, nodebris, disco, training,
  chatterbox, brokenrecord, wrongnames, easymoves, sansring, bamfire, doinkpie, outsidedive. The tests `mod_live_*` switch each one
  on and off in the select screen and in a match with `wwfrun --mod-on NAME[=N]@FRAME` / `--mod-off NAME@FRAME`
  and check that the match is reached and goes on. They do not check each mod's effect when it is switched on late.
- `live = 0` (the MODS page): the mod builds something when a match or the game starts (wrestlers, objects,
  the select screen, the ladder, the roster). Switching it changes nothing in a running match; it shows from the
  next match, or the next start for the ladder, the roster and the select screen: adambomb, bottombuckles, chair,
  coopladder, dink, fourplayer, morebuddies, moredrones, referee, rounds. Switching them is safe but, as far as
  tried, not complete until then.
