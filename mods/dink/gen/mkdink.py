#!/usr/bin/env python3
"""Makes the art of Dink for mods/dink (see its README). Run by tools/gsp/genimg.py (gen.txt 'script') as
`mkdink.py --src ORIG --out GEN`; standard library only.

Dink is Doink the Clown drawn smaller (the game's own frames, scaled by the DMA). What he needs of his own is
for the screens around the match, made from Doink's into GEN/lod/DINK.IMG:

  CRUT_DI     his crouton: Doink's (CRUT2.IMG) with the face smaller inside the frame
  DIMUG       his mugshot: Doink's (DKMUG_A-H, WWFMUGS.IMG) put together, the picture smaller inside the frame
  NAM_DINK    his name on the life bar, SHORTDINK his short name, BIGDINK his name at the end of a round:
              Doink's (NAM_DNK, SHORTDNK in METERS.IMG, BIGDNK in WMATCH.IMG) without the O
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..", "tools", "gsp")))

from imglib import ImgLib  # noqa: E402
from imgwrite import box_scale, compose, cut, dist, median_cut, rgb, to555, write_img  # noqa: E402

SMALL = 0.72        # how much of the picture's size he keeps in the crouton and the mugshot


def lib(src, name):
    d = os.path.join(src, "IMG")
    return ImgLib(os.path.join(d, next(f for f in os.listdir(d) if f.upper() == name)))


def pieces_rgb(lb, names):
    """Images put together at their anchors: rows of (r, g, b) or None, and the top left corner
    relative to the common anchor."""
    ps = [lb.find(n) for n in names]
    x0 = min(-p.anix for p in ps)
    y0 = min(-p.aniy for p in ps)
    w = max(-p.anix + p.w for p in ps) - x0
    h = max(-p.aniy + p.h for p in ps) - y0
    img = [[None] * w for _ in range(h)]
    for p in ps:
        stride = (p.w + 3) & ~3
        for y in range(p.h):
            for x in range(p.w):
                v = lb.data[p.pix_off + y * stride + x]
                if v:
                    img[-p.aniy + y - y0][-p.anix + x - x0] = rgb(p.palette.colors[v % len(p.palette.colors)])
    return img, x0, y0


def smaller_inside(img, border):
    """The picture inside a frame `border` pixels wide made smaller (SMALL), standing on the bottom of the
    frame's inside, the rest of the inside in a dark tone of the picture's colours."""
    h, w = len(img), len(img[0])
    inner = [row[border:w - border] for row in img[border:h - border]]
    ih, iw = len(inner), len(inner[0])
    sw, sh = round(iw * SMALL), round(ih * SMALL)
    small = box_scale(inner, sw, sh)
    px = [c for row in inner for c in row if c]
    bg = tuple(sum(c[k] for c in px) // len(px) * 2 // 5 for k in range(3))   # its colours, dark
    out = [row[:] for row in img]
    ox, oy = border + (iw - sw) // 2, border + ih - sh
    for y in range(border, h - border):
        for x in range(border, w - border):
            out[y][x] = bg
    for y in range(sh):
        for x in range(sw):
            if small[y][x]:
                out[oy + y][ox + x] = small[y][x]
    return out


def quantize(img, n, name, palname, anix, aniy, pal_index):
    colors = median_cut([c for row in img for c in row if c], n)
    pal = [0] + [to555(*c) for c in colors]
    near = {}
    pix = bytearray()
    for row in img:
        for c in row:
            if c is None:
                pix.append(0)
                continue
            if c not in near:
                near[c] = 1 + min(range(len(colors)), key=lambda j: dist(c, colors[j]))
            pix.append(near[c])
    im = {"name": name, "anix": anix, "aniy": aniy, "w": len(img[0]), "h": len(img), "pal": pal_index,
          "pix": bytes(pix)}
    return im, (palname, 8, pal)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="orig")
    ap.add_argument("--out", default="build/gen")
    a = ap.parse_args()

    images, palettes = [], []
    crut, _, _ = pieces_rgb(lib(a.src, "CRUT2.IMG"), ["CRUT_DK"])
    im, pal = quantize(smaller_inside(crut, 2), 63, "CRUT_DI", "CRUTDI_P", 0, 0, len(palettes))
    images.append(im)
    palettes.append(pal)

    mug, x0, y0 = pieces_rgb(lib(a.src, "WWFMUGS.IMG"), [f"DKMUG_{c}" for c in "ABCDEFGH"])
    im, pal = quantize(smaller_inside(mug, 3), 255, "DIMUG", "DIMUG_P", -x0, -y0, len(palettes))
    images.append(im)
    palettes.append(pal)

    mt = lib(a.src, "METERS.IMG")
    d, im_n = cut(mt, "NAM_DNK", 0, 9)              # D O I N K: the glyphs end where the next begins
    ink, _ = cut(mt, "NAM_DNK", 18, im_n.w)
    images.append(compose([d, ink], im_n.aniy, "NAM_DINK", len(palettes)))
    d, im_s = cut(mt, "SHORTDNK", 0, 9)
    ink, _ = cut(mt, "SHORTDNK", 18, im_s.w)
    images.append(compose([d, ink], im_s.aniy, "SHORTDINK", len(palettes)))
    palettes.append((im_n.palette.name, im_n.palette.bitspix, list(im_n.palette.colors)))

    wm = lib(a.src, "WMATCH.IMG")
    d, im_b = cut(wm, "BIGDNK", 0, 19)
    ink, _ = cut(wm, "BIGDNK", 35, im_b.w)
    images.append(compose([d, ink], im_b.aniy, "BIGDINK", len(palettes)))
    palettes.append((im_b.palette.name, im_b.palette.bitspix, list(im_b.palette.colors)))

    os.makedirs(os.path.join(a.out, "lod"), exist_ok=True)
    write_img(os.path.join(a.out, "lod", "DINK.IMG"), images, palettes)
    print(f"mkdink: {len(images)} images")
    return 0


if __name__ == "__main__":
    sys.exit(main())
