"""Finding the checkout, and the one error type every tool raises when the repo doesn't look as expected."""

from __future__ import annotations

import os
from pathlib import Path


class ParseError(Exception):
    """The source or docs don't have the shape a tool expects. The message names the file."""


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
