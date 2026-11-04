"""where_is: definition and declaration sites of a symbol, with which targets compile each one.

A text search, not a compiler. Every whole-word occurrence of the name in comment-stripped source is
classified from its context: the keywords before it, what follows it, and whether it sits in a namespace,
class, enum or function body (from a per-file scope map). Calls and other uses inside function bodies are
not reported. Each hit says which targets compile it (from the per-target preprocessor), so code under
#ifdef FEATURE_SYNTH shows as plus/mini only. The tests cross-check it against locations other tools derive
independently.
"""

from __future__ import annotations

import bisect
import re
from functools import lru_cache
from pathlib import Path
from typing import Any

from .source import ParseError, Source
from .targets import RELEASE_UNDEFINED, TARGET_DEFINES

SCAN = ("core", "platforms/imx/source", "platforms/mk66/source", "synth/include", "util/include", "editor-fx-sdk")
SUFFIXES = (".h", ".hpp", ".cpp", ".c")
KIND_ORDER = ["type definition", "enum definition", "function definition", "macro", "typedef",
              "variable definition", "member", "extern variable", "enumerator", "declaration"]


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


def _matching_paren(code: str, i: int) -> int:
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "(":
            depth += 1
        elif code[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


def _classify(ix: _FileIndex, pos: int, name: str, owner: str | None) -> str | None:
    code = ix.code
    line_start = code.rfind("\n", 0, pos) + 1
    line = code[line_start : code.find("\n", pos) if code.find("\n", pos) >= 0 else len(code)]
    if line.lstrip().startswith("#"):
        return "macro" if re.match(rf"\s*#\s*define\s+{re.escape(name)}\b", line) else None

    qualifier = re.search(r"(\w+)\s*::\s*$", code[max(0, pos - 80) : pos])
    stmt_start = max(code.rfind(";", 0, pos), code.rfind("{", 0, pos), code.rfind("}", 0, pos)) + 1
    prefix = re.sub(r"\s+", " ", code[stmt_start:pos]).strip()
    if qualifier:
        prefix = prefix[: prefix.rfind(qualifier.group(1))].strip()
    after = code[pos + len(name) :]
    nxt = after.lstrip()[:1]
    kind, header = ix.scope_at(pos)
    before = code[:pos].rstrip()[-1:]

    if owner:
        in_owner_class = kind == "class" and re.search(rf"\b{re.escape(owner)}\b", header)
        if not ((qualifier and qualifier.group(1) == owner) or in_owner_class):
            return None
    elif qualifier and kind in ("function", "block"):
        return None  # a qualified use inside a function body

    if re.search(r"\benum(?:\s+(?:class|struct))?$", prefix) and nxt in ("{", ":"):
        return "enum definition"
    if re.search(r"(?:^|\s)(?:class|struct|union)$", prefix) and nxt in ("{", ":", "f"):
        return "type definition"
    if kind == "enum" and before in ("{", ",") and nxt in (",", "=", "}"):
        return "enumerator"
    if kind in ("function", "block", "initializer"):
        return None
    if prefix.startswith("typedef") and nxt == ";":
        return "typedef"
    if re.fullmatch(r"using", prefix) and nxt == "=":
        return "typedef"
    if before == "}" and nxt == ";":
        return "typedef"
    if nxt == "(":
        open_paren = pos + len(name) + (len(after) - len(after.lstrip()))
        close = _matching_paren(code, open_paren)
        if close < 0:
            return None
        tail = code[close + 1 :].lstrip()
        tail = re.sub(r"^(?:const|noexcept|override|final|volatile|\s)+", "", tail)
        if prefix in ("", "else", "return") and not qualifier and kind == "global":
            return None  # a call or macro use at file scope
        if tail.startswith("{") or (tail.startswith(":") and not tail.startswith("::")):
            return "function definition"
        if tail.startswith(";") or tail.startswith("= 0") or tail.startswith("=0"):
            if re.search(r"\bextern\b", prefix):
                return "declaration"
            return "declaration" if prefix else None
        return None
    if re.search(r"\bextern\b", prefix) and nxt in (";", "["):
        return "extern variable"
    if "=" in prefix:
        return None  # the name is in an initializer, not the declarator
    if nxt in ("=", ";", "[", "{") and prefix and not re.match(r"(?:return|case|goto|delete|throw)\b", prefix):
        if re.search(r"[\w>*&\]]$", prefix):
            return "member" if kind == "class" else "variable definition"
    return None


def where_is(root: Path, symbol: str, limit: int = 60) -> dict[str, Any]:
    symbol = symbol.strip()
    m = re.fullmatch(r"(?:(\w+)::)?(\w+)", symbol)
    if not m:
        raise ValueError("symbol must be a name or Class::name")
    owner, name = m.group(1), m.group(2)
    word = re.compile(rf"(?<![\w]){re.escape(name)}(?!\w)")
    hits, seen = [], set()
    for path in _files(root):
        rel = str(path.relative_to(root))
        raw = path.read_text(encoding="utf-8", errors="replace")
        if name not in raw:
            continue
        mtime = path.stat().st_mtime
        ix = _index(str(root), rel, mtime)
        for occ in word.finditer(ix.code):
            kind = _classify(ix, occ.start(), name, owner)
            if kind is None:
                continue
            line = ix.src.line(occ.start())
            if (rel, line) in seen:
                continue
            seen.add((rel, line))
            active = {t: (None if (lines := _active_lines(str(root), rel, mtime, t)) is None else line in lines)
                      for t in TARGET_DEFINES}
            hit = {"kind": kind, "where": f"{rel}:{line}",
                   "line": ix.src.text.split("\n")[line - 1].strip()[:200],
                   "compiled_for": sorted(t for t, a in active.items() if a)}
            unknown = sorted(t for t, a in active.items() if a is None)
            if unknown:
                hit["preprocessing_unknown_for"] = unknown
            hits.append(hit)
    hits.sort(key=lambda h: (KIND_ORDER.index(h["kind"]), h["where"]))
    return {
        "symbol": symbol,
        "hits": hits[:limit],
        "truncated": len(hits) > limit,
        "notes": [
            "Text search over comment-stripped source, not a compiler: it can miss symbols built by macros and "
            "doesn't resolve overloads. Use it to find files, then read them.",
            "Uses inside function bodies (calls, locals) aren't listed; grep for callers, and use "
            "interrupt_context for interrupt reachability.",
            "compiled_for lists the targets whose preprocessor keeps the line (release defines). Empty means no "
            "build compiles it.",
        ],
    }
