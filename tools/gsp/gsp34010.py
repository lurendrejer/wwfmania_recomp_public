"""TMS34010 operand parsing and instruction sizes.

Sizes must match the real encodings: the multitasker computes return
addresses with GETPC + a constant (the JSRP macro in MPROC.EQU), so code
layout has to be exact where such tricks are used.
"""

import re

from gspexpr import REGISTERS, Undefined, evaluate, parse_expr

# B-file registers used by the graphics instructions (PIXBLT, FILL, LINE).
REG_ALIASES = {
    "saddr": "b0", "sptch": "b1", "daddr": "b2", "dptch": "b3", "offset": "b4",
    "wstart": "b5", "wend": "b6", "dydx": "b7", "color0": "b8", "color1": "b9",
    "count": "b10", "inc1": "b11", "inc2": "b12", "pattrn": "b13",
}
# Operand keywords that are not symbols: PIXBLT modes, field sizes.
OPERAND_KEYWORDS = {"b", "l", "w", "xy"}

_REG = re.compile(r"^(a1[0-4]|a[0-9]|b1[0-4]|b[0-9]|sp)$", re.I)


def is_reg(text):
    return bool(_REG.match(text.strip()))


def parse_operand(text):
    """Returns a tuple describing one operand.

    ("reg", r) ("ind", r) ("inc", r) ("dec", r) ("disp", r, expr_text)
    ("xy", r) for *Rn.XY, ("abs", expr_text), ("imm", expr_text)
    """
    t = text.strip()
    if is_reg(t):
        return ("reg", t.lower())
    if t.startswith("-*") or t.startswith("*-"):
        r = t[2:].strip()
        if is_reg(r):
            return ("dec", r.lower())
    if t.startswith("*"):
        body = t[1:].strip()
        if body.endswith("+") and is_reg(body[:-1]):
            return ("inc", body[:-1].strip().lower())
        if is_reg(body):
            return ("ind", body.lower())
        m = re.match(r"^(a1[0-4]|a[0-9]|b1[0-4]|b[0-9]|sp)\s*\((.*)\)$", body, re.I)
        if m:
            return ("disp", m.group(1).lower(), m.group(2).strip())
        m = re.match(r"^(a1[0-4]|a[0-9]|b1[0-4]|b[0-9]|sp)\.xy$", body, re.I)
        if m:
            return ("xy", m.group(1).lower())
    if t.startswith("@"):
        return ("abs", t[1:].strip())
    return ("imm", t)


SIZE16 = set("""
abs add addc addk addxy and andn btst clrc cmp cmpxy cpw cvxyl dec dint divs divu
drav eint emu exgf exgpc fill getpc getst inc jump lmo mods modu movk movx movy
mpys mpyu neg negb nop not or pixt pixblt popst pushst putst reti rets rev rl setc
setf sext sla sll sra srl sub subb subk subxy trap xor zext clr line clip dsjs call
""".split())

SIZE32 = {"mmfm", "mmtm", "callr", "dsj", "dsjeq", "dsjne"}
SIZE48 = {"calla", "andi", "andni", "ori", "xori"}
IMM_WL = {"movi", "addi", "subi", "cmpi"}   # 32 (IW) or 48 (IL)

JR_CONDS = set("""uc p ls hi lt ge le gt c b nc nb lo hs eq z ne nz v nv n nn
yz ynz yn ynn xz xnz xn xnn yv ynv xv xnv yle ygt xle xgt""".split())
JUMPS_REL = {"jr" + c for c in JR_CONDS}
JUMPS_ABS = {"ja" + c for c in JR_CONDS}

KNOWN = SIZE16 | SIZE32 | SIZE48 | IMM_WL | JUMPS_REL | JUMPS_ABS | {"move", "movb"}


def _fits16(v):
    return -32768 <= v <= 32767


def imm_size(ops, lookup):
    """Size of MOVI/ADDI/SUBI/CMPI: short when the value is known and fits."""
    if len(ops) >= 3:
        f = ops[2].strip().lower()
        if f == "w":
            return 32
        if f == "l":
            return 48
    try:
        v = evaluate(parse_expr(ops[0]), lookup)
    except Undefined:
        return 48
    return 32 if _fits16(v) else 48


def move_size(ops):
    size = 16
    for op in ops[:2]:
        kind = parse_operand(op)[0]
        if kind == "disp":
            size += 16
        elif kind == "abs":
            size += 32
    return size


def fixed_size(mn, ops, lookup):
    """Size in bits, or None for relative jumps (sized by relaxation)."""
    if mn in JUMPS_REL:
        return None
    if mn in SIZE16:
        return 16
    if mn in SIZE32:
        return 32
    if mn in SIZE48 or mn in JUMPS_ABS:
        return 48
    if mn in IMM_WL:
        return imm_size(ops, lookup)
    if mn in ("move", "movb"):
        return move_size(ops)
    return 16
