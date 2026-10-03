# The recompiler

The game is not translated by hand. The original assembly is assembled
(`tools/gsp/gspasm.py`), the missing image tables are regenerated
(`tools/gsp/genimg.py`), and every instruction is translated to C
(`tools/gsp/gsp2c.py`). A small hand-written runtime (`src/cpu`) and a
model of the Wolf Unit hardware (`src/wolf`) run the result.

```
orig/*.ASM ──gspasm──► modules laid out at their real addresses
orig/IMG   ──genimg──► *.TBL *.SEQ *.GLO IMGPAL.ASM BGNDTBL.ASM imgrom.txt
           ──gsp2c───► build/gen/c/gsp_<MODULE>.c, rom.bin, ram.bin, symbols.txt
```

CMake runs the generators at configure time; `cmake --build build --target
regen` reruns them.

## Generated code

- **One C function per module** containing `switch (c->pc)`, with a `case`
  for every entry point: labels, instructions after a jump or call, targets
  of code addresses stored in data, and labels in alignment padding.
  25,981 entry points in total.
- **Jumps.** Direct jumps inside a module are `goto`. Everything else sets
  `c->pc` and returns to the dispatcher (`gsp_run`): calls and returns
  through the emulated stack, indirect jumps, `EXGPC` and traps.
- **Stack.** Return addresses live on the emulated stack, so the game's
  multitasker works unchanged. It switches stacks, rewrites return
  addresses and computes them with `GETPC` (JSRP).
- **Entry checks.** Every entry point calls `GSP_ENTER`, which leaves to
  the dispatcher when the frame's instruction budget is used up or an
  interrupt is pending.
- **Tracing.** Build with `-DGSP_TRACE` to record every executed
  instruction. `wwfrun` prints the last 48 when the game stops.

## Runtime (`src/cpu/gsp.*`)

- **Registers and ST.** A0–A14, B0–B14 and the shared SP. The ST flags,
  field sizes (FS/FE for fields 0 and 1) and IE work as on the CPU.
- **Memory** is bit-addressed. Work RAM has a fast path; everything else
  goes through `gsp_read`/`gsp_write` in `src/wolf`.
- **XY arithmetic.** N = X zero, V = X sign, Z = Y zero, C = Y sign. This
  was verified by the diagnostics' line drawing.
- **Traps and interrupts** push PC and ST and clear IE. Priorities are HI,
  DI, WV, INT1, INT2.

## Hardware (`src/wolf/wolf.c`)

| Area | Model |
|------|-------|
| VRAM | 512×1024 pixels of 16 bits (palette byte + pixel byte). The CPU reaches the pixel plane or the palette plane depending on SYSCTRL `PALENB`. |
| Display | Two pages. The visible page comes from `DPYSTRT`, with 56 pixels of left padding (`SCRNXP`). `FILL` in shift-register mode erases a page. |
| DMA | Registers at `0x1A00000`. A blit runs on the `CTRL` write, is decoded from `SAG` through `imgrom.txt` (including clip offsets) and drawn with `video_dma`, then INT1 is raised. Unknown `SAG`s read as zero pixels. |
| Color RAM | `0x1880000`, 32K colors. Palette ≥ 128 is shown in the VMUX match color (black), which hides the "level 1 security" text as on genuine boards. |
| I/O | Switches, coins, DIP and sound status. The addresses depend on the VMUX mode (0–4, `WWFSEC.EQU`). |
| PIC | Command 0 returns serial data that decodes to game 430, serial 123456. Command 15 is an echo. Clock commands return 0. |
| Sound (DCS) | Stub only. It sends the boot bytes `0x79, 0x01`, answers the revision request, and is always ready. **No audio.** |
| CMOS | 128 KB, saved to `--cmos` (default `wwf.cmos`) on exit. |

## The mods' ROM

The code and data the mods add (`gen.txt` modules, tools/gsp/extras.py) are linked to 0xFF000000-0xFF7FFFFF, the
megabyte below the program ROM, which the board does not decode (`EXT_ROM_BASE`, `Linker.place` in
`tools/gsp/gspasm.py`). `rom.bin` holds both (0xFF000000-0xFFFFFFFF) and the machine maps both (`ROM_BASE`,
`src/wolf/wolf.c`). The original modules keep their own addresses; the program ROM has only some 30 KB free, too little
for a mod like `adambomb` (a whole wrestler).

## Patches

`PATCHES` in `gsp2c.py` inserts C at a label. There are two:

- `ROM_COMPARE`: the diagnostics checksum the program and image ROMs. No
  ROM dumps exist, so the computed sum is made equal to the expected one.
- `__blk199#300` (`dma_objlst2d` in `DISPLAY.ASM`, just before the page and
  XPad offset are added to the destination): calls `gsp_hud_shift`, which
  the machine uses to move screen-relative objects (the HUD) outward in a wide
  view. It does nothing unless `wolf_set_hud_spread` was called with a
  non-zero shift. The label name comes from the generator's numbering of
  unnamed blocks; if the source or the generator changes, look up the label
  at that address in `gen/symbols.txt`.

## Timing

A frame is 110,000 instructions (`frame_insns`). The display interrupt
fires at the `DPYINT` line, and `VCOUNT` is derived from the instructions
used so far. This is not cycle-accurate: speed-sensitive effects may
differ.

## Known gaps

- No sound or music. The DCS sound ROMs are not in the source release.
- `LINE` and `DRAV` are only approximated. `PIXBLT B,XY` covers only the
  diagnostics' use.
- The PIC real-time clock returns zeros.
- Overrides for `FLASH1`/`flash1` would clash on case-insensitive file
  systems.
