#!/usr/bin/env python3
"""Regenerates the image tables the original image builders produced.

The source release lacks the output of `loadw` and `wwfld`: *.TBL (image
headers), *.GLO (globals), <wrestler>.SEQ (animation sequence tables) and
a complete IMGPAL.ASM. This writes them, as assembler source, from the IMG
libraries and the LOD scripts, so gspasm assembles the game exactly as it
was built. See docs/IMAGE_TABLES.md for the formats and how they were
derived.

    python3 tools/gsp/genimg.py --src orig --out build/gen
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from extras import load_extras  # noqa: E402
from imglib import ImgLib, read_lod  # noqa: E402

BUILD_LODS = ["MAIN", "BAM", "BRET", "DOINK", "LEX", "RAZOR", "SHAWN", "TAKER", "YOKO", "MISC"]
WRESTLER_LODS = {"BAM", "BRET", "DOINK", "LEX", "RAZOR", "SHAWN", "TAKER", "YOKO"}

# Synthetic image ROM: every image gets a unique source address (SAG). The
# port's DMA hook maps SAG back to (library, image, pixel offset).
IMGROM_BASE = 0x10000000
IMGROM_ALIGN = 256

_SYMBOL = re.compile(r"^[A-Za-z_][A-Za-z0-9_$]*$")


def s8(b):
    return b - 256 if b > 127 else b


def w16(v):
    return v & 0xFFFF


class Generator:
    def __init__(self, src, out, extras=None):
        ex = extras or {"lod": [], "wrestler-lod": []}
        self.wrestler_lods = set(WRESTLER_LODS) | set(ex["wrestler-lod"])
        self.build_lods = list(BUILD_LODS) + [n for n in ex["lod"] + ex["wrestler-lod"] if n not in BUILD_LODS]
        self.src = src
        self.mod_dirs = list(ex.get("srcdir", []))
        self.scripts = list(ex.get("script", []))
        self.mod_lods = {}         # LOD name -> path, for LODs a mod carries itself
        self.imgdir = os.path.join(src, "IMG")
        self.out = out
        self.libs = {}
        self.wrestler_frames = {}          # library file -> [(label, image)] loaded by a wrestler LOD
        self.files = {f.upper(): f for f in os.listdir(self.imgdir)}
        self.sag = {}              # (file, index) -> address
        self.next_sag = IMGROM_BASE
        self.labels = {}           # label -> (lib, image)
        self.tbl = {}              # tbl name -> list of text lines
        self.glo = {}              # glo name -> set of labels
        self.palettes_used = {}    # palette name -> Palette
        self.bg_images = {}        # (bdd file, ordinal) -> (w, h)
        self.warnings = []

    def lib(self, name):
        key = os.path.basename(name).upper()
        if key not in self.libs:
            fn = self.files.get(key)
            path = os.path.join(self.imgdir, fn) if fn else None
            if not fn:                     # a library a mod's script made (extras.py 'script')
                lod_dir = os.path.join(self.out, "lod")
                own = {f.upper(): f for f in os.listdir(lod_dir)} if os.path.isdir(lod_dir) else {}
                if key in own:
                    path = os.path.join(lod_dir, own[key])
            self.libs[key] = ImgLib(path) if path else None
            if not path:
                self.warnings.append(f"missing IMG library {key}")
        return self.libs[key]

    def run_scripts(self):
        """The mods' generators (extras.py 'script'), before anything reads their output."""
        import subprocess
        for sc in self.scripts:
            r = subprocess.run([sys.executable, sc, "--src", self.src, "--out", self.out])
            if r.returncode != 0:
                raise SystemExit(f"{sc} failed ({r.returncode})")

    def alloc_sag(self, lib, im):
        key = (lib.file, im.index)
        if key not in self.sag:
            self.sag[key] = self.next_sag
            size = im.w * im.h * 8
            self.next_sag += (size + IMGROM_ALIGN - 1) // IMGROM_ALIGN * IMGROM_ALIGN
        return self.sag[key]

    # -- headers
    def standard_header(self, label, lib, im, fields):
        out = [f"{label}:"]
        for f in fields:
            if f == "SIZX":
                out.append(f"\t.word\t{im.w}")
            elif f == "SIZY":
                out.append(f"\t.word\t{im.h}")
            elif f == "ANIX":
                out.append(f"\t.word\t{w16(im.anix)}")
            elif f == "ANIY":
                out.append(f"\t.word\t{w16(im.aniy)}")
            elif f == "SAG":
                out.append(f"\t.long\t0{self.alloc_sag(lib, im):X}H")
            elif f == "CTRL":
                out.append("\t.word\t0")          # 8 bits/pixel, no compression
            elif f == "PAL":
                if im.palette is not None:
                    self.palettes_used.setdefault(im.palette.name, im.palette)
                    out.append(f"\t.long\t{im.palette.name}")
                else:
                    out.append("\t.long\t0")
            else:                                 # PWRD1..3, PT3Y: unused by the game
                out.append("\t.word\t0")
        return out

    def frame_header(self, label, lib, im):
        """Wrestler frame: IPCOUNT word, then the 15-word head (ICBZ = 0f0h).

        One piece covering the whole image. The original split frames into
        up to five rectangles to save ROM; the game only draws the pieces and
        takes their bounding box, which is the (trimmed) image either way.
        """
        t = lib.pttbl(im)
        box = (0, 0, 0, 0)
        if t is not None and any(t[36:40]):
            bx, by, bw, bh = s8(t[36]), s8(t[37]), t[38], t[39]
            box = (bx - im.anix, by - im.aniy, bw, bh)
        a2x, a2y, a2z = im.ani2
        return [
            "\t.word\t1",                                          # IPCOUNT
            f"{label}:",
            f"\t.word\t{im.w},{im.h},{w16(im.anix)},{w16(im.aniy)}",
            f"\t.long\t0{self.alloc_sag(lib, im):X}H",
            "\t.word\t0",                                          # CTRL
            f"\t.word\t{w16(a2x)},{w16(a2y)},{w16(a2z)},0",        # IANI2X/Y/Z, IFLAGS
            f"\t.word\t{w16(box[0])},{w16(box[1])},{box[2]},{box[3]}",  # IANI3X/Y/Z/ID
        ]

    # -- LOD processing
    def lod_path(self, lodname):
        """A mod's own NAME.LOD (in one of its srcdirs) wins over IMG/NAME.LOD."""
        for d in self.mod_dirs:
            if os.path.isdir(d):
                for f in os.listdir(d):
                    if f.upper() == lodname + ".LOD":
                        self.mod_lods[lodname] = os.path.join(d, f)
                        return self.mod_lods[lodname]
        return os.path.join(self.imgdir, self.files[lodname + ".LOD"])

    def run_lod(self, lodname):
        path = self.lod_path(lodname)
        wrestler = lodname in self.wrestler_lods
        for e in read_lod(path):
            if e.kind != "img":
                continue
            lib = self.lib(e.file)
            if lib is None:
                continue
            if e.names:
                picks = []
                for n in e.names:
                    im = lib.find(n)
                    if im is None:
                        self.warnings.append(f"{lodname}.LOD:{e.line}: {n} not in {lib.file}")
                    else:
                        picks.append((n, im))
            else:
                picks = [(im.name, im) for im in lib.images]
            if not e.asm:
                continue
            tbl = self.tbl.setdefault(e.asm.upper(), [])
            glo = self.glo.setdefault(e.glo.upper(), set()) if e.glo else None
            for label, im in picks:
                if not _SYMBOL.match(label):
                    continue
                if label in self.labels:
                    continue                       # loadw emitted each label once
                self.labels[label] = (lib, im)
                if wrestler:
                    self.wrestler_frames.setdefault(lib.file, []).append((label, im))
                if wrestler or "PWRD1" in e.ihdr:
                    tbl += self.frame_header(label, lib, im)
                else:
                    tbl += self.standard_header(label, lib, im, e.ihdr)
                if glo is not None:
                    glo.add(label)
                    if im.palette is not None and "PAL" in e.ihdr:
                        glo.add(im.palette.name)

    def sequences(self, lodname):
        bat = open(os.path.join(self.imgdir, self.files[lodname + ".BAT"]), "rb").read().decode("latin-1")
        files = re.search(r"wwfld\s+\S+\s+(.*)", bat).group(1).split()
        seqs = {}
        for f in files:
            lib = self.lib(f if f.lower().endswith(".img") else f + ".img")
            if lib is None:
                continue
            for im in lib.images:
                m = re.match(r"^([A-Za-z0-9_]+?)(\d\d)$", im.name)
                if m:
                    seqs.setdefault(m.group(1), {})[int(m.group(2))] = im.name
        out = []
        for name in sorted(seqs):
            frames = seqs[name]
            n = max(frames)
            out.append(f"\t.global\t{name}")
            out.append(f"{name}:")
            out.append(f"\t.long\t{n}")                 # FR0 slot: frame count
            for k in range(1, n + 1):
                label = frames.get(k)
                out.append(f"\t.long\t{label}" if label in self.labels else "\t.long\t0")
        return out

    def palette_asm(self, defined_elsewhere):
        out = ['\t.FILE "imgpal.asm"', "\t.OPTION B,D,L,T", "", "\t.include imgtbl.glo",
               "\t.DATA", "\t.even", ""]
        emitted = set()
        # every palette of every library loaded, like loadw's IMGPAL.ASM
        pals = dict(self.palettes_used)
        for lib in self.libs.values():
            if lib is None:
                continue
            for p in lib.palettes:
                pals.setdefault(p.name, p)
        for name, p in pals.items():
            if name in defined_elsewhere or name in emitted or not _SYMBOL.match(name):
                continue
            emitted.add(name)
            out.append(f"{name}:")
            out.append(f"\t.word\t {len(p.colors)}")
            for i in range(0, len(p.colors), 8):
                out.append("\t.word\t" + ",".join(f"0{c:X}H" for c in p.colors[i:i + 8]))
            out.append("")
        return out, emitted

    def symbols_defined_elsewhere(self):
        """Symbols the hand-written modules define (palettes in WRESPAL, SPECIAL...)."""
        import gspasm
        tree = gspasm.SourceTree([self.src])
        names = set()
        for mod in gspasm.read_link_modules(tree):
            if mod == "IMGPAL":
                continue
            names |= set(gspasm.assemble(tree, mod).symbols)
        return names

    # -- backgrounds
    def bdd_dims(self, fname):
        path = os.path.join(self.imgdir, fname)
        d = open(path, "rb").read()
        pos = d.index(b"\n") + 1
        n = int(d[:pos].split()[0])
        dims = []
        for _ in range(n):
            e = d.index(b"\n", pos)
            parts = d[pos:e].split()
            w, h = int(parts[1]), int(parts[2])
            dims.append((w, h))
            pos = e + 1 + w * h
        return dims

    def backgrounds(self):
        """Rewrites the original BGNDTBL.ASM (loadw output) so its image
        headers point at synthetic image ROM addresses whose pixels come from
        the matching .BDD file; everything else (blocks, modules) is kept."""
        src = os.path.join(self.src, "BGNDTBL.ASM")
        lines = open(src, "rb").read().decode("latin-1").replace("\r", "").split("\n")
        # header dims per HDRS group
        groups, cur = {}, None
        for ln in lines:
            m = re.match(r"^([A-Za-z0-9_]+):", ln)
            if m:
                cur = m.group(1) if m.group(1).endswith("HDRS") else None
                if cur:
                    groups[cur] = []
                continue
            m = re.match(r"^\s*\.word\s+(\d+)\s*,\s*(\d+)", ln)
            if cur and m:
                groups[cur].append((int(m.group(1)), int(m.group(2))))
        bdds = {f.upper(): self.bdd_dims(f) for f in self.files.values() if f.upper().endswith(".BDD")}
        match = {}
        for g, dims in groups.items():
            cands = sorted(b for b, d in bdds.items() if d == dims)
            if not cands:
                self.warnings.append(f"background group {g}: no .BDD with matching images")
                continue
            match[g] = cands[0]
        out, cur, ordinal, want_ctrl = [], None, 0, False
        for ln in lines:
            m = re.match(r"^([A-Za-z0-9_]+):", ln)
            if m:
                cur = m.group(1) if m.group(1) in match else None
                ordinal = 0
                out.append(ln)
                continue
            if cur and re.match(r"^\s*\.long\s", ln):
                bdd = match[cur]
                w, h = bdds[bdd][ordinal]
                key = (bdd, ordinal)
                if key not in self.sag:
                    self.sag[key] = self.next_sag
                    self.bg_images[key] = (w, h)
                    self.next_sag += (w * h * 8 + IMGROM_ALIGN - 1) // IMGROM_ALIGN * IMGROM_ALIGN
                out.append(f"\t.long\t0{self.sag[key]:X}H\t;{bdd} #{ordinal}")
                ordinal += 1
                want_ctrl = True
                continue
            m = re.match(r"^(\s*\.word\s+)([0-9A-Fa-f]+[hH])(.*)$", ln)
            if cur and want_ctrl and m:
                ctrl = int(m.group(2)[:-1], 16) & 0x807F    # 8 bits/pixel, no compression
                out.append(f"{m.group(1)}0{ctrl:X}H{m.group(3)}")
                want_ctrl = False
                continue
            out.append(ln)
        self.write("BGNDTBL.ASM", out)
        return match

    def bdd_palettes(self, imgpal_text):
        """bddpal.txt: the palette each background image (.BDD) is drawn with, for true-colour art
        overrides (docs/ASSET_OVERRIDES.md). A block of a background module (BGNDTBL.ASM ...BLKS) is
        flags, x, y, word: the image is the word's low 12 bits (an index into the module's HDRS), the
        palette (an index into its PALS table, BGNDPAL.ASM) is the flags' low 4 bits plus the word's
        bits 12-13 as bits 4-5 (checked against the .BDB files). Every image is drawn with one palette."""
        def num(t):
            t = t.strip()
            return int(t[:-1], 16) if t.upper().endswith("H") else int(t, 0)

        def labels_of(text):
            out, cur = {}, None
            for raw in text.replace("\r", "").split("\n"):
                ln = raw.split(";")[0].rstrip()
                m = re.match(r"^([A-Za-z0-9_]+):", ln)
                if m:
                    cur = m.group(1)
                    out[cur] = []
                    ln = ln[m.end():]
                m = re.match(r"^\s*\.(word|long)\s+(.*)$", ln, re.I)
                if m and cur:
                    out[cur] += [(m.group(1).lower(), t.strip()) for t in m.group(2).split(",") if t.strip()]
            return out

        pal_src = os.path.join(self.src, "BGNDPAL.ASM")
        tbl_src = os.path.join(self.src, "BGNDTBL.ASM")
        if not os.path.exists(pal_src) or not os.path.exists(tbl_src):
            return
        pals = labels_of(imgpal_text)
        pals.update(labels_of(open(pal_src, "rb").read().decode("latin-1")))
        tbl = labels_of(open(tbl_src, "rb").read().decode("latin-1"))
        chosen = {}
        for name, items in tbl.items():
            if not name.endswith("BMOD"):
                continue
            longs = [t for k, t in items if k == "long"]
            if len(longs) < 3 or longs[1] not in self.bg_match:
                continue
            blks, hdrs, ptab = longs[:3]
            bdd = self.bg_match[hdrs]
            table = [t for k, t in pals.get(ptab, []) if k == "long"]
            words = [num(t) for k, t in tbl.get(blks, []) if k == "word"]
            for i in range(0, len(words) - 3, 4):
                flags, w4 = words[i], words[i + 3]
                idx, pal = w4 & 0x0FFF, (flags & 0xF) | ((w4 >> 12) & 3) << 4
                if pal < len(table) and (bdd, idx) not in chosen:
                    chosen[(bdd, idx)] = table[pal]
        with open(os.path.join(self.out, "bddpal.txt"), "w") as f:
            f.write("# background image palettes: file index ncolors colours (xRRRRRGGGGGBBBBB, hex)\n")
            for (bdd, idx), pname in sorted(chosen.items()):
                cols = [num(t) for k, t in pals.get(pname, []) if k == "word"]
                if not cols:
                    self.warnings.append(f"bddpal: palette {pname} not found")
                    continue
                n = min(cols[0] & 0xFF if cols[0] & 0xFF else len(cols) - 1, len(cols) - 1)
                f.write(f"{bdd} {idx} {n} " + " ".join(f"{c & 0x7FFF:04X}" for c in cols[1:1 + n]) + "\n")

    def draw_palettes(self):
        """drawpal.txt: wrestler frames drawn with another palette than their own, for true-colour art
        overrides (docs/ASSET_OVERRIDES.md). The game draws a wrestler's frames with his palette (OBJ_PAL,
        set_images in ANIM.ASM), whatever palette each has in its library. Nearly all of a library's
        frames have it; the one or two that do not are marked with another of his palettes (player 2's
        colours, Bam Bam's fire trail, the Undertaker's negative ...), a recolouring with the same index
        layout, which the game takes from WRESPAL.ASM (docs/IMG_FORMAT.md, open questions). In a library loaded by a wrestler LOD where at least 90% of the frames share a palette,
        the others are listed with that palette. Libraries of mixed art (HITSTUFF: blood, sweat ...) are
        left alone."""
        lines = ["# label palette: wrestler frames drawn with the given palette, not their own"]
        for file in sorted(self.wrestler_frames):
            frames = [(n, im) for n, im in self.wrestler_frames[file] if im.palette is not None]
            if len(frames) < 20:
                continue
            count = {}
            for _, im in frames:
                count[im.palette.name] = count.get(im.palette.name, 0) + 1
            main = max(count, key=lambda k: count[k])
            if count[main] * 10 < len(frames) * 9:
                continue
            mainpal = next(im.palette for _, im in frames if im.palette.name == main)
            for n, im in frames:
                if im.palette.name != main and len(im.palette.colors) == len(mainpal.colors):
                    lines.append(f"{n} {main}")
        with open(os.path.join(self.out, "drawpal.txt"), "w") as f:
            f.write("\n".join(lines) + "\n")

    def write(self, name, lines):
        with open(os.path.join(self.out, name), "w") as f:
            f.write("; generated by tools/gsp/genimg.py - do not edit\n")
            f.write("\n".join(lines) + "\n")

    def run(self):
        os.makedirs(self.out, exist_ok=True)
        self.run_scripts()
        for lod in self.build_lods:
            self.run_lod(lod)
        seq_files = {lod: self.sequences(lod) for lod in sorted(self.wrestler_lods)}

        defined = self.symbols_defined_elsewhere()
        pal_lines, pal_names = self.palette_asm(defined)

        for name, lines in self.tbl.items():
            self.write(name, ["\t.even"] + lines)
        all_globals = set(self.labels) | pal_names
        for name, labels in self.glo.items():
            self.write(name, [f"\t.globl\t{x}" for x in sorted(labels)])
        self.write("IMGTBL.GLO", [f"\t.globl\t{x}" for x in sorted(all_globals)])
        for lod, lines in seq_files.items():
            self.write(f"{lod}.SEQ", lines)
        self.write("IMGPAL.ASM", pal_lines)
        # LODs the mods add, for the runtime catalog (src/assets/catalog.c)
        with open(os.path.join(self.out, "extra_lods.txt"), "w") as f:
            f.write("\n".join(("lod/" if n in self.mod_lods else "") + n + ".LOD"
                              for n in self.build_lods if n not in BUILD_LODS) + "\n")
        if self.mod_lods:
            os.makedirs(os.path.join(self.out, "lod"), exist_ok=True)
            for n, path in self.mod_lods.items():
                with open(path, "rb") as fin, open(os.path.join(self.out, "lod", n + ".LOD"), "wb") as fout:
                    fout.write(fin.read())

        # BGNDTBL.GLO: globals of the (pre-built) background tables.
        names = set()
        for mod in ("BGNDTBL.ASM", "BGNDPAL.ASM"):
            bt = os.path.join(self.src, mod)
            if os.path.exists(bt):
                text = open(bt, "rb").read().decode("latin-1")
                names |= set(re.findall(r"^([A-Za-z_][A-Za-z0-9_]*):", text, re.M))
        self.write("BGNDTBL.GLO", [f"\t.globl\t{x}" for x in sorted(names)])

        self.bg_match = self.backgrounds()
        self.bdd_palettes("\n".join(pal_lines))
        self.draw_palettes()

        # Image ROM map for the runtime: SAG base, size, library, image index.
        with open(os.path.join(self.out, "imgrom.txt"), "w") as f:
            f.write("# sag width height library index name\n")
            for (file, index), base in sorted(self.sag.items(), key=lambda kv: kv[1]):
                if file.endswith(".BDD"):
                    w, h = self.bg_images[(file, index)]
                    f.write(f"{base:08X} {w} {h} {file} {index} {file[:-4]}_{index}\n")
                    continue
                im = self.libs[file].images[index]
                f.write(f"{base:08X} {im.w} {im.h} {file} {index} {im.name}\n")
        return all_globals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="orig")
    ap.add_argument("--out", default="build/gen")
    ap.add_argument("--extras", default=None, help="mods/*/gen.txt collected (tools/gsp/extras.py)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    g = Generator(args.src, args.out, load_extras(args.extras))
    g.run()
    print(f"image labels: {len(g.labels)}, images in ROM map: {len(g.sag)}, "
          f"tables: {len(g.tbl)}, warnings: {len(g.warnings)}")
    if args.verbose:
        for w in g.warnings:
            print("warning:", w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
