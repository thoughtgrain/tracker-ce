"""Splitting text into code names without regular expressions.

A name is a run of letters, digits, underscores and `::` (so `cDisplay::update` is one name, and a search
for `update` doesn't match it). Used to find a key file's symbol and, in the tests, to check that names
mentioned in the docs exist in the source.
"""

from __future__ import annotations

from collections.abc import Iterator


def _is_name_char(c: str) -> bool:
    return c.isalnum() or c == "_"


def names(line: str) -> Iterator[str]:
    """Every name in a line, in order, with `A::b` kept whole and a trailing `:` (labels, access specifiers)
    dropped."""
    i, n = 0, len(line)
    while i < n:
        if _is_name_char(line[i]) or (line[i] == ":" and line[i : i + 2] == "::"):
            start = i
            while i < n and (_is_name_char(line[i]) or line[i : i + 2] == "::"):
                i += 2 if line[i : i + 2] == "::" else 1
            yield line[start:i].rstrip(":")
        else:
            i += 1


def words(text: str) -> set[str]:
    """Every identifier in a text, split at `::` too, so both halves of `A::b` count. `-DNAME` (a makefile
    define) also counts as NAME."""
    out: set[str] = set()
    for line in text.splitlines():
        for name in names(line.replace("-D", "-D ")):
            out.update(part for part in name.split("::") if part)
    return out
