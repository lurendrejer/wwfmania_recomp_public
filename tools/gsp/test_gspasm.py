#!/usr/bin/env python3
"""Tests for the GSPA front-end. Run: python3 tools/gsp/test_gspasm.py [orig_dir]"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gspasm  # noqa: E402
from gspexpr import evaluate, parse_expr  # noqa: E402

ORIG = None


def assemble_text(text, name="T", extra=None):
    d = tempfile.mkdtemp()
    with open(os.path.join(d, name + ".ASM"), "w") as f:
        f.write(text)
    for fname, body in (extra or {}).items():
        with open(os.path.join(d, fname), "w") as f:
            f.write(body)
    mod = gspasm.assemble(gspasm.SourceTree([d]), name)
    return mod


def label_offset(mod, name):
    sym = mod.symbols[name]
    return sym.item.offset


def const(mod, name):
    return mod.symbols[name].value


class ExprTests(unittest.TestCase):
    def ev(self, text):
        return evaluate(parse_expr(text), lambda n: {"X": 5}[n])

    def test_numbers(self):
        self.assertEqual(self.ev("0c0h"), 0xC0)
        self.assertEqual(self.ev(">1f"), 0x1F)
        self.assertEqual(self.ev("101b"), 5)
        self.assertEqual(self.ev("'A'"), 65)

    def test_operators(self):
        self.assertEqual(self.ev("2+3*4"), 14)
        self.assertEqual(self.ev("1<<4|1"), 17)
        self.assertEqual(self.ev("X>3"), 1)
        self.assertEqual(self.ev("-X/2"), -2)

    def test_xy(self):
        self.assertEqual(self.ev("[2,-1]") & 0xFFFFFFFF, 0x0002FFFF)


class AssemblerTests(unittest.TestCase):
    def test_macro_substitution_and_unique_labels(self):
        mod = assemble_text(
            "WL\t.macro\tw,l\n\t.word\t:w:\n\t.long\t:l:\n\t.endm\n"
            "LOOPM\t.macro\tR\nL?\tdec\t:R:\n\tjrnz\tL?\n\t.endm\n"
            "start\n\tWL\t3,start\n\tLOOPM\ta0\n\tLOOPM\ta1\n")
        self.assertEqual(mod.errors, [])
        data = [it for it in mod.sections[".text"].items if it.kind == "data"]
        self.assertEqual([d.size for d in data], [16, 32])
        self.assertEqual(sum(1 for n in mod.symbols if n.startswith("L?")), 2)

    def test_conditionals_and_builtins(self):
        mod = assemble_text(
            "M\t.macro\tREG\n\t.if $isreg(REG)\nA_:REG:\t.equ 1\n\t.else\nNOTREG\t.equ 1\n\t.endif\n\t.endm\n"
            "\tM\ta5\n\tM\tfoo\n"
            "S\t.set\t0\n\t.if S = 0\nY\t.equ 7\n\t.elseif 1\nY\t.equ 8\n\t.endif\n")
        self.assertEqual(mod.errors, [])
        self.assertIn("A_a5", mod.symbols)
        self.assertIn("NOTREG", mod.symbols)
        self.assertEqual(const(mod, "Y"), 7)

    def test_asg_eval_loop(self):
        mod = assemble_text(
            "\t.asg\t0,N\n\t.loop\t4\n\t.eval\tN+2,N\n\t.endloop\nRES\t.equ\tN\n"
            "CH\t.macro\tstr\n\t.var\tc\n\t.asg\t:str(2):,c\nC2\t.equ\t':c:'\nLEN\t.equ\t$symlen(str)\n\t.endm\n"
            "\tCH\t\"ABC\"\n")
        self.assertEqual(mod.errors, [])
        self.assertEqual(const(mod, "RES"), 8)
        self.assertEqual(const(mod, "C2"), ord("B"))
        self.assertEqual(const(mod, "LEN"), 3)

    def test_struct_macros(self):
        mod = assemble_text(
            "STRUCT\t.macro\to\n\t.asg\t:o:,SOFF\n\t.endm\n"
            "WORD\t.macro\tn\n:n:\t.set\tSOFF\n\t.eval\tSOFF+16,SOFF\n\t.endm\n"
            "\tSTRUCT\t100h\n\tWORD\tFA\n\tWORD\tFB\n")
        self.assertEqual((const(mod, "FA"), const(mod, "FB")), (0x100, 0x110))

    def test_preasm_local_scopes(self):
        # Only "#****" header lines start a scope; labels inside a function
        # (DEL_IT in BAKGND's bgnd_delnonvis) do not.
        mod = assemble_text(
            "#*****\n"
            "#K\t.equ\t5\n"
            "f1\n#lp\tdec\ta0\n\tjrz\t#x\nMID\n\tjrnz\t#lp\n\tmovi\t#K,a1\n#x\trets\n"
            "#*****\n"
            "#lp\tdec\ta1\n\tjrnz\t#lp\n#x\trets\n")
        self.assertEqual(mod.errors, [])
        ref = [it for it in mod.sections[".text"].items if it.kind == "insn" and it.mnem == "jrz"][0]
        x1 = [n for n in mod.symbols if n.endswith("#x")]
        self.assertEqual(len(x1), 2)
        self.assertIn(ref.ops[0], mod.symbols)
        self.assertEqual(ref.ops[0].split("#")[0], [n for n in x1 if "#x" in n][0].split("#")[0])
        mod2 = assemble_text("#***\nf1\n\tjruc\t#y\n#***\n#y\trets\n")
        self.assertTrue(any("not defined in its scope" in e for e in mod2.errors))

    def test_instruction_sizes_match_jsrp(self):
        # JSRP relies on ADDI 60h (32) + MOVE (16) + JAUC (48) = 96 bits.
        mod = assemble_text(
            "t\tgetpc\ta7\n\taddi\t060h,a7\n\tmove\ta7,-*a12,L\n\tjauc\tt\nret\n")
        items = [it for it in mod.sections[".text"].items if it.kind == "insn"]
        self.assertEqual([it.size for it in items], [16, 32, 16, 48])
        self.assertEqual(label_offset(mod, "ret") - (items[0].offset + 16), 0x60)

    def test_jump_relaxation(self):
        body = "".join("\tmovi\t12345678h,a0\n" for _ in range(60))
        mod = assemble_text("a\tjrz\tb\n\tjrz\ta\n" + body + "b\trets\n")
        items = [it for it in mod.sections[".text"].items if it.kind == "insn"]
        self.assertEqual(items[0].size, 32)   # too far for 8-bit displacement
        self.assertEqual(items[1].size, 16)

    def test_bss_and_sections(self):
        mod = assemble_text("\t.bss\tv1,32\n\t.bss\tv2,16,1\n\t.data\nd\t.long\tv2\n")
        self.assertEqual(label_offset(mod, "v2"), 32)
        self.assertEqual(mod.symbols["d"].section, ".data")


class OriginalSourceTests(unittest.TestCase):
    def test_all_modules_assemble(self):
        if not ORIG:
            self.skipTest("orig not available")
        tree = gspasm.SourceTree([ORIG])
        names = gspasm.read_link_modules(tree)
        self.assertEqual(len(names), 99)
        mods = [gspasm.assemble(tree, n) for n in names]
        errors = [e for m in mods for e in m.errors]
        self.assertEqual(errors, [])
        linker = gspasm.Linker(mods)
        linker.place()
        linker.build_globals()
        self.assertEqual(linker.errors, [])


class GeneratedTablesTests(unittest.TestCase):
    """Regenerated loadw/wwfld output: the whole game must link cleanly."""

    @classmethod
    def setUpClass(cls):
        if not ORIG:
            raise unittest.SkipTest("orig not available")
        import genimg
        cls.gen = tempfile.mkdtemp()
        cls.g = genimg.Generator(ORIG, cls.gen)
        cls.g.run()

    def test_generator_clean(self):
        self.assertEqual(self.g.warnings, [])
        self.assertEqual(len(self.g.labels), 8109)

    def test_full_link(self):
        tree = gspasm.SourceTree([self.gen, ORIG])
        mods = [gspasm.assemble(tree, n) for n in gspasm.read_link_modules(tree)]
        self.assertEqual([e for m in mods for e in m.errors], [])
        self.assertEqual([f for m in mods for f in m.missing_includes], [])
        linker = gspasm.Linker(mods)
        linker.place()
        linker.build_globals()
        undefined = linker.resolve_all()
        self.assertEqual(linker.errors, [])
        self.assertEqual(sorted(undefined), [])

    def test_palettes_match_original_imgpal(self):
        import re

        def pals(path):
            out, cur = {}, None
            for line in open(path, "rb").read().decode("latin-1").replace("\r", "").split("\n"):
                m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", line)
                if m:
                    cur = m.group(1)
                    out[cur] = []
                    continue
                m = re.match(r"^\s*\.word\s+(.*)", line)
                if m and cur:
                    for v in m.group(1).split(","):
                        v = v.strip()
                        out[cur].append(int(v[:-1], 16) if v[-1] in "hH" else int(v))
            return out
        orig = pals(os.path.join(ORIG, "IMGPAL.ASM"))
        gen = pals(os.path.join(self.gen, "IMGPAL.ASM"))
        self.assertEqual(len(orig), 337)
        for name, colors in orig.items():
            self.assertEqual(gen.get(name), colors, name)

    def test_sequences_match_wwfld_headers(self):
        import re
        for w in sorted(__import__("genimg").WRESTLER_LODS):
            h = open(os.path.join(ORIG, f"{w}IMG.H"), "rb").read().decode("latin-1")
            expected = set(re.findall(r"\.global\s+(\S+)", h))
            seq = open(os.path.join(self.gen, f"{w}.SEQ")).read()
            got = set(re.findall(r"^([A-Za-z0-9_]+):", seq, re.M))
            self.assertEqual(got, expected, w)


if __name__ == "__main__":
    if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]):
        ORIG = sys.argv.pop(1)
    unittest.main(verbosity=1)
