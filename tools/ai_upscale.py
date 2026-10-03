#!/usr/bin/env python3
"""AI upscaling of indexed art (Real-ESRGAN) that still ends up as overrides.

Overrides must hold palette indices (docs/ASSET_OVERRIDES.md), so a plain
RGB result from a neural upscaler is not usable. Per image this tool:

  1. expands the indexed PNG to RGB with its own palette, and fills the
     transparent pixels with neighbouring colors (no dark halos),
  2. upscales the RGB with the Real-ESRGAN "realesr-animevideov3" network,
  3. takes the silhouette (index 0) from the index-preserving Scale2x/3x in
     tools/upscale.py, so the outline keeps the game's pixel style,
  4. maps every opaque pixel to the nearest palette color that the source
     image actually uses (never index 0).

The output is exactly k times the source, like tools/upscale.py, and can be
dropped into the same override directory.

With --truecolor step 4 is skipped: the result is an RGBA PNG (alpha 0 or 255
from the silhouette) with all the colors the network produced. The renderer
maps it back to the original palette indices at load time and keeps the
difference as per-pixel detail, so fades and recolors still work
(docs/ASSET_OVERRIDES.md). Use this for the best quality.

Setup (run on your own machine, not needed to build the game):

    python3 -m venv .venv-ai && . .venv-ai/bin/activate
    pip install torch numpy pillow

    ./build/imgtool dump orig/IMG art/original --indexed
    python3 tools/ai_upscale.py --lod orig/IMG/TAKER.LOD art/original art/hd_ai
    python3 tools/ai_upscale.py art/original art/hd_ai U4KM3E01 --scale 4

Device is picked automatically: CUDA, Apple MPS, then CPU (--device to force).
The weights (2.5 MB, BSD-3 licensed by the Real-ESRGAN authors) are downloaded
once to ~/.cache/wwfmania/. `--model lanczos` skips the network and only
resizes; it exists to test the palette/alpha steps without PyTorch.
"""
import argparse
import os
import sys
import urllib.request

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import upscale as ix  # noqa: E402  index-preserving upscaler and PNG io

WEIGHTS_URL = ("https://github.com/xinntao/Real-ESRGAN/releases/download/"
               "v0.2.5.0/realesr-animevideov3.pth")
WEIGHTS_PATH = os.path.expanduser("~/.cache/wwfmania/realesr-animevideov3.pth")
NET_SCALE = 4
PAD = 8  # edge padding so the network sees context at the sprite border


def load_esrgan(device_name):
    import torch
    import torch.nn as nn
    import torch.nn.functional as F

    class SRVGGNetCompact(nn.Module):
        def __init__(self, nin=3, nout=3, nfeat=64, nconv=16, up=4):
            super().__init__()
            self.up = up
            self.body = nn.ModuleList()
            self.body.append(nn.Conv2d(nin, nfeat, 3, 1, 1))
            self.body.append(nn.PReLU(num_parameters=nfeat))
            for _ in range(nconv):
                self.body.append(nn.Conv2d(nfeat, nfeat, 3, 1, 1))
                self.body.append(nn.PReLU(num_parameters=nfeat))
            self.body.append(nn.Conv2d(nfeat, nout * up * up, 3, 1, 1))
            self.shuffle = nn.PixelShuffle(up)

        def forward(self, x):
            out = x
            for layer in self.body:
                out = layer(out)
            out = self.shuffle(out)
            return out + F.interpolate(x, scale_factor=self.up, mode="nearest")

    if not os.path.exists(WEIGHTS_PATH):
        os.makedirs(os.path.dirname(WEIGHTS_PATH), exist_ok=True)
        print("downloading weights to " + WEIGHTS_PATH, file=sys.stderr)
        urllib.request.urlretrieve(WEIGHTS_URL, WEIGHTS_PATH)

    if device_name == "auto":
        if torch.cuda.is_available():
            device_name = "cuda"
        elif getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
            device_name = "mps"
        else:
            device_name = "cpu"
    dev = torch.device(device_name)
    net = SRVGGNetCompact()
    state = torch.load(WEIGHTS_PATH, map_location="cpu")
    net.load_state_dict(state.get("params", state), strict=True)
    net.eval().to(dev)
    print("Real-ESRGAN on " + device_name, file=sys.stderr)

    def run(rgb):  # float32 HxWx3 in 0..1 -> float32 (4H)x(4W)x3
        t = torch.from_numpy(rgb).permute(2, 0, 1)[None].to(dev)
        with torch.no_grad():
            o = net(t)
        return o[0].permute(1, 2, 0).clamp(0, 1).cpu().numpy()

    return run


def lanczos_model(rgb):
    h, w = rgb.shape[:2]
    im = Image.fromarray((rgb * 255 + 0.5).astype(np.uint8))
    im = im.resize((w * NET_SCALE, h * NET_SCALE), Image.LANCZOS)
    return np.asarray(im, dtype=np.float32) / 255.0


def palette_from_chunks(chunks):
    plte = dict(chunks)[b"PLTE"]
    return np.frombuffer(plte, dtype=np.uint8).reshape(-1, 3)


def fill_transparent(rgb, opaque, iters=PAD):
    """Bleeds opaque colors into transparent pixels, `iters` pixels deep."""
    rgb = rgb.copy()
    have = opaque.copy()
    for _ in range(iters):
        if have.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(have.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            m = np.roll(have, (dy, dx), (0, 1))
            if dy == 1:
                m[0, :] = False
            elif dy == -1:
                m[-1, :] = False
            if dx == 1:
                m[:, 0] = False
            elif dx == -1:
                m[:, -1] = False
            c = np.roll(rgb, (dy, dx), (0, 1))
            acc += c * m[..., None]
            cnt += m
        new = (~have) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][..., None]
        have |= new
    return rgb


def quantize(rgb, opaque, pal, used):
    """Nearest palette color among `used` indices; 0 where not opaque."""
    cand = np.array(sorted(i for i in used if i != 0), dtype=np.int32)
    pc = pal[cand].astype(np.float32) / 255.0
    out = np.zeros(rgb.shape[:2], dtype=np.uint8)
    ys, xs = np.nonzero(opaque)
    px = rgb[ys, xs]
    for s in range(0, len(px), 65536):
        d = ((px[s:s + 65536, None, :] - pc[None]) ** 2).sum(-1)
        out[ys[s:s + 65536], xs[s:s + 65536]] = cand[d.argmin(1)]
    return out


def write_rgba(path, w, h, rgba):
    import struct
    import zlib

    def chunk(t, body):
        c = struct.pack(">I", len(body)) + t + body
        return c + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))
    data = (ix.PNG_SIG + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


def process(src, dst, k, model, truecolor=False):
    w, h, rows, chunks = ix.read_png(src)
    idx = np.array([list(r) for r in rows], dtype=np.uint8)
    pal = palette_from_chunks(chunks)
    used = set(np.unique(idx).tolist())
    if not (used - {0}):  # fully transparent: nothing to do
        w2, h2, rows2 = ix.upscale(w, h, rows, k)
        if truecolor:
            write_rgba(dst, w2, h2, np.zeros((h2, w2, 4), dtype=np.uint8))
        else:
            ix.write_png(dst, w2, h2, rows2, chunks)
        return

    opaque = idx != 0
    rgb = pal[idx].astype(np.float32) / 255.0
    rgb = fill_transparent(rgb, opaque)
    rgb = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode="edge")
    big = model(rgb)[PAD * NET_SCALE:-PAD * NET_SCALE, PAD * NET_SCALE:-PAD * NET_SCALE]
    if k != NET_SCALE:
        im = Image.fromarray((big * 255 + 0.5).astype(np.uint8))
        im = im.resize((w * k, h * k), Image.LANCZOS)
        big = np.asarray(im, dtype=np.float32) / 255.0

    # Silhouette from the index-preserving upscale (same size, k x source).
    _, _, srows = ix.upscale(w, h, rows, k)
    sil = np.array([list(r) for r in srows], dtype=np.uint8) != 0
    if truecolor:
        rgba = np.zeros((h * k, w * k, 4), dtype=np.uint8)
        rgba[..., :3] = (big * 255 + 0.5).astype(np.uint8)
        rgba[..., 3] = np.where(sil, 255, 0)
        write_rgba(dst, w * k, h * k, rgba)
        return
    out = quantize(big, sil, pal, used)
    ix.write_png(dst, w * k, h * k, [bytearray(r.tobytes()) for r in out], chunks)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--lod", action="append", default=[],
                    help="upscale the labels this LOD script selects (repeatable)")
    ap.add_argument("--skip-existing", action="store_true",
                    help="leave images that already exist in the output directory")
    ap.add_argument("--scale", type=int, default=2, help="output factor (default 2)")
    ap.add_argument("--model", choices=("esrgan", "lanczos"), default="esrgan")
    ap.add_argument("--truecolor", action="store_true",
                    help="write RGBA overrides (more than the palette's colors)")
    ap.add_argument("--device", default="auto", help="auto, cpu, cuda, mps")
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("labels", nargs="*")
    a = ap.parse_args()

    labels = list(a.labels)
    for lod in a.lod:
        labels += ix.lod_labels(lod)
    if not labels:
        ap.error("give --lod or at least one LABEL")
    if a.scale < 2:
        ap.error("--scale must be at least 2")
    os.makedirs(a.out, exist_ok=True)
    model = lanczos_model if a.model == "lanczos" else load_esrgan(a.device)

    seen = set()
    n = 0
    for lab in labels:
        if lab in seen:
            continue
        seen.add(lab)
        src = os.path.join(a.src, lab + ".png")
        if not os.path.exists(src):
            print("skip (not in source dir): " + lab, file=sys.stderr)
            continue
        dst = os.path.join(a.out, lab + ".png")
        if a.skip_existing and os.path.exists(dst):
            continue
        process(src, dst, a.scale, model, a.truecolor)
        n += 1
        if n % 25 == 0:
            print("%d/%d" % (n, len(labels)), file=sys.stderr)
    print("wrote %d images (%dx) into %s" % (n, a.scale, a.out))


if __name__ == "__main__":
    main()
