#!/usr/bin/env python3
"""Upscale indexed PNGs (from `imgtool dump --indexed`) into art overrides.

Overrides hold palette indices, not colors (docs/ASSET_OVERRIDES.md), so the
upscaler must never blend: every output pixel is copied from an input pixel.
That rules out bilinear/bicubic filters and leaves the pixel-art family of
edge-directed algorithms:

  * k = 2: Scale2x (EPX)
  * k = 3: Scale3x
  * k = 4: Scale2x applied twice
  * k = 6: Scale3x then Scale2x, k = 9: Scale3x twice, k = 8: Scale2x thrice

The output keeps the source PLTE/tRNS and is exactly k times the original on
both axes, so the anchor point (anix, aniy) * k still lines up.

  upscale.py --lod orig/IMG/TAKER.LOD SRC_DIR OUT_DIR [--scale 2]
  upscale.py SRC_DIR OUT_DIR LABEL [LABEL ...] [--scale 2]

Python 3, standard library only.

With --layers the images go into one folder per art layer, and with --zip DIR they are also packed into one zip per layer
(wrestlers_undertaker.zip, hud.zip, ...), each holding its layer folder: the form the game reads a layered set in
(docs/ASSET_OVERRIDES.md, "Layers").
"""
import argparse
import os
import re
import struct
import sys
import zlib

import art_layers as al

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def read_png(path):
    """Returns (width, height, rows, extra_chunks) for an 8-bit indexed PNG.

    `rows` is a list of bytearrays of palette indices. `extra_chunks` is a
    list of (type, data) to copy through (PLTE, tRNS).
    """
    with open(path, "rb") as f:
        d = f.read()
    if d[:8] != PNG_SIG:
        raise ValueError("not a PNG")
    p = 8
    idat = []
    keep = []
    w = h = None
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        body = d[p + 8:p + 8 + n]
        p += 12 + n
        if t == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if ctype != 3 or depth != 8 or interlace:
                raise ValueError("need 8-bit non-interlaced indexed PNG")
        elif t == b"IDAT":
            idat.append(body)
        elif t in (b"PLTE", b"tRNS"):
            keep.append((t, body))
    raw = zlib.decompress(b"".join(idat))
    rows = []
    prev = bytearray(w)
    pos = 0
    for _ in range(h):
        ft = raw[pos]
        cur = bytearray(raw[pos + 1:pos + 1 + w])
        pos += 1 + w
        for x in range(w):
            a = cur[x - 1] if x else 0
            b = prev[x]
            c = prev[x - 1] if x else 0
            if ft == 1:
                cur[x] = (cur[x] + a) & 255
            elif ft == 2:
                cur[x] = (cur[x] + b) & 255
            elif ft == 3:
                cur[x] = (cur[x] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[x] = (cur[x] + pr) & 255
        rows.append(cur)
        prev = cur
    return w, h, rows, keep


def write_png(path, w, h, rows, chunks):
    def chunk(t, body):
        c = struct.pack(">I", len(body)) + t + body
        return c + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    out = PNG_SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    for t, body in chunks:
        out += chunk(t, body)
    out += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def scale2x(w, h, rows):
    out = [bytearray(w * 2) for _ in range(h * 2)]
    for y in range(h):
        up = rows[max(y - 1, 0)]
        cur = rows[y]
        dn = rows[min(y + 1, h - 1)]
        for x in range(w):
            e = cur[x]
            b = up[x]
            h_ = dn[x]
            d = cur[max(x - 1, 0)]
            f = cur[min(x + 1, w - 1)]
            e0 = e1 = e2 = e3 = e
            if b != h_ and d != f:
                if d == b:
                    e0 = d
                if b == f:
                    e1 = f
                if d == h_:
                    e2 = d
                if h_ == f:
                    e3 = f
            o = out[2 * y]
            o[2 * x], o[2 * x + 1] = e0, e1
            o = out[2 * y + 1]
            o[2 * x], o[2 * x + 1] = e2, e3
    return w * 2, h * 2, out


def scale3x(w, h, rows):
    out = [bytearray(w * 3) for _ in range(h * 3)]
    for y in range(h):
        r0 = rows[max(y - 1, 0)]
        r1 = rows[y]
        r2 = rows[min(y + 1, h - 1)]
        for x in range(w):
            xl, xr = max(x - 1, 0), min(x + 1, w - 1)
            a, b, c = r0[xl], r0[x], r0[xr]
            d, e, f = r1[xl], r1[x], r1[xr]
            g, hh, i = r2[xl], r2[x], r2[xr]
            p = [e] * 9
            if b != hh and d != f:
                if d == b:
                    p[0] = d
                if (d == b and e != c) or (b == f and e != a):
                    p[1] = b
                if b == f:
                    p[2] = f
                if (d == b and e != g) or (d == hh and e != a):
                    p[3] = d
                if (b == f and e != i) or (hh == f and e != c):
                    p[5] = f
                if d == hh:
                    p[6] = d
                if (d == hh and e != i) or (hh == f and e != g):
                    p[7] = hh
                if hh == f:
                    p[8] = f
            for j in range(3):
                o = out[3 * y + j]
                o[3 * x:3 * x + 3] = bytes(p[3 * j:3 * j + 3])
    return w * 3, h * 3, out


def upscale(w, h, rows, k):
    """Factors k into 2s and 3s and applies the matching passes."""
    passes = []
    n = k
    for f in (3, 2):
        while n % f == 0:
            passes.append(f)
            n //= f
    if n != 1 or k < 2:
        raise ValueError("scale must be 2, 3, 4, 6, 8, 9, ...")
    for f in passes:
        w, h, rows = (scale2x if f == 2 else scale3x)(w, h, rows)
    return w, h, rows


def lod_labels(path):
    """Image labels a .LOD script selects (the `--->` lines)."""
    labels = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("--->"):
                labels += [s.strip() for s in line[4:].split(",") if s.strip()]
    return labels


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--lod", help="upscale the labels this LOD script selects")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("labels", nargs="*")
    al.add_arguments(ap)
    a = ap.parse_args()
    layout = al.Layout(a)

    labels = list(a.labels)
    if a.lod:
        labels += lod_labels(a.lod)
    if not labels:
        ap.error("give --lod or at least one LABEL")
    os.makedirs(a.out, exist_ok=True)

    done = missing = 0
    seen = set()
    for lab in labels:
        if lab in seen:
            continue
        seen.add(lab)
        src = os.path.join(a.src, lab + ".png")
        if not os.path.exists(src):
            missing += 1
            print("skip (not in source dir): " + lab, file=sys.stderr)
            continue
        w, h, rows, chunks = read_png(src)
        w2, h2, rows2 = upscale(w, h, rows, a.scale)
        write_png(layout.path(a.out, lab), w2, h2, rows2, chunks)
        done += 1
    print("upscaled %d images by %dx into %s (%d missing)" % (done, a.scale, a.out, missing))
    layout.finish(a.out)


if __name__ == "__main__":
    main()
