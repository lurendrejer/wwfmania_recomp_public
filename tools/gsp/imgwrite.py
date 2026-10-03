"""Writing Midway IMG libraries (docs/IMG_FORMAT.md) and small helpers for the art the mods make at
generation time (mods/*/gen/*.py, run by genimg.py: extras.py 'script'). Standard library only."""

import struct


def rgb(c):
    return ((c >> 10) & 31) * 8, ((c >> 5) & 31) * 8, (c & 31) * 8


def to555(r, g, b):
    return (min(31, round(r / 8)) << 10) | (min(31, round(g / 8)) << 5) | min(31, round(b / 8))


def dist(a, b):
    return 3 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 2 * (a[2] - b[2]) ** 2


def write_img(path, images, palettes):
    """images: dicts name, anix, aniy, w, h, pal (index into palettes), pix (w*h bytes),
    ani2 (x, y, z), pttbl (40 bytes or None); palettes: (name, bitspix, [colors])."""
    data = bytearray(28)
    pal_off = []
    for _, _, cols in palettes:
        pal_off.append(len(data))
        data += struct.pack(f"<{len(cols)}H", *cols)
    pix_off = []
    for im in images:
        pix_off.append(len(data))
        stride = (im["w"] + 3) & ~3
        for y in range(im["h"]):
            row = im["pix"][y * im["w"]:(y + 1) * im["w"]]
            data += row + bytes(stride - im["w"])
    oset = len(data)
    pts = []
    for k, im in enumerate(images):
        pt = 0xFFFF
        if im.get("pttbl"):
            pt = len(pts)
            pts.append(im["pttbl"])
        a2 = im.get("ani2", (-1, -1, -1))
        data += im["name"].encode("latin-1")[:15].ljust(16, b"\0")
        data += struct.pack("<HhhHHHIIHhhhHHH", 0, im["anix"], im["aniy"], im["w"], im["h"], im["pal"] + 3,
                            pix_off[k], 0, 0, a2[0], a2[1], a2[2], 0, pt, 0xFFFF)
    for k, (name, bits, cols) in enumerate(palettes):
        data += name.encode("latin-1")[:9].ljust(10, b"\0")
        data += struct.pack("<BBHI", 0, bits, len(cols), pal_off[k]) + bytes(8)
    for p in pts:
        data += p
    struct.pack_into("<HHIHHHHH", data, 0, len(images), len(palettes) + 3, oset, 0x0634, 0, 0, 0, 0xABCD)
    with open(path, "wb") as f:
        f.write(data)


def box_scale(img, w, h):
    sh, sw = len(img), len(img[0])
    out = []
    for y in range(h):
        row = []
        ya, yb = y * sh // h, max(y * sh // h + 1, (y + 1) * sh // h)
        for x in range(w):
            xa, xb = x * sw // w, max(x * sw // w + 1, (x + 1) * sw // w)
            px = [img[j][i] for j in range(ya, yb) for i in range(xa, xb) if img[j][i]]
            row.append(tuple(sum(c[k] for c in px) // len(px) for k in range(3)) if px else None)
        out.append(row)
    return out


def median_cut(colors, n):
    boxes = [list(colors)]
    while len(boxes) < n:
        boxes.sort(key=len)
        b = boxes.pop()
        if len(b) < 2:
            boxes.append(b)
            break
        k = max(range(3), key=lambda c: max(p[c] for p in b) - min(p[c] for p in b))
        b.sort(key=lambda p: p[k])
        boxes += [b[:len(b) // 2], b[len(b) // 2:]]
    return [tuple(sum(p[c] for p in b) // len(b) for c in range(3)) for b in boxes if b]


def cut(lib, name, x0, x1):
    im = lib.find(name)
    stride = (im.w + 3) & ~3
    return [lib.data[im.pix_off + y * stride + x0:im.pix_off + y * stride + x1] for y in range(im.h)], im


def compose(pieces, aniy, name, pal):
    """An image of the columns pieces = [(rows, ...)] side by side, anchored in the middle."""
    h = len(pieces[0])
    rows = [b"".join(p[y] for p in pieces) for y in range(h)]
    w = len(rows[0])
    return {"name": name, "anix": w // 2, "aniy": aniy, "w": w, "h": h, "pal": pal, "pix": b"".join(rows)}
