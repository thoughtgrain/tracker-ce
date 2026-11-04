"""Host test suites: what exists, how to run each one, and which firmware sources each actually covers.

Suites are found by their Makefiles under tests/ and tools/. Coverage comes from `make -n` (make's own
expansion of the suite's test target: every firmware source file the suite compiles is covered by it), so
it follows the Makefiles rather than a list someone keeps. Descriptions are the suite's README heading or
its Makefile's leading comment.
"""

from __future__ import annotations

import os
import re
import shlex
import subprocess
from pathlib import Path
from typing import Any

from .source import ParseError

SUITE_ROOTS = ("tests", "tools")
FIRMWARE_PREFIXES = ("core/", "synth/", "platforms/", "util/", "editor-fx-sdk/")


def _description(directory: Path) -> str | None:
    readme = directory / "README.md"
    if readme.is_file():
        for line in readme.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("#"):
                return line.lstrip("#").strip()
    lines = []
    for line in (directory / "Makefile").read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("#"):
            break
        text = line.lstrip("#").strip()
        if text:
            lines.append(text)
        elif lines:
            break
    return " ".join(lines) or None


def _phony(text: str) -> list[str]:
    out: list[str] = []
    for m in re.finditer(r"^\.PHONY\s*:\s*(.+)$", text, re.M):
        out += m.group(1).split()
    return out


def _dry_run(root: Path, directory: Path, target: str) -> str:
    env = {**os.environ, "LC_ALL": "C", "MAKEFLAGS": ""}
    run = subprocess.run(["make", "-n", "-C", str(directory), target], capture_output=True, text=True, env=env,
                         timeout=120)
    if run.returncode != 0:
        raise ParseError(f"`make -n -C {directory.relative_to(root)} {target}` failed:\n{run.stderr[-1500:]}")
    return run.stdout


def _covered_sources(root: Path, directory: Path, output: str) -> tuple[list[str], list[str]]:
    covered: set[str] = set()
    tools: set[str] = set()
    for line in output.splitlines():
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        while argv and re.fullmatch(r"\w+=.*", argv[0]):
            argv = argv[1:]  # leading VAR=value environment assignments
        if not argv or argv[0].startswith(("make", "echo", "@echo", "mkdir", "rm", "touch", "printf")):
            continue
        tools.add(os.path.basename(argv[0]))
        for a in argv[1:]:
            if a.startswith("-") or not a.endswith((".cpp", ".c", ".cc")):
                continue
            p = (directory / a).resolve()
            try:
                rel = str(p.relative_to(root.resolve()))
            except ValueError:
                continue
            if rel.startswith(FIRMWARE_PREFIXES):
                covered.add(rel)
    return sorted(covered), sorted(tools)


def test_suites(root: Path) -> dict[str, Any]:
    suites = []
    for base in SUITE_ROOTS:
        for makefile in sorted((root / base).glob("*/Makefile")):
            directory = makefile.parent
            rel = str(directory.relative_to(root))
            text = makefile.read_text(encoding="utf-8", errors="replace")
            phony = _phony(text)
            target = "test" if "test" in phony or re.search(r"^test\s*:", text, re.M) else "all"
            output = _dry_run(root, directory, target)
            covered, tools = _covered_sources(root, directory, output)
            suites.append({
                "name": directory.name,
                "path": rel,
                "description": _description(directory),
                "run": f"make -C {rel} {target}",
                "targets": phony,
                "firmware_sources_compiled": covered,
                "commands_used": tools,
                "where": f"{rel}/Makefile:1",
            })
    return {
        "suites": suites,
        "notes": [
            "firmware_sources_compiled comes from `make -n` of each suite's test target: those are the real "
            "firmware files the suite builds and exercises on the host.",
            "Coverage lists the source files on the compile line; header-only code a suite exercises (e.g. "
            "ToroCore.h) isn't listed.",
            "Suites on other branches (or in open PRs) aren't visible until merged into this checkout.",
            "Host suites prove logic and bit-exact output on the host; they don't replace an ARM build or a "
            "hardware test (see the verification ladder in AGENTS.md).",
        ],
    }
