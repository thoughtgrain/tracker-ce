"""Reading C/C++ source for the project MCP: comment stripping that keeps line numbers, enum and brace-list
parsing, and loud failures.

Every parser here either returns exactly what the source says, with file:line provenance, or raises
ParseError naming the file and what it expected. Nothing is guessed. The tests check these parsers against
the compiler (tests/test_*.py), so a parser that silently misreads the source fails the build.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path


class ParseError(Exception):
    """The source didn't have the shape a parser expects (moved, renamed or restructured)."""


def find_repo_root(start: Path | None = None) -> Path:
    """The tracker-ce checkout: $TRACKER_CE_ROOT, or the nearest parent holding core/makefile."""
    env = os.environ.get("TRACKER_CE_ROOT")
    if env:
        root = Path(env).resolve()
        if not (root / "core" / "makefile").is_file():
            raise ParseError(f"TRACKER_CE_ROOT={env} is not a tracker-ce checkout (no core/makefile)")
        return root
    here = (start or Path(__file__)).resolve()
    for parent in [here, *here.parents]:
        if (parent / "core" / "makefile").is_file() and (parent / "synth" / "include").is_dir():
            return parent
    raise ParseError("couldn't find the tracker-ce repository root (set TRACKER_CE_ROOT)")


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, keeping every newline and string literal, so offsets map to the same
    lines as the original file."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def preprocess(code: str, defines: dict[str, str], rel: str = "<source>",
               undefined: frozenset[str] = frozenset()) -> str:
    """Blank out lines in inactive #if/#ifdef/#ifndef/#elif/#else branches (line count preserved).

    Follows C rules: #ifdef / defined() of an unknown name is false. A value test (`#if NAME`) on a name that
    is neither in `defines` nor #defined earlier in this file raises, since it may be defined in another
    header and guessing could silently pick the wrong branch."""
    macros = dict(defines)
    out_lines = []
    # stack of (this branch active, some branch already taken, parent active)
    stack: list[list[bool]] = []

    def active() -> bool:
        return all(frame[0] for frame in stack)

    def cond_value(expr: str, lineno: int) -> bool:
        e = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)|\bdefined\s+(\w+)",
                   lambda m: "1" if (m.group(1) or m.group(2)) in macros else "0", expr)
        def name(m):
            n = m.group(0)
            if n in ("and", "or", "not"):
                return n
            if n in macros:
                v = macros[n].strip() or "1"
                return v if re.fullmatch(r"\d+", v) else "1"
            if n in undefined:
                return "0"
            raise ParseError(f"{rel}:{lineno}: #if uses {n!r}, whose value isn't known here")
        e = re.sub(r"[A-Za-z_]\w*", name, e.replace("&&", " and ").replace("||", " or ").replace("!", " not "))
        e = e.replace(" not =", "!=")
        try:
            return bool(eval(e, {"__builtins__": {}}, {}))  # only digits and boolean operators remain
        except Exception as exc:
            raise ParseError(f"{rel}:{lineno}: can't evaluate #if {expr.strip()!r}") from exc

    for lineno, line in enumerate(code.split("\n"), 1):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif|define|undef)\b(.*)", line)
        directive, rest = (m.group(1), m.group(2).strip()) if m else (None, "")
        if directive in ("ifdef", "ifndef"):
            parent = active()
            name = rest.split()[0] if rest else ""
            taken = (name in macros) == (directive == "ifdef")
            stack.append([taken, taken, parent])
            out_lines.append("")
            continue
        if directive == "if":
            parent = active()
            taken = cond_value(rest, lineno) if parent else False
            stack.append([taken, taken, parent])
            out_lines.append("")
            continue
        if directive in ("elif", "else", "endif"):
            if not stack:
                raise ParseError(f"{rel}:{lineno}: #{directive} without #if")
            frame = stack[-1]
            if directive == "endif":
                stack.pop()
            elif directive == "else":
                frame[0] = not frame[1]
                frame[1] = True
            else:
                frame[0] = (not frame[1]) and frame[2] and cond_value(rest, lineno)
                frame[1] = frame[1] or frame[0]
            out_lines.append("")
            continue
        if not active():
            out_lines.append("")
            continue
        if directive == "define":
            dm = re.match(r"(\w+)(?:\s+(.*))?$", rest)
            if dm:
                macros[dm.group(1)] = (dm.group(2) or "").strip()
        elif directive == "undef":
            macros.pop(rest.split()[0] if rest else "", None)
        out_lines.append(line)
    if stack:
        raise ParseError(f"{rel}: unterminated #if")
    return "\n".join(out_lines)


@dataclass
class Source:
    """One source file: original text, comment-free text, and offset -> line lookup."""

    root: Path
    rel: str
    text: str
    code: str

    @classmethod
    def load(cls, root: Path, rel: str, defines: dict[str, str] | None = None,
             undefined: frozenset[str] = frozenset()) -> "Source":
        """Load a file. With `defines` (a target's command-line macros), inactive #if branches are blanked
        out, keeping line numbers, so parsers see exactly what that target compiles."""
        path = root / rel
        if not path.is_file():
            raise ParseError(f"{rel}: file not found")
        text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        code = strip_comments(text)
        if defines is not None:
            code = preprocess(code, defines, rel, undefined)
        return cls(root, rel, text, code)

    def line(self, offset: int) -> int:
        return self.code.count("\n", 0, offset) + 1

    def at(self, offset: int) -> str:
        """Provenance string, e.g. 'core/synth/Synth.h:37'."""
        return f"{self.rel}:{self.line(offset)}"

    def find(self, pattern: str, what: str, start: int = 0) -> re.Match:
        m = re.compile(pattern, re.S).search(self.code, start)
        if not m:
            raise ParseError(f"{self.rel}: couldn't find {what}")
        return m

    def braces(self, open_index: int) -> tuple[int, int]:
        """(start, end) of the {...} body whose '{' is at open_index, end exclusive of the closing brace."""
        if self.code[open_index] != "{":
            raise ParseError(f"{self.at(open_index)}: expected '{{'")
        depth = 0
        for j in range(open_index, len(self.code)):
            ch = self.code[j]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return open_index + 1, j
        raise ParseError(f"{self.at(open_index)}: unbalanced braces")

    def top_level_items(self, start: int, end: int) -> list[tuple[str, int]]:
        """Split code[start:end] on top-level commas; returns (item text stripped, offset of item start)."""
        items, depth, item_start = [], 0, start
        for j in range(start, end + 1):
            ch = self.code[j] if j < end else ","
            if ch in "({[":
                depth += 1
            elif ch in ")}]":
                depth -= 1
            elif ch == "," and depth == 0:
                raw = self.code[item_start:j]
                stripped = raw.strip()
                if stripped:
                    items.append((stripped, item_start + (len(raw) - len(raw.lstrip()))))
                item_start = j + 1
        return items


@dataclass
class Enumerator:
    name: str
    value: int
    alias_of: str | None  # set when declared as `kA = kB`
    where: str


_INT = re.compile(r"^[+-]?(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$")


def parse_enum(src: Source, enum_name: str) -> list[Enumerator]:
    """Enumerators of `enum [class] enum_name`, with C++ value rules: implicit +1, integer literals, and
    references to earlier enumerators. Any other initializer raises, rather than producing a wrong value."""
    m = src.find(rf"\benum\s+(?:class\s+|struct\s+)?{re.escape(enum_name)}\b[^{{;]*\{{", f"enum {enum_name}")
    return parse_enum_at(src, m.end() - 1, enum_name)


def parse_enum_at(src: Source, open_brace: int, label: str, known: dict[str, int] | None = None) -> list[Enumerator]:
    """Enumerators of the enum whose '{' is at open_brace. `known` supplies values of enumerators from other
    enums that initializers may reference (e.g. `kAlias = OTHER_ENUM_VALUE`)."""
    enum_name = label
    start, end = src.braces(open_brace)
    result: list[Enumerator] = []
    values: dict[str, int] = dict(known or {})
    nxt = 0
    for item, off in src.top_level_items(start, end):
        name, _, init = (part.strip() for part in item.partition("="))
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            raise ParseError(f"{src.at(off)}: unexpected enumerator '{item}' in {enum_name}")
        alias = None
        if init:
            if _INT.match(init):
                value = int(re.sub(r"[uUlL]+$", "", init), 0)
            elif init in values:
                value, alias = values[init], init
            else:
                value = _int_expr(init, values)
                if value is None:
                    raise ParseError(f"{src.at(off)}: can't evaluate '{name} = {init}' in {enum_name}")
        else:
            value = nxt
        values[name] = value
        result.append(Enumerator(name, value, alias, src.at(off)))
        nxt = value + 1
    if not result:
        raise ParseError(f"{src.rel}: enum {enum_name} is empty")
    return result


def _int_expr(expr: str, names: dict[str, int]) -> int | None:
    """Integer arithmetic over literals and known names (+ - * << >> | & ~, parentheses), else None."""
    import ast

    text = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expr.strip())
    try:
        tree = ast.parse(text, mode="eval")
    except SyntaxError:
        return None
    ops = {ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b, ast.Mult: lambda a, b: a * b,
           ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b, ast.BitOr: lambda a, b: a | b,
           ast.BitAnd: lambda a, b: a & b}

    def ev(node):
        if isinstance(node, ast.Expression):
            return ev(node.body)
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.Name) and node.id in names:
            return names[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.Invert)):
            v = ev(node.operand)
            return -v if isinstance(node.op, ast.USub) else ~v
        if isinstance(node, ast.BinOp) and type(node.op) in ops:
            return ops[type(node.op)](ev(node.left), ev(node.right))
        raise ValueError

    try:
        return ev(tree)
    except (ValueError, KeyError):
        return None


def parse_array(src: Source, anchor_regex: str, what: str) -> list[tuple[str, str]]:
    """Items of the brace initializer that follows anchor_regex: [(item text, 'file:line')]."""
    m = src.find(anchor_regex + r"[^;{]*=\s*\{", what)
    start, end = src.braces(m.end() - 1)
    return [(item, src.at(off)) for item, off in src.top_level_items(start, end)]


def unquote(item: str) -> str:
    m = re.fullmatch(r'\(?(?:const\s+)?(?:char\s*\*\s*)?\)?\s*"((?:[^"\\]|\\.)*)"', item.strip())
    if not m:
        raise ParseError(f"expected a string literal, got {item!r}")
    return m.group(1)
