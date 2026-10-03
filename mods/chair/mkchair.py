#!/usr/bin/env python3
"""Writes mods/chair/asm/CHAIRSEQ.ASM from the chair scripts the original left commented out
(orig/*SEQ3.ASM, "CHAIR STUFF"). Run from the repository root: python3 mods/chair/mkchair.py

The scripts were a test loop: bend down, pick the chair up, swing it overhead and down, again and
again. Each is cut in two where the chair is first held overhead (CHAIR_SWING FR4):
  <w>_chair_up_anim     bend down and lift the chair; it stays attached, overhead, when the script
                        ends, and chair_grab marks it as held (the wrestler walks with it)
  <w>_chair_swing_anim  the rest: bring it down, then let go of it
and changed:
  - the ANI_GOTO back to the start is gone
  - the frames are 2 ticks (picking it up) and 3 (the swing) instead of 4
  - the attack (AMODE_HAYMAKER: the victim falls on his back) is on from the first frame the chair
    comes down in front (FR8 or FR9), for ATTACK_FRAMES frames
  - a whoosh as it comes down, and on a hit the chair crash, a screen shake and the commentator
  - with chair_keep set he lifts it overhead again (the end of the pick-up) instead of letting go
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "gsp"))
from imglib import ImgLib  # noqa: E402

WRESTLERS = [("hrt", "HRTSEQ3"), ("rzr", "RZRSEQ3"), ("und", "UNDSEQ3"), ("yok", "YOKSEQ3"),
             ("shn", "SHNSEQ3"), ("bam", "BAMSEQ3"), ("dnk", "DNKSEQ3"), ("lex", "LEXSEQ3")]

ATTACK_FRAMES = 3

HEAD = """\t.word\tANI_SETMODE,MODE_UNINT|MODE_NOAUTOFLIP
\t.word\tANI_SETPLYRMODE,MODE_NORMAL
\t.word\tANI_ZEROVELS
\t.word\tANI_SETSPEED,100h
\t.word\tANI_SETFACING
\t.word\tANI_SET_WRESTLER_XFLIP"""

UP_TAIL = """\tWL\tANI_CODE,chair_grab\t\t;he holds it now
\t.word\tANI_SETMODE,MODE_NORMAL
\t.word\tANI_END"""

SWING_HEAD = "\t.word\tANI_STARTATTACK,AT_HAYMAKER,12"

ATTACK_ON = """\t.word\tANI_SOUND,0Eh\t\t\t;whoosh
\t.word\tANI_ATTACK_ON,AMODE_HAYMAKER,20,20,90,110\t;mode,x,y,w,h"""

ATTACK_OFF = """\t.word\tANI_ATTACK_OFF
\tWL\tANI_IFNOTSTATUS,#missed_PFX
\tWL\tANI_IFBLOCKED,#missed_PFX
\t.word\tANI_SOUND,0C5h\t\t;chair crash
\t.word\tANI_SHAKER,20
\tWL\tANI_CODE,chair_hit
#missed_PFX"""

SWING_KEEP = """\tWL\tANI_CODE,chair_keep_status
\tWL\tANI_IFSTATUS,#again_PFX\t;he keeps it (the mod's number at 1)"""

SWING_TAIL = """\tWLW\tANI_ATTCHIMAGE,0,0\t\t;let go of the chair
\t.word\tANI_FACEDOWN
\t.word\tANI_SETMODE,MODE_NORMAL
\t.word\tANI_END"""


def read_loop(prefix, seqfile):
    """The script's lines between #lp and the ANI_GOTO, comment marks taken off."""
    text = open(os.path.join("orig", seqfile + ".ASM"), "rb").read().decode("latin-1").replace("\r", "")
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if re.match(rf"^; SUBR\t{prefix}_pkup_chair_anim", l))
    body = []
    for l in lines[start:]:
        l = l[1:] if l.startswith(";") else l
        if "ANI_GOTO" in l:
            break
        body.append(l)
    body = body[body.index("#lp") + 1:]
    return [l for l in body if l.strip() and "SINGLESTEP" not in l and "ANI_ATTCHIMAGE," not in l]


def script(prefix, seqfile):
    up, swing = [], []
    out = up
    attack, on_frames, done, overhead = False, 0, False, False
    frames = set()
    for l in read_loop(prefix, seqfile):
        m = re.match(r"^\tWL\t4,(\w+)\+(FR\d+)$", l)
        if m:
            frames.add(m.group(1))
            spd = "CHAIR_UPSPD" if out is up else "CHAIR_SPD"
            out.append(f"\tWL\t{spd},{m.group(1)}+{m.group(2)}")
            if overhead and out is up:
                out = swing                      # the rest is the swing
            if attack and not done:
                on_frames += 1
                if on_frames == ATTACK_FRAMES:
                    out.append(ATTACK_OFF.replace("PFX", prefix))
                    done = True
            continue
        m = re.match(r"^\tWLWWW\tANI_ATTCHIMAGE2,CHAIR_SWING\+(FR\d+),(-?\d+),(-?\d+),(\d+)", l)
        if not m:
            raise SystemExit(f"{seqfile}: cannot read '{l}'")
        fr = m.group(1)
        if fr == "FR4":
            overhead = True
        if not attack and fr in ("FR8", "FR9"):
            out.append(ATTACK_ON)
            attack = True
        out.append(f"\tWLWWW\tANI_ATTCHIMAGE2,CHAIR_SWING+{fr},{m.group(2)},{m.group(3)},{m.group(4)}")
    if not done or not swing:
        raise SystemExit(f"{seqfile}: no swing found")
    # keeping it: lift it overhead again with the last two frames of the pick-up
    attaches = [i for i, l in enumerate(up) if "ANI_ATTCHIMAGE2" in l]
    again = [f"#again_{prefix}"] + up[attaches[-2]:] + UP_TAIL.split("\n")
    src = f"from the commented out {prefix}_pkup_chair_anim in {seqfile}.ASM"
    text = "\n".join([f"*\n* {prefix}: {src}", "",
                      f" SUBR\t{prefix}_chair_up_anim", HEAD, *up, UP_TAIL, "",
                      f" SUBR\t{prefix}_chair_swing_anim", HEAD, SWING_HEAD, *swing, SWING_KEEP.replace("PFX", prefix),
                      SWING_TAIL, *again, ""])
    hold = re.match(r"^\tWL\t\w+,(\w+\+FR\d+)$", up[-1]).group(1)   # the frame he holds it overhead in
    return text, frames, hold


def joint(prefix):
    """Where the wrestler's walking legs facing the screen (orig/IMG/<W>_WLK.IMG, labels ?4WL*) join the
    torso, on average: (anchor x less the secondary point's x, anchor y less its y), rounded."""
    lib = ImgLib(os.path.join("orig", "IMG", prefix.upper() + "_WLK.IMG"))
    legs = [im for im in lib.images if re.match(r"^.4WL", im.name) and im.ani2[0] != -1]
    if not legs:
        raise SystemExit(f"{prefix.upper()}_WLK.IMG: no walking legs")
    dx = sum(im.anix - im.ani2[0] for im in legs) / len(legs)
    dy = sum(im.aniy - im.ani2[1] for im in legs) / len(legs)
    return round(dx), round(dy)


def main():
    parts, refs, holds = [], set(), []
    for prefix, seq in WRESTLERS:
        s, frames, hold = script(prefix, seq)
        parts.append(s)
        refs |= frames
        holds.append(hold)
    hdr = ["*" * 78,
           "* The chair: picking it up and swinging it (written by mods/chair/mkchair.py from",
           "* orig/*SEQ3.ASM; do not edit), included by CHAIR.ASM",
           "*" * 78, "",
           "\t.ref\t" + ",".join(sorted(refs)), "",
           "CHAIR_UPSPD\tequ\t2\t\t;ticks per frame picking the chair up (the test loops had 4)",
           "CHAIR_SPD\tequ\t3\t\t;and swinging it", "", ""]
    # by WRESTLERNUM: Bret, Razor, Taker, Yokozuna, Shawn, Bam Bam, Doink, (spare), Lex
    table = ["*", "* The frame each wrestler holds the chair overhead in (the end of the pick-up), by WRESTLERNUM:",
             "* drawn instead of his standing frame, and its upper part instead of his walking torso (CHAIR.ASM)", "",
             "chair_holds"] + [f"\t.long\t{h}" for h in holds[:7]] + ["\t.long\t0", f"\t.long\t{holds[7]}", ""]
    joints = [joint(prefix) for prefix, _ in WRESTLERS]
    jt = ["*", "* Where each wrestler's walking legs join his torso, on average (x, y up from the feet), by WRESTLERNUM:",
          "* the holding frame's waist, which CHAIR.ASM puts on the legs' secondary point", "",
          "chair_joints"] + [f"\t.word\t{x},{y}" for x, y in joints[:7]] + ["\t.word\t0,0",
                                                                         "\t.word\t%d,%d" % joints[7], ""]
    with open(os.path.join("mods", "chair", "asm", "CHAIRSEQ.ASM"), "w", newline="\n") as f:
        f.write("\n".join(hdr) + "\n".join(parts) + "\n".join(table) + "\n".join(jt))


if __name__ == "__main__":
    main()
