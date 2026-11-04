"""memory_map accuracy, with GNU ld as ground truth.

The host linker evaluates the real linker scripts (it reads MEMORY and SECTIONS whatever the target CPU), so:
- regions are checked against the "Memory Configuration" table in ld's own map file, and
- every placement the tool reports is checked by compiling a probe that uses the real macros (through the
  real config.h / cr_section_macros.h), linking it against the real script, and reading which output
  section and region each probe symbol actually landed in. That covers orphan-rule predictions too.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from support import ROOT, Mirror, check_provenance, line_text
from tracker_mcp import memory
from tracker_mcp.source import ParseError

LD = shutil.which("ld")
OBJDUMP = shutil.which("objdump")
CXX = shutil.which("g++") or shutil.which("clang++")


def _probe_source(sources):
    """C++ that puts one symbol in each placement source, using the real macros."""
    lines = ['#include "config.h"', '#include "cr_section_macros.h"']
    for i, s in enumerate(sources):
        src, kind = s["source"], s["kind"]
        if src.startswith("plain"):
            attr = ""
        elif src.startswith("section("):
            attr = f'__attribute__((section("{s["input_section"]}"), used))'
        else:
            attr = src  # a macro: config.h name or e.g. __NOINIT(EXTERNAL_RAM)
        if kind == "code":
            lines.append(f"{attr} void probe_{i}() {{}}")
        elif src == "plain initialized data":
            lines.append(f"int probe_{i}[4] = {{1}};")
        else:
            # Zero-initialized, as real code uses these sections: GCC rejects initializers in
            # .bss*/.noinit* named sections, and the section attribute alone decides placement.
            lines.append(f"{attr} int probe_{i}[4];")
    return "\n".join(lines) + "\n"


class LinkedProbe:
    """Compile once, link against one target's script, expose ld's view of the result."""

    def __init__(self, target, sources):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        (d / "stub").mkdir()
        # library.ld only lists ARM runtime libraries; a host link can't use them.
        (d / "stub" / "library.ld").write_text("/* host probe: no ARM libraries */\n")
        (d / "probe.cpp").write_text(_probe_source(sources))
        subprocess.run([CXX, "-std=gnu++20", "-c", "-fno-asynchronous-unwind-tables", "-fno-exceptions",
                        "-fno-pic", f"-I{ROOT / 'core/config'}", f"-I{ROOT / 'core/include'}",
                        str(d / "probe.cpp"), "-o", str(d / "probe.o")], check=True, capture_output=True)
        search = [f"-L{d / 'stub'}"] + [f"-L{ROOT / s}" for s in target["search_dirs"]]
        run = subprocess.run([LD, *search, "-T", str(ROOT / target["linker_script"]), "-Map", str(d / "probe.map"),
                              "--no-warn-rwx-segments", str(d / "probe.o"), "-o", str(d / "probe.out")],
                             capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(f"host link failed:\n{run.stderr}")
        self.map = (d / "probe.map").read_text()
        table = subprocess.run([OBJDUMP, "-t", str(d / "probe.out")], capture_output=True, text=True,
                               check=True).stdout
        self.symbols = {}
        for line in table.splitlines():
            m = re.match(r"^([0-9a-f]+)\s.*?\s(\.\S+)\s+[0-9a-f]+\s+(?:\.hidden\s+)?(_Z\d+probe_\d+v|probe_\d+)$",
                         line)
            if m:
                idx = int(re.search(r"probe_(\d+)", m.group(3)).group(1))
                self.symbols[idx] = (int(m.group(1), 16), m.group(2))

    def regions(self):
        block = self.map.split("Memory Configuration", 1)[1].split("Linker script and memory map", 1)[0]
        out = {}
        for m in re.finditer(r"^(\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", block, re.M):
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
        return out

    def close(self):
        self.tmp.cleanup()


@unittest.skipUnless(LD and OBJDUMP and CXX, "needs GNU binutils (ld, objdump) and a C++ compiler")
class LinkerGroundTruth(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = memory.memory_map(ROOT)
        cls.sources = memory.placement_sources(ROOT)
        cls.probes = {name: LinkedProbe(t, cls.sources) for name, t in cls.data["targets"].items()}

    @classmethod
    def tearDownClass(cls):
        for p in cls.probes.values():
            p.close()

    def test_three_targets_from_the_makefiles(self):
        self.assertEqual(set(self.data["targets"]), {"original", "plus", "mini"})

    def test_regions_match_lds_memory_configuration(self):
        for name, t in self.data["targets"].items():
            ld_regions = self.probes[name].regions()
            ours = {r["name"]: (int(r["origin"], 16), r["length"]) for r in t["regions"]}
            self.assertEqual(ours, ld_regions, name)

    def test_every_placement_matches_where_ld_put_it(self):
        for name, t in self.data["targets"].items():
            probe = self.probes[name]
            regions = probe.regions()
            for i, row in enumerate(t["placements"]):
                with self.subTest(target=name, source=row["source"]):
                    self.assertIn(i, probe.symbols, f"probe symbol {i} not found in the link")
                    addr, out_sec = probe.symbols[i]
                    region = next((r for r, (o, length) in regions.items() if o <= addr < o + length), None)
                    self.assertEqual(row["placement"]["region"], region)
                    self.assertEqual(row["placement"]["output_section"], out_sec)


class CrossChecks(unittest.TestCase):
    def setUp(self):
        self.data = memory.memory_map(ROOT)

    def test_provenance_points_at_real_lines(self):
        self.assertEqual(check_provenance(self.data), [])
        for t in self.data["targets"].values():
            for r in t["regions"]:
                self.assertIn(r["name"], line_text(r["where"]))

    def test_audio_core_ram_lands_in_fast_ram_only_on_imx(self):
        # The fact AGENTS.md states: AUDIO_CORE_RAM -> ITCM on IMX, flash on MK66.
        place = {n: next(p for p in t["placements"] if p["source"] == "AUDIO_CORE_RAM")["placement"]["region"]
                 for n, t in self.data["targets"].items()}
        self.assertEqual(place, {"original": "FLASH", "plus": "SRAM_ITC", "mini": "SRAM_ITC"})


class Mutation(unittest.TestCase):
    PATHS = ["platforms/imx/makefile", "platforms/imx/linker", "platforms/mk66/makefile",
             "platforms/mk66/3rdparty/teensy_core"]

    def test_region_change_is_reflected(self):
        m = Mirror(self.PATHS)
        try:
            m.edit("platforms/imx/linker/TrackerMini/memory.ld", "ORIGIN = 0x60020000", "ORIGIN = 0x60040000")
            targets = memory._targets(m.root)
            ls = memory.LinkerScript(m.root, targets["mini"]["script"], targets["mini"]["search_dirs"])
            flash = next(r for r in ls.regions if r.name == "QSPI_FLASH")
            self.assertEqual(flash.origin, 0x60040000)
        finally:
            m.close()

    def test_removing_a_pattern_changes_placement(self):
        m = Mirror(self.PATHS)
        try:
            m.edit("platforms/imx/linker/linkerScript.ld", "*(.audio_ram_functions*)", "")
            targets = memory._targets(m.root)
            ls = memory.LinkerScript(m.root, targets["plus"]["script"], targets["plus"]["search_dirs"])
            p = ls.place(".audio_ram_functions", writable=False)
            self.assertEqual(p["method"], "orphan")
            self.assertNotEqual(p["region"], "SRAM_ITC")
        finally:
            m.close()

    def test_unparseable_length_fails_loudly(self):
        m = Mirror(self.PATHS)
        try:
            m.edit("platforms/imx/linker/TrackerPlus/memory.ld", "LENGTH = 0x58000", "LENGTH = 0x50000 + 0x8000")
            targets = memory._targets(m.root)
            with self.assertRaises(ParseError):
                memory.LinkerScript(m.root, targets["plus"]["script"], targets["plus"]["search_dirs"])
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
