# Contributing

Thanks for looking at the port. Bug reports, fixes and new mods are welcome.

## What must never be in a pull request

The original game is Midway's. This repository does not contain it, and a pull request must not add it:

- no original source or art (`orig/`, anything copied out of it), no ROM images, no sound ROMs, and nothing extracted from
  them (WAV files, `sounds.txt`);
- no AI-redrawn or upscaled art made from the original art (`art/`), no zip files of it;
- no screenshots that show a lot of original art beyond what the README already uses.

`orig/`, `art/`, `sounds/` and `*.zip` are in `.gitignore`; please do not force them in. Mods load their own assets and
must be written so that they work from the original data fetched by `tools/fetch_orig.sh`.

## Building and testing

```sh
tools/fetch_orig.sh                          # the original game into orig/ (once)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Every change should leave all tests passing. A loader or a mod gets tests in `tests/` or in `CMakeLists.txt` (synthetic data
always, plus checks against `orig/IMG` when it is present).

## Conventions

- C11, no compiler extensions, warning-free with `-Wall -Wextra -Wpedantic`; portable (Linux, macOS, Windows/MSVC).
  Build-time tools (the assembler front end and code generators in `tools/gsp/`) are Python 3 with the standard library only.
- Core libraries have no external dependencies. SDL2 is allowed only in `src/platform/`.
- File data is little-endian and is read through byte helpers, never by casting structs.
- Game logic works in original pixel units; only the renderer knows about high-resolution art.
- New features that were not in the original game go in `mods/<name>/`, never in `src/` or the code generated from `orig/`.
  A mod is off unless switched on, and without one the game must behave as the original (docs/MODS.md).
- Record anything you did not verify under "Open questions" in the relevant doc. Do not present guesses as facts.

See `CLAUDE.md` for the same rules in more detail.

## Pull requests

Keep them small and about one thing. Describe what you changed and how you tried it (which platform, which tests). Pull
requests are squash-merged into `main`, so the title of the pull request becomes the commit message: make it say what the
change does.
