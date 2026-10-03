#!/usr/bin/env python3
"""Makes Adam Bomb for mods/adambomb (see its README). Run by tools/gsp/genimg.py (gen.txt 'script')
as `mkadam.py --src ORIG --out GEN`; standard library only.

Adam Bomb is played with Razor Ramon's moves: his own code (orig/ADAM.ASM, ADMSEQ1-3.ASM) is for an
older engine and was never finished. This writes, from orig/ and frames.txt:

  GEN/ADAMB.ASM, ADBSEQ1-4.ASM   RAZOR.ASM and RZRSEQ1-4.ASM with every name they define renamed
                                 (rzr_ -> adm_, razor_ -> adam_, move_razor -> move_adam), Razor's
                                 frame groups replaced by Adam's (R4ST4G -> AB4ST4G), his sound row
                                 (W_ADAM), and Razor's own voice lines left out
  GEN/ADAMB.H                    .global for the AB groups
  GEN/ADAMTBL.ASM                the AB groups (frame tables like the .SEQ files the image tools
                                 made: a count, then the frames), Adam's other seven palettes, and
                                 the image headers (adamimg.tbl, adamrzr.tbl, adamsel.tbl)
  GEN/lod/ADAMRZR.IMG            the Razor frames Adam has no frame of his own for, recoloured to
                                 Adam's palette (AZ<rest of Razor's name>)
  GEN/lod/ADAMSEL.IMG            Adam's crouton for the select screen (CRUT_AB), his mugshot
                                 (ADMUG_A-H, WWFMUGS.IMG) made small, and his name in the two
                                 lettering the game has none of his in (BIGADM, SHORTADM)
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..", "tools", "gsp")))

from imglib import ImgLib, read_lod  # noqa: E402
from imgwrite import box_scale, compose, cut, dist, median_cut, rgb, to555, write_img  # noqa: E402

SYMBOL = re.compile(r"^[A-Za-z_][A-Za-z0-9_$]*$")
GROUP = re.compile(r"^R\d[A-Z]{2}\d[A-Z]$")
ADAM_COSTUME = range(12, 27)       # ADMRED_P: the red of his singlet and knee pads (ADMGRN_P differs only there)
RAZOR_COSTUME = range(52, 64)      # Razor's palettes (WRESPAL.ASM) differ only there


class Orig:
    def __init__(self, src):
        self.src = src
        self.imgdir = os.path.join(src, "IMG")
        self.files = {f.upper(): f for f in os.listdir(self.imgdir)}
        self.libs = {}

    def lib(self, name):
        key = os.path.basename(name).upper()
        if not key.endswith(".IMG"):
            key += ".IMG"
        if key not in self.libs:
            self.libs[key] = ImgLib(os.path.join(self.imgdir, self.files[key]))
        return self.libs[key]

    def lod_labels(self, lodname):
        """Labels a LOD selects -> (library, image), as genimg.py does."""
        out = {}
        for e in read_lod(os.path.join(self.imgdir, self.files[lodname + ".LOD"])):
            if e.kind != "img":
                continue
            lib = self.lib(e.file)
            picks = [(n, lib.find(n)) for n in e.names] if e.names else [(im.name, im) for im in lib.images]
            for n, im in picks:
                if im is not None and SYMBOL.match(n):
                    out.setdefault(n, (lib, im))
        return out

    def groups(self, lodname, labels):
        """Frame groups as in <wrestler>.SEQ: name -> [label or None for frames 1..n]."""
        bat = open(os.path.join(self.imgdir, self.files[lodname + ".BAT"]), "rb").read().decode("latin-1")
        seqs = {}
        for f in re.search(r"wwfld\s+\S+\s+(.*)", bat).group(1).split():
            for im in self.lib(f).images:
                m = re.match(r"^([A-Za-z0-9_]+?)(\d\d)$", im.name)
                if m:
                    seqs.setdefault(m.group(1), {})[int(m.group(2))] = im.name
        return {g: [fr.get(k) if fr.get(k) in labels else None for k in range(1, max(fr) + 1)]
                for g, fr in seqs.items()}

    def source(self, name):
        path = os.path.join(self.src, name)
        if not os.path.exists(path):
            path = os.path.join(self.src, next(f for f in os.listdir(self.src) if f.upper() == name.upper()))
        return open(path, "rb").read().decode("latin-1").replace("\r", "").replace("\x1a", "").split("\n")


# ---- colours ---------------------------------------------------------------------------------------


def razor_to_adam(rzr_colors, adm_colors):
    """Razor's palette indices -> Adam's: his costume ramp onto Adam's, the rest to the nearest colour."""
    rest = [i for i in range(1, len(adm_colors)) if i not in ADAM_COSTUME]
    m = [0] * 256
    n_r, n_a = len(RAZOR_COSTUME), len(ADAM_COSTUME)
    for i in range(1, 256):
        if i >= len(rzr_colors):
            m[i] = m[i & 63]
        elif i in RAZOR_COSTUME:
            m[i] = ADAM_COSTUME[0] + round((i - RAZOR_COSTUME[0]) * (n_a - 1) / (n_r - 1))
        else:
            c = rgb(rzr_colors[i])
            m[i] = min(rest, key=lambda j: dist(c, rgb(adm_colors[j])))
    return m


def variant(adm_red, hue):
    """Adam's palette with the singlet in another colour: each shade of the red ramp keeps its
    brightness (max channel) and its white (min channel), in the colour `hue`."""
    out = list(adm_red)
    for i in ADAM_COSTUME:
        r, g, b = rgb(adm_red[i])
        hi, lo = max(r, g, b), min(r, g, b)
        out[i] = to555(*(lo + (hi - lo) * h for h in hue))
    return out


ADAM_PALETTES = [          # name, singlet colour (ADMRED_P and ADMGRN_P are in ADM_WLK.IMG)
    ("ADMBLU_P", (0.15, 0.35, 1.0)),
    ("ADMYEL_P", (1.0, 0.9, 0.0)),
    ("ADMPRP_P", (0.7, 0.1, 1.0)),
    ("ADMORG_P", (1.0, 0.5, 0.0)),
    ("ADMPNK_P", (1.0, 0.35, 0.75)),
    ("ADMGRY_P", (0.62, 0.62, 0.62)),
]


# ---- the crouton -----------------------------------------------------------------------------------

def mug_rgb(o):
    """Adam's mugshot (ADMUG_A-H) put together, as rows of (r, g, b)."""
    lib = o.lib("WWFMUGS.IMG")
    pieces = [lib.find(f"ADMUG_{c}") for c in "ABCDEFGH"]
    x0 = min(-p.anix for p in pieces)
    y0 = min(-p.aniy for p in pieces)
    x1 = max(-p.anix + p.w for p in pieces)
    y1 = max(-p.aniy + p.h for p in pieces)
    w, h = x1 - x0, y1 - y0
    img = [[None] * w for _ in range(h)]
    for p in pieces:
        stride = (p.w + 3) & ~3
        for y in range(p.h):
            for x in range(p.w):
                v = lib.data[p.pix_off + y * stride + x]
                if v:
                    img[-p.aniy + y - y0][-p.anix + x - x0] = rgb(p.palette.colors[v % len(p.palette.colors)])
    return img


def crouton(o):
    """The face from the mugshot (the croutons are close-ups), inside the frame of Razor's crouton."""
    mug = mug_rgb(o)
    face = box_scale([row[33:95] for row in mug[22:94]], 31, 36)
    lib = o.lib("CRUT2.IMG")
    rr = lib.find("CRUT_RR")
    stride = (rr.w + 3) & ~3
    small = []
    for y in range(40):
        row = []
        for x in range(35):
            if 2 <= x < 33 and 2 <= y < 38:
                row.append(face[y - 2][x - 2])
            else:
                v = lib.data[rr.pix_off + y * stride + x]
                row.append(rgb(rr.palette.colors[v]) if v else None)
        small.append(row)
    pal = median_cut([c for row in small for c in row if c], 63)
    colors = [0] + [to555(*c) for c in pal]
    pix = bytearray()
    for row in small:
        for c in row:
            pix.append(0 if c is None else 1 + min(range(len(pal)), key=lambda j: dist(c, pal[j])))
    # anchored like the original croutons (CRUT2.IMG: top left)
    return {"name": "CRUT_AB", "anix": 0, "aniy": 0, "w": 35, "h": 40, "pal": 0, "pix": bytes(pix)}, \
        ("CRUTAB_P", 8, colors)


# ---- names ----------------------------------------------------------------------------------------


def names(o, first_pal):
    """His name where the game has the others' and he has none: BIGADM (WMATCH.IMG's big names, at the
    end of a round, from the letters of YOKOZUNA, BRET, RAZOR RAMON, UNDERTAKER and BAM BAM) and SHORTADM (the
    first word of NAM_ADM, the one name of his the game has, METERS.IMG)."""
    wm = o.lib("WMATCH.IMG")
    big_a, im = cut(wm, "BIGYOK", 116, 135)      # the first and the last letter are the tall ones
    big_b, _ = cut(wm, "BIGHRT", 0, 19)
    a, _ = cut(wm, "BIGRAZ", 19, 37)
    d, _ = cut(wm, "BIGUND", 36, 52)
    m, _ = cut(wm, "BIGRAZ", 127, 151)
    b, _ = cut(wm, "BIGBAM", 69, 85)
    oo, _ = cut(wm, "BIGRAZ", 53, 69)
    sp = [bytes(8)] * im.h
    big = compose([big_a, d, a, m, sp, b, oo, m, big_b], im.aniy, "BIGADM", first_pal)
    mt = o.lib("METERS.IMG")
    adam, im2 = cut(mt, "NAM_ADM", 0, 45)
    short = compose([adam], im2.aniy, "SHORTADM", first_pal + 1)
    pals = [(im.palette.name, im.palette.bitspix, list(im.palette.colors)),
            (im2.palette.name, im2.palette.bitspix, list(im2.palette.colors))]
    return [big, short], pals


# ---- frames ----------------------------------------------------------------------------------------

def read_frames_txt(path):
    m = {}
    for raw in open(path):
        line = raw.split("#")[0].split()
        if line:
            align = "align" in line
            nums = [int(x) for x in line[2:] if x != "align"]
            m[line[0]] = (line[1], nums or None, align)
    return m


def body(lib, im):
    """Centroid of the opaque pixels and their lowest row, relative to the anchor."""
    stride = (im.w + 3) & ~3
    sx = sy = n = 0
    low = -1
    for y in range(im.h):
        row = lib.data[im.pix_off + y * stride:im.pix_off + y * stride + im.w]
        for x, v in enumerate(row):
            if v:
                sx += x
                sy += y
                n += 1
                low = y
    return sx / n - im.anix, sy / n - im.aniy, low - im.aniy


def aligned(rlib, rim, alib, aim):
    """How far to move Adam's anchor so his body is where Razor's is: the feet on the same row where
    both stand on the ground, else the centroids together; x by the centroids."""
    rx, ry, rlow = body(rlib, rim)
    ax, ay, alow = body(alib, aim)
    dy = alow - rlow if abs(rlow) <= 6 and abs(alow) <= 6 else ay - ry
    return round(ax - rx), round(dy)


def adam_labels(o):
    labels = o.lod_labels("ADAM")
    # Doink's LOD loads four of Adam's standing frames from ADM_WLK2.IMG (A4ST4J05-08)
    for n, v in o.lod_labels("DOINK").items():
        if n.startswith("A4ST4J"):
            labels.setdefault(n, v)
    return labels


def adam_groups(o):
    labels = adam_labels(o)
    groups = {}
    for n in labels:
        m = re.match(r"^(A\d[A-Z]{2}\d[A-Z])(\d\d)$", n)
        if m:
            groups.setdefault(m.group(1), {})[int(m.group(2))] = n
    return groups


def pick(have, k):
    """Adam's frame k, or the nearest one he has."""
    return have[min(have, key=lambda j: (abs(j - k), j))]


def build_frames(o, out):
    rlabels = o.lod_labels("RAZOR")
    rgroups = {g: f for g, f in o.groups("RAZOR", rlabels).items() if GROUP.match(g)}
    agroups = adam_groups(o)
    mapping = read_frames_txt(os.path.join(HERE, "frames.txt"))
    adm = o.lib("ADM_WLK.IMG")
    adm_red = next(p for p in adm.palettes if p.name == "ADMRED_P").colors

    alabels = adam_labels(o)
    tables, borrowed, shifted = {}, [], {}
    for g in sorted(rgroups):
        frames = rgroups[g]
        if g in mapping:
            ag, explicit, align = mapping[g]
            have = agroups[ag]
            n_a = max(have)
            row = []
            for k, lab in enumerate(frames, 1):
                if explicit is not None:
                    t = explicit[k - 1] if k <= len(explicit) else 0
                    row.append(pick(have, t) if t else None)
                else:
                    row.append(pick(have, max(1, round(k * n_a / len(frames)))) if lab else None)
            for k, (lab, x) in enumerate(zip(frames, row), 1):
                if lab and not x:
                    raise SystemExit(f"frames.txt: {g} frame {k} has no frame of Adam's")
                if align and lab:              # a copy of Adam's frame with the anchor moved
                    dx, dy = aligned(*rlabels[lab], *alabels[x])
                    if abs(dx) > 3 or abs(dy) > 3:
                        name = f"AY{x[1:]}{dx:+d}{dy:+d}".replace("+", "P").replace("-", "M")
                        shifted[name] = (x, dx, dy)
                        row[k - 1] = name
            tables["AB" + g[1:]] = row
        else:
            row = []
            for lab in frames:
                if lab:
                    row.append("AZ" + lab[1:])
                    borrowed.append(lab)
                else:
                    row.append(None)
            tables["AB" + g[1:]] = row

    images, palettes, maps = [], [("ADMRED_P", 6, list(adm_red))], {}
    for lab in borrowed:
        lib, im = rlabels[lab]
        key = id(im.palette)
        if key not in maps:
            maps[key] = razor_to_adam(im.palette.colors, adm_red)
        mp = maps[key]
        stride = (im.w + 3) & ~3
        pix = bytearray(im.w * im.h)
        for y in range(im.h):
            src = lib.data[im.pix_off + y * stride:im.pix_off + y * stride + im.w]
            pix[y * im.w:(y + 1) * im.w] = bytes(mp[v & 63] if v else 0 for v in src)
        images.append({"name": "AZ" + lab[1:], "anix": im.anix, "aniy": im.aniy, "w": im.w, "h": im.h,
                       "pal": 0, "pix": bytes(pix), "ani2": im.ani2, "pttbl": lib.pttbl(im)})
    adm_pal = {p.name: k for k, p in enumerate(adm.palettes)}
    for name, (x, dx, dy) in sorted(shifted.items()):
        lib, im = alabels[x]
        stride = (im.w + 3) & ~3
        pix = b"".join(lib.data[im.pix_off + y * stride:im.pix_off + y * stride + im.w] for y in range(im.h))
        # the secondary point (where a second piece hangs, ANIM.ASM set_images) is in image pixels
        # like the anchor, so it stays with the picture; the point table too (genimg.py makes the
        # collision box relative to the anchor)
        images.append({"name": name, "anix": im.anix + dx, "aniy": im.aniy + dy, "w": im.w, "h": im.h,
                       "pal": 0, "pix": pix, "ani2": im.ani2, "pttbl": lib.pttbl(im)})
    os.makedirs(os.path.join(out, "lod"), exist_ok=True)
    write_img(os.path.join(out, "lod", "ADAMRZR.IMG"), images, palettes)

    asm = ['\t.FILE\t"adamtbl.asm"', "\t.OPTION\tB,D,L,T", "; generated by mods/adambomb/gen/mkadam.py - do not edit",
           "", "\t.include\timgtbl.glo", "\t.DATA", "\t.even", "", "\t.include\tadamimg.tbl",
           "\t.include\tadamrzr.tbl", "\t.include\tadamsel.tbl", "", "\t.even"]
    for name, row in tables.items():
        asm += [f"\t.global\t{name}", f"{name}:", f"\t.long\t{len(row)}"] + [f"\t.long\t{x or 0}" for x in row]
    asm.append("")
    for name, hue in ADAM_PALETTES:
        cols = variant(adm_red, hue)
        asm += [f"\t.global\t{name}", f"{name}:", f"\t.word\t{len(cols)}"]
        asm += ["\t.word\t" + ",".join(f"0{c:X}H" for c in cols[i:i + 8]) for i in range(0, len(cols), 8)]
    asm.append("\t.end")
    with open(os.path.join(out, "ADAMTBL.ASM"), "w") as f:
        f.write("\n".join(asm) + "\n")
    with open(os.path.join(out, "ADAMB.H"), "w") as f:
        f.write("; generated by mods/adambomb/gen/mkadam.py - do not edit\n")
        f.write("".join(f"\t.global\t{n}\n" for n in tables))
    own = sum(1 for g in rgroups if g in mapping)
    return set(rgroups), own, len(borrowed), len(shifted)


# ---- Razor's code as Adam's ------------------------------------------------------------------------

CLONES = [("RAZOR.ASM", "ADAMB.ASM"), ("RZRSEQ1.ASM", "ADBSEQ1.ASM"), ("RZRSEQ2.ASM", "ADBSEQ2.ASM"),
          ("RZRSEQ3.ASM", "ADBSEQ3.ASM"), ("RZRSEQ4.ASM", "ADBSEQ4.ASM")]
DEFINES = re.compile(r"^\s*SUBRP?\s+([A-Za-z_]\w*)|^([A-Za-z_]\w*):?(?=\s|$)", re.I)
DATA = re.compile(r"^\s*(\.long|\.word|\.ref|REFLONG|LWWW)\s", re.I)
VOICE = re.compile(r"^\s*WL\s+ANI_CODE\s*,\s*DO_RAZOR_(RUG_SPEECH|PUSH)\b")   # Razor's own voice


def new_name(n):
    if n.startswith("rzr_"):
        return "adm_" + n[4:]
    if n.startswith("razor_"):
        return "adam_" + n[6:]
    return {"move_razor": "move_adam"}.get(n, n)


def code_part(ln):
    """(code, comment) of a line; ; inside quotes is not a comment."""
    q = None
    for i, ch in enumerate(ln):
        if q:
            if ch == q:
                q = None
        elif ch == '"':
            q = ch
        elif ch == ";":
            return ln[:i], ln[i:]
    return ln, ""


def victim_block(lines, i):
    """End of the #Razor victim block starting at line i (the lines under it, see gspasm copy_block)."""
    j = i + 1
    while j < len(lines) and (not lines[j].strip() or lines[j][0] in " \t"):
        j += 1
    while j > i + 1 and not lines[j - 1].strip():
        j -= 1
    return j


def clone(o, out, rgroups):
    srcs = {a: o.source(a) for a, _ in CLONES}
    defined = set()
    for lines in srcs.values():
        for ln in lines:
            m = DEFINES.match(code_part(ln)[0])
            if m:
                n = m.group(1) or m.group(2)
                if n.upper() not in ("SUBR", "SUBRP") and new_name(n) != n:
                    defined.add(n)

    def ren(code, groups=True):
        def one(mo):
            n = mo.group(0)
            if n in defined:
                return new_name(n)
            if n == "rzr":                   # FACE24 rzr,punch_anim -> rzr_2_punch_anim, rzr_4_...
                return "adm"
            if n == "W_RAZOR":
                return "W_ADAM"
            if groups and n in rgroups:
                return "AB" + n[1:]
            if n.lower() == "razorimg.h":
                return "adamb.h"
            return n
        return re.sub(r"[A-Za-z_][A-Za-z0-9_.]*", one, code)

    others = re.compile(r"\b#?(hrt|und|yok|shn|bam|dnk|lex|doink|bret|taker|yoko|shawn)_", re.I)
    for src, dst in CLONES:
        lines, res, i = srcs[src], [], 0
        # runs of data lines naming other wrestlers' animations are tables by victim: Razor's
        # entries there are Razor being thrown and stay his (gen.txt 'tokslot' fills Adam's)
        by_victim, run = set(), []
        for k, ln in enumerate(lines + [""]):
            c = code_part(ln)[0]
            if DATA.match(c):
                run.append(k)
                continue
            if c.strip():
                if any(others.search(code_part(lines[j])[0]) for j in run):
                    by_victim.update(run)
                run = []
        while i < len(lines):
            code, comment = code_part(lines[i])
            if i in by_victim:
                res.append(lines[i])
                i += 1
                continue
            if VOICE.match(code):
                res.append(";" + lines[i] + "\t(Razor's voice: not for Adam)")
                i += 1
                continue
            if re.match(r"^#Razor\s*$", code):          # Razor as the victim: his own frames
                e = victim_block(lines, i)
                block = lines[i + 1:e]
                res.append(lines[i])
                res += [ren(code_part(b)[0], groups=False) + code_part(b)[1] for b in block]
                i = e
                continue
            if re.match(r"^#Adam\s*$", code):           # Adam as the victim: Razor's lines, Adam's frames
                e = victim_block(lines, i)
                last = max(k for k in range(i) if re.match(r"^#Razor\s*$", lines[k]))
                res.append(lines[i])
                res += [ren(code_part(b)[0]) for b in lines[last + 1:victim_block(lines, last)]]
                i = e
                continue
            if re.search(r";\s*\(?razor\b", comment, re.I) and DATA.match(code):
                res.append(lines[i])                    # a table by victim: Razor's line stays his
                i += 1
                continue
            if re.search(r";\s*Adam\b", comment) and not code.lstrip().startswith(";"):
                for k in range(i - 1, max(i - 14, -1), -1):   # numbers per victim: Razor's
                    c2, m2 = code_part(lines[k])
                    if re.search(r";\s*Razor\b", m2, re.I):
                        code = c2
                        break
            if re.search(r";\s*spare\b", comment, re.I) and not code.lstrip().startswith(";"):
                for k in range(i - 1, max(i - 14, -1), -1):
                    c2, m2 = code_part(lines[k])
                    if re.search(r";\s*razor\b", m2, re.I):
                        code = ren(c2)
                        break
            res.append(ren(code) + comment)
            i += 1
        res.insert(0, "; generated by mods/adambomb/gen/mkadam.py from orig/%s - do not edit" % src)
        with open(os.path.join(out, dst), "w") as f:
            f.write("\n".join(res) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="orig")
    ap.add_argument("--out", default="build/gen")
    a = ap.parse_args()
    o = Orig(a.src)
    os.makedirs(a.out, exist_ok=True)
    rgroups, own, borrowed, shifted = build_frames(o, a.out)
    im, pal = crouton(o)
    nims, npals = names(o, 1)
    write_img(os.path.join(a.out, "lod", "ADAMSEL.IMG"), [im] + nims, [pal] + npals)
    clone(o, a.out, rgroups)
    print(f"mkadam: {len(rgroups)} frame groups, {own} from Adam's art ({shifted} of his frames moved), "
          f"{borrowed} Razor frames recoloured")
    return 0


if __name__ == "__main__":
    sys.exit(main())
