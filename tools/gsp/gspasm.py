#!/usr/bin/env python3
"""GSPA-compatible assembler front-end for the WWF WrestleMania source.

It reproduces what `preasm` + `GSPA` + `gsplnk` did, up to the point of
laying out every module in the TMS34010 address space and resolving every
symbol. Instead of machine code it keeps each instruction as parsed text,
which the C translator consumes.

    python3 tools/gsp/gspasm.py --src orig --gen build/gen --report

Supported: .include, TI macros (:p: substitution, NAME? unique labels,
.var/.asg/.eval/.loop/.mexit, $isreg/$symcmp/$symlen/$isname), .if/.elseif/
.else/.endif, .equ/.set/equ, .bss/.usect/.sect/.text/.data, .word/.long/
.byte/.string/.field, .even/.align, '#' and '$' local labels, .def/.ref/
.global/.globl.
"""

import argparse
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gsp34010 import JUMPS_REL, KNOWN, fixed_size, is_reg  # noqa: E402
from gspexpr import (REGISTERS, ExprError, Undefined, evaluate, parse_expr,  # noqa: E402
                     split_operands, symbols_in, to_s32)

IGNORED = {
    ".title", ".file", ".width", ".option", ".mnolist", ".mlist", ".list", ".nolist",
    ".page", ".length", ".asm", ".sslist", ".ssnolist", ".tab", ".cdecls", ".version",
}
GLOBAL_DIRS = {".global", ".globl", ".def", ".ref"}


# preasm scoping of '#' locals: a "#****" header line in the module's own
# source starts a new scope, nothing else does. With this rule every local
# reference in the game resolves inside its own scope (docs/GSPASM.md).


class AsmError(Exception):
    pass


class Loc:
    __slots__ = ("file", "line")

    def __init__(self, file, line):
        self.file = file
        self.line = line

    def __str__(self):
        return f"{self.file}:{self.line}"


# ---- items -------------------------------------------------------------------

class Item:
    """One thing placed in a section. size is in bits."""
    __slots__ = ("kind", "size", "value", "mnem", "ops", "loc", "offset", "name", "scope")

    def __init__(self, kind, size=0, value=None, mnem=None, ops=None, loc=None, name=None):
        self.kind = kind      # "label", "data", "insn", "space", "align"
        self.size = size
        self.value = value    # data: int or AST; align: boundary
        self.mnem = mnem
        self.ops = ops
        self.loc = loc
        self.name = name
        self.offset = 0
        self.scope = None


class Section:
    def __init__(self, name):
        self.name = name
        self.items = []
        self.size = 0
        self.base = None  # absolute address after linking


class Symbol:
    __slots__ = ("name", "kind", "value", "section", "item", "loc")

    def __init__(self, name, kind, value=None, section=None, item=None, loc=None):
        self.name = name
        self.kind = kind          # "abs" (int), "expr" (AST, deferred), "label"
        self.value = value
        self.section = section
        self.item = item
        self.loc = loc


class Macro:
    def __init__(self, name, params, body, loc):
        self.name = name
        self.params = params
        self.body = body
        self.loc = loc


# ---- source handling ----------------------------------------------------------

class SourceTree:
    """Case-insensitive file lookup over the generated dir and the original source."""

    # set by set_extras(): directories searched before the original source, and
    # in-memory edits per file (tools/gsp/extras.py)
    extra_dirs = []
    edits = {}

    def __init__(self, dirs):
        dirs = [d for d in dirs if d and os.path.isdir(d)]
        if len(dirs) >= 2 and self.extra_dirs:      # [generated, ..., original]
            dirs = dirs[:-1] + [d for d in self.extra_dirs if os.path.isdir(d)] + dirs[-1:]
        self.dirs = dirs
        self.index = {}
        for d in reversed(self.dirs):  # earlier dirs win
            for f in os.listdir(d):
                self.index[f.upper()] = os.path.join(d, f)
        self.cache = {}

    extra_modules = []

    def defined_names(self):
        """Names the extra modules define (SUBR/SUBRP lines and labels in column 0)."""
        if not hasattr(self, "_defined"):
            self._defined = set()
            for m in self.extra_modules:
                path = self.find(m + ".ASM")
                if not path:
                    continue
                with open(path, "rb") as f:
                    for ln in f.read().decode("latin-1").split("\n"):
                        mm = re.match(r"^\s*SUBRP?\s+([A-Za-z_][A-Za-z0-9_]*)", ln, re.I) or \
                            re.match(r"^([A-Za-z_][A-Za-z0-9_]*):?\s*(?:;.*)?$", ln)
                        if mm:
                            self._defined.add(mm.group(1))
        return self._defined

    def fill_slot(self, lines, slots, renames):
        """See extras.py 'edit FILE slot': copy the operands of slot M's line to slot N's."""
        only = None
        always = False
        if "!" in slots:                   # N=M!: copy the line even when nothing in it is renamed
            slots = slots.replace("!", "")
            always = True
        if "@" in slots:                   # N=M@REGEX: only the ";N" lines matching REGEX
            slots, only = slots.split("@", 1)
            only = re.compile(only)
        n, m = slots.split("=")
        last = None
        if "/" in m:                       # N=M/L: tables that stop at slot L get an entry for N
            m, last = m.split("/")
        cn = re.compile(r";\s*\(?%s\b" % re.escape(n), re.I)
        cm = re.compile(r";\s*\(?%s\b" % re.escape(m), re.I)
        pairs = [p.split("=") for p in renames.split(",") if p]
        defined = self.defined_names()
        out = list(lines)
        for i, ln in enumerate(lines):
            if not cn.search(ln) or ln.lstrip().startswith(";"):
                continue
            if only is not None and not only.search(ln):
                continue
            for j in range(i - 1, max(i - 14, -1), -1):
                if cm.search(lines[j]) and not lines[j].lstrip().startswith(";"):
                    body = re.split(r";", lines[j], 1)[0]
                    d = re.match(r"^(\s*)(\.long|\.word|REFLONG|LWWW)(\s+)(.*)$", string=body, flags=re.I)
                    if not d:
                        break
                    changed = False

                    def ren(mo):
                        nonlocal changed
                        cand = self.rename(mo.group(0), pairs, defined)
                        if cand != mo.group(0):
                            changed = True
                        return cand
                    ops = re.sub(r"[A-Za-z_][A-Za-z0-9_]*", ren, d.group(4))
                    if changed or always:
                        out[i] = f"{d.group(1)}{d.group(2)}{d.group(3)}{ops.rstrip()}\t\t" + ln[ln.index(";"):]
                    break
        if last is None:
            return out
        # tables with no entry for slot N at all: append one after slot L's line,
        # made from slot M's (renamed where the extra modules have a counterpart,
        # else the same entry)
        cl = re.compile(r";\s*%s\b" % re.escape(last))
        added = {}
        for i, ln in enumerate(lines):
            if not cl.search(ln) or ln.lstrip().startswith(";"):
                continue
            if any(cn.search(lines[k]) for k in range(i + 1, min(i + 4, len(lines)))):
                continue
            for j in range(i - 1, max(i - 14, -1), -1):
                if cm.search(lines[j]) and not lines[j].lstrip().startswith(";"):
                    d = re.match(r"^(\s*)(\.long|\.word|REFLONG|LWWW)(\s+)(.*)$", string=lines[j].split(";")[0], flags=re.I)
                    if not d:
                        break

                    def ren2(mo):
                        return self.rename(mo.group(0), pairs, defined)
                    ops = re.sub(r"[A-Za-z_][A-Za-z0-9_]*", ren2, d.group(4))
                    added[i] = f"{d.group(1)}{d.group(2)}{d.group(3)}{ops.rstrip()}\t\t;{n} added by a mod"
                    break
        if added:
            res = []
            for i, ln in enumerate(out):
                res.append(ln)
                if i in added:
                    res.append(added[i])
            out = res
        return out

    @staticmethod
    def rename(name, pairs, defined):
        """The first pair that maps name to something the extra modules define (fill_slot)."""
        for a, b in pairs:
            if b.endswith("!"):                # A=B! (A_=B_!): whether the extra modules define it or not
                if name == a:
                    return b[:-1]
                if a.endswith("_") and name.startswith(a):
                    return b[:-1] + name[len(a):]
                continue
            if a.endswith("_") and name.startswith(a):
                cand = b + name[len(a):]
            elif a.endswith("*") and name.startswith(a[:-1]):
                cand = b.rstrip("*") + name[len(a) - 1:]
            else:
                cand = b if name == a else None
            if cand and cand in defined:
                return cand
        return name

    def token_slot(self, lines, spec, renames):
        """See extras.py 'edit FILE tokslot': an entry by its neighbours in a run of data operands."""
        off, before, after, src = spec.split(":", 3)
        off = int(off)
        rb, ra, rs = re.compile(before, re.I), re.compile(after, re.I), re.compile(src, re.I)
        pairs = [p.split("=") for p in renames.split(",") if p]
        defined = self.defined_names()
        data = re.compile(r"^(\s*(?:\.long|\.word|REFLONG)\s+)([^;]*?)(\s*(?:;.*)?)$", re.I)
        out = list(lines)
        toks = []                          # (line, operand index, text); None ends a run
        for i, ln in enumerate(lines):
            m = data.match(ln)
            if not m:
                if ln.strip() and not ln.lstrip().startswith(";"):
                    toks.append(None)
                continue
            for k, t in enumerate(m.group(2).split(",")):
                toks.append((i, k, t.strip()))
        new_ops = {}
        for k in range(off, len(toks) - 1):
            run = toks[k - off:k + 2]
            if any(t is None for t in run):
                continue
            t, prev, nxt, frm = toks[k], toks[k - 1], toks[k + 1], toks[k - off]
            if rb.search(prev[2]) and ra.search(nxt[2]) and not ra.search(t[2]) and rs.search(frm[2]):
                new_ops[(t[0], t[1])] = re.sub(r"[A-Za-z_#][A-Za-z0-9_]*",
                                               lambda mo: self.rename(mo.group(0), pairs, defined), frm[2])
        for i in sorted({i for i, _ in new_ops}):
            m = data.match(lines[i])
            ops = [x.strip() for x in m.group(2).split(",")]
            for k in range(len(ops)):
                ops[k] = new_ops.get((i, k), ops[k])
            out[i] = m.group(1) + ",".join(ops) + m.group(3)
        return out

    def clone_block(self, lines, spec, renames):
        """See extras.py 'edit FILE cloneblock': a renamed copy of the lines from START to END."""
        start, end = spec.split(":", 1)
        rs, re_ = re.compile(start), re.compile(end)
        pairs = [p.split("=") for p in renames.split(",") if p]
        defined = self.defined_names()
        a = next((i for i, ln in enumerate(lines) if rs.search(ln)), None)
        if a is None:
            return lines
        b = next((i for i in range(a + 1, len(lines)) if re_.search(lines[i])), len(lines))
        copy = []
        for ln in lines[a:b]:
            code, sep, comment = ln.partition(";")
            copy.append(re.sub(r"[A-Za-z_][A-Za-z0-9_]*", lambda mo: self.rename(mo.group(0), pairs, defined), code)
                        + sep + comment)
        return lines[:b] + copy + lines[b:]

    def copy_block(self, lines, spec, renames):
        """See extras.py 'edit FILE copyblock': a wrestler's block from another's."""
        head, src = spec.split("=", 1)
        rh, rs = re.compile(head), re.compile(src)
        pairs = [p.split("=") for p in renames.split(",") if p]
        defined = self.defined_names()

        def body(i):                       # lines under line i: up to the next one in column 0
            j = i + 1
            while j < len(lines) and (not lines[j].strip() or lines[j][0] in " \t"):
                j += 1
            while j > i + 1 and not lines[j - 1].strip():
                j -= 1                     # blank lines before the next label stay
            return j

        out, i, last_src = [], 0, None
        while i < len(lines):
            ln = lines[i]
            if rs.search(ln):
                last_src = i
            if rh.search(ln) and last_src is not None and last_src != i:
                out.append(ln)
                e = body(i)
                for k in range(last_src + 1, body(last_src)):
                    out.append(re.sub(r"[A-Za-z_][A-Za-z0-9_]*",
                                      lambda mo: self.rename(mo.group(0), pairs, defined), lines[k].split(";")[0]).rstrip())
                i = e
                continue
            out.append(ln)
            i += 1
        return out

    def clone_smove(self, lines, label, spec):
        """See extras.py 'edit FILE clone': a copy of a special move process with another input."""
        parts = spec.split()
        new_label, joy, btn = parts[:3]
        orig = parts[3] if len(parts) > 3 else None       # the button of the original way in: presses of btn count for it too
        head = re.compile(r"^(\s*SUBRP?\s+)?%s\s*$" % re.escape(label), re.I)
        start = next((i for i, ln in enumerate(lines) if head.match(ln)), None)
        if start is None:
            return lines
        stop = re.compile(r"^\s*SUBRP?\s+\S|^[A-Za-z_]\w*\s*$")
        end = next((i for i in range(start + 1, len(lines)) if stop.match(lines[i])), len(lines))
        block = ["\tSUBRP\t" + new_label]
        wait = re.compile(r"^(\s*)WAITSWITCH_DWN\s+([^,\s]+)\s*,\s*[^,]+,\s*(.+?)\s*$", re.I)
        gate_done = False
        alias_done = False
        for ln in lines[start + 1:end]:
            m = wait.match(ln)
            if m:
                if m.group(2).upper().startswith("B_"):
                    ln = f"{m.group(1)}WAITSWITCH_DWN\t{btn},J_ALL,{m.group(3)}"
                    if orig and orig != btn and not alias_done:
                        ln += ("\n\tPUSH\ta0,a1\n\tmovi\t" + btn.replace("B_", "PLAYER_") + "_VAL,a0\n\tmovi\t"
                               + orig.replace("B_", "PLAYER_") + "_VAL,a1\n\tcalla\teasy_alias\n\tPULL\ta0,a1")
                        alias_done = True
                else:
                    ln = f"{m.group(1)}WAITSWITCH_DWN\t{joy},0,{m.group(3)}"
            block.extend(ln.split("\n"))
            if not gate_done and re.match(r"^#(lp|reset)\s*$", ln, re.I):
                gate = ln.strip()
                # only while the mod's switch is on: otherwise look again next tick
                block += ["\tmove\t@easy_enabled,a14", "\tjrnz\t#egok", "\tSLEEPK\t1", f"\tjruc\t{gate}", "#egok"]
                gate_done = True
        if not gate_done:
            return lines
        return lines[:end] + block + lines[end:]

    def find(self, name):
        return self.index.get(os.path.basename(name.replace("\\", "/")).upper())

    def lines(self, path):
        if path not in self.cache:
            with open(path, "rb") as f:
                text = f.read().decode("latin-1")
            lines = text.replace("\r", "").replace("\x1a", "").split("\n")
            key = os.path.basename(path).upper()
            for op, pat, new in self.edits.get(key, []) + self.edits.get("*", []):
                if op == "slot":
                    lines = self.fill_slot(lines, pat, new)
                    continue
                if op == "clone":
                    lines = self.clone_smove(lines, pat, new)
                    continue
                if op == "cloneblock":
                    lines = self.clone_block(lines, pat, new)
                    continue
                if op == "tokslot":
                    lines = self.token_slot(lines, pat, new)
                    continue
                if op == "copyblock":
                    lines = self.copy_block(lines, pat, new)
                    continue
                nth = 0
                mo = re.match(r"^\{(\d+)\}(.*)$", pat)
                if mo:                       # {N}pattern: only the Nth line that matches
                    nth, pat = int(mo.group(1)), mo.group(2)
                rx = re.compile(pat, re.I)
                out = []
                done = False
                seen = 0
                for ln in lines:
                    hit = bool(rx.search(ln))
                    seen += hit
                    if op == "sub":
                        if not nth or seen == nth:
                            ln = rx.sub(new, ln)
                    out.extend(ln.split("\n"))
                    if op == "after" and not done and hit and (not nth or seen == nth):
                        out.extend(new.split("\n"))
                        done = True
                lines = out
            self.cache[path] = lines
        return self.cache[path]


def strip_comment(line):
    quote = None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
        elif ch == '"':
            quote = ch
        elif ch == "'" and re.match(r"'(?:[^'\\]|\\.){1,4}'", line[i:]):
            quote = ch
        elif ch == ";":
            return line[:i]
    return line


_LABEL = re.compile(r"^([A-Za-z_#$.][A-Za-z0-9_$#?.]*):?")
# ":sym:" and ":sym(i):"; a ".S"-style qualifier (":R1.S:" in SWAP) is ignored
_FORCED = re.compile(r":([A-Za-z_][A-Za-z0-9_]*)(?:\.[A-Za-z])?(?:\(([^():]*)\))?:")
_IDENT = re.compile(r"[A-Za-z_$#][A-Za-z0-9_$#?]*")
_BUILTIN = re.compile(r"\$(isreg|symcmp|symlen|isname|isdefed|iscons|firstch|lastch)\s*\(([^()]*)\)", re.I)


def split_line(line):
    """Returns (label, mnemonic, operand_text)."""
    if not line.strip():
        return None, None, ""
    label = None
    rest = line
    if not line[0].isspace():
        m = _LABEL.match(line)
        if m:
            label = m.group(1)
            rest = line[m.end():]
        else:
            rest = line
    rest = rest.strip()
    if not rest:
        return label, None, ""
    parts = rest.split(None, 1)
    return label, parts[0], parts[1].strip() if len(parts) > 1 else ""


# ---- the assembler -------------------------------------------------------------

class Module:
    def __init__(self, name):
        self.name = name
        self.sections = {}
        self.symbols = {}
        self.globals = set()
        self.refs = set()
        self.errors = []
        self.missing_includes = set()
        self.mnemonics = Counter()

    def section(self, name):
        if name not in self.sections:
            self.sections[name] = Section(name)
        return self.sections[name]


class Assembler:
    MAX_SUBST_DEPTH = 16

    def __init__(self, tree, module_name):
        self.tree = tree
        self.mod = Module(module_name)
        self.macros = {}
        self.subst_global = {}
        self.frames = []          # macro frames: dict of local substitution symbols
        self.cond = []            # stack of [active, taken, parent_active]
        self.cur = self.mod.section(".text")
        self.scope = ""           # last non-local label, for '#'/'$' locals
        self.uid = 0
        self.loc = Loc(module_name, 0)
        self.stop_file = False
        self.local_defs = defaultdict(list)  # "#x" -> [(seq, "scope#x")]
        self.local_refs = []                 # (seq, "#x", "scope#x")
        self.seq = 0
        self.ambiguous_locals = []

    # -- diagnostics
    def error(self, msg):
        self.mod.errors.append(f"{self.loc}: {msg}")

    # -- substitution symbols
    def subst_lookup(self, name):
        for frame in reversed(self.frames):
            if name in frame:
                return frame[name]
        return self.subst_global.get(name)

    def subst_assign(self, name, value):
        for frame in reversed(self.frames):
            if name in frame:
                frame[name] = value
                return
        self.subst_global[name] = value

    def forced_subst(self, text):
        for _ in range(self.MAX_SUBST_DEPTH):
            changed = False

            def rep(m):
                nonlocal changed
                val = self.subst_lookup(m.group(1))
                if val is None:
                    return m.group(0)
                changed = True
                if m.group(2) is not None:
                    try:
                        idx = self.eval_abs(self.plain_subst(m.group(2)))
                    except (Undefined, ExprError):
                        idx = 1
                    return val[idx - 1:idx]
                return val

            text = _FORCED.sub(rep, text)
            if not changed:
                break
        return text

    def builtin_text(self, arg):
        arg = arg.strip()
        if len(arg) >= 2 and arg[0] == arg[-1] == '"':
            return arg[1:-1]
        val = self.subst_lookup(arg)
        return val if val is not None else arg

    def eval_builtins(self, text):
        def rep(m):
            fn = m.group(1).lower()
            args = split_operands(m.group(2)) if m.group(2).strip() else []
            texts = [self.builtin_text(a) for a in args]
            if fn == "isreg":
                return "1" if texts and is_reg(texts[0]) else "0"
            if fn == "symlen":
                return str(len(texts[0])) if texts else "0"
            if fn == "symcmp":
                a, b = (texts + ["", ""])[:2]
                return "0" if a == b else ("-1" if a < b else "1")
            if fn == "isname":   # syntactically a symbol name (not a number)
                return "1" if texts and re.match(r"^[A-Za-z_#$.][A-Za-z0-9_$#?.]*$", texts[0]) else "0"
            if fn == "isdefed":
                return "1" if texts and self.lookup_symbol_any(texts[0]) else "0"
            if fn == "iscons":
                try:
                    self.eval_abs(texts[0])
                    return "1"
                except Exception:
                    return "0"
            if fn == "firstch":
                return str(ord(texts[0][0])) if texts and texts[0] else "0"
            if fn == "lastch":
                return str(ord(texts[0][-1])) if texts and texts[0] else "0"
            return m.group(0)
        return _BUILTIN.sub(rep, text)

    def plain_subst(self, text):
        for _ in range(self.MAX_SUBST_DEPTH):
            out, i, changed, quote = [], 0, False, None
            while i < len(text):
                ch = text[i]
                if quote:
                    out.append(ch)
                    if ch == quote:
                        quote = None
                    i += 1
                    continue
                if ch == '"':
                    quote = ch
                    out.append(ch)
                    i += 1
                    continue
                m = _IDENT.match(text, i)
                if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] in "_$#?")):
                    word = m.group(0)
                    val = self.subst_lookup(word)
                    if val is not None and val != word:
                        out.append(val)
                        changed = True
                    else:
                        out.append(word)
                    i = m.end()
                    continue
                out.append(ch)
                i += 1
            text = "".join(out)
            if not changed:
                break
        return text

    # -- symbols
    @staticmethod
    def is_local(name):
        return len(name) > 1 and (name[0] == "#" or (name[0] == "$" and name[1:].isdigit()))

    def local_name(self, name):
        if self.is_local(name):
            full = f"{self.scope}{name}"
            self.seq += 1
            self.local_defs[name].append((self.seq, full))
            return full
        return name

    def localize_expr(self, text):
        """Rewrites '#local' references in an expression to their scoped names."""
        if "#" not in text and "$" not in text:
            return text

        def rep(m):
            w = m.group(0)
            if self.is_local(w):
                self.seq += 1
                self.local_refs.append((self.seq, w, f"{self.scope}{w}"))
                return f"{self.scope}{w}"
            return w
        return re.sub(r"(?<![A-Za-z0-9_])[#$][A-Za-z0-9_?]+", rep, text)

    def lookup_symbol_any(self, name):
        return name in self.mod.symbols

    def abs_lookup(self, name):
        if name.lower() in REGISTERS:
            raise Undefined(name)
        if name == "$":
            raise Undefined(name)
        sym = self.mod.symbols.get(name)
        if sym is not None and sym.kind == "abs":
            return sym.value
        raise Undefined(name)

    def eval_abs(self, text):
        return evaluate(parse_expr(self.localize_expr(text)), self.abs_lookup)

    def freeze(self, ast):
        """Partially evaluates an AST, folding symbols that are absolute now."""
        k = ast[0]
        if k == "sym":
            name = ast[1]
            sym = self.mod.symbols.get(name)
            if sym is not None and sym.kind == "abs":
                return ("num", sym.value)
            return ast
        if k == "un":
            a = self.freeze(ast[2])
            return ("num", evaluate(("un", ast[1], a), None)) if a[0] == "num" else ("un", ast[1], a)
        if k == "bin":
            a, b = self.freeze(ast[2]), self.freeze(ast[3])
            if a[0] == "num" and b[0] == "num":
                return ("num", evaluate(("bin", ast[1], a, b), None))
            return ("bin", ast[1], a, b)
        if k == "xy":
            return ("xy", self.freeze(ast[1]), self.freeze(ast[2]))
        return ast

    def value_expr(self, text):
        """Parses an operand expression: an int if absolute now, else a frozen AST."""
        text = self.localize_expr(text)
        if "$" in re.sub(r"\$[A-Za-z0-9_]+", "", text):
            text = re.sub(r"(?<![A-Za-z0-9_])\$(?![A-Za-z0-9_])", "__here__", text)
            here = Item("label", name=None, loc=self.loc)
            self.here_marker(here)
            ast = parse_expr(text)
            ast = self.replace_here(ast, here.name)
        else:
            ast = parse_expr(text)
        ast = self.freeze(ast)
        return ast[1] if ast[0] == "num" else ast

    def here_marker(self, item):
        self.uid += 1
        item.name = f"__here{self.uid}"
        self.define_label(item.name, local_ok=True)

    def replace_here(self, ast, name):
        k = ast[0]
        if k == "sym" and ast[1] == "__here__":
            return ("sym", name)
        if k == "un":
            return ("un", ast[1], self.replace_here(ast[2], name))
        if k == "bin":
            return ("bin", ast[1], self.replace_here(ast[2], name), self.replace_here(ast[3], name))
        if k == "xy":
            return ("xy", self.replace_here(ast[1], name), self.replace_here(ast[2], name))
        return ast

    def define_label(self, name, local_ok=False):
        if not local_ok:
            if self.is_local(name):
                name = self.local_name(name)
            else:
                pass
        if name in self.mod.symbols and self.mod.symbols[name].kind == "label":
            self.error(f"label {name} defined twice")
            return
        item = Item("label", name=name, loc=self.loc)
        self.cur.items.append(item)
        self.mod.symbols[name] = Symbol(name, "label", section=self.cur.name, item=item, loc=self.loc)

    def define_equ(self, name, text, redefinable):
        name = self.local_name(name)
        try:
            val = self.value_expr(text)
        except ExprError as e:
            self.error(f"bad expression for {name}: {e}")
            return
        old = self.mod.symbols.get(name)
        if old is not None and not redefinable and old.kind == "label":
            self.error(f"{name} redefined")
        if isinstance(val, int):
            self.mod.symbols[name] = Symbol(name, "abs", value=to_s32(val), loc=self.loc)
        else:
            self.mod.symbols[name] = Symbol(name, "expr", value=val, loc=self.loc)

    # -- emitting
    def emit(self, item):
        self.cur.items.append(item)

    def emit_data(self, bits, text):
        try:
            v = self.value_expr(text) if isinstance(text, str) else text
        except ExprError as e:
            self.error(str(e))
            v = 0
        self.emit(Item("data", size=bits, value=v, loc=self.loc))

    # -- directives
    def do_data(self, mnem, ops):
        if mnem == ".word":
            for op in split_operands(ops):
                self.emit_data(16, op)
        elif mnem == ".long":
            for op in split_operands(ops):
                self.emit_data(32, op)
        elif mnem in (".byte", ".string"):
            for op in split_operands(ops):
                if len(op) >= 2 and op[0] == '"' and op[-1] == '"':
                    for ch in op[1:-1]:
                        self.emit_data(8, ord(ch))
                else:
                    self.emit_data(8, op)
        elif mnem == ".field":
            parts = split_operands(ops)
            bits = self.eval_abs(parts[1]) if len(parts) > 1 else 32
            self.emit_data(bits, parts[0])

    def do_bss(self, ops, section=".bss"):
        parts = split_operands(ops)
        name = parts[0]
        size = self.eval_abs(parts[1]) if len(parts) > 1 else 0
        align = self.eval_abs(parts[2]) if len(parts) > 2 else 0
        saved = self.cur
        self.cur = self.mod.section(section)
        if align:
            self.emit(Item("align", value=16))
        self.define_label(self.local_name(name), local_ok=True)
        self.emit(Item("space", size=size))
        self.cur = saved

    def directive(self, label, mnem, ops):
        if mnem in IGNORED:
            return
        if mnem in (".equ", ".set", "equ", ".asg", ".eval", ".var"):
            pass
        if label and mnem not in (".equ", ".set", "equ", ".macro"):
            self.define_label(label)
        if mnem in (".equ", "equ", ".set"):
            if not label:
                self.error(f"{mnem} without a symbol")
                return
            self.define_equ(label, ops, mnem == ".set")
        elif mnem in GLOBAL_DIRS:
            for name in split_operands(ops):
                if name:
                    self.mod.globals.add(name)
                    if mnem == ".ref":
                        self.mod.refs.add(name)
        elif mnem in (".word", ".long", ".byte", ".string", ".field"):
            self.do_data(mnem, ops)
        elif mnem == ".even":
            self.emit(Item("align", value=16))
        elif mnem == ".align":
            self.emit(Item("align", value=self.eval_abs(ops) if ops else 256))
        elif mnem == ".space":
            self.emit(Item("space", size=self.eval_abs(ops)))
        elif mnem == ".bss":
            self.do_bss(ops)
        elif mnem == ".usect":
            parts = split_operands(ops)
            self.do_bss(label + "," + ",".join(parts[1:]) if label else ops,
                        section=parts[0].strip('"'))
        elif mnem == ".text":
            self.cur = self.mod.section(".text")
        elif mnem == ".data":
            self.cur = self.mod.section(".data")
        elif mnem == ".sect":
            self.cur = self.mod.section(ops.strip().strip('"'))
        elif mnem == ".end":
            self.stop_file = True
        elif mnem == ".emsg":
            self.error(f".emsg {ops}")
        elif mnem == ".wmsg":
            pass
        else:
            self.error(f"unknown directive {mnem}")

    # -- main line processing
    def run_lines(self, lines, file, first_line=1):
        i = 0
        self._lines_stack = getattr(self, "_lines_stack", 0)
        n = len(lines)
        while i < n:
            if self.stop_file:
                return
            raw = lines[i]
            lineno = first_line + i
            i += 1
            self.loc = Loc(file, lineno)
            if not raw or raw[0] == "*" or raw.startswith("#*"):
                if raw.startswith("#*") and not self.frames and \
                        file.upper() == self.main_file:
                    # preasm: a "#****" header line opens a new local scope
                    self.uid += 1
                    self.scope = f"__blk{self.uid}"
                continue
            line = strip_comment(raw)
            if not line.strip():
                continue

            # Peek at the mnemonic for conditional handling before substitution.
            _, pm, pops = split_line(line)
            pml = pm.lower() if pm else None

            if pml in (".if", ".elseif", ".else", ".endif"):
                self.conditional(pml, pops)
                continue
            if self.cond and not self.cond[-1][0]:
                if pml == ".macro":
                    i = self.skip_block(lines, i, ".macro", ".endm")
                elif pml == ".loop":
                    i = self.skip_block(lines, i, ".loop", ".endloop")
                continue

            if pml == ".macro":
                label, _, params = split_line(line)
                body, i = self.collect_block(lines, i, ".macro", ".endm")
                plist = [p.strip() for p in split_operands(params) if p.strip()]
                self.macros[label.lower()] = Macro(label, plist, body, self.loc)
                continue
            if pml == ".loop":
                body, i = self.collect_block(lines, i, ".loop", ".endloop")
                text = self.plain_subst(self.eval_builtins(self.forced_subst(pops)))
                count = self.eval_abs(text) if text.strip() else 1024
                for _ in range(max(0, count)):
                    self.run_lines(body, file, lineno + 1)
                continue

            line = self.forced_subst(line)
            label, mnem, ops = split_line(line)
            if mnem is None:
                if label:
                    self.define_label(label)
                continue
            ml = mnem.lower()

            if ml in (".asg", ".eval", ".var"):
                self.subst_directive(ml, ops)
                continue
            if ml == ".include" or ml == ".copy":
                self.include(ops)
                continue
            if ml == ".mexit":
                raise MacroExit()

            ops = self.plain_subst(self.eval_builtins(ops))

            if ml in self.macros:
                if label:
                    self.define_label(label)
                self.expand_macro(self.macros[ml], ops)
                continue
            if ml.startswith(".") or ml == "equ":
                try:
                    self.directive(label, ml, ops)
                except (ExprError, Undefined) as e:
                    self.error(f"{ml} {ops}: {e}")
                continue

            # A real instruction.
            if label:
                self.define_label(label)
            self.instruction(ml, ops)

    def conditional(self, d, ops):
        if d == ".if":
            parent = not self.cond or self.cond[-1][0]
            val = parent and self.cond_value(ops)
            self.cond.append([val, val, parent])
        elif not self.cond:
            self.error(f"{d} without .if")
        elif d == ".elseif":
            top = self.cond[-1]
            if top[1] or not top[2]:
                top[0] = False
            else:
                top[0] = self.cond_value(ops)
                top[1] = top[0]
        elif d == ".else":
            top = self.cond[-1]
            top[0] = top[2] and not top[1]
            top[1] = True
        else:
            self.cond.pop()

    def cond_value(self, ops):
        text = self.plain_subst(self.eval_builtins(self.forced_subst(ops)))
        while text.count(")") > text.count("("):   # GSPA tolerated a stray ')'
            text = text[:text.rindex(")")] + text[text.rindex(")") + 1:]
        try:
            return self.eval_abs(text) != 0
        except Undefined as e:
            self.error(f".if uses undefined symbol {e.name}")
        except ExprError as e:
            self.error(f".if: {e}")
        return False

    def skip_block(self, lines, i, start, end):
        depth = 1
        while i < len(lines):
            _, m, _ = split_line(strip_comment(lines[i]))
            i += 1
            if m and m.lower() == start:
                depth += 1
            elif m and m.lower() == end:
                depth -= 1
                if depth == 0:
                    break
        return i

    def collect_block(self, lines, i, start, end):
        body = []
        depth = 1
        while i < len(lines):
            raw = lines[i]
            _, m, _ = split_line(strip_comment(raw))
            i += 1
            if m and m.lower() == start:
                depth += 1
            elif m and m.lower() == end:
                depth -= 1
                if depth == 0:
                    return body, i
            body.append(raw)
        self.error(f"missing {end}")
        return body, i

    def subst_directive(self, d, ops):
        parts = split_operands(ops)
        if d == ".var":
            frame = self.frames[-1] if self.frames else self.subst_global
            for p in parts:
                frame[p.strip()] = ""
            return
        if len(parts) < 2:
            self.error(f"{d} needs two operands")
            return
        value, name = parts[0], parts[-1].strip()
        value = self.plain_subst(self.eval_builtins(value))
        if d == ".asg":
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
            self.subst_assign(name, value)
        else:
            try:
                self.subst_assign(name, str(self.eval_abs(value)))
            except (Undefined, ExprError) as e:
                self.error(f".eval {value}: {e}")

    def include(self, ops):
        name = ops.strip().strip('"').strip()
        path = self.tree.find(name)
        if not path:
            self.mod.missing_includes.add(os.path.basename(name).upper())
            return
        saved_loc, saved_stop = self.loc, self.stop_file
        self.run_lines(self.tree.lines(path), os.path.basename(path))
        self.loc, self.stop_file = saved_loc, False
        _ = saved_stop

    def expand_macro(self, macro, ops):
        args = split_operands(ops) if ops.strip() else []
        if macro.params and len(args) > len(macro.params):
            # GSPA gives the last parameter the rest of the argument list.
            n = len(macro.params)
            args = args[:n - 1] + [",".join(args[n - 1:])]
        frame = {}
        for i, p in enumerate(macro.params):
            a = args[i].strip() if i < len(args) else ""
            if len(a) >= 2 and a[0] == a[-1] == '"':
                a = a[1:-1]
            frame[p] = a
        self.uid += 1
        uid = self.uid
        body = [re.sub(r"(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)\?(?![A-Za-z0-9_])",
                       lambda m: f"{m.group(1)}?{uid}", ln) for ln in macro.body]
        self.frames.append(frame)
        saved_loc = self.loc
        depth = len(self.cond)
        try:
            self.run_lines(body, f"{saved_loc.file}:{saved_loc.line}<{macro.name}>", 1)
        except MacroExit:
            del self.cond[depth:]
        finally:
            self.frames.pop()
            self.loc = saved_loc

    def instruction(self, mnem, ops):
        self.mod.mnemonics[mnem] += 1
        if mnem not in KNOWN:
            self.error(f"unknown instruction {mnem}")
        oplist = [self.localize_expr(o) if not is_reg(o) else o for o in split_operands(ops)]
        if mnem in JUMPS_REL or mnem.startswith("dsj"):
            # "label.S" (from the JRX*/JRY* macros) is a short-jump hint.
            oplist = [re.sub(r"\.[SsLl]$", "", o) for o in oplist]
        here_re = re.compile(r"(?<![A-Za-z0-9_$#?])\$(?![A-Za-z0-9_])")
        if any(here_re.search(o) for o in oplist):
            marker = Item("label", loc=self.loc)
            self.here_marker(marker)
            oplist = [here_re.sub(marker.name, o) for o in oplist]
        try:
            size = fixed_size(mnem, oplist, self.abs_lookup)
        except ExprError as e:
            self.error(f"{mnem} {ops}: {e}")
            size = 16
        item = Item("insn", size=size, mnem=mnem, ops=oplist, loc=self.loc)
        item.scope = self.scope
        self.emit(item)

    # -- local labels
    def resolve_locals(self):
        """Every '#x' reference must be defined in its own '#****' scope."""
        syms = self.mod.symbols
        for seq, raw, full in self.local_refs:
            if full not in syms and any(d in syms for _, d in self.local_defs.get(raw, ())):
                self.ambiguous_locals.append((full, [d for _, d in self.local_defs[raw]]))

    # -- layout
    def layout(self):
        """Assigns section offsets, relaxing relative jumps to short/long."""
        for sec in self.mod.sections.values():
            for it in sec.items:
                if it.kind == "insn" and it.mnem in JUMPS_REL:
                    it.size = 16
            for _ in range(20):
                self.assign_offsets(sec)
                changed = False
                for it in sec.items:
                    if it.kind == "insn" and it.mnem in JUMPS_REL and it.size == 16:
                        if not self.jr_short_ok(sec, it):
                            it.size = 32
                            changed = True
                if not changed:
                    break

    def assign_offsets(self, sec):
        off = 0
        for it in sec.items:
            if it.kind == "align":
                a = it.value
                off = (off + a - 1) // a * a
            it.offset = off
            if it.kind in ("data", "insn", "space"):
                off += it.size
        sec.size = off

    def jr_short_ok(self, sec, it):
        target = it.ops[0] if it.ops else ""
        sym = self.mod.symbols.get(target)
        if sym is None or sym.kind != "label" or sym.section != sec.name:
            return False
        disp = (sym.item.offset - (it.offset + 16)) // 16
        return -128 <= disp <= 127 and disp != 0


class MacroExit(Exception):
    pass


def assemble(tree, module):
    path = tree.find(module + ".ASM")
    if not path:
        raise AsmError(f"module {module}.ASM not found")
    a = Assembler(tree, module.upper())
    a.main_file = os.path.basename(path).upper()
    a.run_lines(tree.lines(path), os.path.basename(path))
    if a.cond:
        a.error("unterminated .if")
    a.resolve_locals()
    for full, defs in a.ambiguous_locals:
        a.mod.errors.append(f"{a.mod.name}: local {full} is not defined in its scope "
                            f"(defined as {', '.join(defs[:4])})")
    a.layout()
    return a.mod


# ---- linking ----------------------------------------------------------------------

LINK_ORDER_SECTIONS = [
    # (section, memory base) following WRESTLE.CMD
    ("VECTORS", 0xFFFFFC00),
    ("unzip", 0x01000000),
    ("FIXED", None),
    ("OFIXED", None),
    (".bss", None),
    (".text", 0xFF800000),
    (".data", None),
]


# The mods' ROM (Linker.place, src/wolf/wolf.c): the 1 MB below the original program ROM
# (0xFF800000), which the board does not decode.
EXT_ROM_BASE, EXT_ROM_END = 0xFF000000, 0xFF800000
EXT_ROM_SECTIONS = {".text", ".data"}


def set_extras(extras):
    SourceTree.extra_modules = list(extras["module"])
    SourceTree.extra_dirs = list(extras["srcdir"])
    SourceTree.edits = dict(extras["edits"])


def read_link_modules(tree, cmd_name="WRESTLE.CMD", extra=()):
    path = tree.find(cmd_name)
    mods = []
    text = "\n".join(tree.lines(path))
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for line in text.split("\n"):
        line = line.strip()
        if re.match(r"^[A-Za-z0-9_]+\.obj$", line, re.I):
            mods.append(line[:-4].upper())
    for name in extra:                      # modules a mod adds (extras.py), linked last
        if name.upper() not in mods:
            mods.append(name.upper())
    return mods


class Linker:
    def __init__(self, modules):
        self.modules = modules
        self.globals = {}
        self.errors = []
        self.case_folded = Counter()

    def place(self):
        prev_end = {}
        cursor = None
        # the code and data the mods add (extras.py) go to a ROM of their own below the original
        # one (EXT_ROM_BASE): the original's 1 MB has only some 30 KB left, and the original
        # modules keep their addresses
        extra = {m.upper() for m in SourceTree.extra_modules}
        ext_cursor = EXT_ROM_BASE
        for secname, base in LINK_ORDER_SECTIONS:
            if base is not None:
                cursor = base
            for mod in self.modules:
                sec = mod.sections.get(secname)
                if sec is None:
                    continue
                if mod.name.upper() in extra and secname in EXT_ROM_SECTIONS:
                    ext_cursor = (ext_cursor + 15) // 16 * 16
                    sec.base = ext_cursor
                    ext_cursor += sec.size
                    continue
                cursor = (cursor + 15) // 16 * 16
                sec.base = cursor
                cursor += sec.size
            prev_end[secname] = cursor
        if ext_cursor > EXT_ROM_END:
            self.errors.append(f"the mods' code and data need {(ext_cursor - EXT_ROM_BASE) // 8} bytes, "
                               f"more than the {(EXT_ROM_END - EXT_ROM_BASE) // 8} of their ROM")
        if prev_end.get(".data", 0) > 0xFFFFFC00:
            self.errors.append("the program runs into the trap vectors (ROM full)")
        for mod in self.modules:
            for sec in mod.sections.values():
                if sec.base is None:
                    self.errors.append(f"{mod.name}: section {sec.name} not placed by WRESTLE.CMD")
                    sec.base = 0

    def build_globals(self):
        self.folded = {}
        for mod in self.modules:
            for name in mod.globals:
                sym = mod.symbols.get(name)
                if sym is None:
                    continue
                if name in self.globals and self.globals[name][0] is not mod:
                    other = self.globals[name][0]
                    self.errors.append(f"{name} defined in both {other.name} and {mod.name}")
                    continue
                self.globals[name] = (mod, sym)
        for name, v in self.globals.items():
            self.folded.setdefault(name.upper(), []).append(name)

    def symbol_value(self, mod, name, depth=0):
        if depth > 50:
            raise ExprError(f"recursive definition of {name}")
        sym = mod.symbols.get(name)
        owner = mod
        if sym is None:
            g = self.globals.get(name)
            if g is None:
                cands = self.folded.get(name.upper(), [])
                if len(cands) == 1:
                    self.case_folded[name] += 1
                    g = self.globals[cands[0]]
            if g is None:
                raise Undefined(name)
            owner, sym = g
        if sym.kind == "abs":
            return sym.value
        if sym.kind == "label":
            sec = owner.sections[sym.section]
            return to_s32(sec.base + sym.item.offset)
        return to_s32(evaluate(sym.value, lambda n: self.symbol_value(owner, n, depth + 1)))

    def resolve_all(self):
        undefined = defaultdict(set)
        for mod in self.modules:
            for sec in mod.sections.values():
                for it in sec.items:
                    if it.kind == "data" and not isinstance(it.value, int):
                        try:
                            it.value = to_s32(evaluate(it.value, lambda n: self.symbol_value(mod, n)))
                        except Undefined as e:
                            undefined[e.name].add(mod.name)
                        except ExprError as e:
                            self.errors.append(f"{it.loc}: {e}")
                    elif it.kind == "insn":
                        for op in it.ops:
                            for name in operand_symbols(op):
                                try:
                                    self.symbol_value(mod, name)
                                except Undefined:
                                    undefined[name].add(mod.name)
                                except ExprError as e:
                                    self.errors.append(f"{it.loc}: {e}")
        return undefined


def operand_symbols(op):
    from gsp34010 import parse_operand
    o = parse_operand(op)
    text = o[2] if o[0] == "disp" else o[1] if o[0] in ("abs", "imm") else None
    if not text:
        return set()
    try:
        names = symbols_in(parse_expr(text))
    except ExprError:
        return set()
    from gsp34010 import OPERAND_KEYWORDS, REG_ALIASES
    return {n for n in names if n.lower() not in REGISTERS and not n.startswith("__here")
            and n.lower() not in REG_ALIASES and n.lower() not in OPERAND_KEYWORDS}


# ---- CLI ------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="orig")
    ap.add_argument("--gen", default=None, help="directory with generated include files")
    ap.add_argument("--module", action="append", help="assemble only these modules")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--extras", default=None, help="mods/*/gen.txt collected (extras.py)")
    ap.add_argument("--max-errors", type=int, default=40)
    args = ap.parse_args()

    from extras import load_extras
    extras = load_extras(args.extras)
    set_extras(extras)
    tree = SourceTree([args.gen, args.src])
    names = args.module or read_link_modules(tree, extra=extras["module"])
    modules = []
    for n in names:
        try:
            modules.append(assemble(tree, n))
        except AsmError as e:
            print(f"error: {e}", file=sys.stderr)

    linker = Linker(modules)
    linker.place()
    linker.build_globals()
    undefined = linker.resolve_all()

    errors = [e for m in modules for e in m.errors] + linker.errors
    missing = Counter()
    for m in modules:
        for f in m.missing_includes:
            missing[f] += 1
    insns = sum(1 for m in modules for s in m.sections.values() for it in s.items if it.kind == "insn")
    data = sum(1 for m in modules for s in m.sections.values() for it in s.items if it.kind == "data")

    print(f"modules:            {len(modules)}")
    print(f"instructions:       {insns}")
    print(f"data items:         {data}")
    print(f"global symbols:     {len(linker.globals)}")
    print(f"errors:             {len(errors)}")
    print(f"missing includes:   {len(missing)} ({', '.join(sorted(missing))})")
    print(f"undefined symbols:  {len(undefined)}")
    print(f"case-folded refs:   {len(linker.case_folded)}")
    for m in modules:
        for s in m.sections.values():
            pass
    if args.report:
        mn = Counter()
        for m in modules:
            mn.update(m.mnemonics)
        print("\nerrors:")
        for e in errors[: args.max_errors]:
            print("  " + e)
        print("\nundefined symbols (first 60):")
        for name in sorted(undefined)[:60]:
            print(f"  {name:24s} {', '.join(sorted(undefined[name]))[:80]}")
        print("\ncase-folded (first 20):", ", ".join(sorted(linker.case_folded)[:20]))
        print("\nmnemonics:", ", ".join(f"{k}:{v}" for k, v in mn.most_common()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
