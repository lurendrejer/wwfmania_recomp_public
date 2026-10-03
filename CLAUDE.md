# wwfmania_recomp: notes for Claude

A C11 port of WWF WrestleMania (Midway Wolf Unit, TMS34010) from the assembly
source in `orig/` (a copy of historicalsource/wwf-wrestlemania that
`tools/fetch_orig.sh` downloads; it is not in this repository; never edit it).

## Conventions

- Build-time tools (assembler front-end, code generators in `tools/gsp/`)
  are Python 3 with the standard library only. Everything that ships is C11.
- C11, no compiler extensions, and it must stay warning-free with
  `-Wall -Wextra -Wpedantic`. Portable: Linux, macOS, Windows (MSVC).
- Core libraries (`src/assets`, `src/video`, game code) have no external
  dependencies. SDL2 is allowed only in `src/platform/`.
- File data is little-endian. Read it through byte helpers (`rd16`/`rd32`),
  never by casting structs.
- Images are identified by their **label name** (as written in the LOD
  scripts and referenced in the .ASM), never by ROM address. That is what
  keeps the art replaceable.
- Game logic works in original pixel units. Only the renderer knows about
  high-res overrides.
- Drawing goes through `video_dma` (`src/video/video.h`) with the same
  registers the original DMA code set (ctrl, palette, constant, scale).
  See `docs/VIDEO.md` for how the original's flip and clip math maps onto it.
- Every loader gets tests in `tests/`: synthetic data always, plus checks
  against `orig/IMG` when it is present, with counts pinned to the vendored
  commit.
- Record anything unverified in `docs/IMG_FORMAT.md` under "Open
  questions". Don't present guesses as facts.

## Mods

- Features that were not in the original game go in `mods/<name>/`, never in
  `src/` or the code generated from `orig/`. A mod is off unless run with
  `--mod <name>`; without one the game must behave as the original.
- Mods use only the hooks in `src/mods/mods.h` and load their own assets.
  See `docs/MODS.md`. Adding a hook to the machine must not change anything
  when no mod is enabled.
- The wide view (`video.view_pad`, `wolf_set_draw_margin`, `--classic`) is not a
  mod: it widens what is shown and how far the game culls (docs/VIDEO.md).
  `--classic` must give the original 400x254 view and the original limits.

## Translating assembly (steps 3 and 4)

- The source is assembled by `tools/gsp/gspasm.py` (see `docs/GSPASM.md`),
  which reproduces preasm/GSPA/gsplnk behavior. Symbols are case-sensitive.

- Keep one C file per .ASM module, with the same base name in lowercase.
- Keep the original labels as C identifiers where possible, so code can be
  compared line by line with `orig/`.
- Addresses in TMS34010 code are bit addresses. Field offsets in the .EQU
  files (e.g. `ISAG .equ 40h`) are in bits.

## Sound

- The sound ROMs and anything extracted from them (WAVs, `sounds.txt`) are
  copyrighted and never go into the repository.
- The game does not emulate the DCS board. The ADSP-2105/DCS emulation
  lives only in `tools/dcs` (used by `dcsrip`). The game plays the
  extracted files through `src/sound`, which has no dependencies. Sounds are
  identified by their command code, and every file must stay replaceable.
- Board behavior the player needs (stop codes, replies, volume curves) comes
  from `sounds.txt` as measured by dcsrip. It is not hard-coded. See
  `docs/SOUND.md`, and record anything unverified under "Open questions"
  there.

## Build and test

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
