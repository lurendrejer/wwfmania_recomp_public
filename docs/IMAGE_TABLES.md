# Regenerated image tables

The original build ran two image tools that are not in the source release.
`loadw` read the `.LOD` scripts and produced the image header tables
(`*.TBL`), global lists (`*.GLO`) and `IMGPAL.ASM`. `wwfld` produced the
wrestler animation sequences (`<WRESTLER>.SEQ`) and `<WRESTLER>IMG.H`.
Their output is missing too, so `tools/gsp/genimg.py` regenerates it as
assembler source:

```sh
python3 tools/gsp/genimg.py --src orig --out build/gen
python3 tools/gsp/gspasm.py --src orig --gen build/gen   # 0 errors, 0 undefined
```

## Validation

- **Linking.** All 99 modules assemble and link with no errors and no
  undefined symbols.
- **Palettes.** All 337 palettes in the original `IMGPAL.ASM` (left over from
  the last original build) come out identical, count and every color.
- **Sequences.** The sequence names generated for all 8 wrestlers (753 in
  total) match the original `*IMG.H` files exactly.

## Formats

### Standard image header (non-wrestler LODs)

The fields are listed on the `IHDR` line in the order given, usually
`SIZX:W, SIZY:W, ANIX:W, ANIY:W, SAG:L, CTRL:W, PAL:L`, which puts `PAL` at
`ICMAP` (70h).

- `CTRL` is `0`, meaning 8 bits per pixel and no zero compression. Bits
  12–14 of the original CTRL held bits per pixel, as `orig/BGNDTBL.ASM`
  shows.
- `PAL` is the palette label (`ALEX_P`). The palette data is
  `.word count` followed by the colors.

### Wrestler frame (BAM, BRET, DOINK, LEX, RAZOR, SHAWN, TAKER, YOKO)

This layout comes from the active branch in `orig/SYS.EQU`. The consumers
are `ANIM.ASM`, `WRESTLE2.ASM` and `COLLIS.ASM`. A frame has no palette,
because the wrestler's palette comes from the object.

```
-10h  IPCOUNT   pieces (always 1, see below)
 00h  SIZX SIZY ANIX ANIY SAG(L) CTRL       first piece (7 words, ICPBZ)
 70h  IANI2X IANI2Y IANI2Z                  secondary anchor, -1 = none (IMG ani2)
 A0h  IFLAGS                                0 (no consumer in the game)
 B0h  IANI3X IANI3Y IANI3Z IANI3ID          collision box: x off, y off, width, height
 F0h  (ICBZ)
```

The collision box comes from bytes 36–39 of the image's IMG point table:
signed left and top in image coordinates, then width and height. The game
stores `IANI3X = left - ANIX` and `IANI3Y = top - ANIY`. The signedness was
settled statistically: with signed bytes, box centers cluster within ±10
pixels of the anchor, and with unsigned bytes about 500 of them land
around 250 pixels away.

**One piece per frame.** `wwfld` split frames into up to five rectangles
(bytes 16–35 of the point table) to save ROM. The game only draws the
pieces and takes their bounding box (`get_mpart_offsets` and
`get_mpart_xsize`). 99.7 % of IMG images are trimmed to their content, so
a single piece covering the whole image behaves the same. It also makes
high-resolution overrides simpler.

### Sequences (`<WRESTLER>.SEQ`)

Images named `<SEQ><nn>` are grouped into a table of longs. Scripts
reference entries as `SEQ+FRn` (`FRn = n*20h`, from `ANIM.EQU`), and entry
`n` points to frame `<SEQ><nn>`, or is 0 if the LOD scripts did not load
that frame. Entry 0 holds the frame count (assumed; no code reads it).

### Labels

- Labels are spelled as in the LOD selection lists. Symbols are
  case-sensitive, so `FLASH1` (`LADDER.IMG`) and `flash1` (`FLASH.IMG`) are
  different images.
- Each label is emitted once, the first selection in build order.
- Names that are not valid symbols (`!STAND2`) are skipped.

## Image ROM addresses

The original `SAG` values were addresses in the image ROMs. The generator
gives every image a unique synthetic address from `0x10000000` (bits, at
256-bit alignment) and writes `build/gen/imgrom.txt`
(`sag width height library index name`). The port's DMA hook uses that map
to turn an `SAG`, including the clip offsets `DISPLAY.ASM` adds to it,
back into an image and a pixel position.

## Backgrounds

`BGNDTBL.ASM` and `BGNDPAL.ASM` in the source are original `loadw` output.
Their block tables and modules are used as they are. The image headers,
however, point into the real image ROMs. The generator rewrites
`BGNDTBL.ASM` so that each `*HDRS` group points at the `.BDD` file whose
image sizes match it exactly:

| Group | File |
|-------|------|
| `ingbHDRS` | `NEWRINGB.BDD` (the ring) |
| `KPGHDRS` | `BLANKPG.BDD` |
| `ELBKHDRS` | `WWFSELBK.BDD` |
| `ERHDRS` | `LADDER.BDD` |
| `WFHDRS` | `BIGWWF.BDD` |
| `BKHDRS` | `SPRTBK.BDD` |

In each header, the `SAG` becomes a synthetic address, and the CTRL
word's bits-per-pixel and compression bits are cleared.

**`.BDD` format.** A count line, then per image `index width height
palette`, followed by width×height pixel bytes, then palettes.

**Overrides.** A background override is named `<BDD>_<n>.png`, e.g.
`NEWRINGB_12.png`.
