#!/usr/bin/env python3
"""Generative remaster of all the game's art, through a local ComfyUI (docs/REMASTER.md).

    python3 tools/remaster/remaster.py prepare                 # export every image the game draws -> art/remaster/src
    python3 tools/remaster/remaster.py run --only doink --limit 20   # a first try
    python3 tools/remaster/remaster.py run                     # everything (resumes: done images are skipped)
    python3 tools/remaster/remaster.py finish                  # check, and write art/hd/remaster_<layer>.zip (--flat: remaster_partNN.zip)

`run` sends each image to ComfyUI (http://127.0.0.1:8188) through the workflow in sdxl_esrgan_tile.json: an upscale
model (Real-ESRGAN) first enlarges it 4x without pixel steps or blur, then an SDXL checkpoint redraws it at about 1024
pixels, adding detail, held to that sharp base by a tile ControlNet. The result is then
fitted to exactly 4x the original, cut to the original's outline (alpha 0 or 255, as the game needs), and, unless
--no-keep-color, given back the original's colours and lighting with only the generated fine detail on top. That
keeps the game's palette effects working and makes the frames of an animation flicker less. Every frame of one
animation gets the same seed and prompt.

Needs Pillow and numpy (pip install pillow numpy), a build (build/imgtool, build/gen) and orig/IMG.
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import time
import uuid
import zipfile
import zlib
import urllib.error
import urllib.parse
import urllib.request

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import art_layers as al  # noqa: E402  art layers: the zips of `finish`
sys.path.insert(0, os.path.join(ROOT, "tools", "gsp"))
from imglib import ImgLib  # noqa: E402
K = 4

WRESTLERS = {
    "HRT": ("bret_hart", "Bret 'The Hitman' Hart, professional wrestler, long dark hair, sunglasses, "
                         "pink and black wrestling singlet"),
    "RZR": ("razor_ramon", "Razor Ramon, professional wrestler, slicked-back dark hair, stubble, gold chains, "
                           "wrestling trunks and boots"),
    "UND": ("undertaker", "The Undertaker, tall professional wrestler, long red-brown hair, pale skin, "
                          "black and purple gear, gloves"),
    "YOK": ("yokozuna", "Yokozuna, huge sumo-style professional wrestler, black hair in a topknot, "
                        "red and black sumo belt"),
    "SHN": ("shawn_michaels", "Shawn Michaels, professional wrestler, long blond hair, athletic, "
                              "colourful tights and knee pads"),
    "BAM": ("bam_bam_bigelow", "Bam Bam Bigelow, big bald professional wrestler with flame tattoos on his head, "
                               "red and black gear"),
    "DNK": ("doink", "Doink the Clown, professional wrestler in clown make-up, green curly hair, "
                     "colourful clown costume"),
    "LEX": ("lex_luger", "Lex Luger, muscular blond professional wrestler, stars and stripes trunks"),
}
FONTS = {"FNT9", "WSFNT14", "WSFNT10", "WGSFNT18", "WGSFNT24", "WGSFNT22", "WGSFNT20", "WGSFNT14", "TROGF15",
         "TROGF7", "OSGEMD", "OGMD10", "SGMD8", "WINFONT"}
LABEL_OK = re.compile(r"^[A-Za-z0-9_]+$")

STYLE = "sharp focus, highly detailed, realistic photo, 1990s arcade game digitized video, clean edges"
NEGATIVE = ("blurry, lowres, jpeg artifacts, noise, text, watermark, signature, frame, border, cartoon, drawing, "
            "painting, deformed, extra limbs, extra fingers, bad anatomy, duplicate")
DEFAULTS = {  # denoise, ControlNet strength (for sdxl_esrgan_tile.json: the model starts from a sharp 4x upscale)
    "wrestlers": (0.4, 0.6),
    "backgrounds": (0.4, 0.6),
    "other": (0.35, 0.65),
    "mods": (0.4, 0.6),
    "fonts": (0.25, 0.85),
}
SOFT_DEFAULTS = {"wrestlers": (0.55, 0.65), "backgrounds": (0.55, 0.65), "other": (0.5, 0.7), "mods": (0.55, 0.65),
                 "fonts": (0.3, 0.9)}  # for a workflow without an upscale model (sdxl_tile.json): softened input


def die(msg):
    sys.exit(f"remaster: {msg}")


# ---- prepare: every image the game draws, as RGBA PNG, with a manifest ---------------------------------------

def category(lib, name, kind, mod):
    lib = lib.rsplit(".", 1)[0].upper()
    if kind == "background":
        return "backgrounds", ""
    if mod:
        return "mods/" + mod, ""
    if lib == "ROBOTRON" or lib.startswith("ADM") or re.match(r"^A\d", name):
        return "unused", ""
    pre = lib[:3]
    if pre in WRESTLERS and (len(lib) == 3 or lib[3] == "_"):
        return "wrestlers/" + WRESTLERS[pre][0], pre
    if lib in FONTS:
        return "fonts", ""
    return "other", ""


def rgb555(c):
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)


def bdd_images(path):
    data = open(path, "rb").read()
    pos = 0

    def line():
        nonlocal pos
        e = data.index(b"\n", pos)
        s = data[pos:e].decode("latin-1").strip()
        pos = e + 1
        return s
    out = []
    for _ in range(int(line().split()[0])):
        f = line().split()
        w, h = int(f[1]), int(f[2])
        out.append((w, h, data[pos:pos + w * h]))
        pos += w * h
    return out


def imgtool_info(imgtool, path):
    info = {}
    txt = subprocess.run([imgtool, "info", path], capture_output=True, text=True).stdout
    for ln in txt.splitlines():
        p = ln.split()
        if len(p) >= 6 and p[1].isdigit() and p[2].isdigit():
            info[p[0].upper()] = (int(p[1]), int(p[2]), int(p[3]), int(p[4]))
    return info


def draw_palette_png(img, lib, label, palname, dest):
    """Writes a sprite in the palette the game draws it with (gen/drawpal.txt) instead of its own."""
    il = ImgLib(os.path.join(img, lib))
    im = il.find(label)
    pal = next((p for p in il.palettes if p.name == palname), None)
    if im is None or pal is None:
        return False
    mask = 0xFF if not pal.bitspix or pal.bitspix >= 8 else (1 << pal.bitspix) - 1
    stride = (im.w + 3) & ~3
    cols = [rgb555(c) for c in pal.colors]
    px = []
    for y in range(im.h):
        row = il.data[im.pix_off + y * stride:im.pix_off + y * stride + im.w]
        for v in row:
            v &= mask
            px.append((0, 0, 0, 0) if v == 0 else (*cols[v], 255) if v < len(cols) else (255, 0, 255, 255))
    out = Image.new("RGBA", (im.w, im.h))
    out.putdata(px)
    out.save(dest)
    return True


def cmd_prepare(a):
    imgtool, gen, img = a.imgtool, a.gen, a.img
    out = os.path.join(a.work, "src")
    tmp = os.path.join(a.work, "tmp_dump")
    for p in (imgtool, os.path.join(gen, "imgrom.txt"), os.path.join(gen, "bddpal.txt"),
              os.path.join(gen, "drawpal.txt")):
        if not os.path.exists(p):
            die(f"{p} not found: build the game first (cmake --build build)")
    os.makedirs(tmp, exist_ok=True)
    subprocess.run([imgtool, "dump", img, tmp], check=True, capture_output=True)
    drawn = {}
    for ln in open(os.path.join(gen, "imgrom.txt")):
        p = ln.split()
        if not ln.startswith("#") and len(p) >= 6:
            drawn.setdefault(p[5].upper(), p[3].upper())
    items = []
    for e in json.load(open(os.path.join(tmp, "manifest.json"))):
        items.append((e["name"], e["lib"], e["width"], e["height"], e["anix"], e["aniy"], "sprite", "",
                      os.path.join(tmp, e["name"] + ".png"), None))
    have = {i[0].upper() for i in items}
    # images only the mods' own LODs load (the referee, Bam Bam's fireball)
    for lib in sorted({l for n, l in drawn.items() if n not in have and not l.endswith(".BDD")}):
        d = os.path.join(tmp, "mod_" + lib[:-4])
        os.makedirs(d, exist_ok=True)
        subprocess.run([imgtool, "export", os.path.join(img, lib), d], check=True, capture_output=True)
        info = imgtool_info(imgtool, os.path.join(img, lib))
        for n, l in drawn.items():
            if l == lib and n not in have and n in info and os.path.exists(os.path.join(d, n + ".png")):
                w, h, ax, ay = info[n]
                mod = "referee" if lib.startswith("REF") else "bamfire"
                items.append((n, lib, w, h, ax, ay, "sprite", mod, os.path.join(d, n + ".png"), None))
    # backgrounds, coloured with the palette the game draws each with (gen/bddpal.txt)
    for ln in open(os.path.join(gen, "bddpal.txt")):
        if ln.startswith("#"):
            continue
        p = ln.split()
        items.append((f"{p[0][:-4]}_{p[1]}", p[0], 0, 0, 0, 0, "background", "", None,
                      (p[0], int(p[1]), [rgb555(int(c, 16)) for c in p[3:]])))
    # wrestler frames carrying another palette than the one the game draws them with (gen/drawpal.txt)
    drawpal = {}
    for ln in open(os.path.join(gen, "drawpal.txt")):
        p = ln.split()
        if not ln.startswith("#") and len(p) == 2:
            drawpal[p[0].upper()] = p[1]
    bdd_cache = {}
    entries = []
    for name, lib, w, h, ax, ay, kind, mod, src, bg in items:
        if not LABEL_OK.match(name):
            continue
        cat, wrestler = category(lib, name, kind, mod)
        dest_dir = os.path.join(out, cat, lib.rsplit(".", 1)[0].upper())
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, name + ".png")
        if bg:
            f, idx, cols = bg
            if f not in bdd_cache:
                bdd_cache[f] = bdd_images(os.path.join(img, f))
            w, h, px = bdd_cache[f][idx]
            im = Image.new("RGBA", (w, h))
            im.putdata([(0, 0, 0, 0) if v == 0 else (*cols[v], 255) if v < len(cols) else (255, 0, 255, 255)
                        for v in px])
            im.save(dest)
        elif not (name.upper() in drawpal and draw_palette_png(img, lib, name, drawpal[name.upper()], dest)):
            with open(src, "rb") as fi, open(dest, "wb") as fo:
                fo.write(fi.read())
        m = re.match(r"^([A-Za-z0-9_]+?)(\d\d)$", name)
        seq = f"{lib.rsplit('.', 1)[0].upper()}/{m.group(1)}" if m and kind == "sprite" else name
        entries.append({"label": name, "file": os.path.relpath(dest, out), "kind": kind, "category": cat,
                        "wrestler": wrestler, "library": lib, "width": w, "height": h,
                        "anchor_x": ax, "anchor_y": ay, "sequence": seq})
    json.dump({"scale": K, "images": entries}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    counts = {}
    for e in entries:
        counts[e["category"]] = counts.get(e["category"], 0) + 1
    print(f"prepared {len(entries)} images in {out}")
    for c in sorted(counts):
        print(f"  {c}: {counts[c]}")


# ---- run: through ComfyUI ---------------------------------------------------------------------------------

def load_manifest(a):
    path = os.path.join(a.work, "src", "manifest.json")
    if not os.path.exists(path):
        die("no manifest: run `prepare` first")
    return json.load(open(path))["images"]


def selected(entries, a):
    out = []
    for e in entries:
        if e["category"] == "unused":
            continue
        if e["category"] == "fonts" and not a.fonts and not a.only:
            continue
        if a.only:
            words = [w.strip().lower() for w in a.only.split(",")]
            hay = " ".join([e["label"], e["category"], e["library"], e["sequence"]]).lower()
            if not any(w in hay for w in words):
                continue
        out.append(e)
    order = {"wrestlers": 0, "backgrounds": 1, "other": 2, "mods": 3, "fonts": 4}
    out.sort(key=lambda e: (order.get(e["category"].split("/")[0], 9), e["category"], e["sequence"], e["label"]))
    return out[: a.limit] if a.limit else out


def prompt_for(e):
    cat = e["category"]
    if cat.startswith("wrestlers/"):
        subject = WRESTLERS[e["wrestler"]][1] + ", full body, isolated on a plain grey background"
    elif cat == "backgrounds":
        subject = ("1995 WWF wrestling arena, audience in the stands, wrestling ring, barriers, arena lighting"
                   if e["library"].startswith("NEWRINGB") else "1995 WWF arcade game title and menu screen art")
    elif cat == "fonts":
        subject = "a single bold arcade game font character, clean solid shape, isolated on a plain grey background"
    elif cat.startswith("mods/referee"):
        subject = "a wrestling referee in a striped shirt, full body, isolated on a plain grey background"
    else:
        lib = e["library"].upper()
        if lib.startswith("CROWD") or lib in ("STANDS.IMG",):
            subject = "spectators at a 1995 wrestling event, people in the crowd"
        elif lib in ("WWFMUGS.IMG",):
            subject = "portrait photo of a professional wrestler"
        else:
            subject = "an element of a 1995 wrestling arcade game, isolated on a plain grey background"
    return f"{subject}, {STYLE}"


def box_blur(arr, r):
    """Mean over a (2r+1) square, same size (numpy, summed-area table)."""
    p = np.pad(arr, r, mode="edge").astype(np.float64)
    c = p.cumsum(0).cumsum(1)
    c = np.pad(c, ((1, 0), (1, 0)))
    n = 2 * r + 1
    return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / (n * n)


def fill_transparent(im):
    """RGB with the transparent pixels filled from their neighbours (no grey or black halos at the edges)."""
    a = (np.asarray(im.getchannel("A")) >= 128).astype(np.float64)
    rgb = np.asarray(im.convert("RGB"), dtype=np.float64)
    known = a > 0
    out = rgb.copy()
    r = 1
    while not known.all() and known.any() and r < 1024:
        wsum = box_blur(a, r)
        new = (wsum > 1e-6) & ~known
        for c in range(3):
            acc = box_blur(rgb[..., c] * a, r)
            out[..., c][new] = acc[new] / wsum[new]
        known |= new
        r *= 2
    out[~known] = 128
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGB")


def work_size(w, h):
    tw, th = w * K, h * K
    long_side = max(tw, th)
    s = 1024 / long_side if long_side < 1024 else (1536 / long_side if long_side > 1536 else 1.0)
    return max(64, int(round(tw * s / 8)) * 8), max(64, int(round(th * s / 8)) * 8)


class Comfy:
    def __init__(self, url):
        self.url = url.rstrip("/")
        self.client = str(uuid.uuid4())

    def _req(self, path, data=None, headers=None, timeout=60):
        req = urllib.request.Request(self.url + path, data=data, headers=headers or {})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()

    def check(self):
        try:
            self._req("/system_stats", timeout=5)
        except (urllib.error.URLError, OSError) as ex:
            die(f"ComfyUI is not answering at {self.url} ({ex}); start it first (docs/REMASTER.md)")

    def upload(self, name, png_bytes):
        boundary = "----wwf" + uuid.uuid4().hex
        body = b"".join([
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\n"
            f"Content-Type: image/png\r\n\r\n".encode(), png_bytes, b"\r\n",
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n".encode(),
            f"--{boundary}--\r\n".encode()])
        r = json.loads(self._req("/upload/image", body, {"Content-Type": f"multipart/form-data; boundary={boundary}"}))
        return r["name"] if not r.get("subfolder") else f"{r['subfolder']}/{r['name']}"

    def run(self, graph, timeout):
        body = json.dumps({"prompt": graph, "client_id": self.client}).encode()
        pid = json.loads(self._req("/prompt", body, {"Content-Type": "application/json"}))["prompt_id"]
        t0 = time.time()
        while time.time() - t0 < timeout:
            hist = json.loads(self._req(f"/history/{pid}"))
            if pid in hist:
                h = hist[pid]
                if h.get("status", {}).get("status_str") == "error":
                    raise RuntimeError(f"ComfyUI error: {json.dumps(h.get('status'))[:400]}")
                for node in h.get("outputs", {}).values():
                    for im in node.get("images", []):
                        q = urllib.parse.urlencode({"filename": im["filename"], "subfolder": im.get("subfolder", ""),
                                                    "type": im.get("type", "output")})
                        return self._req(f"/view?{q}")
                raise RuntimeError("ComfyUI finished without an image")
            time.sleep(0.5)
        raise RuntimeError("ComfyUI timed out")


def build_graph(template, **kw):
    text = json.dumps(template)
    for k, v in kw.items():
        text = text.replace(f'"%{k}%"', json.dumps(v))
    return json.loads(text)


def outline(orig, size):
    """The original's outline at the target size: smoothed, then cut at half (alpha 0 or 255)."""
    a = orig.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
    a = a.resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(K * 0.6))
    return a.point(lambda v: 255 if v >= 128 else 0)


def soften(orig_filled, size):
    """The original enlarged without its pixel steps: bicubic, then blurred by about half an original pixel, so the
    model (and the tile ControlNet, which copies what it is shown) sees shapes to redraw, not blocks to keep."""
    step = size[0] / max(orig_filled.width, 1)
    return orig_filled.resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(step * 0.5))


def keep_color(gen, orig_rgb, size, strength, radius_px):
    """The original's colours and lighting with the generated detail on top. Only the broad colour is taken from the
    original (a blur of `radius_px` original pixels), so its blocks and blotches do not come back."""
    radius = K * radius_px
    g = np.asarray(gen, dtype=np.float32)
    g_low = np.asarray(gen.filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32)
    o_low = np.asarray(orig_rgb.resize(size, Image.BICUBIC).filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32)
    mixed = g + strength * (o_low - g_low)
    return Image.fromarray(np.clip(mixed, 0, 255).astype(np.uint8), "RGB")


def cmd_run(a):
    entries = selected(load_manifest(a), a)
    template = json.load(open(a.workflow))
    two_pass = "%upscale_model%" in json.dumps(template)   # the workflow upscales first (Real-ESRGAN), then SDXL
    comfy = Comfy(a.url)
    comfy.check()
    out_dir = os.path.join(a.work, "out")
    os.makedirs(out_dir, exist_ok=True)
    todo = [e for e in entries if a.redo or not os.path.exists(os.path.join(out_dir, e["label"] + ".png"))]
    print(f"{len(entries)} selected, {len(entries) - len(todo)} already done, {len(todo)} to do")
    t_start = time.time()
    for n, e in enumerate(todo, 1):
        t0 = time.time()
        orig = Image.open(os.path.join(a.work, "src", e["file"])).convert("RGBA")
        size = (orig.width * K, orig.height * K)
        ws = work_size(orig.width, orig.height)
        # two-pass: the original itself (the upscale model enlarges it sharply); otherwise enlarged and softened
        src = fill_transparent(orig) if two_pass else soften(fill_transparent(orig), ws)
        buf = io.BytesIO()
        src.save(buf, "PNG")
        denoise, cn = (DEFAULTS if two_pass else SOFT_DEFAULTS)[e["category"].split("/")[0]]
        if a.denoise is not None:
            denoise = a.denoise
        if a.control is not None:
            cn = a.control
        seed = zlib.crc32(e["sequence"].encode()) + a.seed
        try:
            name = comfy.upload(f"wwf_{e['label']}.png", buf.getvalue())
            graph = build_graph(template, image=name, positive=prompt_for(e), negative=NEGATIVE, seed=seed,
                                denoise=denoise, control_strength=cn, steps=a.steps, cfg=a.cfg,
                                checkpoint=a.checkpoint, controlnet=a.controlnet, upscale_model=a.upscale_model,
                                width=ws[0], height=ws[1])
            png = comfy.run(graph, a.timeout)
        except (RuntimeError, urllib.error.URLError, OSError, KeyError, ValueError) as ex:
            print(f"[{n}/{len(todo)}] {e['label']}: FAILED: {ex}")
            continue
        gen = Image.open(io.BytesIO(png)).convert("RGB").resize(size, Image.LANCZOS)
        if a.keep_color > 0:
            gen = keep_color(gen, fill_transparent(orig), size, a.keep_color, a.color_radius)
        res = gen.convert("RGBA")
        mask = outline(orig, size)
        res = Image.composite(res, Image.new("RGBA", size, (0, 0, 0, 0)), mask)
        res.putalpha(mask)
        res.save(os.path.join(out_dir, e["label"] + ".png"), optimize=True)
        left = (time.time() - t_start) / n * (len(todo) - n)
        print(f"[{n}/{len(todo)}] {e['label']} {time.time() - t0:.1f}s, about {left / 3600:.1f} h left", flush=True)


# ---- finish: check and pack for the game ------------------------------------------------------------------

def cmd_finish(a):
    entries = {e["label"].upper(): e for e in load_manifest(a)}
    out_dir = os.path.join(a.work, "out")
    files = sorted(f for f in os.listdir(out_dir) if f.lower().endswith(".png")) if os.path.isdir(out_dir) else []
    ok, problems = [], []
    for f in files:
        e = entries.get(f[:-4].upper())
        if not e:
            problems.append(f"{f}: not an image of the game")
            continue
        im = Image.open(os.path.join(out_dir, f))
        want = (e["width"] * K, e["height"] * K)
        if im.size != want:
            problems.append(f"{f}: {im.size[0]}x{im.size[1]}, must be {want[0]}x{want[1]}")
            continue
        if im.mode != "RGBA":
            problems.append(f"{f}: not RGBA")
            continue
        alpha = set(im.getchannel("A").getextrema())
        if not alpha <= {0, 255}:
            problems.append(f"{f}: alpha must be 0 or 255")
            continue
        ok.append(f)
    for p in problems:
        print(p)
    os.makedirs(a.dest, exist_ok=True)
    if a.flat:        # the old form: parts of a flat list, no layers
        part, size, z = 0, 0, None
        for f in ok:
            p = os.path.join(out_dir, f)
            n = os.path.getsize(p)
            if z is None or size + n > 900 * 1024 * 1024:
                if z:
                    z.close()
                part += 1
                size = 0
                z = zipfile.ZipFile(os.path.join(a.dest, f"{a.name}_part{part:02d}.zip"), "w", zipfile.ZIP_STORED)
            z.write(p, f)
            size += n
        if z:
            z.close()
        print(f"{len(ok)} images packed into {part} zip file(s) in {a.dest} ({a.name}_partNN.zip); "
              f"{len(problems)} left out")
        return
    # one zip per art layer, each holding its layer folder (docs/ASSET_OVERRIDES.md, "Layers")
    layers = al.layer_map(a.imgtool, a.img)
    made = al.pack_files([os.path.join(out_dir, f) for f in ok], a.dest, layers, 900, prefix=f"{a.name}_", report=False)
    nzip = sum(len(v) for v in made.values())
    print(f"{len(ok)} images packed into {nzip} zip file(s) in {a.dest} ({a.name}_<layer>.zip, one per art layer); "
          f"{len(problems)} left out")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=os.path.join(ROOT, "art", "remaster"), help="working folder")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare", help="export every image the game draws")
    p.add_argument("--imgtool", default=os.path.join(ROOT, "build", "imgtool"))
    p.add_argument("--gen", default=os.path.join(ROOT, "build", "gen"))
    p.add_argument("--img", default=os.path.join(ROOT, "orig", "IMG"))
    p = sub.add_parser("run", help="remaster through ComfyUI")
    p.add_argument("--url", default="http://127.0.0.1:8188")
    p.add_argument("--workflow", default=os.path.join(HERE, "sdxl_esrgan_tile.json"),
                   help="sdxl_esrgan_tile.json (default: Real-ESRGAN 4x, then SDXL) or sdxl_tile.json (SDXL only)")
    p.add_argument("--upscale-model", default="RealESRGAN_x4plus.pth", help="file in ComfyUI's models/upscale_models")
    p.add_argument("--checkpoint", default="RealVisXL_V5.0_fp16.safetensors")
    p.add_argument("--controlnet", default="xinsir-controlnet-tile-sdxl-1.0.safetensors")
    p.add_argument("--only", help="comma-separated words matched against label, category, library, animation")
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--fonts", action="store_true", help="include the fonts (left out unless named in --only)")
    p.add_argument("--redo", action="store_true", help="redo images already in out/")
    p.add_argument("--denoise", type=float, help="0..1, how much the model may change (per-category default)")
    p.add_argument("--control", type=float, help="ControlNet strength (per-category default)")
    p.add_argument("--keep-color", type=float, default=1.0, help="0..1, original colours and lighting (0 = off)")
    p.add_argument("--no-keep-color", dest="keep_color", action="store_const", const=0.0)
    p.add_argument("--color-radius", type=float, default=3.0,
                   help="how coarse the colour taken from the original is, in original pixels (larger = smoother)")
    p.add_argument("--steps", type=int, default=28)
    p.add_argument("--cfg", type=float, default=5.0)
    p.add_argument("--seed", type=int, default=0, help="added to every animation's seed")
    p.add_argument("--timeout", type=float, default=600)
    p = sub.add_parser("finish", help="check the results and write stored zips for the game, one per art layer")
    p.add_argument("--dest", default=os.path.join(ROOT, "art", "hd"))
    p.add_argument("--name", default="remaster")
    p.add_argument("--flat", action="store_true", help="the old form: remaster_partNN.zip of a flat list, no layers")
    p.add_argument("--imgtool", default=os.path.join(ROOT, "build", "imgtool"))
    p.add_argument("--img", default=os.path.join(ROOT, "orig", "IMG"))
    a = ap.parse_args()
    {"prepare": cmd_prepare, "run": cmd_run, "finish": cmd_finish}[a.cmd](a)


if __name__ == "__main__":
    main()
