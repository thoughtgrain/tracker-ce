"""Reading C/C++ source: comment stripping that keeps line numbers, a strict per-device #if evaluator, and
loud failures. If the preprocessor meets a condition it can't evaluate, it raises ParseError naming the file
rather than guessing which branch a device compiles."""

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
