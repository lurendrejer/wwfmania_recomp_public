# Generative remaster of the art

`tools/remaster/remaster.py` redraws every image the game shows at 4x with an image model (SDXL) running
locally in ComfyUI, and packs the result so the game uses it (docs/ASSET_OVERRIDES.md). It is an experiment:
a generative model adds detail the original never had, so it can look much better, but it can also drift from the
original (faces, costumes) and flicker between the frames of an animation. Try a few images first.

Nothing here is needed to build or run the game, and the art it makes stays out of the repository (`art/` is
ignored).

## What it does

1. `prepare` exports the 8,629 images the game draws from the build: every sprite (the twelve wrestler frames
   marked with another palette in the palette the game draws them with, `gen/drawpal.txt`), the background pieces in
   the palette the game draws them with (`gen/bddpal.txt`), and the art of the referee and fireball mods. It writes them
   to `art/remaster/src/` with a manifest (category, wrestler, animation).
2. `run` sends each image to ComfyUI, one at a time, through `tools/remaster/sdxl_esrgan_tile.json`:
   - The transparent parts are filled with neighbouring colours.
   - An upscale model (Real-ESRGAN x4plus) enlarges the image 4x, sharp and without the pixel steps, and it is
     scaled to about 1024 pixels.
   - SDXL redraws that (image to image, `denoise`), adding texture and detail, held to it by a tile ControlNet.
     (`--workflow tools/remaster/sdxl_tile.json` skips the upscale model and gives SDXL a softened enlargement
     instead. That was the first version; it came out blurry.)
   - The prompt names the wrestler (or arena, crowd, referee ...). Every frame of one animation gets the same seed.
   - The result is scaled to exactly 4x the original and cut to the original's outline, with alpha 0 or 255 as the
     game needs.
   - By default (`--keep-color 1`) it takes its colours and lighting from the original and only its fine detail from
     the model. This keeps the game's palette effects (player 2's colours, flashes, fades) looking right and reduces
     flicker. `--keep-color 0` keeps the model's own colours.

   Results go to `art/remaster/out/LABEL.png`. A run can be stopped and started again: images already in `out/` are
   skipped (unless `--redo`).
3. `finish` checks every result (size, RGBA, alpha 0/255) and packs them as uncompressed zips,
   `art/hd/remaster_part01.zip` and on, of at most 900 MB each. The game reads them from there when started with
   `--art art/hd` (other zips and loose PNGs in the folder are used too).

Fonts are left out unless asked for (`--fonts`, or `--only` naming them): the model tends to change letters. The
Robotron and Adam Bomb art is never drawn and is skipped.

## Setting up (macOS, Apple silicon)

1. **ComfyUI**: install the desktop app from comfy.org (it runs on the Mac's GPU through Metal) and start it. The
   script talks to it at `http://127.0.0.1:8188`; pass `--url` if yours differs (the desktop app may use port 8000).
2. **Models**, into ComfyUI's `models` folder:
   - `models/checkpoints/`: an SDXL checkpoint. The default is the photographic RealVisXL V5.0
     (`RealVisXL_V5.0_fp16.safetensors`, Hugging Face `SG161222/RealVisXL_V5.0`).
   - `models/controlnet/`: the SDXL tile ControlNet from Hugging Face `xinsir/controlnet-tile-sdxl-1.0`, saved as
     `xinsir-controlnet-tile-sdxl-1.0.safetensors`.
   - `models/upscale_models/`: `RealESRGAN_x4plus.pth` from the Real-ESRGAN releases on GitHub (xinntao/Real-ESRGAN,
     release v0.1.0). Any ESRGAN-type model ComfyUI loads works with `--upscale-model NAME`, for example 4x-UltraSharp.

   Other file names or models work with `--checkpoint NAME` and `--controlnet NAME` (names as ComfyUI lists them).
3. **Python** for the script: `python3 -m venv .venv-remaster && . .venv-remaster/bin/activate &&
   pip install pillow numpy`.
4. A build of the game (`cmake -S . -B build && cmake --build build`), which gives `build/imgtool` and `build/gen`.

## Using it

    python3 tools/remaster/remaster.py prepare
    python3 tools/remaster/remaster.py run --only doink --limit 20     # a first try: 20 Doink frames
    python3 tools/remaster/remaster.py finish
    ./build/wwf --art art/hd --scale 4                               # or render scale 4 in the F1 menu

`--only` takes words matched against the label, category, library and animation, comma separated, for example
`--only newringb` (the arena), `--only undertaker`, `--only wwfmugs` (the portraits), `--only D2KB3A` (one
animation). Look at the results in `art/remaster/out/` and in the game, then tune and redo:

| Option | Default | Effect |
|--------|---------|--------|
| `--denoise` | 0.4 wrestlers and backgrounds, 0.35 other | how much the model may change: 0.25 stays close, 0.55 and up invents more (and drifts and flickers more) |
| `--control` | 0.6–0.65 | how hard the ControlNet holds it to the original's shapes |
| `--keep-color` | 1 | 1 = the original's colours and lighting, 0 = the model's |
| `--color-radius` | 3 | how coarse that colour is, in original pixels; smaller brings back the original's blotches |
| `--steps`, `--cfg`, `--seed` | 28, 5, 0 | sampler settings; `--seed` changes every animation's seed |
| `--redo` | off | redo images already in `out/` |

When a setting looks right, run everything (`run` without `--only`; the wrestlers first, then backgrounds, other,
mods) and then `finish`. `run` prints the time per image and an estimate of what is left.

## Finding bad results

Start the game with `--inspect`. When something blinks or looks wrong, press **F6**, step back with **←** to
the frame, click the image and press **C**: its label is copied. Redo it, or its whole animation, with
`run --only LABEL --redo` (see "Finding a bad image in the game" in ASSET_OVERRIDES.md).

## Open points (unverified)

- Nothing here has been run with a real model yet: the ComfyUI calls, the post-processing and the packing were
  tested against a stand-in for ComfyUI's API, and the packed zip was checked in the game. The prompts, the defaults
  and how good the result looks are guesses until tried.
- Time: SDXL at about 1024 pixels on an Apple silicon laptop is probably tens of seconds per image, so the whole set
  is likely days, not hours. Run it in parts with `--only`.
- Flicker between the frames of an animation and drift of the wrestlers' faces are the known weak points of the
  approach; `--keep-color` and the per-animation seed reduce them, a lower `--denoise` more so.
- The game keeps each sprite's original palette (to recolour player 2, fade, flash) and stores only a limited
  difference on top of it, so colours far from the original are pulled back toward it.
