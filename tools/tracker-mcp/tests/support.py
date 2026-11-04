"""Shared helpers for the accuracy tests: compiler ground truth, repo mirrors for mutation tests, and a
provenance checker that every tool answer must pass."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Iterable

from tracker_mcp.source import find_repo_root

ROOT = find_repo_root()

# Include paths matching core/makefile + the platform makefile for the headers the tests compile.
CXX_INCLUDES = [
    "core/synth",
    "core/config",
    "core/include",
    "core/source",
    "core/libs/Audio",
    "synth/include",
    "util/include",
    "util/3rdparty",
]


# Wider include set for probes that pull in firmware headers (mtSequencer.h, interfaceDefs.h, ...).
FIRMWARE_INCLUDES = CXX_INCLUDES + [
    "core/libs", "core/libs/ElapsedMillis", "core/libs/NewDisplay", "core/libs/FT812", "core/interface",
    "core/AudioEngine", "core/AudioEngine/ConstantValues", "core/FileManager",
]

# Firmware headers static_assert struct sizes that only hold on the 32-bit ARM targets (e.g. strInstrument is
# 372 bytes there, larger with 64-bit host pointers). Probes that only read constants, enums and pure
# functions disable static_assert in the probe translation unit alone; nothing else is affected.
FIRMWARE_PROBE_FLAGS = ["-includecstdint", "-Dstatic_assert(...)=", "-w"]


def compile_and_run(source: str, defines: Iterable[str] = ("FEATURE_SYNTH",), root: Path = ROOT,
                    includes: Iterable[str] = CXX_INCLUDES, extra_flags: Iterable[str] = ()) -> list[str]:
    """Compile a tiny C++ program against the real headers of `root` and return its stdout lines.
    This is ground truth: the compiler, not a regex, decides what the headers mean."""
    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        raise RuntimeError("accuracy tests need g++ or clang++ for compiler ground truth")
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "probe.cpp"
        exe = Path(tmp) / "probe"
        src.write_text(source)
        cmd = [cxx, "-std=gnu++20", "-Werror=switch", "-o", str(exe), str(src)]
        cmd += [f"-D{d}" for d in defines]
        cmd += list(extra_flags)
        cmd += [f"-I{root / inc}" for inc in includes]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(f"compiler ground-truth probe failed to build:\n{result.stderr[:3000]}")
        return subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()


class Mirror:
    """A temporary copy of selected repo paths, for mutation tests: edit the copy, re-run a parser, check
    the answer changed. Proves a tool reads the source rather than returning a hard-coded answer."""

    def __init__(self, paths: Iterable[str]):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        for rel in paths:
            src = ROOT / rel
            dst = self.root / rel
            if src.is_dir():
                shutil.copytree(src, dst)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)

    def edit(self, rel: str, old: str, new: str, count: int = 1) -> None:
        path = self.root / rel
        text = path.read_text(encoding="utf-8", errors="replace")
        if text.count(old) < 1:
            raise AssertionError(f"mutation target not found in {rel}: {old!r}")
        path.write_text(text.replace(old, new, count), encoding="utf-8")

    def close(self) -> None:
        self._tmp.cleanup()


_WHERE = re.compile(r"^([\w./+-]+):(\d+)$")


def provenance_strings(obj: Any) -> list[str]:
    """Every 'file:line' value anywhere in a tool answer (keys named where/declared/position)."""
    found: list[str] = []
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ("where", "declared", "position") and isinstance(v, str):
                found.append(v)
            else:
                found += provenance_strings(v)
    elif isinstance(obj, list):
        for v in obj:
            found += provenance_strings(v)
    return found


def check_provenance(answer: Any, root: Path = ROOT) -> list[str]:
    """Problems with an answer's provenance: unparseable, missing file, or line past the end of the file."""
    problems = []
    for p in provenance_strings(answer):
        m = _WHERE.match(p)
        if not m:
            problems.append(f"not file:line: {p!r}")
            continue
        path = root / m.group(1)
        if not path.is_file():
            problems.append(f"missing file: {p}")
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").count("\n") + 1
        if not 1 <= int(m.group(2)) <= lines:
            problems.append(f"line out of range: {p} (file has {lines})")
    return problems


def line_text(where: str, root: Path = ROOT) -> str:
    rel, line = where.rsplit(":", 1)
    return (root / rel).read_text(encoding="utf-8", errors="replace").splitlines()[int(line) - 1]
