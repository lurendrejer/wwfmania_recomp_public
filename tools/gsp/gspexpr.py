"""Expression parsing and evaluation for TI GSPA (TMS34010 assembler) syntax.

Numbers: 123, 0c0h / 1A000C0H (hex suffix), >ff (hex prefix), 0x1f,
1010b (binary), 'A' (character). [y,x] packs two 16-bit halves into a long.
Operators (loosely C precedence): unary - ~ !, * / %, + -, << >>,
< <= > >=, = == != <>, &, ^, |.
"""

import re

REGISTERS = {f"a{i}" for i in range(15)} | {f"b{i}" for i in range(15)} | {"sp"}


class ExprError(Exception):
    pass


class Undefined(Exception):
    """Raised when evaluation needs a symbol that is not known yet."""

    def __init__(self, name):
        super().__init__(name)
        self.name = name


_TOKEN = re.compile(
    r"""\s*(?:
      (?P<num>0[xX][0-9a-fA-F]+|>[0-9a-fA-F]+|[0-9][0-9a-fA-F]*[hH]\b|[01]+[bB]\b|[0-9]+)
    | (?P<chr>'(?:[^'\\]|\\.)+')
    | (?P<str>"[^"]*")
    | (?P<id>[A-Za-z_$.#][A-Za-z0-9_$.#?]*|[A-Za-z_][A-Za-z0-9_?]*)
    | (?P<op><<|>>|<=|>=|==|!=|<>|[-+*/%&|^~!()<>=,\[\]])
    )""",
    re.X,
)


def tokenize(text):
    pos, out = 0, []
    text = text.rstrip()
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m or m.end() == pos:
            raise ExprError(f"bad character in expression: {text[pos:]!r}")
        kind = m.lastgroup
        val = m.group(kind)
        # '>' is a hex prefix only where an operand is expected ("movk >f,a0"),
        # otherwise it is the greater-than operator.
        if kind == "num" and val.startswith(">") and out and (
                out[-1][0] in ("num", "id", "chr", "str") or out[-1][1] in (")", "]")):
            out.append(("op", ">"))
            pos = m.start(kind) + 1
            continue
        pos = m.end()
        out.append((kind, val))
    return out


def parse_number(s):
    if s.startswith(">"):
        return int(s[1:], 16)
    if s[:2].lower() == "0x":
        return int(s[2:], 16)
    if s[-1] in "hH":
        return int(s[:-1], 16)
    if s[-1] in "bB" and set(s[:-1]) <= set("01"):
        return int(s[:-1], 2)
    return int(s, 10)


# AST: ("num", v) ("sym", name) ("un", op, a) ("bin", op, a, b) ("xy", y, x) ("str", s)

_BINARY = [
    ("|",),
    ("^",),
    ("&",),
    ("=", "==", "!=", "<>"),
    ("<", "<=", ">", ">="),
    ("<<", ">>"),
    ("+", "-"),
    ("*", "/", "%"),
]


class _Parser:
    def __init__(self, tokens, text):
        self.t = tokens
        self.i = 0
        self.text = text

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else (None, None)

    def take(self):
        tok = self.peek()
        self.i += 1
        return tok

    def expect(self, value):
        kind, v = self.take()
        if v != value:
            raise ExprError(f"expected {value!r} in {self.text!r}")

    def parse(self, level=0):
        if level == len(_BINARY):
            return self.unary()
        left = self.parse(level + 1)
        while True:
            kind, v = self.peek()
            if kind == "op" and v in _BINARY[level]:
                self.take()
                right = self.parse(level + 1)
                left = ("bin", v, left, right)
            else:
                return left

    def unary(self):
        kind, v = self.peek()
        if kind == "op" and v in ("-", "~", "!", "+"):
            self.take()
            return ("un", v, self.unary())
        return self.primary()

    def primary(self):
        kind, v = self.take()
        if kind == "num":
            return ("num", parse_number(v))
        if kind == "chr":
            body = v[1:-1]
            val = 0
            for ch in body:
                val = (val << 8) | ord(ch)
            return ("num", val)
        if kind == "str":
            return ("str", v[1:-1])
        if kind == "id":
            if self.peek() == ("op", "(") and v.startswith("$"):
                self.take()
                args = []
                if self.peek() != ("op", ")"):
                    while True:
                        args.append(self.parse())
                        if self.peek() == ("op", ","):
                            self.take()
                            continue
                        break
                self.expect(")")
                return ("call", v.lower(), args)
            return ("sym", v)
        if (kind, v) == ("op", "("):
            e = self.parse()
            self.expect(")")
            return e
        if (kind, v) == ("op", "["):
            y = self.parse()
            self.expect(",")
            x = self.parse()
            self.expect("]")
            return ("xy", y, x)
        raise ExprError(f"unexpected {v!r} in {self.text!r}")


_cache = {}


def parse_expr(text):
    ast = _cache.get(text)
    if ast is None:
        p = _Parser(tokenize(text), text)
        ast = p.parse()
        if p.i != len(p.t):
            raise ExprError(f"trailing input in expression {text!r}")
        _cache[text] = ast
    return ast


def to_s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def evaluate(ast, lookup):
    """Evaluates an AST. lookup(name) returns an int or raises Undefined."""
    k = ast[0]
    if k == "num":
        return ast[1]
    if k == "sym":
        return lookup(ast[1])
    if k == "str":
        s = ast[1]
        val = 0
        for ch in s:
            val = (val << 8) | ord(ch)
        return val
    if k == "xy":
        y = evaluate(ast[1], lookup)
        x = evaluate(ast[2], lookup)
        return to_s32(((y & 0xFFFF) << 16) | (x & 0xFFFF))
    if k == "un":
        a = evaluate(ast[2], lookup)
        op = ast[1]
        if op == "-":
            return -a
        if op == "+":
            return a
        if op == "~":
            return ~a
        return 0 if a else 1
    if k == "bin":
        op = ast[1]
        a = evaluate(ast[2], lookup)
        b = evaluate(ast[3], lookup)
        if op == "+":
            return a + b
        if op == "-":
            return a - b
        if op == "*":
            return a * b
        if op == "/":
            if b == 0:
                raise ExprError("division by zero")
            q = abs(a) // abs(b)
            return q if (a >= 0) == (b >= 0) else -q
        if op == "%":
            if b == 0:
                raise ExprError("division by zero")
            return a - b * (abs(a) // abs(b) * (1 if (a >= 0) == (b >= 0) else -1))
        if op == "<<":
            return to_s32(a << (b & 63)) if b < 64 else 0
        if op == ">>":
            return a >> b
        if op == "&":
            return a & b
        if op == "|":
            return a | b
        if op == "^":
            return a ^ b
        if op in ("=", "=="):
            return int(a == b)
        if op in ("!=", "<>"):
            return int(a != b)
        if op == "<":
            return int(a < b)
        if op == "<=":
            return int(a <= b)
        if op == ">":
            return int(a > b)
        if op == ">=":
            return int(a >= b)
    raise ExprError(f"cannot evaluate {ast!r}")


def symbols_in(ast, out=None):
    if out is None:
        out = set()
    k = ast[0]
    if k == "sym":
        out.add(ast[1])
    elif k == "un":
        symbols_in(ast[2], out)
    elif k == "bin":
        symbols_in(ast[2], out)
        symbols_in(ast[3], out)
    elif k == "xy":
        symbols_in(ast[1], out)
        symbols_in(ast[2], out)
    elif k == "call":
        for a in ast[2]:
            symbols_in(a, out)
    return out


def split_operands(text):
    """Splits an operand field on top-level commas (outside quotes and brackets)."""
    out, cur, depth, quote = [], [], 0, None
    for ch in text:
        if quote:
            cur.append(ch)
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    last = "".join(cur).strip()
    if last or out:
        out.append(last)
    return out
