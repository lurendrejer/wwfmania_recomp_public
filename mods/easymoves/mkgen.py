#!/usr/bin/env python3
"""Writes mods/easymoves/gen.txt and the table in README.md from the original source.

    python3 mods/easymoves/mkgen.py [orig-dir]

Every special move gets a second, easy way in: Toward, Toward + a button, and when the button is
taken by another move of the same wrestler, Down, Down + a button. Order of the slots:

    Toward Toward + super kick, kick, punch        (+ super punch is the grab, left to it)
    Down   Down   + super kick, kick, punch, super punch

Three kinds of move are found in each wrestler's file:
  * the records of the secret move table (*_secret_moves): the moves from standing;
  * the special move processes of *_smove_table that only work in a head hold (the high risk
    moves, and the reversal when the hold is on you): their group is separate;
  * the other processes of that table (throws of a jumping opponent, the spirits and so on):
    they share the standing group with the records.
Moves that already have such a sequence keep it; moves that start the same way (the same motion and
button) share one new sequence, because the game decides between them by the state of the match.
"""
import os
import re
import sys

ORIG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "orig")
HERE = os.path.dirname(os.path.abspath(__file__))

WRESTLERS = [("Bret Hart", "BRET"), ("Razor Ramon", "RAZOR"), ("Undertaker", "TAKER"), ("Yokozuna", "YOKO"),
             ("Shawn Michaels", "SHAWN"), ("Bam Bam", "BAM"), ("Doink", "DOINK"), ("Lex Luger", "LEX")]
SLOTS = [("TT", "B_SKICK"), ("TT", "B_KICK"), ("TT", "B_PUNCH"), ("DD", "B_SKICK"), ("DD", "B_KICK"),
         ("DD", "B_PUNCH"), ("DD", "B_SPUNCH")]
RESERVED = ("TT", "B_SPUNCH")            # the grab
JOY = {"TT": "J_TOWARD", "DD": "J_DOWN"}
BTN = {"B_SKICK": "super kick", "B_KICK": "kick", "B_PUNCH": "punch", "B_SPUNCH": "super punch"}
NAME = {"TT": "Toward, Toward", "DD": "Down, Down"}
SHORT = {"J_TOWARD": "T", "J_AWAY": "A", "J_DOWN": "D", "J_UP": "U", "J_DOWN_TOWARD": "DT", "J_DOWN_AWAY": "DA",
         "J_UP_TOWARD": "UT", "J_UP_AWAY": "UA"}


def read(name):
    with open(os.path.join(ORIG, name + ".ASM"), encoding="latin-1") as f:
        return f.read().replace("\r", "").split("\n")


def code(line):
    return line.split(";")[0].rstrip()


def block(lines, label):
    """The lines of the definition of `label` (a record or a process)."""
    head = re.compile(r"^(\s*SUBRP?\s+)?%s\s*$" % re.escape(label), re.I)
    stop = re.compile(r"^\s*SUBRP?\s+\S|^[A-Za-z_]\w*\s*$|^#" + re.escape("") + r"$")
    for i, ln in enumerate(lines):
        if head.match(ln):
            j = i + 1
            while j < len(lines) and not (re.match(r"^\s*SUBRP?\s+\S", lines[j]) or re.match(r"^[A-Za-z_]\w*\s*$", lines[j])
                                          or (label.startswith("#") and re.match(r"^#\w+\s*$", lines[j]))):
                j += 1
            return lines[i + 1:j]
    return None


def table(lines, suffix):
    for i, ln in enumerate(lines):
        if re.match(r"^\s*SUBR\s+\w+_%s\s*$" % suffix, ln) or re.match(r"^\w+_%s\s*$" % suffix, ln):
            names = []
            for l2 in lines[i + 1:i + 60]:
                c = code(l2).strip()
                if not c or l2.strip().startswith(";") or c.startswith(".if") or c.startswith(".endif"):
                    continue
                m = re.match(r"\.long\s+(\S+)$", c)
                if not m:
                    break
                if m.group(1) == "0":
                    break
                names.append(m.group(1))
            return names
    return []


def slot_of(motion, btn):
    for k, (m, b) in enumerate(SLOTS):
        if btn == b and len(motion) == 2 and motion[0] == motion[1] == JOY[m]:
            return k
    return None


def assign(items, keep_grab):
    """items: list of dicts with 'key' = (motion tuple, button). Returns key -> slot index."""
    keys = []
    for it in items:
        if it["key"] not in keys:
            keys.append(it["key"])
    taken = {}
    for k in keys:
        s = slot_of(*k)
        if s is not None:
            taken[k] = s
    used = set(taken.values())
    result = dict(taken)
    for k in keys:
        if k in result or (keep_grab and k[1] == "B_SPUNCH" and k[0] == ("J_TOWARD", "J_TOWARD")):
            continue
        free = next((s for s in range(len(SLOTS)) if s not in used), None)
        if free is None:
            continue
        used.add(free)
        result[k] = free
    return result


def alias_code(new_btn, orig_btn):
    """Code (for a gen.txt replacement text) that makes presses of new_btn count as presses of orig_btn too."""
    if new_btn == orig_btn:
        return ""
    return ("\\tPUSH\\ta0,a1\\n\\tmovi\\t" + new_btn.replace("B_", "PLAYER_") + "_VAL,a0\\n\\tmovi\\t"
            + orig_btn.replace("B_", "PLAYER_") + "_VAL,a1\\n\\tcalla\\teasy_alias\\n\\tPULL\\ta0,a1\\n")


def motion_text(motion, btn):
    return ",".join(SHORT.get(m, m) for m in motion) + " + " + BTN[btn.replace("B_", "B_")]


def main():
    out = ["""# Easy special moves: Toward, Toward + a button for every special move, Down, Down + a button when that
# is taken (mods/easymoves/README.md). Written by mods/easymoves/mkgen.py; do not edit by hand.
# The extra records and processes are there all the time; they do nothing unless the mod has set
# easy_enabled, so the game is the original without the mod.
srcdir asm
module EASYMOVES
"""]
    rows = []
    for title, fn in WRESTLERS:
        lines = read(fn)
        prefix = fn
        rec_table = table(lines, "secret_moves")
        proc_table = table(lines, "smove_table")
        records, procs = [], []
        for ref in rec_table:
            if not ref.startswith("#"):
                continue
            body = block(lines, ref)
            if body is None:
                continue
            words = []
            routine = None
            for l2 in body:
                c = code(l2).strip()
                if not c:
                    continue
                m = re.match(r"\.word\s+(.+)$", c)
                if m:
                    words.append([x.strip() for x in m.group(1).split(",", 1)])
                    continue
                m = re.match(r"\.long\s+(\S+)$", c)
                if m:
                    routine = m.group(1)
                    break
                break
            if routine is None or len(words) < 3:
                continue
            trig = words[0]
            if not trig[0].startswith("B_") or "|" in trig[0] or trig[1] != "J_ALL":
                continue
            joys = [w[0] for w in words[1:-1]]
            joys = ["J_DOWN_TOWARD" if sorted(j.replace(" ", "").split("|")) == ["J_DOWN", "J_TOWARD"] else j for j in joys]
            if any("|" in j or j.startswith("B_") for j in joys):
                continue
            records.append({"kind": "rec", "ref": ref, "routine": routine, "key": (tuple(joys), trig[0])})
        for pname in proc_table:
            if re.search(r"charge|finish|^std_", pname):
                continue
            body = block(lines, pname)
            if body is None:
                continue
            waits = [l2 for l2 in body if "WAITSWITCH_DWN" in l2 and not l2.strip().startswith(";")]
            if not waits:
                continue
            seq = []
            for w in waits:
                m = re.match(r"\s*WAITSWITCH_DWN\s+([^,\s]+)\s*,", w)
                seq.append(m.group(1))
            # the first run: joystick steps then the button
            run = []
            for a in seq:
                run.append(a)
                if a.startswith("B_"):
                    break
            if len(run) != 3 or not run[2].startswith("B_") or run[0].startswith("B_") or run[1].startswith("B_"):
                continue
            hold = "hdhold" in pname                # they wait for the head hold (the others only look at it)
            procs.append({"kind": "proc", "name": pname, "hold": hold, "key": ((run[0], run[1]), run[2])})
        groups = {"stand": [i for i in records] + [p for p in procs if not p["hold"]],
                  "hold": [p for p in procs if p["hold"]]}
        for gname, items in groups.items():
            res = assign(items, gname == "stand")
            for it in items:
                k = it["key"]
                s = res.get(k)
                slot_form = slot_of(*k) is not None
                if s is None or slot_form:
                    if s is None and not (gname == "stand" and k[1] == "B_SPUNCH" and k[0] == ("J_TOWARD", "J_TOWARD")):
                        rows.append((title, gname, it, None))
                    continue
                m, b = SLOTS[s]
                it["new"] = (m, b)
                rows.append((title, gname, it, (m, b)))
        wfile = fn + ".ASM"
        for title2, gname, it, new in [r for r in rows if r[0] == title]:
            if new is None:
                continue
            m, b = new
            if it["kind"] == "rec":
                l = it["ref"][1:]
                rep = (f"#{l}_easy\\n\\t.word\\t{b},\\t\\tJ_ALL\\n\\t.word\\t{JOY[m]},\\t\\tJ_REAL_LR\\n\\t.word\\t{JOY[m]},\\t\\tJ_REAL_LR\\n"
                       f"\\t.word\\t8000h | 32\\n\\t.long\\t#{l}_erun\\n#{l}_erun\\n\\tmove\\t@easy_enabled,a0\\n\\tjrz\\t#{l}_eout\\n"
                       f"{alias_code(b, it['key'][1])}\\tjruc\\t{it['routine']}\\n#{l}_eout\\n\\trets\\n#{l}")
                out.append(f"edit\t{wfile}\tsub\t^#{l}\\s*$\t{rep}\n")
                out.append(f"edit\t{wfile}\tafter\t^\\t\\.long\\t#{l}$\t\\t.long\\t#{l}_easy\n")
            else:
                n = it["name"]
                out.append(f"edit\t{wfile}\tclone\t{n}\t{n}_easy {JOY[m]} {b} {it['key'][1]}\n")
                out.append(f"edit\t{wfile}\tafter\t^\\t\\.long\\t{n}$\t\\t.long\\t{n}_easy\n")
    out.append("""# Repeating moves (Doink's hammer, Lex's morning star) repeat on the kick button in the original;
# with the mod it is the super kick they start with. The check reads KICKB_COUNT, so the count of
# super kicks is copied over it first.
""")
    for f in ("DNKSEQ2.ASM", "LEXSEQ2.ASM"):
        out.append(f"edit\t{f}\tsub\t^(\\tWWWL\\tANI_IF_BUTCOUNT_LT,KICKB_COUNT,[12],#(missedb|failed))$\t\\tWL\\tANI_CODE,easy_sync_kick\\n\\1\n")
    out.append("""# A move started with another button than in the original repeats on the button it is started with: presses of it
# count as presses of the original button too (easy_alias, EASYMOVES.ASM), in count_button_presses.
""")
    out.append("edit\tWRESTLE.ASM\tsub\t{3}^\\tcalla\\twres_get_but_val_down$\t\\tcalla\\twres_get_but_val_down\\n\\tcalla\\teasy_alias_mask\n")
    with open(os.path.join(HERE, "gen.txt"), "w") as f:
        f.write("".join(out))

    # README table
    table_lines = ["| Wrestler | Group | Move | Original | With the mod |", "|---|---|---|---|---|"]
    seen = set()
    for title, gname, it, new in rows:
        if new is None:
            name = it.get("ref", it.get("name"))
            table_lines.append(f"| {title} | {gname} | {name.lstrip('#')} | {motion_text(*it['key'])} | (no free combination, unchanged) |")
            continue
        name = it.get("ref", it.get("name")).lstrip("#")
        ident = (title, gname, name)
        if ident in seen:
            continue
        seen.add(ident)
        table_lines.append(f"| {title} | {'head hold' if gname == 'hold' else 'standing'} | {name} | {motion_text(*it['key'])} | {NAME[new[0]]} + {BTN[new[1]]} |")
    with open(os.path.join(HERE, "MOVES.md"), "w") as f:
        f.write("# Moves of the easymoves mod\n\nWritten by `mkgen.py`. Moves that already are Toward, Toward or Down, Down + a button "
                "are not listed (they keep it). \"head hold\": the high risk move when you hold, the reversal when you are held.\n\n"
                + "\n".join(table_lines) + "\n")
    print(len(rows), "moves,", len([r for r in rows if r[3]]), "given a new way in")


main()
