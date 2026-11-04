"""Per-file scope map used by interrupt_context.

Each source file is comment- and string-blanked, then every brace block is classified (namespace, class,
enum, function, initializer, plain block) from the text before its opening brace. interrupt_context uses
it to find function bodies and class members; _active_lines says which lines each target's preprocessor
keeps, so a call path can report the devices that compile it.
"""

from __future__ import annotations

import bisect
import re
from functools import lru_cache
from pathlib import Path

from .source import ParseError, Source
from .targets import RELEASE_UNDEFINED, TARGET_DEFINES

SCAN = ("core", "platforms/imx/source", "platforms/mk66/source", "synth/include", "util/include", "editor-fx-sdk")
SUFFIXES = (".h", ".hpp", ".cpp", ".c")


def _files(root: Path) -> list[Path]:
    out = []
    for d in SCAN:
        base = root / d
        if base.is_dir():
            out += [p for p in base.rglob("*") if p.suffix in SUFFIXES and p.is_file()]
    return sorted(out)


def _blank_strings(code: str) -> str:
    """Replace string and char literal contents with spaces (same length), so braces in them don't count."""
    return re.sub(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"',
                  code)


class _FileIndex:
    """Comment- and string-blanked code plus a scope map: (start, end, kind, header) for every brace block."""

    def __init__(self, root: Path, rel: str):
        self.src = Source.load(root, rel)
        self.code = _blank_strings(self.src.code)
        self.scopes: list[tuple[int, int, str, str]] = []
        stack: list[tuple[int, str, str]] = []
        last_break = 0
        for i, c in enumerate(self.code):
            if c == "{":
                header = self.code[last_break:i].strip()
                parent = stack[-1][1] if stack else "global"
                stack.append((i, self._kind(header, parent), header))
                last_break = i + 1
            elif c == "}":
                if stack:
                    start, kind, header = stack.pop()
                    self.scopes.append((start, i, kind, header))
                last_break = i + 1
            elif c == ";":
                last_break = i + 1
        self.scopes.sort()
        self.starts = [s[0] for s in self.scopes]

    @staticmethod
    def _kind(header: str, parent: str) -> str:
        h = re.sub(r"\s+", " ", header.split("\n")[-1] if header.startswith("#") else header)
        if parent in ("function", "block"):
            return "block"
        if re.search(r"\bnamespace\b", h):
            return "namespace"
        if re.search(r"\benum\b", h):
            return "enum"
        if re.search(r"(?:^|\s)(?:class|struct|union)\b[^()]*$", h) or re.search(r"\b(?:class|struct|union)\s+\w+\s*(?::[^(){}]*)?$", h):
            return "class"
        if h.endswith("=") or re.search(r"=\s*$", h) or re.search(r"\[\s*\w*\s*\]\s*$", h):
            return "initializer"
        if "(" in h:
            return "function"
        if re.search(r'\bextern\s+"\s*"$', h):  # extern "C" { (string contents are blanked by now)
            return "namespace"
        return "block"

    def scope_at(self, pos: int) -> tuple[str, str]:
        """(kind, header) of the innermost block containing pos, or ('global', '')."""
        i = bisect.bisect_right(self.starts, pos)
        best = ("global", "")
        best_start = -1
        for start, end, kind, header in self.scopes[:i]:
            if start < pos < end and start > best_start:
                best, best_start = (kind, header), start
        return best


@lru_cache(maxsize=4096)
def _index(root: str, rel: str, mtime: float) -> _FileIndex:
    return _FileIndex(Path(root), rel)


@lru_cache(maxsize=4096)
def _active_lines(root: str, rel: str, mtime: float, target: str) -> frozenset[int] | None:
    """Lines that survive preprocessing for `target`, or None if a #if can't be evaluated."""
    try:
        src = Source.load(Path(root), rel, TARGET_DEFINES[target], RELEASE_UNDEFINED)
    except ParseError:
        return None
    return frozenset(i for i, line in enumerate(src.code.split("\n"), 1) if line.strip())
