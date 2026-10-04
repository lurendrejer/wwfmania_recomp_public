# Video model

`src/video/` models the Wolf Unit video hardware the way the game code uses
it, so the translated display code (`DISPLAY.ASM`, `PAL.ASM`) can drive it
almost 1:1.

## Framebuffer and color RAM

- The screen is 400x254 (`SCRNMID = [128,200]` in `orig/DISPLAY.EQU`).
- Each framebuffer entry is 16 bits, `palette << 8 | color`. It is an index
  into color RAM, not an RGB value.
- Color RAM holds 128 palettes of 256 colors in `xRRRRRGGGGGBBBBB`. The game
  allocates up to 80 (`NUMPAL`, `orig/SYS.EQU`), and `pal_find`/`pal_getf`
  return palette numbers as `n * 0x101`.
- RGB conversion happens only at presentation (`video_to_argb`), so fades
  and palette swaps affect everything already drawn, as on the hardware.

## Resolution

`video_init(v, scale)` makes the framebuffer `scale` times larger. All
callers use original screen coordinates. Each blit covers
`scale x scale` framebuffer pixels per original pixel, and each one samples
the pixel center of its source:

- with no override, the source is the IMG image (nearest neighbor);
- with an override at factor k, the source is the k-times image, so detail
  finer than an original pixel shows up whenever `scale > 1`.

## DMA blit (`video_dma`)

| Field     | Hardware register         | Notes                                            |
|-----------|---------------------------|--------------------------------------------------|
| `x`, `y`  | `DMAHORIZ`, `DMAVERT`     | top-left of the drawn rectangle (see flip below) |
| `ctrl`    | `DMACTRL`                 | `DMA_WZ/WNZ/CZ/CNZ`, `DMA_FLIPH/FLIPV`           |
| `pal`     | `DMACMAP`                 | bits 8–14 select the palette                     |
| `color`   | `DMACONST`                | low 8 bits, drawn as `pal | color`               |
| `scale_*` | `DMASCALEX/Y`             | 8.8; `0x100` = 1:1, larger values shrink         |
| window    | `DMAWINDOW` (`DMALT/DMARB`) | `video_set_window`, inclusive, clamped to screen |

Write rules per source pixel:

- **Non-zero pixel.** With `CNZ` it becomes `pal | color`. Otherwise, with
  `WNZ`, it becomes `pal | pixel`. Otherwise it is skipped.
- **Zero pixel.** With `CZ` it becomes `pal | color`. Otherwise, with
  `WZ`, it becomes `pal`. Otherwise it is skipped.

**Flip.** The original code moves the DMA start point to the far edge
(`+ WIDTH-1` in `dma_objlst2d`), because the hardware draws backwards. In
the port, `x,y` is always the top-left of the rectangle and flipping
happens inside it. When translating, drop that adjustment but keep the
`ODOFF` anchor mirroring.

**Clipping.** The original code adjusts `SAG` and the offset register to
clip. In the port the blitter clips against the window itself.

**Row skip** (`DMAOFFST`, register 0) drops pixels from every row. Per
MAME's midtunit DMA: with control bit 6 the low byte is a start skip and
the high byte an end skip. Without it, the whole word is an end skip. A
start skip moves the source but not the destination. The life bars depend
on it: `update_meter` hides the lost health through the object's `OFSET`.
The Wolf Unit layer (`dma_run` in `src/wolf/wolf.c`) applies it before
calling `video_dma`.

**Zero compression** (`ctrl` bit 7) is a ROM storage detail. The port
reads uncompressed IMG pixels, so it is ignored.

## Wide view

The bitmap is 512 pixels wide and the screen sits at `SCRNXP = 56`
(`orig/DISPLAY.EQU`), so 56 pixels on each side can be shown.

The game only draws what passes its cull limits `SCRNTL`/`SCRNLR`
(variables set once at start-up from `SCRNST`/`SCRNEND`, x = -32..432). With
the original limits a view wider than 464 shows black at the edges, but only
for some parts: the ring and floor reach further (checked in a match with a
56 pixel view), while single background parts such as parts of the crowd and
wall do not.

`wolf_set_draw_margin` rewrites the X halves of those two variables before
every frame (addresses from `symbols.txt`), so the game draws the same
things over -margin..400+margin. With 56 the missing pieces fill in for the
most part (checked in the same match: the side "RECOVER" signs appear).

`video.view_pad` widens the displayed area by that many original pixels on
each side; `video_view_width/height` give its size. The SDL window sets it
from its own shape (`sdl_video_fit`): about `254 * width / height` pixels
wide, at most 56 extra per side, so a 16:9 window gets about 452x254.
`wwf --classic` keeps 400x254 and the game's own limits.

### Wider than the bitmap (test)

The 512 pixel width is only how the game addresses VRAM. The blitter is C
here, so `wolf_set_extra_width(w, N)` (`wwf --wide N`, `wwfrun --extra N`)
makes the port's bitmap `512 + 2 * N` wide: every x the game uses (DMA
position, CPU VRAM access) is shifted right by N, and a clip window of
0..511 means the whole bitmap. The view and the draw margin can then be
`56 + N`. With `--extra 200 --view-pad 256 --margin 256` a match frame is 912
pixels wide and the picture continues to the right; on the left it ends where
the level ends (black), because the camera never scrolls further than the
level. Checked on one frame in a match only.

### `--res WxH` and the shape of the view

`wwf --res 1920x1080` opens the window at that size; without `--res` the
desktop's shape is used (that is what F11 gives). The size can also be chosen in the F1 menu (Display and
speed, "WINDOW SIZE": auto or a list of common sizes) and is kept in `wwf.cfg` as `res=WxH`; the view is planned
at start, so it takes effect after "APPLY AND RESTART". `--res` wins over the file, and a restart drops it
from the command line since the settings hold the size then. `main_game.c` plans the view
from the shape `a = W / H`:

- `a` up to 512/254 (about 2.0): the view is 512 columns (56 extra per side, the
  game's own bitmap) and `512 / a` rows, so extra rows above and below.
  16:9 gives 512 x 288 (17 extra rows), 16:10 512 x 320, 4:3 512 x 384.
- wider than that: 254 rows and `254 * a` columns; the bitmap is widened
  (`wolf_set_extra_size`), 3440x1440 needs about 96 extra columns per side.

The render scale is `ceil(H / 254)` (at most 8) unless `--scale` is given.
The window always renders the whole planned view and shows the largest part
that fits its current shape.

### Extra rows

The two display pages are `256 + 2 * extra_y` rows apart in the port's bitmap;
the game's row 0 of each page is at `extra_y`. `wolf_set_extra_size` does the
mapping: the DMA position and CPU VRAM access are shifted page by page (the
page comes from the DMA clip window: top >= 256 means page 1), the clip window
means the whole page, and the page erase clears the extra rows too. The
game's cull limits get the same margin (`wolf_set_draw_margin_y`, the Y halves
of `SCRNTL`/`SCRNLR`). Checked in a match with 100 extra rows: crowd above and
floor below, HUD unchanged at the original rows.

Rows 512 to 1023 of the game's VRAM are not display pages, but the power-up
test writes and reads all of them, so they get room of their own after the two
pages (the bitmap is `2 * (256 + 2 * extra_y) + 512` rows). Without that the
test failed with extra rows and the game showed "RAM CHIPS BAD / PRESS ANY
BUTTON" at start (then ran on after a button press). The tests `selftest_wide`
and `game_wide` cover it.

Vertical extent of the background of the test match's level, from forcing the
camera (`WORLDTLY`) to several values and looking for black: world y from -142
to 405. The game's own camera limit at the front fence is 151 (`0x97`,
`scroll_world`), i.e. the screen bottom 405, so there is no slack below; above,
the camera went to -106 in the match. `wolf_set_scroll_inset_y(n)` keeps
`WORLDTLY` in `-142 + n .. 151 - n` on a level screen (`wwf` uses `n` = the
rows visible above the screen). A 13000 frame run with 65 extra rows and 56
extra columns showed no black in the match frames sampled every 500 frames (4000
to 10000). Only one level, one match, the AI playing against an idle player.

### Resolution, scale and zoom

Three separate settings in `wwf`:

| Setting | What it is | Default |
|---------|------------|---------|
| `--res WxH` | the window's size in pixels | 512 x 254 game pixels times `--scale` |
| `--scale N` | render scale: framebuffer pixels per game pixel, i.e. how sharp the art can be (and the memory used) | with `--res`: about one window pixel per framebuffer pixel at the start zoom (`ceil(H * zoom / rows)`, at most 8); otherwise 3 |
| `--zoom Z` | how much of the planned view is shown: above 1 magnifies the middle, below 1 shows more than the planned view (down to `--min-zoom`); the wheel and `+`/`-` change it while running, `0` resets to 1 | 1 |
| `--min-zoom Z` | the smallest zoom (0.25 to 1). The bitmap is made big enough to fill the window at that zoom, so it costs memory (1 = no zooming out) | 0.5 |

The start prints e.g. `view 512x288 game pixels (rendered 1024x576), render scale 4, zoom 1.00 (0.50 to 4)`. The
window title shows the current zoom. Zoom beyond what `--scale` renders is
just magnified (blurrier); raise `--scale` for a sharper zoom.

### Zooming out

The planned view (zoom 1) is what the window shape needs: 512 x 288 for 16:9.
For `--min-zoom 0.5` the bitmap is rendered twice as big in each direction
(1024 x 576 for 16:9: `wolf_set_extra_size` with 312 extra columns and 161
extra rows), the game's cull limits follow, and the window shows the planned
view divided by the zoom. `sdl_video_present` converts only the visible part
of the framebuffer each frame, so a big bitmap costs memory but not much time.
When the view is larger than the level (a 576 row view is taller than the
547 rows of background) the camera is pinned to the middle of its range
instead of being clamped, which leaves about 10 to 15 black rows at the top and
bottom. Seen in one match at 1024 x 576 only; performance in a real window,
and other window shapes at low zoom, were not checked. The scale is reduced
if the bitmap would exceed 160 million entries.

### Dynamic zoom

F1 > Display and speed > DYNAMIC ZOOM (`dyn_zoom=1` in `wwf.cfg`, off by default), with two limits, FARTHEST OUT
(`dyn_min`, default 50) and CLOSEST IN (`dyn_max`, default 100), both 10 to 200 percent (100 = the normal view, 10 shows
ten times as much). In a match the zoom follows how far apart the wrestlers are: it goes out until they fit with 110 pixels of
room on each side, and back in when they come together, always within the two limits. The normal zoom setting is not
used while it is on. `wolf_wrestler_extent` reads the x position (`OBJ_XPOSINT`) of the wrestler processes 0 to 5, leaving out the dead (`PLYRMODE` `MODE_DEAD`; a wrestler who is only knocked down still counts; if all are dead, all count); the
game's camera still centres on them itself. It zooms out by 10 percent of the gap per frame and in by 3 percent.
Only the width is considered, not the depth.

The view is sized at start for the farthest-out limit (`--min-zoom` now accepts 0.1 to 1, and is lowered to `dyn_min`
when the dynamic zoom is on), so after going farther out than before use APPLY AND RESTART. At 10 percent a 16:9 window
renders 5120 x 2880 game pixels (the extra rows are limited to 1400, the extra columns to 3000) and the render scale is
lowered by the existing 160 million entry limit, here from 3 to 2. Checked in a four-wrestler match at 1280x720 with
limits 10 and 200: it started without a memory error, zoomed in (200 percent) when the wrestlers were together and out to about
100 percent for four spread across the ring. Not checked: zooming out beyond that in a very wide fight, the speed at 10
percent, or other window shapes.

### Zoom

The zoom only applies during a match (`GAMSTATE` INGAME, `wolf.in_match`); the game's own menus and screens
(select, program, high scores, attract) are shown at zoom 1, since they were drawn for the 400 x 254 screen and look
odd at 0.5. The wheel and `+`/`-` still change the zoom for the next match.

The SDL window always renders the whole wide view (`view_pad` at its maximum)
and shows a middle part of it (`sdl_video_present`): the full height divided
by the zoom, and as wide as the window's shape asks for, at most what was
rendered (narrower windows get bars). Mouse wheel or `+`/`-` zoom, `0` resets;
range 1..4. Zoom 1 is what `--res` describes above. The camera inset follows
the width actually visible, so zooming in frees the camera again. Zoom crops
around the middle of the screen, also vertically (the game scrolls up and
down as well, and that is unchanged).

### Recover meter bars

`RECVR_L`, `RECVR_R` and `RECVRBLK` (the recover meter's side bars) sit outside
the 400 pixel screen and slide in when needed. The original always culled
them; in a wide view they showed as loose banners. `dma_run` leaves them out
while they are fully outside the original screen (only when the draw margin is
wider than 32). Found by logging the images drawn at the edges in one match;
other HUD parts outside the screen, if any, were not looked for.

### HUD at the edges

The HUD (life bars, names, timer, credit, recover bars) is made of
screen-relative objects (`M_SCRNREL` in `OFLAGS`), so it stays at the original
400 x 254 screen when the view is wider or taller. `PATCHES` has a hook in
`dma_objlst2d` (`docs/RECOMPILER.md`) that calls `gsp_hud_shift` for every
object about to be queued; `wolf_set_hud_spread(dx, dy)` makes it move the
screen-relative ones: left of the middle by `-dx`, right of it by `+dx`, in the
top part by `-dy`, in the bottom part by `+dy`. Bars and other pieces wider than
64 pixels go with the side they lie on; small pieces within 25 pixels of the
middle (timer, credit) stay in x. Whole-screen objects (wider than 300 or taller
than 200) stay. It acts only on a level screen. `wwf` sets `dx`/`dy` to the
extra columns/rows visible at that moment, every frame, so it follows the zoom
on the fly; the menu line "HUD AT EDGES" (`hud_spread` in `wwf.cfg`) turns it off.
Strings are drawn a letter per object (`print_string`, `JAM_STR`), and each
letter used to go with its own side, so a message in the middle ("CHALLENGER
FOUND!" when a player joins during a match) came apart. Small pieces (at most
40 x 40) that sit side by side on a row, at most 12 pixels apart, now count as
one piece with the box around them all: a line of text moves (or stays) as one.
The boxes are found once a frame by walking the object list from the first small
piece queued. Their OID cannot tell letters apart (the strings use many: `TYPTEXT`,
`CLSDEAD` for the Royal Rumble's damage line, ...), hence the size. Bigger pieces
with a string (the banner of "MATCH AWARDED TO", a name in one picture) keep the
rules above. Checked headless: "CHALLENGER FOUND!" stays whole and centred, a
whole match and its end are unchanged except the sparkles of "PERFECT!", which now
stay on the word. The Royal Rumble's end was not run.
With a shift the recover bars need no special hiding: they rest `dx` pixels
further out than the visible edge.

Checked on one screenshot of a match at 1024 x 576 (bars in the corners, timer
in the middle, recover bars at the edges). Not checked: whether every HUD piece
is screen-relative (a piece that is not would stay behind), the round-end and
other overlays, the animation of the bars while they change, and that the
classification by size holds for all HUD pieces. The object flag is read from
memory with `gsp_read`, so the shift costs a memory read per queued object.

### Checking the HUD and the view without a window

`wwf` has two test aids that work with SDL's dummy video driver
(`SDL_VIDEODRIVER=dummy`): `--input FRAME,LEN,p1|p2|x3|x4|coins,BITS` (as
`wwfrun --input`, repeatable; x3 and x4 are the mods' third and fourth player) and `--shot FRAME FILE.bmp`, which saves that
frame as the window would show it, with the real view planning, zoom and HUD
shift, and then ends the run (unpaced). The HUD shot at `--zoom 0.5` for
1420x644 and 1720x720 windows, at match frames 3300, 3450 and 3599 (timer 93,
89, 86), had the life bars in the top corners.

### Camera inset

A wide view runs off the level's background where the camera is at the end of
its range (`scroll_world` in `WRESTLE2.ASM` keeps `WORLDTLX` in
`0x12F..0x648`). `wolf_set_scroll_inset(w, n)` keeps the camera `n` pixels
away from both ends, by clamping `WORLDTLX` just before each display. It only
acts while `WORLDTLX` is inside the original range, so title and menu
screens are left alone. `wwf` uses `n = view_pad - 16`; `wwfrun --inset N`.

Measured on the level of the test match (forced camera at each end, view
pad 256, extra 200): black edges are gone with `n = 241` (= pad - 15) at both
ends, and 41 px (left) and 17 px (right) of black remain with `n = 200`. So
the background reaches about 15 px past the camera range there. Only that
one level was measured; other levels may have less slack, and the
background's layers may not all end at the same place.

The camera can then no longer reach the outer part of the ring, so a
wrestler in a far corner may be less centered than in the original. Not
played through.

Unverified for this mode: the CPU-drawn effects (screen wipes and scalers in
`DISPLAY.ASM`, the star field) only work on the original 512 columns and leave
the extra columns alone; 488 DMAs found no image (`unmapped DMA`, before it
was 0 to 1), not examined; nothing was played through.

Unverified:

- Whether everything is still drawn at 56 (some background parts may still be
  missing; the crowd at the far top left looked a little short in one
  screenshot).
- Whether the wider cull range changes game behavior: objects are probably
  removed later when they leave the screen. Only a 3600 frame run to the match
  was checked (same game state as before).
- The HUD stays laid out for 400 pixels, and the `[400,0]` constants in
  `DISPLAY.ASM` (the star field, for one) were not looked at.
- The bitmap caps the width at 512.

## GPU path (optional)

The software rasterizer (`video_dma`) and the palette lookup at present (`video_to_argb_area`) both cost time in
proportion to the pixels, which is what makes render scale 3 with HD art slow on a small CPU. With `--gpu` (or the
`GPU DRAWING` line of the F1 menu's MODS page, the `gpu=1` line of `wwf.cfg`, or on Android an empty file `gpu` next to
`wwf.cfg`) both are done by OpenGL ES 2 instead. The software path stays the default, and is also what the game falls
back to, with a line in the log, when the GPU path cannot start or fails.

How it hangs together (`src/video/video.h` `video_sink`, `src/platform/gpu_video.c`):

- `video_dma` still does all its arithmetic (clip window, flips, scale steps, source rectangle). With a sink installed
  it hands the result to it as a `video_blit_job` instead of writing `video.fb`. `video_clear`, `video_fill_rows` and
  `video_put_pixel` become rectangle fills. Without a sink nothing changes: the CPU code is the same code as before.
- The sink records a display list and plays it, in order, at the next present (or after 8192 entries) into an RGBA8
  texture of the size of `video.fb`, which stores the 16-bit value `palette << 8 | color` (R = low byte, G = high byte).
  So the index framebuffer keeps the palette semantics: fades and colour cycling still work, the lookup happens when the
  frame is shown.
- A blit is one quad over the clipped rectangle. The fragment shader finds each pixel's source position with the
  integer maths of `src_pos()` (`floor((i + 0.5) * step)`, step in 16.16, clamp, flips) done exactly in float32 (it
  needs `highp` floats in fragment shaders; the shader does not compile without them and the GPU path is refused), reads the
  source texel from the image's texture (`R8`/luminance, with the palette mask already applied, or the HD override), and
  writes the value or discards the pixel according to the DMA write mode (`CNZ`/`WNZ`/`CZ`/`WZ`). No blending is used.
  Image textures are made on first use and kept in `gfx_image` (`sink_tex`), keyed by the image, never by address.
- True-colour overrides: a second texture of the same size holds `video.detail` (R, G, B residual, A luma) and the list is
  played a second time into it. The present pass adds it with the integer maths of `gfx_apply_detail` (divisions are done as
  `floor((n + 0.5) / d)`, which is exact for integers, and the signed division truncates towards zero like C).
- Present pass: a 256 x 128 palette texture (colour RAM converted as `img_color_argb` does, re-sent when colour RAM changed),
  the `high_pal_argb` rule for pixels with bit 15 set, and the crop `sdl_video_present` has always chosen. It draws into the
  renderer's own frame texture, so zoom, aspect, smoothing, scanlines, the overlays (F1 menu, inspector) and `--shot` are the
  same code as on the CPU path.
- Where the SDL renderer's context comes from: the GPU path does not make a second window or context. `sdl_video_open`
  asks SDL for its `opengles2` render driver (`SDL_HINT_RENDER_DRIVER`, only when the GPU path was asked for), and
  `gpu_video.c` uses that renderer's current GL ES 2 context: it loads the GL entry points through
  `SDL_GL_GetProcAddress` (nothing is linked against libGLESv2), calls `SDL_RenderFlush`, saves the GL state it touches,
  renders into the frame texture (`SDL_GL_BindTexture` gives its GL name; the texture is `ABGR8888`, `TARGET` access) and
  restores the state, so SDL's cached state stays true. One window, one context: nothing special is needed for the
  Android window that is made at start for the copy progress bar.

What the CPU still does, and the cases that leave the GPU for a moment (`video_sync_cpu`): anything that needs the pixels
on the CPU reads the picture back (`glReadPixels`, once) and from then on draws on the CPU until the next present, which
sends `video.fb` back to the GPU. These are: the machine reading VRAM (`video_get_pixel`: the power-up self test and a
byte write into the palette plane; with the usual `skip_selftest` these never happen), saving a state (F5, replay
recording), the image inspector's capture (it reads the converted picture, not the framebuffer), and a blit the GPU cannot
take (an image or override wider than `GL_MAX_TEXTURE_SIZE`; logged once). `wolf_state_load` replaces `fb`, so the CPU copy
is sent up. All of these are rare; a run that hit them many times would show it in the "read backs" count the GPU path
prints when it closes.

Checked (llvmpipe under Xvfb / SDL's offscreen driver, not a real GPU): `gpu_video_selftest` draws random scenes on a
CPU video and a GPU one and compares the framebuffer, the detail plane and the converted picture bit for bit. It covers all
four write modes with flips, 14 scale factors per axis, clip windows, source rectangles, palette masks, overrides with k = 1
to 3 with and without detail words, zero blocks, fills, single pixels, bit 15, view pads and the CPU read-back and upload
paths. It runs at every start (a few milliseconds): any difference, or a GL error, keeps the game on the CPU path. The
`gpu` ctest runs it much longer at render scales 1 to 4 and compares whole `sdl_video_present` frames (zoom, aspect, scanlines,
smoothing, a palette change between frames) read back from the renderer; it is skipped (exit 77) where SDL has no OpenGL ES 2
renderer. `wwf --gpu --shot N file.bmp` against `wwf --shot N file.bmp` gave identical files; the cases are listed here:

Not verified: a real GLES driver (Tegra on the Shield, Mali, Adreno, PowerVR). The risks are float precision (the start-up
comparison is there to catch it and fall back), driver limits on a texture the size of the bitmap (about 1700 x 3400 at
render scale 3, bigger with the wide view and `extra_y`; it needs `GL_MAX_TEXTURE_SIZE` of that: a smaller limit refuses the
GPU path with a message), texture memory (the HD override is kept on the CPU too, so the art takes twice the memory), and the
Android context loss on pause (`SDL_RENDER_DEVICE_RESET` makes the GPU path start again from the CPU copy; never run).
No speed-up is claimed here: llvmpipe is a CPU emulation of a GPU.

Whole-game comparisons (`wwf --shot N a.bmp` against `wwf --gpu --shot N b.bmp`, 1280x720 window, fresh CMOS, same scripted
inputs, Xvfb + llvmpipe; all files byte-identical, 23 pairs): attract/boot (frame 400), mode select (1000, 1100), character select
with its fades (1250, 1300, 1400), a Doink vs Razor Ramon match (1800, 2600), the hall-of-fame tables of a one-player start (1500,
2400); render scale 1 and 3 (the auto scale), `--scale 2`; no art, Scale3x art at k = 3 (exact step), Scale2x art at k = 2 with
render scale 3 (step 43690) and 2, true-colour RGBA art at k = 2 (detail words; Doink, Razor, UI); `--classic`; dynamic zoom;
`--zoom 1.7`; scanlines + CRT aspect + nearest filtering. Not reached by these scripts: the one-player ladder screen itself, a
three or four wrestler match, the inspector (`--inspect` with the GPU path converts on the GPU and reads the picture back).
Sanity timing, 1800 frames to a match at render scale 3 with HD art, `--classic`: 31 s software, 61 s `--gpu` (llvmpipe runs the
shaders on the same CPU cores: this says nothing about a real GPU, only that the path costs something where there is none).

## Open questions

- The write-mode priority, the `pal & 0x7F00` palette mask and the
  `pal | (color & 0xFF)` constant come from MAME's midtunit/midwunit DMA
  emulation, as best remembered. They have not been checked against the
  MAME source or hardware.
- Rounding of DMA scaling: the port samples pixel centers, and the
  hardware's exact stepping may differ by one pixel at the edges.
- The GPU path is checked only on llvmpipe; see "Not verified" in the GPU path section.
- Shadows (`M_SHAD`, `PLACE_SHADOWS`) and 3D objects are not modeled yet.
- Async compute ("Async compute (option)" below): it is checked against the CPU path with llvmpipe only. Whether 2 ms per frame removes the hitches on a
  real GPU (Mesa radeonsi, Mali, Adreno), whether `glTexImage2D` with `GL_LUMINANCE` is the expensive part (a `GL_R8` texture
  may be cheaper), and whether a shared GL context on a second thread is worth its risk are all unmeasured.

## How far apart wrestlers can go

In a two-player game, `keep_onscreen` (WRESTLE2.ASM) stops a wrestler who is
outside the ring from going more than 185 pixels from the middle of the
screen, so he cannot leave the 400 pixel view. With a wider view (zoomed out)
that limit is visible. The generator now reads it from `view_extra`
(`src/wolf/core.gen.txt`, changes that belong to the machine, not to a mod),
which `wolf.c` sets to the draw margin; `--classic` leaves it at 0, the
original limit. Not tried in a real window.

### Pausing the GPU path for pixel-moving effects

The original's first screen fades its picture in with small squares: the code copies pixels one by one through video
memory (`BLOW_0_TO_1`, `DISPLAY_ARRAY`, `COPY_NEXT` in SCREEN.ASM), tens of thousands of reads and writes per frame. With
the GPU path every such frame costs a full read back and a full upload (measured on a Shield at render scale 4: 60 frames
in 23 s). `sdl_video_present` therefore pauses the GPU path when two frames in a row show more than 2000 single pixel
fills or a full read back (`gpu_video_activity`): the picture is read back once (`gpu_video_suspend`), the CPU draws and
converts as without `--gpu`, and after twenty frames with fewer than 1500 pixel reads and writes (`video.cpu_px_ops`), or after three seconds whatever
they are (a pause that cannot end would keep the slow CPU drawing), the GPU takes over again (`gpu_video_resume`, one upload). The log says `GPU path: paused ...` and `resumed`. The picture is
the same before and after (the `gpu` ctest pauses and resumes between frames and compares them bit for bit; screenshots of
frames 30 to 600 of a fresh start are byte-identical with and without `--gpu`). Unmeasured on a device.

### Async compute (option)

On the GPU path the textures of an HD override (`image_tex` in `gpu_video.c`) are made the first time the game draws the
image: the pixels are copied and `glTexImage2D` runs, on the game's thread, in the middle of the frame. At render scale 4
an image is 16 times its original size and a screen needs many of them, so the first minute of play can hitch whenever
something new is shown (precache reads the art into memory, but it does not make the GPU textures).

`--async-compute` (the `ASYNC COMPUTE` line of the F1 display page, `async_compute=1` in `wwf.cfg`, on Android an empty file
`asynccompute`; it needs GPU drawing and takes effect at start) moves that work out of the drawing:

- `video_dma` asks the sink (`video_sink.hi_ready`) before it uses an override. If the textures are not on the GPU yet the
  sink queues the image (`gpu_video.c`, bookkeeping only, no GL call) and `video_dma` records the blit from the original
  pixels, exactly as for an image without an override or while the art is still being read from disk.
- At each present, before the picture is drawn, `async_pump` makes the textures of queued images until 2 ms are spent (the
  first one always, so the queue moves). A few frames later the image shows the override.
- Off, `hi_ready` is NULL and nothing changes: the GPU path is the code it was. The `gpu` ctest checks the first frame (the
  original pixels), the second (the override) and the off case against the CPU path bit for bit, at render scale 2 with an
  indexed and a detail override.

With PRECACHE on, the same textures are made ahead of time for every image the precache has read (`gpu_video_precache`, called
every frame, 3 ms of work at most, with or without async compute), and a progress box in the top left corner shows how far it
is; async compute then only matters for what the precache did not cover. The `gpu` ctest checks that a precached override is
drawn at once, even with async compute on.

It is spreading the work over frames, not a second thread: the SDL renderer's GL context belongs to the game's thread, and
a shared context or pixel buffers would need a driver and a platform test that was not done (see Open questions). While
an image waits it is drawn at the original resolution for a few frames (visible as a brief pop-in of sharpness). The stats
line says how many textures were made from the queue and how many blits were drawn from the original meanwhile.
