# Midway IMG library format

The `.IMG` files in `orig/IMG` are the art source libraries that Midway's
DOS image tools wrote. The original build ran them through the `wwfld` and
`loadw` ROM builders (`orig/LD.BAT`, `orig/IMG/*.BAT`) driven by `.LOD`
scripts. That produced image ROMs plus assembly tables (`*img.tbl`,
`*img.glo`, `*.seq`), which are **not** in the source release. The port reads
the IMG files directly instead (`src/assets/img.c`).

Everything below was verified against all 264 IMG files in `orig/IMG`,
17,514 images in total (`tests/test_assets.c`). All values are
little-endian.

## Library header (28 bytes)

| Offset | Size | Field                                                         |
|-------:|-----:|---------------------------------------------------------------|
| 0      | 2    | image count                                                   |
| 2      | 2    | palette count, **including 3 built-in palettes** that are not stored |
| 4      | 4    | offset of the image record table                              |
| 8      | 2    | version: `0x0626`–`0x064f`, or `0` for the old layout         |
| 10     | 2    | sequence count (editor data, unused by the port)              |
| 12     | 2    | script count (editor data, unused)                            |
| 14     | 2    | damage table count                                            |
| 16     | 2    | `0xABCD` marker (0 in version 0)                              |
| 18     | 10   | editor state                                                  |

Four files are zero bytes long (`CHANGES`, `L2HNDPNC`, `MARKER`, `MISC_JMS`),
and `_MARKER_.IMG` is an empty library.

## Layout

```
header | palette colors + pixel data ... | image records | palette records | seq/script chunks | point tables
                                          ^ header.oset
```

The sequence and script chunks are editor data (the game's animation
scripts are in the .ASM files). There are `sequence count + script count`
of them, sequences first. Each is a 16-byte name, a word, a count `n` at
offset 18, and `98 + 18 * n` bytes in all. The point tables fill the rest
of the file. This holds exactly for 159 of the 161 libraries with point
tables (`OSGEMD` and `SGMD8` end with 48 and 64 unused bytes).

## Image record: 50 bytes (42 bytes in version 0)

| Offset | Size | Field                                                  |
|-------:|-----:|--------------------------------------------------------|
| 0      | 16   | name, NUL-terminated (the bytes after the NUL are garbage) |
| 16     | 2    | flags (bits `0x01 0x04 0x10 0x20` seen; meaning unknown) |
| 18     | 2    | anim X: anchor point, from the left                    |
| 20     | 2    | anim Y: anchor point, from the top                     |
| 22     | 2    | width                                                  |
| 24     | 2    | height                                                 |
| 26     | 2    | palette index (subtract 3 for the stored palettes)     |
| 28     | 4    | pixel data offset                                      |
| 32     | 4    | runtime pointer (garbage)                              |
| 36     | 2    | lib index (editor)                                     |
| 38     | 6    | secondary anim point X/Y/Z (`-1` = none)               |
| 44     | 2    | frame                                                  |
| 46     | 2    | point table index (`0xFFFF` = none)                    |
| 48     | 2    | alternate palette index (`0xFFFF` = none)              |

Version 0 (used only by `TROGF15.IMG`, the 15-point font) has no flags field:
anim X/Y are at 16 and 18, width/height at 20 and 22, the palette at 24 and
the pixel offset at 26. It has no secondary point, point table or alternate
palette.

**Pixels:** one byte per pixel, a palette index, with rows padded to a
multiple of 4 bytes (`stride = (width + 3) & ~3`). Index 0 is transparent.

## Palette record (26 bytes)

| Offset | Size | Field                                                 |
|-------:|-----:|-------------------------------------------------------|
| 0      | 10   | name                                                  |
| 10     | 1    | flags                                                 |
| 11     | 1    | bits per pixel the ROM builder packs images to (1–8)  |
| 12     | 2    | color count                                           |
| 14     | 4    | color data offset                                     |
| 18     | 8    | runtime data / editor fields                          |

Colors are 16-bit `xRRRRRGGGGGBBBBB`.

## Point tables

There are `max(point table index) + 1` tables of 40 bytes each, at the
end of the file (see Layout). What is known of their layout:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0      | 16   | unknown, usually zero |
| 16     | 20   | up to five `x, y, w, h` byte rectangles that tile the image (inferred from the data: most likely the DMA pieces) |
| 36     | 4    | collision box `x, y` (signed bytes, image pixels) and `w, h` (unsigned); all zero = none |

`genimg.py` turns the box into the frame header's `IANI3X/Y/Z/ID` (relative
to the animation point), which `COLLIS.ASM` uses as the frame's hit box.
Reading the tables right after the palettes, as the port first did, gave
wrong or empty boxes for libraries with sequence data. `YOK_WLK.IMG`
holds Yokozuna's stance and walk frames, which left him almost impossible
to hit.

## LOD scripts

See `src/assets/lod.h` for the line syntax. The shipped game was built from
`MAIN, BAM, BRET, DOINK, LEX, RAZOR, SHAWN, TAKER, YOKO, MISC` in that
order (`orig/LD.BAT`). `ADAM`, `REF`, `HART`, `FONTS`, `FRAME` and `TEMP`
are leftovers, and some of them reference images that no longer exist.
Together the ten build scripts select 8,085 images.

## Open questions

- **Wrestler frames marked with another palette.** Twelve wrestler frames (`gen/drawpal.txt`, one or two per
  walking library, e.g. Bam Bam's B1TT5Z02 with the fire trail's `BAMFRE_P`, Bret's H2ST2A01 with player 2's
  `HRTBLU_P`) have one of the wrestler's other palettes in the IMG file instead of his main one. The game never uses
  it: wrestler frame headers have no palette field, the frames are drawn with `OBJ_PAL`, and the other palettes the
  game uses come from `WRESPAL.ASM` (`HRTBLU_P`, `BAMFRE_P`, `UNDNEG_P` ... there match the IMG copies; `UNDRED_P`
  differs). A guess, not verified: the artists made or checked those palettes in the image editor by putting them
  on a frame, and then copied them to `WRESPAL.ASM`; the IMG file then keeps the palette only because a frame uses it.

- **Stray pixel indices.** 36 images contain pixel values at or above their
  palette's color count, e.g. index 64 in a 64-color, 6 bpp palette. The
  loader masks pixels to the palette's `bitspix`, which is presumably what
  the ROM packer did. After masking, 5 images still point outside their
  palette. Verify both against MAME.
- **Duplicate labels.** The build scripts select 73 names twice. `FLASH1`
  and `FLASH2` exist in both `LADDER.IMG` and `FLASH.IMG` with different
  pixels, and `PROGRESS.ASM` refers to both `flash1` and `FLASH1`.
  *Update:* the assembler was case-sensitive (docs/GSPASM.md), so these are
  distinct labels spelled as in the LOD lists. The catalog is still
  case-insensitive and keeps the first selection; this has to change when
  the image tables are generated.
- **Runtime header fields.** `CTRL`, `PWRD1..3`/`PT3Y` (the `IHDR` lines)
  and the point table layout need to be reconstructed. The best reference
  is the header tables in the MAME program ROMs.
- **Backgrounds.** `BBB>` entries (`.BDB`/`.BDD`) are a separate format that
  is not loaded yet.
- **Headers under an `IHDR` with both `PAL` and `PWRD1`.** `tools/gsp/genimg.py`
  writes every entry whose `IHDR` has `PWRD1` as a wrestler frame header,
  which has no palette. In `MISC.LOD` that includes `chair.img` (CHSWNG01-09,
  CHBRAK01-10, CHSWNFL) and `ladder.img`, whose `IHDR` line also lists `PAL:L`.
  Whether loadw wrote a palette there is not known. It matters only for
  images the game draws as attached images (they read one at `ICMAP`), which
  the original never does with these. `mods/chair` copies the chair headers
  and adds `CHAIR_P` itself.
