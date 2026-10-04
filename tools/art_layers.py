"""Shared by the art tools (upscale.py, ai_upscale.py, art_pack.py, remaster/remaster.py): sorting art overrides into
the art layers of src/assets/layers.c and packing them into one zip per layer, with the layer's folder inside the zip
(docs/ASSET_OVERRIDES.md, "Layers").

The layer of an image comes from `imgtool layers <imgdir> list`, so the C code is the only place that knows the grouping.
Names that are not in the catalog and look like a background piece (<BDD>_<n>) go to backgrounds/, anything else to other/.

The game reads a zip at any folder depth (only file names count), but it does NOT read loose files in sub folders, so
a layered set is meant to be zips; loose layer folders are for editing and have to be zipped or flattened for the game.
Standard library only.
"""
import os
import re
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def add_arguments(ap):
    """The options every tool shares for the layer layout."""
    ap.add_argument("--layers", action="store_true",
                    help="write <out>/<layer>/<LABEL>.png (loose sub folders are for editing: the game only reads them zipped)")
    ap.add_argument("--zip", metavar="DIR",
                    help="also pack the result into one zip per layer in DIR (wrestlers_undertaker.zip, hud.zip, ...), "
                         "each holding its layer folder; a zip is split above --max-zip-mb")
    ap.add_argument("--max-zip-mb", type=int, default=900,
                    help="split a layer's zip above this size; the game reads zips below 2 GB (default 900)")
    ap.add_argument("--imgtool", default=os.path.join(ROOT, "build", "imgtool"), help="the imgtool program for the layers")
    ap.add_argument("--img", default=os.path.join(ROOT, "orig", "IMG"), help="the IMG directory for the layers")


def layer_map(imgtool, img_dir):
    """{LABEL: layer folder} for every image of the catalog."""
    try:
        out = subprocess.run([imgtool, "layers", img_dir, "list"], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("cannot get the layers from %s (is it built, and is %s the IMG directory?): %s" % (imgtool, img_dir, e))
    m = {}
    for line in out.splitlines():
        if "\t" in line:
            name, folder = line.split("\t", 1)
            m[name.upper()] = folder.strip()
    if not m:
        sys.exit("no layers from %s %s" % (imgtool, img_dir))
    return m


def folder_of(stem, layers):
    """(layer folder, known): known is False for a name that is neither in the catalog nor a background piece."""
    f = layers.get(stem.upper())
    if f:
        return f, True
    if re.fullmatch(r"[A-Za-z0-9]+_\d+", stem):
        return "backgrounds", True
    return "other", False


class Layout:
    """Where an image goes. Built from the options added by add_arguments()."""

    def __init__(self, args):
        self.args = args
        self.layers = None
        if args.layers or args.zip:
            self.layers = layer_map(args.imgtool, args.img)

    def path(self, out, label):
        """The file to write for LABEL under `out`: flat, or inside its layer folder with --layers."""
        if not self.args.layers:
            return os.path.join(out, label + ".png")
        folder, _ = folder_of(label, self.layers)
        d = os.path.join(out, *folder.split("/"))
        os.makedirs(d, exist_ok=True)
        return os.path.join(d, label + ".png")

    def finish(self, out, prefix=""):
        """With --zip: packs the PNGs under `out` (flat or layered) into the layer zips."""
        if self.args.zip:
            pack_dir(out, self.args.zip, self.layers, self.args.max_zip_mb, prefix)


def pngs_under(src):
    for root, _, files in os.walk(src):
        for f in sorted(files):
            if f.lower().endswith(".png"):
                yield os.path.join(root, f)


def pack_files(files, dest, layers, max_mb=900, prefix="", report=True):
    """Writes the PNGs in `files` as one stored zip per layer into `dest`, each entry as <layer folder>/<name>.png.
    Returns {layer folder: [zip names]}."""
    groups, unknown = {}, []
    for p in files:
        stem = os.path.splitext(os.path.basename(p))[0]
        folder, known = folder_of(stem, layers)
        if not known:
            unknown.append(os.path.basename(p))
        groups.setdefault(folder, []).append(p)
    if unknown and report:
        print("warning: %d file(s) are not in the catalog and not background pieces, put in other/: %s%s"
              % (len(unknown), ", ".join(unknown[:5]), " ..." if len(unknown) > 5 else ""), file=sys.stderr)
    os.makedirs(dest, exist_ok=True)
    limit = max_mb * 1024 * 1024
    made = {}
    for folder in sorted(groups):
        part, size, zf = 0, 0, None
        base = prefix + folder.replace("/", "_")
        made[folder] = []
        for p in groups[folder]:
            n = os.path.getsize(p)
            if zf is None or (size + n > limit and size > 0):
                if zf:
                    zf.close()
                part += 1
                name = "%s%s.zip" % (base, "" if part == 1 else "_%d" % part)
                made[folder].append(name)
                zf = zipfile.ZipFile(os.path.join(dest, name), "w", zipfile.ZIP_STORED)   # PNG is compressed already
                size = 0
            zf.write(p, "%s/%s" % (folder, os.path.basename(p)))
            size += n
        if zf:
            zf.close()
        if report:
            print("%-24s %5d files -> %s" % (folder, len(groups[folder]), ", ".join(made[folder])))
    return made


def pack_dir(src, dest, layers, max_mb=900, prefix=""):
    return pack_files(list(pngs_under(src)), dest, layers, max_mb, prefix)
