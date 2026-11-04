"""Memory map: linker memory regions per target, the output sections that fill them, and where each
placement macro (AUDIO_CORE_RAM, __NOINIT(EXTERNAL_RAM), ...) and plain code/data actually land.

The linker script and target selection are parsed from the makefiles, so a new script or product shows
up here. Placement follows the linker's rule that an input section goes to the first statement in script
order that matches it. Sections that match no statement are placed by ld's orphan rules, which this
module only predicts and labels as such (method "orphan"). The tests check every placement, explicit and
orphan, by linking real macro expansions with the host GNU ld against the real scripts.
"""

from __future__ import annotations

import fnmatch
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .source import ParseError, Source

IMX_MAKEFILE = "platforms/imx/makefile"
MK66_MAKEFILE = "platforms/mk66/makefile"
CONFIG_H = "core/config/config.h"
SECTION_MACROS_H = "core/include/cr_section_macros.h"
SCAN_DIRS = ("core", "platforms/imx/source", "platforms/mk66/source")


def _strip_ld_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)


def _ld_number(expr: str, where: str) -> int:
    m = re.fullmatch(r"\s*(0[xX][0-9a-fA-F]+|\d+)\s*([KkMm]?)\s*", expr)
    if not m:
        raise ParseError(f"{where}: can't evaluate linker expression {expr.strip()!r}")
    value = int(m.group(1), 0)
    return value * {"": 1, "k": 1024, "m": 1024 * 1024}[m.group(2).lower()]


@dataclass
class Region:
    name: str
    attrs: str
    origin: int
    length: int
    where: str


@dataclass
class Statement:
    file_pattern: str
    excluded_files: list[str]
    section_patterns: list[str]
    where: str


@dataclass
class OutputSection:
    name: str
    noload: bool
    region: str | None
    load_region: str | None
    where: str
    statements: list[Statement] = field(default_factory=list)


class LinkerScript:
    """One target's linker script, with INCLUDEs resolved through its -L search directories."""

    def __init__(self, root: Path, script_rel: str, search_dirs: list[str]):
        self.root = root
        self.search_dirs = search_dirs
        self.files: list[str] = []
        self._full: dict[str, str] = {}
        self._load(script_rel)
        self.regions = self._parse_regions()
        self.sections = self._parse_sections()

    def _resolve(self, name: str) -> str:
        for d in self.search_dirs:
            rel = f"{d}/{name}" if d else name
            if (self.root / rel).is_file():
                return rel
        raise ParseError(f"linker INCLUDE {name!r} not found in {self.search_dirs}")

    def _load(self, rel: str) -> None:
        """Read `rel` and, depth first, every file it INCLUDEs (searched in the -L directories)."""
        text = _strip_ld_comments((self.root / rel).read_text(encoding="utf-8", errors="replace"))
        self.files.append(rel)
        self._full[rel] = text
        for m in re.finditer(r'^\s*INCLUDE\s+"?([\w./-]+)"?', text, re.M):
            self._load(self._resolve(m.group(1)))

    def _line(self, rel: str, offset_in_full: int) -> str:
        return f"{rel}:{self._full[rel].count(chr(10), 0, offset_in_full) + 1}"

    def _parse_regions(self) -> list[Region]:
        regions: list[Region] = []
        for rel in self.files:
            text = self._full[rel]
            for block in re.finditer(r"\bMEMORY\s*\{(.*?)\}", text, re.S):
                for m in re.finditer(
                    r"(\w+)\s*\(([^)]*)\)\s*:\s*ORIGIN\s*=\s*([^,]+?)\s*,\s*LENGTH\s*=\s*([^\n]+)", block.group(1)
                ):
                    where = self._line(rel, block.start(1) + m.start())
                    regions.append(
                        Region(m.group(1), m.group(2).strip(), _ld_number(m.group(3), where),
                               _ld_number(m.group(4), where), where)
                    )
        if not regions:
            raise ParseError(f"no MEMORY regions found in {self.files}")
        return regions

    def _parse_sections(self) -> list[OutputSection]:
        for rel in self.files:
            text = self._full[rel]
            sm = re.search(r"\bSECTIONS\s*\{", text)
            if not sm:
                continue
            body_start = sm.end()
            depth, i = 1, body_start
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[body_start : i - 1]
            return self._output_sections(rel, body, body_start)
        raise ParseError(f"no SECTIONS block found in {self.files}")

    def _output_sections(self, rel: str, body: str, base: int) -> list[OutputSection]:
        out: list[OutputSection] = []
        head = re.compile(r"(?m)^\s*(\.[\w.$]+|/DISCARD/)\s*([^:{}\n;]*?)\s*:\s*([^{};]*?)\{")  # "{" may sit on the next line
        pos = 0
        while True:
            m = head.search(body, pos)
            if not m:
                break
            depth, j = 1, m.end()
            while depth and j < len(body):
                depth += {"{": 1, "}": -1}.get(body[j], 0)
                j += 1
            inner = body[m.end() : j - 1]
            tail = body[j : body.find("\n", j) if body.find("\n", j) >= 0 else len(body)]
            region = re.search(r"(?<![A-Za-z])>\s*(\w+)", tail)
            load = re.search(r"\bAT\s*>\s*(\w+)", tail)
            sec = OutputSection(
                name=m.group(1),
                noload="NOLOAD" in (m.group(2) + m.group(3)),
                region=region.group(1) if region else None,
                load_region=load.group(1) if load else None,
                where=self._line(rel, base + m.start(1)),
            )
            sec.statements = self._statements(rel, inner, base + m.end())
            out.append(sec)
            pos = j
        if not out:
            raise ParseError(f"{rel}: no output sections parsed")
        return out

    def _statements(self, rel: str, inner: str, base: int) -> list[Statement]:
        text = re.sub(r"\bSORT(?:_BY_NAME|_BY_ALIGNMENT)?\s*\(([^()]*)\)", r"\1", inner)
        text = re.sub(r"\bKEEP\s*\(", "(", text)
        stmts = []
        for m in re.finditer(
            r"(?<![\w.$])(\*[\w.*:-]*|[\w.*-]+\.o)\s*\(\s*(?:EXCLUDE_FILE\s*\(([^)]*)\)\s*)?([^()]*?)\s*\)", text
        ):
            file_pat = m.group(1)
            if file_pat.endswith(":"):
                file_pat = file_pat[:-1]
            stmts.append(
                Statement(
                    file_pattern=file_pat,
                    excluded_files=m.group(2).split() if m.group(2) else [],
                    section_patterns=m.group(3).split(),
                    where=self._line(rel, base + m.start()),
                )
            )
        return stmts

    def region_of(self, address: int) -> str | None:
        for r in self.regions:
            if r.origin <= address < r.origin + r.length:
                return r.name
        return None

    def place(self, input_section: str, object_file: str = "probe.o", writable: bool | None = None) -> dict[str, Any]:
        """Where an input section from an ordinary application object file lands."""
        for sec in self.sections:
            for st in sec.statements:
                if ":" in st.file_pattern:
                    continue  # archive:member patterns (e.g. *libPolyDSP.a:*.o) only select library objects
                if not fnmatch.fnmatchcase(object_file, st.file_pattern):
                    continue
                if any(fnmatch.fnmatchcase(object_file, x) for x in st.excluded_files):
                    continue
                if any(fnmatch.fnmatchcase(input_section, p) for p in st.section_patterns):
                    return {"output_section": sec.name, "region": sec.region, "load_region": sec.load_region,
                            "noload": sec.noload, "method": "explicit", "where": st.where}
        # Orphan: ld gives an unmatched input section an output section of its own name (or joins the
        # same-named one if the script has it), placed after an output section with compatible flags, so it
        # lands in that section's region. Predicted, labelled as such, and checked against ld by the tests.
        same = next((s for s in self.sections if s.name == input_section), None)
        anchor = same
        if anchor is None and writable is not None:
            anchor = next((s for s in self.sections if s.name == (".data" if writable else ".text")), None)
        return {"output_section": input_section, "region": anchor.region if anchor else None,
                "load_region": anchor.load_region if anchor else None,
                "noload": anchor.noload if anchor else None, "method": "orphan",
                "where": anchor.where if anchor else None,
                "note": "matched no statement: ld creates an output section of this name next to "
                        + (anchor.name if anchor else "a compatible section")
                        + " (predicted; confirmed against ld by the tests)"}


def _targets(root: Path) -> dict[str, dict[str, Any]]:
    """target -> {script, search_dirs} parsed from the platform makefiles."""
    targets: dict[str, dict[str, Any]] = {}

    mk66 = Source.load(root, MK66_MAKEFILE)
    t = mk66.find(r"-T\s+(\S+\.ld)", "the -T linker script in the MK66 link line")
    ldirs = re.findall(r"-L\s*(\./[\w./-]+)", mk66.code[mk66.code.rfind("\n", 0, t.start()) : t.end()])
    dirs = ["platforms/mk66/" + d.removeprefix("./").rstrip("/") for d in ldirs]
    script = next((f"{d}/{t.group(1)}" for d in dirs if (root / d / t.group(1)).is_file()), None)
    if script is None:
        raise ParseError(f"{mk66.at(t.start())}: {t.group(1)} not found in {dirs}")
    targets["original"] = {"script": script, "search_dirs": dirs, "where": mk66.at(t.start())}

    imx = Source.load(root, IMX_MAKEFILE)
    products = dict(
        (prod.lower(), name)
        for prod, name in re.findall(r"ifeq\s*\(\$\(PRODUCT\),\s*(\w+)\)\s*\n\s*TARGET\s*:=\s*(\w+)", imx.code)
    )
    if set(products) != {"mini", "plus"}:
        raise ParseError(f"{IMX_MAKEFILE}: expected MINI and PLUS TARGET assignments, found {products}")
    t = imx.find(r"-T\s+(\S+\.ld)", "the -T linker script in the IMX link line")
    link_line = imx.code[imx.code.rfind("\n", 0, t.start()) : t.end()]
    for product, target_dir in products.items():
        ldirs = [d.replace("$(TARGET)", target_dir) for d in re.findall(r"-L\s*(\./[\w./$()-]+)", link_line)]
        dirs = ["platforms/imx/" + d.removeprefix("./").rstrip("/") for d in ldirs]
        script = next((f"{d}/{t.group(1)}" for d in dirs if (root / d / t.group(1)).is_file()), None)
        if script is None:
            raise ParseError(f"{imx.at(t.start())}: {t.group(1)} not found in {dirs}")
        targets[product] = {"script": script, "search_dirs": dirs, "where": imx.at(t.start())}
    return targets


def placement_sources(root: Path) -> list[dict[str, Any]]:
    """Every way the code picks a section: config.h macros, cr_section_macros banks in use, direct
    section attributes, and the defaults for plain code / data / zero-initialized data."""
    found: list[dict[str, Any]] = []
    cfg = Source.load(root, CONFIG_H)
    macro_sections = {}
    for m in re.finditer(r"#define\s+(\w+)\s+__attribute__\s*\(\(\s*section\(\"([^\"]+)\"\)", cfg.code):
        macro_sections[m.group(1)] = m.group(2)
        # Code macros name their sections after functions; everything else holds data. Orphan placement
        # depends on this (ld groups orphans with sections of compatible flags).
        is_code = "function" in m.group(2) or m.group(2).startswith((".text", ".ramfunc"))
        found.append({"source": m.group(1), "input_section": m.group(2), "where": cfg.at(m.start()),
                      "writable": not is_code, "kind": "code" if is_code else "data"})

    bank_macros = {"__NOINIT": "noinit", "__DATA": "data", "__BSS": "bss", "__RAMFUNC": "ramfunc"}
    uses: dict[tuple[str, str], int] = {}
    direct: dict[str, int] = {}
    for d in SCAN_DIRS:
        for path in (root / d).rglob("*"):
            if path.suffix not in (".cpp", ".h", ".c") or path.name == "cr_section_macros.h":
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for m in re.finditer(r"\b(__NOINIT|__DATA|__BSS|__RAMFUNC)\s*\(\s*(\w+)\s*\)", text):
                if m.group(2) != "bank":
                    uses[(m.group(1), m.group(2))] = uses.get((m.group(1), m.group(2)), 0) + 1
            if path.name != "config.h":
                for m in re.finditer(r"__attribute__\s*\(\(\s*section\(\"([^\"]+)\"\)", text):
                    if m.group(1) not in macro_sections.values():
                        direct[m.group(1)] = direct.get(m.group(1), 0) + 1
    macros_h = Source.load(root, SECTION_MACROS_H)
    for (macro, bank), n in sorted(uses.items()):
        definition = macros_h.find(rf"#define\s+{macro}\s*\(\s*bank\s*\)", f"#define {macro}(bank)")
        found.append({"source": f"{macro}({bank})", "input_section": f".{bank_macros[macro]}.${bank}",
                      "uses": n, "where": macros_h.at(definition.start()), "writable": macro != "__RAMFUNC",
                      "kind": "code" if macro == "__RAMFUNC" else "data"})
    for sec, n in sorted(direct.items()):
        found.append({"source": f'section("{sec}")', "input_section": sec, "uses": n, "where": None,
                      "writable": True, "kind": "data"})
    found += [
        {"source": "plain code", "input_section": ".text", "where": None, "writable": False, "kind": "code"},
        {"source": "plain initialized data", "input_section": ".data", "where": None, "writable": True,
         "kind": "data"},
        {"source": "plain zero-initialized data", "input_section": ".bss", "where": None, "writable": True,
         "kind": "bss"},
    ]
    return found


def memory_map(root: Path) -> dict[str, Any]:
    targets = _targets(root)
    sources = placement_sources(root)
    result: dict[str, Any] = {}
    for name, t in targets.items():
        ls = LinkerScript(root, t["script"], t["search_dirs"])
        placements = []
        for s in sources:
            p = ls.place(s["input_section"], writable=s["writable"])
            row = {**{k: v for k, v in s.items() if k not in ("writable",)}, **{"placement": p}}
            if s["where"] is None:
                row.pop("where")
            placements.append(row)
        result[name] = {
            "linker_script": t["script"],
            "search_dirs": t["search_dirs"],
            "script_files": ls.files,
            "selected_by": t["where"],
            "regions": [
                {"name": r.name, "attrs": r.attrs, "origin": hex(r.origin), "length": r.length,
                 "length_kib": round(r.length / 1024, 2), "where": r.where}
                for r in ls.regions
            ],
            "output_sections": [
                {"name": s.name, "region": s.region, "load_region": s.load_region, "noload": s.noload,
                 "where": s.where}
                for s in ls.sections
            ],
            "placements": placements,
        }
    return {
        "targets": result,
        "notes": [
            "Sizes come from evaluating ORIGIN/LENGTH, never from the comments beside them (several comments "
            "in the IMX memory.ld files disagree with the numbers).",
            "NOLOAD regions (e.g. .noinit_RAM4, EXTERNAL_RAM) are not zeroed at startup.",
            "placement.method 'orphan' means no linker statement matches and ld's orphan rules decide; the "
            "prediction is verified by the tests against the real linker.",
        ],
    }
