#!/usr/bin/env python3
"""Packs a flat folder of art overrides (<LABEL>.png) into one zip per art layer, each zip holding its layer folder
(docs/ASSET_OVERRIDES.md, "Layers").

    python3 tools/art_pack.py art/hd art/layers                # art/layers/wrestlers_undertaker.zip, hud.zip, mugshots.zip, ...
    python3 tools/art_pack.py art/hd art/layers --folders      # loose folders instead: art/layers/wrestlers/undertaker/..., for editing

The tools that make the art (upscale.py, ai_upscale.py, remaster/remaster.py) can do the same while they write, with
--layers and --zip; this is for a set that already exists. The zips are what the game reads (`--art art/layers`): it finds the
images at any folder depth inside a zip, but it does not read loose files in sub folders. Standard library only.
"""
import argparse
import os
import shutil
import sys

import art_layers as al


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="folder of <LABEL>.png files (any folder depth)")
    ap.add_argument("out", help="folder to write the zips (or the layer folders) to")
    ap.add_argument("--folders", action="store_true", help="loose layer folders instead of zips (for editing)")
    ap.add_argument("--move", action="store_true",
                    help="with --folders: move the files instead of copying them (not while a conversion that resumes from SRC is running)")
    ap.add_argument("--max-zip-mb", type=int, default=900, help="split a layer's zip above this size (default 900)")
    ap.add_argument("--imgtool", default=os.path.join(al.ROOT, "build", "imgtool"))
    ap.add_argument("--img", default=os.path.join(al.ROOT, "orig", "IMG"))
    args = ap.parse_args()

    layers = al.layer_map(args.imgtool, args.img)
    files = list(al.pngs_under(args.src))
    if not files:
        sys.exit("no .png files under %s" % args.src)
    if args.move and not args.folders:
        ap.error("--move needs --folders (zips are written next to the files, which stay)")
    if not args.folders:
        al.pack_files(files, args.out, layers, args.max_zip_mb)
        return
    counts = {}
    for p in files:
        folder, _ = al.folder_of(os.path.splitext(os.path.basename(p))[0], layers)
        d = os.path.join(args.out, *folder.split("/"))
        os.makedirs(d, exist_ok=True)
        (shutil.move if args.move else shutil.copy2)(p, os.path.join(d, os.path.basename(p)))
        counts[folder] = counts.get(folder, 0) + 1
    for folder in sorted(counts):
        print("%-24s %5d files -> %s" % (folder, counts[folder], os.path.join(args.out, folder)))


if __name__ == "__main__":
    main()
