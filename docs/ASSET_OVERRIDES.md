# Replacing art with higher-quality assets

The game code refers to images by their label (e.g. `D4BK3A06`), never by
ROM address. The catalog (`src/assets/catalog.h`) resolves a label to the
original IMG pixels. When `<override_dir>/<LABEL>.png` exists, the renderer
draws that PNG instead (`src/video/gfx.c`).

## Format: indexed PNG

An override holds **palette indices, not colors**, just like the original.
The game recolors wrestlers, fades and flashes by changing color RAM, and
indexed art gets all of that for free.

- **Color type.** 8-bit (or 1/2/4-bit) indexed, or 8-bit grayscale whose
  values are the indices. RGB/RGBA files are rejected with a warning.
- **Palette.** Use the original image's palette, which `imgtool export
  --indexed` embeds. The PNG's own PLTE is ignored when the game runs,
  because color RAM decides the colors.
- **Transparency.** Index 0 is transparent.
- **Size.** It must be exactly k times the original on both axes (k = 2, 3,
  4, ...). A mismatch is reported and the original is used.
- **Framing.** Keep the framing. The anchor point stays at `(anix, aniy) * k`,
  so don't crop or re-center.

The main limitation of indexed overrides is that the art has to stick to the
original palette, usually 64 colors per wrestler. True-color overrides remove
that limit (next section).

## True-color (RGB/RGBA) overrides

`<LABEL>.png` may also be an 8-bit RGBA (or RGB) PNG, same size rule (k times
the original). At load time each opaque pixel (alpha >= 128; there is no
partial alpha) is matched to the nearest color that the *original* image's
palette uses, which gives the palette index the game logic and color RAM
need. What is left over (the true color minus that palette color, clamped to
+-127 per channel) is stored per pixel as a detail word, and the blitter
copies it into a parallel framebuffer plane (`video.detail`).

"The original image's palette" is the one the game draws it with. For twelve wrestler frames that is not the
palette their library gives them: one or two frames per walking library are marked with one of the wrestler's
other palettes (player 2's colours, Bam Bam's fire trail `BAMFRE_P`, the Undertaker's negative ...), but the game
draws every wrestler frame with the wrestler's palette (`OBJ_PAL`), and its wrestler frame headers have no palette
field. The game takes those other palettes from `WRESPAL.ASM`, not from the frames (see "Open questions" in
IMG_FORMAT.md for what the marks may have been). `tools/gsp/genimg.py` lists the frames in `gen/drawpal.txt` with
the palette they are drawn with, and the game maps their true-colour overrides onto that one. (Mapped onto the marked
palette,
a fire-coloured B1TT5Z02 came out as noise in Bam Bam's colours: the fire palette's near-identical shades stand for
quite different colours in his.)

When the frame is converted to RGB, `pal[index] + detail * dim` is shown,
where `dim` is 1 unless the color RAM entry is darker than the original
palette color, in which case it drops proportionally. So:

- fades to black fade the detail as well;
- flashes and alternate wrestler palettes keep the extra detail on top of
  the new base color;
- constant-color blits (shadows, white flashes) draw a flat color without
  detail.

Limits and open points (unverified, not checked against the real game):

- Only the residual is scaled, so a palette swap that changes hue a lot
  (e.g. a recolor to a very different color family) shows the *original*
  art's fine color differences on top of the new color, which may look
  off. Check the alternate-palette wrestlers in game.
- Alpha is binary. Soft edges would need a blend against the framebuffer.
- Loading a true-color override runs a nearest-color search per pixel, so
  the first draw of a large image costs a few milliseconds.

`tools/ai_upscale.py --truecolor` writes this format.

## Where overrides are read from: folders and zips

The override source (`--art DIR` on the desktop; on Android `art/hd`, then `art/hd.zip`, then `art` in the data
folder) can be:

- a folder of loose `<LABEL>.png` files,
- a folder holding one or more `.zip` files (and loose files too): the PNGs inside the zips are used as if they lay
  in the folder, at any folder depth inside the zip (only the file name counts),
- a single `.zip` file.

Loose files win over zipped ones, and the first zip in the folder listing wins when two hold the same name. Entries
may be stored (no compression) or deflated. Zip64, encrypted entries and zips of 2 GB or more are not read: split a
large set into several zips of up to about 1 GB (`src/assets/artsrc.c`). Backgrounds (`<BDD>_<n>.png`, below) come
from the same place.

Backgrounds (the `.BDD` images, e.g. `NEWRINGB_12`) have no palette in their image file. The build writes the one
each is drawn with to `gen/bddpal.txt` (`tools/gsp/genimg.py`, from the background tables `BGNDTBL.ASM` and
`BGNDPAL.ASM`), so they take true-color overrides like the sprites. Checked: a 4x true-color copy of every arena
piece gives the same picture as the original to within 1 per colour channel, and a grey copy shows grey.

## Layers: switching parts of the HD art on and off, and keeping them in folders

Every image belongs to one **art layer** (`src/assets/layers.c`): the 8 wrestlers (Bret, Razor, Undertaker, Yokozuna,
Shawn, Bam Bam, Doink, Lex), mugshots and names, the crowd, menu screens, HUD and fonts, effects, ring and props, other,
and the arena backgrounds. A wrestler's layer is the LOD script that selected the image (`BRET.LOD`, ...); for the rest
it is the IMG library the image comes from, listed by name in `layers.c`; the backgrounds (`<BDD>_<n>.png`) are their
own layer. The split of the non-wrestler libraries is a judgment from the image labels, not something the game's data says;
`./build/imgtool layers orig/IMG` prints how many catalog images each layer has and `imgtool layers orig/IMG list` every label
with its layer (tab separated). Counts at the vendored commit: wrestlers 642 / 681 / 632 / 627 / 684 / 719 / 669 / 669, mugshots
91, crowd 123, screens 137, HUD 1018, effects 941, ring 433, other 19 (8085 in all).

- **In the game:** F1, DISPLAY, HD ART LAYERS lists the layers with ON/OFF (Enter or left/right toggles one, ALL ON and ALL
  OFF do the rest). An image of a layer that is OFF is drawn from its original pixels, as without an override, and its
  override is not read from disk. It takes effect from the next picture after the menu closes and needs no restart. What is
  already loaded stays in memory (and on the GPU), so switching a layer back on is instant. SAVE SETTINGS keeps the choice
  (`art_off=<hex bit mask, bit n = layer n>` in `wwf.cfg`). The precache and its progress box leave OFF layers out.
- **In the art set:** a layered set is **zips, one per layer, each holding its layer folder** (`wrestlers_undertaker.zip` holds
  `wrestlers/undertaker/<LABEL>.png`, `hud.zip` holds `hud/...`, ...). The game reads a zip at any folder depth, only the file
  name counts, and a folder holding such zips (or `--art` pointing at one) is a normal override source. Loose files in sub
  folders are **not** read (a loose folder is listed one level only), so a layered folder of loose files is for editing and has
  to be zipped or flattened for the game; `imgtool catalog orig/IMG <dir>` shows how many overrides the game finds in it.
- **The tools write it directly.** `tools/upscale.py` and `tools/ai_upscale.py` take `--layers` (write `<out>/<layer>/<LABEL>.png`)
  and `--zip DIR` (also pack the result into the layer zips in DIR); `tools/remaster/remaster.py finish` now writes
  `remaster_<layer>.zip` (one per layer; `--flat` gives the old `remaster_partNN.zip`). A zip is split above `--max-zip-mb`
  (900; the game does not read zips of 2 GB or more). `--imgtool` and `--img` say where to get the layers (defaults
  `build/imgtool`, `orig/IMG`; the build needs the `layers` command). The grouping itself comes from `imgtool layers`, so
  the C code is the one place that has it (`tools/art_layers.py`).
- **A set that exists already:** `tools/art_pack.py` packs a flat folder (any depth) after the fact and never changes it:

      python3 tools/art_pack.py art/remaster/out art/layers            # art/layers/wrestlers_undertaker.zip, hud.zip, ...
      python3 tools/art_pack.py art/hd art/layers --folders [--move]   # loose layer folders, to edit (--move moves the files)

  Names that are not in the catalog but look like `<BDD>_<n>` go to `backgrounds`, anything else to `other` with a warning.
  Do not `--move` the output of a conversion that is still running and resumes from it (`remaster.py run` skips what is
  in `out/`: moved files would be made again).
  Checked: `upscale.py --layers --zip` on the Undertaker (632 images into `wrestlers_undertaker.zip`, all 632 found by
  `imgtool catalog`), and `art_pack.py` on 7122 images of a running remaster (19 zips, 6919 found in the catalog, the rest
  background pieces). Not checked: the game drawing from such zips (the reading code is the one that already reads zips at any
  depth, `tests/test_video.c`), an 8000 image set, Android.

## Loading while playing (`src/platform/gfx_async.c`)

Overrides are read when the game first draws an image, not at start-up (only the background pieces, which the game
draws from its own tables, are read when the machine starts). Reading and decoding a large PNG on the game thread
stalls the frame it happens in: a wrestler's new animation frame, the background scrolling to new pieces, the start of a
match. The SDL game therefore reads them on worker threads:

- **On demand.** The first time the game draws an image with an override, `gfx_get` (`src/video/gfx.c`) queues a job
  and marks the image pending (`hi_state` 2). Until the result is there the image is drawn from its original pixels,
  exactly like an image without an override, and a few frames later it switches to the HD art. Nothing in the game's
  logic can see the difference: the game works in original pixel units and only the renderer looks at `hi`/`hi_state`.
  The worker does the whole job (file or zip entry, inflate, PNG decode, and for true-colour art the mapping onto the
  palette with its detail words, `gfx_job_run`); the main loop attaches the result once a frame
  (`gfx_async_poll`, before the game runs), which only swaps pointers and bumps `gen` as a synchronous load does, so the GPU
  path rebuilds its textures. An image is queued once; a reload (`gfx_cache_reload`) or stopping the workers makes pending
  images "not tried" again, and results of jobs started before that are dropped.
- **Prefetch.** `wolf_prefetch_tick` (`src/wolf/wolf.c`, once a frame, only while async is on) watches the game state.
  From the "get ready" phase on it queues the images of the two wrestlers in the match (`index1`, `index2`: the
  wrestler's LOD script, the standing and walking libraries first) and then, whenever the final lineup
  (`FINAL_BATTLE_LINEUP`, the Royal Rumble and the WWF final) holds a fresh order of the 8 wrestlers, the others in
  the order they fight. The wrestler numbers map to LOD scripts as in `WRESTLE2.ASM` (0 Bret, 1 Razor, 2 Undertaker,
  3 Yokozuna, 4 Shawn, 5 Bam Bam, 6 Doink, 7 Lex). Prefetch jobs wait behind every image the game asks for: there are two
  queues, and a prefetched image the game then draws moves to the front of the first. The queue of prefetch jobs is
  bounded (24), the plan feeds it as jobs start, and results waiting to be attached are bounded too (32).
- **Memory.** Decoded images are kept (1 byte per pixel, plus 4 more for true-colour art), as before; there are 8085
  images and a wrestler has 600 to 700, so prefetch stops after 256 MB (`--art-prefetch-mb N`, 192 MB at most on Android;
  0 = no limit) and the rest is loaded when the game draws it. Images the game asked for are not counted. There is no
  eviction: an LRU that frees the HD data of images not drawn for some seconds (it is the `gfx_image_free_override` path, so
  the image would simply be "not tried" again) is the obvious next step if memory is tight; it was not needed here.
- **Switches.** `--sync-art` (or the environment variable `WWF_SYNC_ART`, or on Android an empty file `syncart` next to
  `wwf.cfg`) reads on the game thread as before; `--art-threads N` (default 2). `wwfrun`, the tools and the tests never
  turn async on, so they load synchronously and stay deterministic. The log line every 60 frames (Android) now counts what the
  workers finished: `hd art read from disk: N images, X ms (on the workers), M pending` (X is the sum of the workers'
  wall times, not a stall).
- **Thread safety.** A worker touches only the job it was given: `art_locate` (main thread: the zip index and the loose
  file lookup) hands it a copy of where the file is (`art_ref`), `art_read` opens its own file handle and shares nothing, and
  the inflate tables are no longer static. It reads the image's original pixels and palette, which are not changed while the
  machine runs, and nothing else of the cache.
- **Test aids.** `WWF_ART_DELAY_MS=N` makes every worker job take N ms longer (a slow device, to see the original pixels
  show first); `WWF_FRAME_STATS=1` prints the wall time of the frames when the run ends; `--art-settle FRAME` waits for
  all loads at every frame from FRAME on, so that a `--shot` run can be compared byte for byte with a `--sync-art` one.

Checked on the development machine only (Linux, software GL under Xvfb, `tests/test_gfx_async.c`, `tests/test_gfx_worker.c`
also under ThreadSanitizer and AddressSanitizer). Not checked: the Shield or any Android device (nothing is claimed about
its frame times), macOS and MSVC builds (the code uses only SDL's threads and C11). Not done: prefetch of the one-player ladder's
next opponents before the match is set up (they are queued when their match begins, `index2`), eviction, and a priority
between the demands themselves (they are served in the order the game asks).

## Workflow

```sh
# 1. export the game's images as indexed PNGs, plus metadata
./build/imgtool dump orig/IMG art/original --indexed

# 2. upscale or repaint in an indexed-color editor (Aseprite, GIMP in
#    Indexed mode, ...), keep the file names, save to e.g. art/hd/

# 3. check the result
./build/imgtool catalog orig/IMG art/hd
./build/imgtool render  orig/IMG D4BK3A06 check.png --scale 4 --override art/hd
./build/imgview         orig/IMG art/hd --scale 4 --label D4BK3A06
```

In `imgview`, press **O** to toggle overrides and **R** to reload them after
editing. Upscaled art only shows at `--scale` 2 or more, because the
renderer draws at 400x254 times that scale.

`manifest.json` lists each image's library, size, anchor (`anix`/`aniy`)
and palette.

## Automatic upscaling (`tools/upscale.py`)

Because overrides hold palette indices, the upscaler never blends colors. It
uses Scale2x/Scale3x (repeated for 4x, 6x, ...), so every output pixel is
copied from an input pixel and the original palette still applies. Output is
exactly k times the source, with PLTE/tRNS kept.

```sh
./build/imgtool dump orig/IMG art/original --indexed
python3 tools/upscale.py --lod orig/IMG/TAKER.LOD art/original art/hd --scale 2
```

`art/` is gitignored, so generated sets are not committed. The Undertaker
(632 images, 2x) was the first test. Hand-painted or AI-upscaled art can
replace individual files later.

## AI upscaling (`tools/ai_upscale.py`)

Runs Real-ESRGAN (`realesr-animevideov3`) locally. By default it converts the
result back to an indexed override; with `--truecolor` it writes an RGBA
override instead (recommended, see above). In indexed mode the silhouette comes from the Scale2x/3x pass, and each
opaque pixel is snapped to the nearest palette color the source image uses
(never index 0). Needs `torch numpy pillow`; on a Mac it uses the GPU via MPS.

```sh
python3 tools/ai_upscale.py --lod orig/IMG/TAKER.LOD art/original art/hd_ai --scale 2 --truecolor
```

The neural step itself has not been run in the repo's CI or by the author of
the tool (no PyTorch in that environment); only the alpha/palette steps were
checked, using `--model lanczos`. Snapping to a 64-color palette removes much
of the network's extra detail, so expect a modest gain. Check the result with
`imgview` before upscaling everything.

## Finding a bad image in the game (`wwf --inspect`)

With `--inspect` the game keeps the last few seconds of frames (up to about 512 MB) and, for each, the list of
images it drew. **F6** freezes it on the newest frame and boxes every image with its label:

- green: drawn from a high-resolution override
- orange: the original art
- magenta: a fill (a constant colour, e.g. the game's own white flash)

Keys while frozen:

| Key | Action |
|-----|--------|
| **←** / **→** | step back and forward through the kept frames; **→** at the newest runs the game one frame on |
| **Shift** + **←** / **→** | ten frames at a time |
| **↑** / **↓**, or a click | select an image; its label, position, size and palette are shown |
| **C** | copy the selected image's label to the clipboard (and print it) |
| **B** | also box the background pieces |
| **H** | hide the boxes (except the selected one) |
| **F6** or **Esc** | carry on playing |

So a frame that blinks or looks wrong can be stepped back to and named, then redone, for example with
`tools/remaster/remaster.py run --only LABEL --redo`. `wwfrun --draws FRAME` prints the same list for a frame of a
headless run.

## Generative remaster (`tools/remaster/remaster.py`)

Redraws every image with SDXL in a local ComfyUI, fits the results to these rules and packs them as zips for
`art/hd/`. See [REMASTER.md](REMASTER.md).
