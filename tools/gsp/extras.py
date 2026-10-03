"""Extra pieces a mod adds to the build (mods/<name>/gen.txt, docs/MODS.md).

The generator normally builds exactly what WRESTLE.CMD and the LOD scripts
list. A mod that brings code and art the original build left out (Adam Bomb,
the referee) lists them in its gen.txt, one per line:

    module NAME        an assembler module (orig/NAME.ASM) added to the link
    lod NAME           a LOD script (orig/IMG/NAME.LOD) whose image tables are made
    wrestler-lod NAME  the same, with wrestler frame headers and NAME.SEQ
    srcdir DIR         a directory (relative to this file) searched for sources
                       before orig/, so a mod can carry its own version of a file
    edit FILE after PATTERN NEW      (fields separated by tabs; write a tab inside NEW
                       as \\t and a line break as \\n) inserts the line NEW
                       after the first line of FILE matching the regex PATTERN
                       ({N}regex: after the Nth)
    edit FILE sub PATTERN REPL       regex substitution on every line of FILE (REPL may hold \\n);
                       PATTERN written {N}regex only changes the Nth line that matches
    edit FILE slot N=M[!][@REGEX] PREFIX=PREFIX,NAME=NAME,...   (FILE may be *) fills a wrestler slot:
                       on every line commented ";N ..." in the table copy the
                       operands of the nearest earlier line commented ";M ..." and
                       rename them with the prefix/name pairs, but only names the
                       extra modules define; lines where nothing maps stay (with ! they are
                       copied all the same). With @REGEX only
                       the ";N" lines that match it. A prefix pair is written A_=B_ or A*=B*
                       (the latter for names without an underscore); NAME=NAME! renames whether
                       the extra modules define the new name or not. N and M may be words
                       (";Adam" lines from ";Razor" lines)
    edit FILE clone LABEL NEWLABEL JOY BUTTON [ORIGBUTTON]   puts a copy of the special move process LABEL after it,
                       named NEWLABEL, that waits for JOY, JOY, BUTTON instead of the original
                       sequence (every WAITSWITCH_DWN of it), and only does anything while the
                       variable easy_enabled is set (mods/easymoves). With ORIGBUTTON, after the
                       first BUTTON wait easy_alias is called, so that presses of BUTTON count
                       as presses of ORIGBUTTON too (the repeats of the original move look at that)
    edit FILE copyblock HEAD=FROM PREFIX=PREFIX,...   replaces the lines under every line matching
                       the regex HEAD (up to the next line that starts in column 0, blank lines
                       before it kept) by those under the nearest earlier line matching FROM,
                       renamed like slot does (tables with a block per wrestler: TABLES.ASM, and
                       the #Razor/#Adam blocks of the attack scripts)
    edit FILE cloneblock START:END PREFIX=PREFIX,...   puts a copy of the lines from the first one
                       matching the regex START up to the next matching END (not included) before
                       that line, renamed like slot does (A_=B_! renames every name with the
                       prefix, defined or not: the new labels of the copy)
    edit FILE tokslot OFF:BEFORE:AFTER:FROM PREFIX=PREFIX,...   in a run of data operands (.long,
                       .word, REFLONG lines, one after the other), every operand whose neighbours
                       match the regexes BEFORE and AFTER becomes the operand OFF places before it,
                       renamed like slot does, when that one matches FROM (tables by wrestler
                       without a comment per entry, or several entries on a line)
    script FILE        a Python script (relative to this file) that genimg.py runs first, as
                       `FILE --src ORIG --out GEN`: it may write modules (into GEN, which is
                       searched before orig/) and IMG libraries (into GEN/lod, where the image
                       tables and the runtime catalog look for libraries not in orig/IMG)
    include FILE       another gen.txt (relative to this file)

The edits change a copy in memory; the files in orig/ are never touched.

The extras are linked in after the original modules and are inactive until the
mod uses them, so the original game does not change.
"""

import os


def load_extras(path, out=None):
    first = out is None
    if first:
        out = {"module": [], "lod": [], "wrestler-lod": [], "srcdir": [], "script": [], "edits": {}}
    if not path:
        return out
    base = os.path.dirname(os.path.abspath(path))
    with open(path) as f:
        for raw in f:
            raw = raw.rstrip("\n")
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            if raw.startswith("edit\t"):
                parts = raw.split("\t")
                if len(parts) != 5 or parts[2] not in ("after", "sub", "slot", "clone", "copyblock", "tokslot", "cloneblock"):
                    raise ValueError(f"{path}: cannot read edit line '{raw}'")
                out["edits"].setdefault(parts[1].upper(), []).append((parts[2], parts[3], parts[4].replace("\\t", "\t").replace("\\n", "\n")))
                continue
            line = raw.split("#")[0].strip()
            kind, _, name = line.partition(" ")
            name = name.strip()
            if kind == "include":
                load_extras(os.path.join(base, name), out)
            elif kind == "script":
                f = os.path.normpath(os.path.join(base, name))
                if f not in out["script"]:
                    out["script"].append(f)
            elif kind == "srcdir":
                d = os.path.normpath(os.path.join(base, name))
                if d not in out["srcdir"]:
                    out["srcdir"].append(d)
            elif kind in ("module", "lod", "wrestler-lod"):
                if name.upper() not in out[kind]:
                    out[kind].append(name.upper())
            else:
                raise ValueError(f"{path}: cannot read line '{line}'")
    return out
