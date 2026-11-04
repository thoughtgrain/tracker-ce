"""Project map: the curated purpose of each top-level area, checked against the tree on every call.

Purposes come from data/project_map.json, the one place in this server with hand-written facts. Everything
else is looked up live: each area must exist, file counts come from the filesystem, and each key file's
symbol must be found in that file, with the current line reported. A stale entry raises instead of
returning something wrong.
"""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from .source import ParseError

DATA = Path(__file__).with_name("data") / "project_map.json"
COUNTED = (".cpp", ".c", ".h", ".hpp", ".py", ".md", ".ld")


def locate_symbol(root: Path, rel: str, symbol: str) -> int:
    """1-based line of the first line in `rel` containing `symbol` as whole tokens, or raise."""
    path = root / rel
    if not path.is_file():
        raise ParseError(f"project map: {rel} does not exist")
    pattern = re.compile(r"(?<![\w:])" + r"\s+".join(map(re.escape, symbol.split())) + r"(?!\w)")
    for i, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if pattern.search(line):
            return i
    raise ParseError(f"project map: symbol {symbol!r} not found in {rel}")


def load_entries() -> list[dict[str, Any]]:
    return json.loads(DATA.read_text(encoding="utf-8"))["areas"]


def project_map(root: Path) -> dict[str, Any]:
    areas = []
    for area in load_entries():
        directory = root / area["path"]
        if not directory.is_dir():
            raise ParseError(f"project map: area {area['path']} does not exist")
        counts = Counter(p.suffix for p in directory.rglob("*") if p.is_file() and p.suffix in COUNTED)
        key_files = []
        for k in area.get("key_files", []):
            line = locate_symbol(root, k["path"], k["symbol"])
            key_files.append({**k, "where": f"{k['path']}:{line}"})
        areas.append(
            {
                "path": area["path"],
                "purpose": area["purpose"],
                "file_counts": dict(sorted(counts.items())),
                "key_files": key_files,
            }
        )
    return {
        "areas": areas,
        "build": {
            "firmware": "make all | original | plus | mini (GNU Arm 10.3-2021.10); make ptfx for plugins",
            "compiled_core_dirs": _compiled_core_dirs(root),
        },
        "sources": [str(DATA.relative_to(DATA.parents[2])), "core/makefile"],
    }


def _compiled_core_dirs(root: Path) -> list[str]:
    """Directories whose every .cpp core/makefile compiles into the firmware (parsed from its find lines)."""
    text = (root / "core" / "makefile").read_text(encoding="utf-8", errors="replace")
    dirs = re.findall(r"CPP_SRCS\s*\+=\s*\$\(shell find \./([\w/]+?)/? -name \*\.cpp\)", text)
    if not dirs:
        raise ParseError("core/makefile: no `CPP_SRCS += $(shell find ./<dir>/ -name *.cpp)` lines found")
    return [f"core/{d}" for d in dirs]
