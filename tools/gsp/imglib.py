"""Python reader for Midway IMG libraries and LOD scripts (see docs/IMG_FORMAT.md).

Mirrors src/assets/img.c and src/assets/lod.c for the build-time generators.
"""

import os
import re
import struct


class Image:
    __slots__ = ("name", "index", "flags", "anix", "aniy", "w", "h", "palette",
                 "pix_off", "ani2", "frame", "pttbl", "opals")


class Palette:
    __slots__ = ("name", "flags", "bitspix", "colors")


class ImgLib:
    def __init__(self, path):
        self.path = path
        self.file = os.path.basename(path).upper()
        with open(path, "rb") as f:
            d = f.read()
        self.data = d
        self.images, self.palettes, self.pttbls = [], [], []
        if len(d) < 28:
            return
        ic, pc, oset, ver = struct.unpack_from("<HHIH", d, 0)
        rs = 42 if ver == 0 else 50
        npal = max(pc - 3, 0)
        for j in range(npal):
            o = oset + ic * rs + j * 26
            p = Palette()
            p.name = d[o:o + 10].split(b"\0")[0].decode("latin-1")
            p.flags, p.bitspix, n, coff = struct.unpack_from("<BBHI", d, o + 10)
            p.colors = list(struct.unpack_from(f"<{n}H", d, coff))
            self.palettes.append(p)
        maxpt = -1
        for i in range(ic):
            o = oset + i * rs
            im = Image()
            im.index = i
            im.name = d[o:o + 16].split(b"\0")[0].decode("latin-1")
            if ver == 0:
                im.flags = 0
                im.anix, im.aniy, im.w, im.h, pal, im.pix_off = struct.unpack_from("<hhHHHI", d, o + 16)
                im.ani2, im.frame, im.pttbl, im.opals = (-1, -1, -1), 0xFFFF, 0xFFFF, 0xFFFF
            else:
                im.flags, im.anix, im.aniy, im.w, im.h, pal, im.pix_off = \
                    struct.unpack_from("<HhhHHHI", d, o + 16)
                a2x, a2y, a2z, im.frame, im.pttbl, im.opals = struct.unpack_from("<hhhHHH", d, o + 38)
                im.ani2 = (a2x, a2y, a2z)
            im.palette = self.palettes[pal - 3] if 3 <= pal < 3 + npal else None
            if im.pttbl != 0xFFFF:
                maxpt = max(maxpt, im.pttbl)
            self.images.append(im)
        # Sequence and script chunks (editor data) follow the palettes: a
        # 16-byte name, a word, a count n at +18, 98 + 18 * n bytes in all.
        # The point tables come after them and run to the end of the file.
        pto = oset + ic * rs + npal * 26
        if ver != 0:
            seqc, scrc = struct.unpack_from("<HH", d, 10)
            for _ in range(seqc + scrc):
                pto += 98 + 18 * struct.unpack_from("<H", d, pto + 18)[0]
        for k in range(maxpt + 1):
            self.pttbls.append(d[pto + k * 40:pto + k * 40 + 40])
        self.by_name = {}
        for im in self.images:
            self.by_name.setdefault(im.name, im)
        self.by_upper = {}
        for im in self.images:
            self.by_upper.setdefault(im.name.upper(), im)

    def find(self, name):
        return self.by_name.get(name) or self.by_upper.get(name.upper())

    def pttbl(self, im):
        if im.pttbl == 0xFFFF or im.pttbl >= len(self.pttbls):
            return None
        return self.pttbls[im.pttbl]


class LodEntry:
    def __init__(self, kind, file, line):
        self.kind = kind          # "img", "bbb", "frm"
        self.file = file
        self.names = []           # as written in the LOD, empty = whole library
        self.line = line
        self.ihdr = []
        self.asm = None
        self.glo = None
        self.toggles = set()


def read_lod(path):
    entries = []
    ihdr, asm, glo, toggles = [], None, None, set()
    last = None
    with open(path, "rb") as f:
        lines = f.read().decode("latin-1").replace("\r", "").split("\n")
    for n, raw in enumerate(lines, 1):
        s = raw.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("--->"):
            if last is not None:
                last.names += [x.strip() for x in s[4:].split(",") if x.strip()]
            continue
        m = re.match(r"^(\S\S\S)>\s*(.*)$", s)
        if m:
            d, arg = m.group(1).upper(), m.group(2).strip()
            if d == "ASM":
                asm = arg
            elif d == "GLO":
                glo = arg
            elif d in ("BBB", "FRM"):
                e = LodEntry(d.lower(), arg, n)
                e.ihdr, e.asm, e.glo, e.toggles = list(ihdr), asm, glo, set(toggles)
                entries.append(e)
            elif d[1:] == "ON":
                toggles.add(d[0])
            elif d[1:] == "OF":
                toggles.discard(d[0])
            continue
        if s.upper().startswith("IHDR"):
            ihdr = [f.split(":")[0].strip().upper() for f in s[4:].split(",") if f.strip()]
            continue
        if s.lower().endswith(".img"):
            e = LodEntry("img", re.split(r"[\\/]", s)[-1], n)
            e.ihdr, e.asm, e.glo, e.toggles = list(ihdr), asm, glo, set(toggles)
            entries.append(e)
            last = e
    return entries
