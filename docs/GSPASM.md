# gspasm: assembling the original source

`tools/gsp/gspasm.py` does what the original toolchain did
(`preasm` → TI `GSPA` → `gsplnk`, see `orig/MAKE.INI` and `orig/WRESTLE.CMD`),
up to the point where every module is laid out in the TMS34010 address
space and every symbol is resolved. It keeps instructions as parsed text
for the C translator instead of encoding them.

```sh
python3 tools/gsp/gspasm.py --src orig --report          # all 99 linked modules
python3 tools/gsp/test_gspasm.py orig                    # tests
```

Current state: with the regenerated image tables (`docs/IMAGE_TABLES.md`),
all 99 modules in `WRESTLE.CMD` assemble and link with no errors and no
undefined symbols. That is 70,372 instructions and about 317,000 data items.

## Rules reconstructed from the source

These are not documented anywhere. They were derived by making all 99
modules assemble cleanly.

- **Symbols are case-sensitive.** 186 names exist in two spellings with
  different meanings (`MODE_NORMAL` is an equate, `mode_normal` is a
  label). Mnemonics, directives and macro names are case-insensitive.
- **`#name` locals (`preasm`).** Only a `#*****` header line in the
  module's own source starts a new scope. That is why every function in
  the source has one. Neither labels nor `SUBR` lines do: `DEL_IT` inside
  `bgnd_delnonvis` shares its scope. With this rule all 16,134 local
  references in the game resolve inside their own scope. An earlier
  heuristic ("labels start scopes, else take the nearest definition")
  assembled cleanly but bound `jrz #x` to another function's `#x`, which
  broke the stack at run time.
- **Instruction sizes are the real encodings.** `JSRP` computes its return
  address with `GETPC` + `ADDI 060h`. MOVI/ADDI/SUBI/CMPI use the short form
  when the value is known and fits in 16 bits. Relative jumps are relaxed
  between 16 and 32 bits.
- **Extra macro arguments.** When a macro gets more arguments than it has
  parameters, the last parameter receives the rest of the list
  (`WRSND W_BRET,UPRCUT_T1,UPRCUT,T2` in `BRET.ASM`).
- **`$isname(x)`** tests whether `x` is syntactically a symbol, not whether
  it is defined (`JJXM.H`). GSPA also tolerated a stray `)` in `.if`
  (`FACETBL`).
- **Operand details.** A `label.S` jump operand is a short-jump hint.
  `SADDR`, `DYDX`, `COLOR0` and similar are aliases for B-file registers.
- **Link layout** (`WRESTLE.CMD`): `.text` then `.data` at `0xFF800000`,
  `unzip`/`FIXED`/`OFIXED`/`.bss` at `0x01000000`, and `VECTORS` at
  `0xFFFFFC00`. All addresses are bit addresses.
