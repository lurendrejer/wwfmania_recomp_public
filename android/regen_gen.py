#!/usr/bin/env python3
"""Generates the recompiled game (what CMake does at configure time) with Python only, so that the Android build
does not need a C compiler or CMake on the desktop: writes <out>/extras.txt (the core changes and the mods, as
CMakeLists.txt does) and runs tools/gsp/genimg.py and gsp2c.py.

    python3 android/regen_gen.py [OUT_DIR]      (default: build/gen)
"""
import os
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "build", "gen")
orig = os.path.join(root, "orig")
if not os.path.exists(os.path.join(orig, "WRESTLE.CMD")):
    sys.exit("orig/ is not there: %s" % orig)

extras = os.path.join(os.path.dirname(out), "extras.txt")
os.makedirs(os.path.dirname(extras), exist_ok=True)
lines = ["include " + os.path.join(root, "src", "wolf", "core.gen.txt")]
mods = os.path.join(root, "mods")
for name in sorted(os.listdir(mods)):
    g = os.path.join(mods, name, "gen.txt")
    if os.path.isfile(g):
        lines.append("include " + g)
with open(extras, "w") as f:
    f.write("\n".join(lines) + "\n")

py = sys.executable
tools = os.path.join(root, "tools", "gsp")
print("generating the game into", out)
subprocess.check_call([py, os.path.join(tools, "genimg.py"), "--src", orig, "--out", out, "--extras", extras])
subprocess.check_call([py, os.path.join(tools, "gsp2c.py"), "--src", orig, "--gen", out, "--out", out, "--extras", extras])
if not os.path.exists(os.path.join(out, "c", "modules.cmake")):
    sys.exit("the generator did not produce c/modules.cmake")
print("done")
