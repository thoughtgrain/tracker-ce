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


@dataclass
class Source:
    """One source file: original text, comment-free text, and offset -> line lookup."""

    root: Path
    rel: str
    text: str
    code: str

    @classmethod
    def load(cls, root: Path, rel: str) -> "Source":
        path = root / rel
        if not path.is_file():
            raise ParseError(f"{rel}: file not found")
        text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        return cls(root, rel, text, strip_comments(text))

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
    start, end = src.braces(m.end() - 1)
    result: list[Enumerator] = []
    values: dict[str, int] = {}
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
                raise ParseError(f"{src.at(off)}: can't evaluate '{name} = {init}' in {enum_name}")
        else:
            value = nxt
        values[name] = value
        result.append(Enumerator(name, value, alias, src.at(off)))
        nxt = value + 1
    if not result:
        raise ParseError(f"{src.rel}: enum {enum_name} is empty")
    return result


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
