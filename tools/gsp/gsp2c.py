#!/usr/bin/env python3
"""TMS34010 → C static recompiler for the WWF WrestleMania source.

Assembles and links the original source with gspasm (plus the regenerated
image tables), then writes:

  <out>/c/gsp_<module>.c   one function per module, one case per entry point
  <out>/c/gsp_modules.c    module address table for the dispatcher
  <out>/rom.bin            memory image 0xFF000000..0xFFFFFFFF (bytes, LSB first): the mods' ROM
                           (gspasm.EXT_ROM_BASE), then the original program ROM at 0xFF800000
  <out>/ram.bin            initialised RAM 0x01000000..0x013FFFFF
  <out>/symbols.txt        address → label map for debugging

    python3 tools/gsp/gsp2c.py --src orig --gen build/gen --out build/gen
"""

import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gspasm  # noqa: E402
from gsp34010 import JUMPS_ABS, JUMPS_REL, REG_ALIASES, parse_operand  # noqa: E402
from gspexpr import ExprError, Undefined, evaluate, parse_expr  # noqa: E402

ROM_BASE, ROM_END = gspasm.EXT_ROM_BASE, 0x100000000
RAM_BASE, RAM_END = 0x01000000, 0x01400000

CONDS = {
    "uc": "1", "p": "(!c->n && !c->z)", "ls": "(c->c || c->z)", "hi": "(!c->c && !c->z)",
    "lt": "(c->n ^ c->v)", "ge": "!(c->n ^ c->v)", "le": "((c->n ^ c->v) || c->z)",
    "gt": "(!(c->n ^ c->v) && !c->z)",
    "c": "c->c", "b": "c->c", "lo": "c->c", "nc": "!c->c", "nb": "!c->c", "hs": "!c->c",
    "eq": "c->z", "z": "c->z", "ne": "!c->z", "nz": "!c->z",
    "v": "c->v", "nv": "!c->v", "n": "c->n", "nn": "!c->n",
    # XY conditions (after ADDXY/SUBXY/CMPXY): N = X zero, V = X sign,
    # Z = Y zero, C = Y sign
    "yz": "c->z", "ynz": "!c->z", "yn": "c->c", "ynn": "!c->c",
    "xz": "c->n", "xnz": "!c->n", "xn": "c->v", "xnn": "!c->v",
    "yle": "(c->c || c->z)", "ygt": "(!c->c && !c->z)",
    "xle": "(c->n ^ c->v)", "xgt": "!(c->n ^ c->v)",
    "yv": "c->v", "ynv": "!c->v", "xv": "c->v", "xnv": "!c->v",
}

# Hardware the port cannot reproduce, patched at a label. Each entry inserts
# C before the instruction at that label. Keep this list short and explain
# every entry (also listed in docs/RECOMPILER.md).
PATCHES = {
    # The diagnostics checksum the program and image ROMs. There are no ROM
    # dumps (code is recompiled, images come from IMG files), so make every
    # ROM's computed sum (A8) equal the expected one (A7).
    "ROM_COMPARE": "c->r[8] = c->r[7];",
    # DISPLAY.ASM dma_objlst2d, just before the page/XPad offset is added to
    # the destination (A10 = dest Y:X, A9 = HEIGHT:WIDTH, A0 = the object).
    # Lets the machine move screen-relative objects (the HUD) outward when the
    # view is wider than the original screen; a no-op otherwise.
    "__blk199#300": "gsp_hud_shift(c);",
}

CONTROL = JUMPS_REL | JUMPS_ABS | {
    "calla", "callr", "call", "rets", "reti", "trap", "exgpc", "jump", "dsj", "dsjs",
    "dsjeq", "dsjne",
}


class TranslateError(Exception):
    pass


def reg_index(text):
    t = text.strip().lower()
    t = REG_ALIASES.get(t, t)
    if t == "sp":
        return 15
    if t[0] == "a":
        return int(t[1:])
    if t[0] == "b":
        return 16 + int(t[1:])
    raise TranslateError(f"not a register: {text}")


def R(text):
    return f"c->r[{reg_index(text)}]"


def u32(v):
    return f"0x{v & 0xFFFFFFFF:08X}u"


class Program:
    def __init__(self, src, gen, extra_modules=(), extras=None):
        if extras:
            gspasm.set_extras(extras)
        tree = gspasm.SourceTree([gen, src])
        self.modules = [gspasm.assemble(tree, n) for n in gspasm.read_link_modules(tree, extra=extra_modules)]
        errors = [e for m in self.modules for e in m.errors]
        if errors:
            raise TranslateError("assembly errors:\n" + "\n".join(errors[:20]))
        self.linker = gspasm.Linker(self.modules)
        self.linker.place()
        self.linker.build_globals()
        undefined = self.linker.resolve_all()
        if undefined or self.linker.errors:
            raise TranslateError(f"link errors: {sorted(undefined)[:20]} {self.linker.errors[:10]}")

        # every instruction, by absolute address
        self.insns = {}          # addr -> (module, item)
        self.data_items = []     # (addr, bits, value)
        self.labels = defaultdict(list)
        for mod in self.modules:
            for sec in mod.sections.values():
                for it in sec.items:
                    addr = (sec.base + it.offset) & 0xFFFFFFFF
                    if it.kind == "insn":
                        self.insns[addr] = (mod, it)
                    elif it.kind == "data":
                        self.data_items.append((addr, it.size, it.value))
            for name, sym in mod.symbols.items():
                if sym.kind == "label" and not name.startswith("__here"):
                    sec = mod.sections[sym.section]
                    self.labels[(sec.base + sym.item.offset) & 0xFFFFFFFF].append(name)

    def value(self, mod, text):
        try:
            ast = parse_expr(text)
            return evaluate(ast, lambda n: self.linker.symbol_value(mod, n)) & 0xFFFFFFFF
        except (Undefined, ExprError) as e:
            raise TranslateError(f"cannot evaluate {text!r}: {e}")


class ModuleWriter:
    def __init__(self, prog, mod):
        self.prog = prog
        self.mod = mod
        self.code = []           # (addr, item) sorted
        for sec in mod.sections.values():
            for it in sec.items:
                if it.kind == "insn":
                    self.code.append(((sec.base + it.offset) & 0xFFFFFFFF, it, sec))
        self.code.sort(key=lambda x: x[0])
        self.addrs = {a for a, _, _ in self.code}
        # instructions followed by the next instruction with only alignment
        # padding in between: execution runs through the padding (NOPs)
        self.falls_through = set()
        for sec in mod.sections.values():
            prev = None
            for it in sec.items:
                if it.kind == "insn":
                    if prev is not None:
                        self.falls_through.add((sec.base + prev.offset) & 0xFFFFFFFF)
                    prev = it
                elif it.kind in ("data", "space"):
                    prev = None
        self.out = []
        self.unimpl = defaultdict(int)
        self.alias_of = defaultdict(list)
        for a, target in prog.aliases.items():
            if target in self.addrs:
                self.alias_of[target].append(a)

    # -- helpers
    def val(self, text):
        return self.prog.value(self.mod, text)

    def field(self, ops, idx):
        if len(ops) <= idx or not ops[idx].strip():
            return 0
        t = ops[idx].strip().lower()
        if t in ("l", "w"):
            return 1 if t == "l" else 0
        return self.val(ops[idx]) & 1

    def jump(self, target):
        target = self.prog.aliases.get(target, target)
        if target in self.addrs:
            return f"goto L_{target:08X};"
        return f"{{ c->pc = {u32(target)}; return; }}"

    def addr_expr(self, op, f):
        """C expression for a memory operand's address, plus pre/post code."""
        kind = op[0]
        if kind == "ind":
            return R(op[1]), "", ""
        if kind == "inc":
            return R(op[1]), "", f"{R(op[1])} += {f};"
        if kind == "dec":
            return R(op[1]), f"{R(op[1])} -= {f};", ""
        if kind == "disp":
            d = self.val(op[2])
            d = (d & 0xFFFF) - 0x10000 if d & 0x8000 and d < 0x10000 else d
            return f"({R(op[1])} + {u32(d)})", "", ""
        if kind == "abs":
            return u32(self.val(op[1])), "", ""
        raise TranslateError(f"bad memory operand {op}")

    # -- instruction emitters
    def emit_move(self, mn, ops, next_pc):
        src, dst = parse_operand(ops[0]), parse_operand(ops[1])
        if mn == "movb":
            size_expr, ld, st = "8", "gsp_sext(gsp_rd(c, {a}, 8), 8)", "gsp_wr(c, {a}, 8, {v})"
        else:
            f = self.field(ops, 2)
            size_expr = f"c->fs[{f}]"
            ld = f"gsp_ld(c, {{a}}, {f})"
            st = f"gsp_st(c, {{a}}, {f}, {{v}})"
        if src[0] == "reg" and dst[0] == "reg":
            return [f"{{ uint32_t t = {R(ops[0])}; {R(ops[1])} = t; GSP_NZ(c, t); c->v = 0; }}"]
        if src[0] == "reg":
            a, pre, post = self.addr_expr(dst, size_expr)
            return [f"{{ uint32_t t = {R(ops[0])}; {pre} " + st.format(a=a, v="t") + f"; {post} }}"]
        if dst[0] == "reg":
            a, pre, post = self.addr_expr(src, size_expr)
            return [f"{{ {pre} uint32_t t = " + ld.format(a=a) +
                    f"; {post} {R(ops[1])} = t; GSP_NZ(c, t); c->v = 0; }}"]
        sa, spre, spost = self.addr_expr(src, size_expr)
        da, dpre, dpost = self.addr_expr(dst, size_expr)
        return [f"{{ {spre} uint32_t t = " + ld.format(a=sa) + f"; {spost} {dpre} " +
                st.format(a=da, v="t") + f"; {dpost} }}"]

    def imm_or_reg(self, op):
        o = parse_operand(op)
        if o[0] == "reg":
            return R(op), True
        return u32(self.val(o[1])), False

    def normalize(self, op):
        """B-file aliases (SADDR, DYDX, COLOR1...) as operands name registers."""
        t = op.strip()
        if t.lower() in REG_ALIASES:
            return REG_ALIASES[t.lower()]
        return op

    def emit(self, addr, it, next_pc):
        mn = it.mnem
        ops = [self.normalize(o) for o in it.ops]
        if mn in ("move", "movb"):
            return self.emit_move(mn, ops, next_pc)
        if mn == "movi":
            v = self.val(ops[0])
            if len(ops) > 2 and ops[2].strip().lower() == "w":
                v = (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF
            return [f"{R(ops[1])} = {u32(v)}; GSP_NZ(c, {R(ops[1])}); c->v = 0;"]
        if mn == "movk":
            return [f"{R(ops[1])} = {u32(self.val(ops[0]))};"]
        if mn == "clr":
            return [f"{R(ops[0])} = 0; c->z = 1;"]
        if mn in ("add", "addc", "sub", "subb", "cmp"):
            s, d = R(ops[0]), R(ops[1])
            if mn.startswith("add"):
                e = f"gsp_add(c, {d}, {s}, {'c->c' if mn == 'addc' else '0'})"
            else:
                e = f"gsp_sub(c, {d}, {s}, {'c->c' if mn == 'subb' else '0'})"
            return [f"{e};" if mn == "cmp" else f"{d} = {e};"]
        if mn in ("addi", "subi", "cmpi", "addk", "subk"):
            v = self.val(ops[0])
            if mn in ("addi", "subi", "cmpi") and len(ops) > 2 and ops[2].strip().lower() == "w":
                v = (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF
            d = R(ops[1])
            if mn.startswith("add"):
                return [f"{d} = gsp_add(c, {d}, {u32(v)}, 0);"]
            e = f"gsp_sub(c, {d}, {u32(v)}, 0)"
            return [f"{e};" if mn == "cmpi" else f"{d} = {e};"]
        if mn in ("inc", "dec"):
            d = R(ops[0])
            return [f"{d} = gsp_{'add' if mn == 'inc' else 'sub'}(c, {d}, 1, 0);"]
        if mn == "neg":
            return [f"{R(ops[0])} = gsp_sub(c, 0, {R(ops[0])}, 0);"]
        if mn == "negb":
            return [f"{R(ops[0])} = gsp_sub(c, 0, {R(ops[0])}, c->c);"]
        if mn == "abs":
            d = R(ops[0])
            return [f"{{ uint32_t t = {d}; c->n = (uint8_t)(t >> 31); {d} = (t >> 31) ? 0u - t : t; "
                    f"c->z = {d} == 0; c->v = t == 0x80000000u; }}"]
        if mn in ("and", "andn", "or", "xor"):
            s, d = R(ops[0]), R(ops[1])
            op = {"and": "&=", "or": "|=", "xor": "^=", "andn": "&= ~"}[mn]
            return [f"{d} {op}{s}; c->z = {d} == 0;" if mn == "andn" else f"{d} {op} {s}; c->z = {d} == 0;"]
        if mn in ("andi", "andni", "ori", "xori"):
            v = self.val(ops[0])
            d = R(ops[1])
            if mn == "andni":
                v = ~v
            op = {"andi": "&=", "andni": "&=", "ori": "|=", "xori": "^="}[mn]
            return [f"{d} {op} {u32(v)}; c->z = {d} == 0;"]
        if mn == "not":
            return [f"{R(ops[0])} = ~{R(ops[0])}; c->z = {R(ops[0])} == 0;"]
        if mn == "btst":
            b, is_reg = self.imm_or_reg(ops[0])
            bit = f"({b} & 31)" if is_reg else str(self.val(ops[0]) & 31)
            return [f"c->z = !(({R(ops[1])} >> {bit}) & 1);"]
        if mn in ("sll", "sla", "srl", "sra", "rl"):
            return self.emit_shift(mn, ops)
        if mn in ("sext", "zext"):
            f = self.field(ops, 1)
            d = R(ops[0])
            if mn == "sext":
                return [f"{d} = gsp_sext({d}, c->fs[{f}]); GSP_NZ(c, {d}); c->v = 0;"]
            return [f"{d} = gsp_zext({d}, c->fs[{f}]); c->z = {d} == 0;"]
        if mn in ("mpys", "mpyu", "divs", "divu", "mods", "modu"):
            return [f"gsp_{mn}(c, {reg_index(ops[0])}, {reg_index(ops[1])});"]
        if mn == "lmo":
            s, d = R(ops[0]), R(ops[1])
            return [f"{{ uint32_t t = {s}; if (t) {{ int p = 31; while (!(t >> p)) p--; {d} = 31u - (uint32_t)p; c->z = 0; }} "
                    f"else {{ {d} = 0; c->z = 1; }} }}"]
        if mn == "movx":
            return [f"{R(ops[1])} = ({R(ops[1])} & 0xFFFF0000u) | ({R(ops[0])} & 0xFFFFu);"]
        if mn == "movy":
            return [f"{R(ops[1])} = ({R(ops[1])} & 0xFFFFu) | ({R(ops[0])} & 0xFFFF0000u);"]
        if mn == "addxy":
            return [f"{R(ops[1])} = gsp_addxy(c, {R(ops[1])}, {R(ops[0])});"]
        if mn == "subxy":
            return [f"{R(ops[1])} = gsp_subxy(c, {R(ops[1])}, {R(ops[0])});"]
        if mn == "cmpxy":
            return [f"gsp_subxy(c, {R(ops[1])}, {R(ops[0])});"]
        if mn == "setf":
            fs = self.val(ops[0]) & 31
            fe = self.val(ops[1]) & 1 if len(ops) > 1 else 0
            f = self.val(ops[2]) & 1 if len(ops) > 2 else 0
            return [f"c->fs[{f}] = {fs or 32}; c->fe[{f}] = {fe};"]
        if mn == "exgf":
            f = self.field(ops, 1)
            d = R(ops[0])
            return [f"{{ uint32_t o = (uint32_t)(c->fs[{f}] & 31) | (uint32_t)c->fe[{f}] << 5; "
                    f"c->fs[{f}] = (uint8_t)({d} & 31); if (!c->fs[{f}]) c->fs[{f}] = 32; "
                    f"c->fe[{f}] = (uint8_t)({d} >> 5 & 1); {d} = o; }}"]
        if mn == "getst":
            return [f"{R(ops[0])} = gsp_get_st(c);"]
        if mn == "putst":
            return [f"gsp_set_st(c, {R(ops[0])});"]
        if mn == "pushst":
            return ["gsp_push(c, gsp_get_st(c));"]
        if mn == "popst":
            return ["gsp_set_st(c, gsp_pop(c));"]
        if mn == "dint":
            return ["c->st_rest &= ~GSP_ST_IE;"]
        if mn == "eint":
            return ["c->st_rest |= GSP_ST_IE;"]
        if mn == "setc":
            return ["c->c = 1;"]
        if mn == "clrc":
            return ["c->c = 0;"]
        if mn in ("nop", "emu"):
            return [";"]
        if mn == "getpc":
            return [f"{R(ops[0])} = {u32(next_pc)};"]
        if mn == "mmtm":
            return self.emit_mm(True, ops)
        if mn == "mmfm":
            return self.emit_mm(False, ops)
        # -- control flow
        if mn in JUMPS_REL or mn in JUMPS_ABS:
            cond = CONDS[mn[2:]]
            target = self.val(ops[0])
            j = self.jump(target)
            return [j] if cond == "1" else [f"if ({cond}) {j}"]
        if mn in ("calla", "callr"):
            return [f"gsp_push(c, {u32(next_pc)});", self.jump(self.val(ops[0]))]
        if mn == "call":
            return [f"gsp_push(c, {u32(next_pc)}); c->pc = {R(ops[0])}; return;"]
        if mn == "jump":
            return [f"c->pc = {R(ops[0])}; return;"]
        if mn == "exgpc":
            d = R(ops[0])
            return [f"{{ uint32_t t = {d}; {d} = {u32(next_pc)}; c->pc = t; return; }}"]
        if mn == "rets":
            n = self.val(ops[0]) if ops and ops[0].strip() else 0
            extra = f" c->r[15] += {n * 16}u;" if n else ""
            return [f"c->pc = gsp_pop(c);{extra} return;"]
        if mn == "reti":
            return ["gsp_set_st(c, gsp_pop(c)); c->pc = gsp_pop(c); return;"]
        if mn == "trap":
            return [f"gsp_trap(c, {self.val(ops[0]) & 31}, {u32(next_pc)}); return;"]
        if mn in ("dsj", "dsjs"):
            d = R(ops[0])
            return [f"if (--{d} != 0) {self.jump(self.val(ops[1]))}"]
        if mn in ("dsjeq", "dsjne"):
            d = R(ops[0])
            cond = "c->z" if mn == "dsjeq" else "!c->z"
            return [f"if ({cond} && --{d} != 0) {self.jump(self.val(ops[1]))}"]
        # -- graphics instructions
        if mn == "pixt":
            s, d = parse_operand(ops[0]), parse_operand(ops[1])
            if s[0] == "reg" and d[0] == "xy":
                return [f"gsp_pixt_to_xy(c, {R(ops[0])}, {R(d[1])});"]
            if s[0] == "ind" and d[0] == "reg":     # pixel from memory (8 bits/pixel)
                return [f"{R(ops[1])} = gsp_rd(c, {R(s[1])}, 8);"]
        if mn == "fill":
            xy = 1 if ops and ops[0].strip().lower() == "xy" else 0
            return [f"gsp_fill(c, {xy});"]
        if mn == "pixblt":
            return ["gsp_pixblt_b_xy(c);"]
        if mn == "line":
            return [f"gsp_line(c, {self.val(ops[0]) & 1 if ops else 0});"]
        if mn == "drav":
            return [f"gsp_drav(c, {reg_index(ops[0])}, {reg_index(ops[1])});"]
        if mn == "cvxyl":
            return [f"{R(ops[1])} = gsp_cvxyl(c, {R(ops[0])});"]
        self.unimpl[mn] += 1
        return [f'gsp_unimplemented(c, {u32(addr)}, "{mn} {",".join(ops)}"); return;']

    def emit_shift(self, mn, ops):
        d = R(ops[1])
        o = parse_operand(ops[0])
        if o[0] == "reg":
            s = R(ops[0])
            # SLL/SLA/RL use the 5 LSBs; SRL/SRA use their two's complement.
            k = f"((0u - {s}) & 31)" if mn in ("srl", "sra") else f"({s} & 31)"
        else:
            k = str(self.val(o[1]) & 31)
        return [f"{d} = gsp_{mn}(c, {d}, {k});"]

    def emit_mm(self, store, ops):
        rp = reg_index(ops[0])
        regs = [reg_index(r) for r in ops[1:] if r.strip()]
        base = 16 if rp >= 16 else 0
        # order by register number within the file (SP counts as 15)
        key = lambda r: (r - 16) if r >= 16 else r  # noqa: E731
        out = []
        if store:     # R0 first at the highest address
            for r in sorted(regs, key=key):
                out.append(f"c->r[{rp}] -= 32; gsp_wr(c, c->r[{rp}], 32, c->r[{r}]);")
        else:         # lowest address into the highest register
            for r in sorted(regs, key=key, reverse=True):
                out.append(f"c->r[{r}] = gsp_rd(c, c->r[{rp}], 32); c->r[{rp}] += 32;")
        _ = base
        return out

    # -- module output
    def write(self, entries):
        name = self.mod.name
        out = self.out
        out.append(f"/* {name}: generated by tools/gsp/gsp2c.py - do not edit */")
        out.append('#include "cpu/gsp.h"')
        out.append("")
        out.append(f"void gsp_mod_{name}(gsp_t *c)")
        out.append("{")
        out.append("    switch (c->pc) {")
        # block lengths for the budget
        block_len = {}
        cur, n = None, 0
        for addr, it, sec in self.code:
            if addr in entries:
                if cur is not None:
                    block_len[cur] = n
                cur, n = addr, 0
            n += 1
        if cur is not None:
            block_len[cur] = n
        for i, (addr, it, sec) in enumerate(self.code):
            next_pc = (addr + it.size) & 0xFFFFFFFF
            for alias in self.alias_of.get(addr, ()):
                names = self.prog.labels.get(alias, [])
                out.append(f"    case {u32(alias)}:  /* {', '.join(names[:3])} (before alignment) */")
            if addr in entries:
                names = self.prog.labels.get(addr)
                if names:
                    out.append(f"    /* {', '.join(names[:3])} */")
                out.append(f"    case {u32(addr)}: L_{addr:08X}: GSP_ENTER(c, {u32(addr)}, {block_len[addr]});")
            for name in self.prog.labels.get(addr, ()):
                if name in PATCHES:
                    out.append(f"        {PATCHES[name]}  /* patch: {name} */")
            try:
                lines = self.emit(addr, it, next_pc)
            except TranslateError as e:
                self.unimpl["error"] += 1
                lines = [f'gsp_unimplemented(c, {u32(addr)}, "{str(e)[:60]!s}"); return;'.replace("\\", "/")]
            src = f"{it.mnem} {','.join(it.ops)}".replace("*/", "* /")
            out.append(f"        GSP_TRACE_INSN(c, {u32(addr)}); {' '.join(lines)}  /* {src} */")
            # falling through into data or the next module
            nxt = self.code[i + 1][0] if i + 1 < len(self.code) else None
            if nxt != next_pc and addr in self.falls_through and nxt is not None:
                out.append(f"        goto L_{nxt:08X};  /* through alignment padding */")
            elif nxt != next_pc and it.mnem not in ("rets", "reti", "jump", "exgpc", "trap", "call",
                                                   "jruc", "jauc"):
                out.append(f"        c->pc = {u32(next_pc)}; return;")
        out.append("    default:")
        out.append('        c->fault = "no entry point"; c->fault_pc = c->pc; c->stop = 1; return;')
        out.append("    }")
        out.append("}")


def find_aliases(prog):
    """Labels placed before .align/.even padding: their address holds NOPs
    and execution reaches the next instruction. Map label address -> insn."""
    aliases = {}
    for mod in prog.modules:
        for sec in mod.sections.values():
            pending = []
            for it in sec.items:
                addr = (sec.base + it.offset) & 0xFFFFFFFF
                if it.kind == "label":
                    pending.append(addr)
                elif it.kind == "align":
                    continue
                elif it.kind == "insn":
                    for a in pending:
                        if a != addr:
                            aliases[a] = addr
                    pending = []
                else:
                    pending = []
    return aliases


def find_entries(prog):
    entries = set()
    for mod in prog.modules:
        for sec in mod.sections.values():
            prev_kind = None
            pending_labels = False
            for it in sec.items:
                addr = (sec.base + it.offset) & 0xFFFFFFFF
                if it.kind == "label":
                    pending_labels = True
                    continue
                if it.kind == "insn":
                    if pending_labels or prev_kind in ("data", "space", None, "ctrl"):
                        entries.add(addr)
                    prev_kind = "ctrl" if it.mnem in CONTROL else "insn"
                elif it.kind in ("data", "space"):
                    prev_kind = "data"
                elif it.kind == "align":
                    prev_kind = "data"      # the next instruction needs a label
                pending_labels = False
    # immediate operands and data words that point at code
    code = prog.insns
    for addr, (mod, it) in code.items():
        for op in it.ops:
            o = parse_operand(op)
            text = o[1] if o[0] in ("imm", "abs") else None
            if text:
                try:
                    v = prog.value(mod, text)
                except TranslateError:
                    continue
                if v in code:
                    entries.add(v)
    for addr, bits, value in prog.data_items:
        if bits == 32 and isinstance(value, int) and (value & 0xFFFFFFFF) in code:
            entries.add(value & 0xFFFFFFFF)
    return entries


def write_image(prog, path, base, end):
    size = (end - base) // 8
    buf = bytearray(size)
    for addr, bits, value in prog.data_items:
        if not isinstance(value, int) or not (base <= addr < end):
            continue
        off = addr - base
        v = value & ((1 << bits) - 1)
        if off % 8 == 0 and bits % 8 == 0:
            for k in range(bits // 8):
                buf[off // 8 + k] = (v >> (8 * k)) & 0xFF
        else:
            for b in range(bits):
                bit = off + b
                if (v >> b) & 1:
                    buf[bit // 8] |= 1 << (bit % 8)
                else:
                    buf[bit // 8] &= ~(1 << (bit % 8)) & 0xFF
    with open(path, "wb") as f:
        f.write(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="orig")
    ap.add_argument("--gen", default="build/gen")
    ap.add_argument("--out", default="build/gen")
    ap.add_argument("--extras", default=None, help="mods/*/gen.txt collected (extras.py)")
    args = ap.parse_args()

    from extras import load_extras
    extras = load_extras(args.extras)
    prog = Program(args.src, args.gen, extras["module"], extras)
    prog.aliases = find_aliases(prog)
    entries = find_entries(prog) | set(prog.aliases.values())
    cdir = os.path.join(args.out, "c")
    os.makedirs(cdir, exist_ok=True)

    table = []
    unimpl = defaultdict(int)
    names = []
    for mod in prog.modules:
        w = ModuleWriter(prog, mod)
        if not w.code:
            continue
        w.write(entries)
        for k, v in w.unimpl.items():
            unimpl[k] += v
        path = os.path.join(cdir, f"gsp_{mod.name}.c")
        new = "\n".join(w.out) + "\n"
        if not os.path.exists(path) or open(path).read() != new:
            with open(path, "w") as f:
                f.write(new)
        names.append(mod.name)
        # contiguous code ranges per section
        by_sec = defaultdict(list)
        for addr, it, sec in w.code:
            by_sec[sec.name].append((addr, addr + it.size))
        for rng in by_sec.values():
            table.append((min(a for a, _ in rng), max(b for _, b in rng), mod.name))
    table.sort()

    lines = ["/* generated by tools/gsp/gsp2c.py - do not edit */", '#include "cpu/gsp.h"', ""]
    for n in names:
        lines.append(f"void gsp_mod_{n}(gsp_t *c);")
    lines.append("")
    lines.append("const gsp_module gsp_modules[] = {")
    for start, end, n in table:
        lines.append(f"    {{{u32(start)}, {u32(end)}, gsp_mod_{n}, \"{n}\"}},")
    lines.append("};")
    lines.append(f"const int gsp_nmodules = {len(table)};")
    path = os.path.join(cdir, "gsp_modules.c")
    new = "\n".join(lines) + "\n"
    if not os.path.exists(path) or open(path).read() != new:
        with open(path, "w") as f:
            f.write(new)
    with open(os.path.join(cdir, "modules.cmake"), "w") as f:
        f.write("set(WWF_GSP_SOURCES\n")
        for n in names:
            f.write(f"  ${{WWF_GEN_DIR}}/c/gsp_{n}.c\n")
        f.write("  ${WWF_GEN_DIR}/c/gsp_modules.c\n)\n")

    write_image(prog, os.path.join(args.out, "rom.bin"), ROM_BASE, ROM_END)
    write_image(prog, os.path.join(args.out, "ram.bin"), RAM_BASE, RAM_END)
    with open(os.path.join(args.out, "symbols.txt"), "w") as f:
        for addr in sorted(prog.labels):
            for n in prog.labels[addr]:
                f.write(f"{addr:08X} {n}\n")

    print(f"modules: {len(names)}, instructions: {len(prog.insns)}, entries: {len(entries)}")
    if unimpl:
        print("untranslated:", ", ".join(f"{k}:{v}" for k, v in sorted(unimpl.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
