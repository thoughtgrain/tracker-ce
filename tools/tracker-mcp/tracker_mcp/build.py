"""Build configuration per target, as make itself expands it.

Makefile conditionals are make's to interpret, so this module doesn't re-implement them: it runs
`make -n <target>` with a dummy toolchain prefix (nothing is compiled, nothing can run, and the tests check
the tree is left untouched), then reads the commands make prints. Compile flags are resolved the way GCC
resolves them (the last -D/-U of a macro wins; the last -mcpu/-mfpu/-O/-std wins), and each define is traced
to the makefile line that sets it.
"""

from __future__ import annotations

import os
import re
import shlex
import subprocess
from pathlib import Path
from typing import Any

from .source import ParseError

TARGETS = ("original", "plus", "mini")
DRY = "__tracker_mcp_dryrun__-"
MAKEFILES = ("Makefile", "version.mk", "core/makefile", "platforms/imx/makefile", "platforms/mk66/makefile")
LAST_WINS = ("-mcpu=", "-mfpu=", "-mfloat-abi=", "-O", "-std=", "-mfp16-format=")


def dry_run(root: Path, target: str) -> str:
    if target not in TARGETS:
        raise ValueError(f"unknown target {target!r}; expected one of {TARGETS}")
    env = {**os.environ, "LC_ALL": "C", "MAKEFLAGS": ""}
    run = subprocess.run(["make", "-n", "-w", "-C", str(root), target, f"TOOLCHAIN={DRY}"],
                         capture_output=True, text=True, env=env, timeout=180)
    if run.returncode != 0:
        raise ParseError(f"`make -n {target}` failed:\n{run.stderr[-2000:]}")
    return run.stdout


def _rel(root: Path, cwd: Path, path: str) -> str:
    p = (cwd / path).resolve() if not os.path.isabs(path) else Path(path).resolve()
    try:
        return str(p.relative_to(root.resolve()))
    except ValueError:
        return str(p)


def _commands(root: Path, output: str) -> list[tuple[Path, list[str]]]:
    """(working directory, argv) for each toolchain command make printed, tracking make -w directories."""
    cwd = root
    stack: list[Path] = []
    cmds = []
    for line in output.splitlines():
        m = re.match(r"make(?:\[\d+\])?: (Entering|Leaving) directory '(.*)'", line)
        if m:
            if m.group(1) == "Entering":
                stack.append(cwd)
                cwd = Path(m.group(2))
            else:
                cwd = stack.pop() if stack else root
            continue
        if DRY not in line:
            continue
        line = line.strip().lstrip("@")
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        if argv and argv[0].startswith(DRY):
            cmds.append((cwd, argv))
    return cmds


def _effective(argv: list[str]) -> dict[str, Any]:
    defines: dict[str, str | None] = {}
    overridden: dict[str, list[str]] = {}
    last: dict[str, str] = {}
    includes, forced = [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-include" and i + 1 < len(argv):
            forced.append(argv[i + 1])
            i += 2
            continue
        if a.startswith("-include") and len(a) > len("-include"):
            forced.append(a[len("-include"):])
        elif a.startswith("-D"):
            name, eq, value = a[2:].partition("=")
            defines[name] = value if eq else "1"
        elif a.startswith("-U"):
            defines.pop(a[2:], None)
        elif a == "-I" and i + 1 < len(argv):
            includes.append(argv[i + 1])
            i += 2
            continue
        elif a.startswith("-I"):
            includes.append(a[2:])
        else:
            for prefix in LAST_WINS:
                if a.startswith(prefix):
                    if prefix in last and last[prefix] != a:
                        overridden.setdefault(prefix, []).append(last[prefix])
                    last[prefix] = a
        i += 1
    return {"defines": dict(sorted(defines.items())), "codegen": sorted(last.values()),
            "overridden": {k: v for k, v in overridden.items()}, "includes": includes, "forced_includes": forced}


def _option_values(argv: list[str], opt: str) -> list[str]:
    """Values of an option given either joined (-Ldir) or separate (-L dir)."""
    values = []
    for k, a in enumerate(argv):
        if a == opt and k + 1 < len(argv):
            values.append(argv[k + 1])
        elif a.startswith(opt) and len(a) > len(opt):
            values.append(a[len(opt):])
    return values


def _define_sources(root: Path) -> dict[str, list[str]]:
    """Macro name -> makefile lines that set it with -D."""
    found: dict[str, list[str]] = {}
    for rel in MAKEFILES:
        path = root / rel
        if not path.is_file():
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in re.finditer(r"-D(\w+)", line):
                found.setdefault(m.group(1), []).append(f"{rel}:{n}")
    return found


def _guards_used(root: Path) -> set[str]:
    """Macro names tested by #ifdef / #ifndef / defined() / #if NAME in core/."""
    names: set[str] = set()
    for path in (root / "core").rglob("*"):
        if path.suffix not in (".cpp", ".h", ".c"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        names |= set(re.findall(r"#\s*if(?:n?def)?\s+!?\s*(?:defined\s*\(\s*)?(\w+)", text))
        names |= set(re.findall(r"defined\s*\(?\s*(\w+)", text))
    return names


def build_config(root: Path) -> dict[str, Any]:
    sources = _define_sources(root)
    guards = _guards_used(root)
    targets: dict[str, Any] = {}
    for target in TARGETS:
        cmds = _commands(root, dry_run(root, target))
        groups: dict[str, dict[str, Any]] = {}
        link = None
        firmware = None
        for cwd, argv in cmds:
            if "-c" in argv:
                src = next((a for a in argv if a.endswith((".cpp", ".c", ".S")) and not a.startswith("-")), None)
                if src is None:
                    continue
                unit = "core" if _rel(root, cwd, src).startswith("core/") else _rel(root, cwd, ".")
                lang = "c++" if src.endswith(".cpp") else ("asm" if src.endswith(".S") else "c")
                key = f"{unit} ({lang})"
                eff = _effective(argv[1:])
                eff["includes"] = [_rel(root, cwd, p) for p in eff["includes"]]
                eff["forced_includes"] = [_rel(root, cwd, p) if "/" in p else p for p in eff["forced_includes"]]
                g = groups.setdefault(key, {"files": 0, "variants": [], "example": _rel(root, cwd, src)})
                g["files"] += 1
                if eff not in g["variants"]:
                    g["variants"].append(eff)
            elif "-o" in argv and argv.index("-o") + 1 < len(argv) and argv[argv.index("-o") + 1].endswith(".axf"):
                script = next((argv[k + 1] for k, a in enumerate(argv) if a == "-T" and k + 1 < len(argv)), None)
                link = {
                    "linker_script": script,
                    "search_dirs": [_rel(root, cwd, d) for d in _option_values(argv, "-L")],
                    "libraries": [a for a in argv if a.startswith("-l") or a.endswith(".a")],
                    "linker_flags": [argv[k + 1] for k, a in enumerate(argv) if a == "-Xlinker" and k + 1 < len(argv)],
                    "codegen": sorted({a for a in argv if a.startswith(("-mcpu=", "-mfpu=", "-mfloat-abi="))}),
                    "output": _rel(root, cwd, argv[argv.index("-o") + 1]),
                }
            elif argv[0].endswith("objcopy") and argv[-1].endswith(".ptf"):
                firmware = _rel(root, cwd, argv[-1])
        if not groups:
            raise ParseError(f"`make -n {target}` printed no compile commands")
        if link is None:
            raise ParseError(f"`make -n {target}` printed no link command producing an .axf")
        compile_units = {}
        for key, g in sorted(groups.items()):
            eff = g["variants"][0]
            compile_units[key] = {
                "files": g["files"],
                "example": g["example"],
                "uniform_flags": len(g["variants"]) == 1,
                "codegen": eff["codegen"],
                "overridden_flags": eff["overridden"],
                "defines": {k: {"value": v, "set_in": sources.get(k, [])} for k, v in eff["defines"].items()},
                "forced_includes": eff["forced_includes"],
                "include_dirs": eff["includes"],
            }
        core_defines = next((u["defines"] for k, u in compile_units.items() if k.startswith("core (c++)")), {})
        targets[target] = {
            "make_command": f"make {target}",
            "compile_units": compile_units,
            "link": link,
            "firmware_file": firmware,
            "guards_defined_for_core": sorted(k for k in core_defines if k in guards),
        }
    all_guards = sorted({g for t in targets.values() for g in t["guards_defined_for_core"]})
    return {
        "targets": targets,
        "guard_matrix": {g: {t: g in targets[t]["guards_defined_for_core"] for t in TARGETS} for g in all_guards},
        "sources": list(MAKEFILES),
        "notes": [
            "Expanded by make itself (`make -n` with a dummy toolchain); flags resolved as GCC does: the last "
            "-D/-U and the last -mcpu/-mfpu/-O/-std win. overridden_flags lists earlier values that lose.",
            "guard_matrix covers -D macros that core/ actually tests with #if/#ifdef/defined(). Macros set in "
            "headers (e.g. GUI_HANDLE_SYNTH in interfaceDefs.h) are not command-line defines and aren't listed.",
        ],
    }
