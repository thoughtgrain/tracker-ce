"""build_config accuracy.

- GCC ground truth: the raw core compile flags make printed are fed to the host GCC with -dM -E, and every
  macro the tool reports must be defined by GCC with the same value (GCC applies the -D/-U ordering rules).
- Provenance: every define traces to a makefile line that sets it.
- Cross-tool: the link search directories and script agree with memory_map, which derives them a different
  way (parsing the makefile text instead of make's expansion).
- Documentation: the guard matrix matches developer guide section 7.
- Read-only: running the tool leaves the working tree exactly as it was.
- Mutation: a define added to a mirrored makefile shows up for exactly the targets its branch covers.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from support import ROOT, Mirror
from tracker_mcp import build, memory

CXX = shutil.which("g++")
MAKE = shutil.which("make")


def _tree_state(root):
    return subprocess.run(["git", "-C", str(root), "status", "--porcelain", "--ignored"], capture_output=True,
                          text=True, check=True).stdout


@unittest.skipUnless(MAKE, "needs make")
class BuildConfig(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = _tree_state(ROOT)
        cls.data = build.build_config(ROOT)
        cls.after = _tree_state(ROOT)

    def test_read_only(self):
        self.assertEqual(self.before, self.after)

    @unittest.skipUnless(CXX, "needs g++")
    def test_defines_match_gcc(self):
        for target in build.TARGETS:
            cmds = build._commands(ROOT, build.dry_run(ROOT, target))
            argv = next(a for cwd, a in cmds if "-c" in a and any(x.endswith(".cpp") and "core" in str(cwd)
                                                                   for x in a))
            flags = [a for a in argv[1:] if a.startswith(("-D", "-U"))]
            with tempfile.TemporaryDirectory() as tmp:
                empty = Path(tmp) / "e.cpp"
                empty.write_text("")
                out = subprocess.run([CXX, "-dM", "-E", *flags, str(empty)], capture_output=True, text=True,
                                     check=True).stdout
            gcc = dict(re.findall(r"^#define (\w+) ?(.*)$", out, re.M))
            reported = self.data["targets"][target]["compile_units"]["core (c++)"]["defines"]
            for name, d in reported.items():
                with self.subTest(target=target, macro=name):
                    self.assertIn(name, gcc)
                    self.assertEqual(d["value"], gcc[name])

    def test_every_define_traces_to_a_makefile_line(self):
        for target, t in self.data["targets"].items():
            for unit, u in t["compile_units"].items():
                for name, d in u["defines"].items():
                    with self.subTest(target=target, unit=unit, macro=name):
                        self.assertTrue(d["set_in"], "no makefile line sets it")
                        for where in d["set_in"]:
                            rel, line = where.rsplit(":", 1)
                            text = (ROOT / rel).read_text().splitlines()[int(line) - 1]
                            self.assertIn(f"-D{name}", text)

    def test_link_agrees_with_memory_map(self):
        mem = memory.memory_map(ROOT)["targets"]
        for target in build.TARGETS:
            link = self.data["targets"][target]["link"]
            self.assertEqual(Path(mem[target]["linker_script"]).name, link["linker_script"])
            for d in mem[target]["search_dirs"]:
                self.assertIn(d, link["search_dirs"], target)

    def test_guard_matrix_matches_the_developer_guide(self):
        # docs/developer_guide.md section 7: IMX-only, Mini-only and synth guards.
        g = self.data["guard_matrix"]
        self.assertEqual(g["PLATFORM_IMX"], {"original": False, "plus": True, "mini": True})
        self.assertEqual(g["TRACKER_MINI"], {"original": False, "plus": False, "mini": True})
        self.assertEqual(g["FEATURE_SYNTH"], {"original": False, "plus": True, "mini": True})

    def test_imx_core_codegen_resolves_to_m7(self):
        # core/makefile passes Cortex-M4 flags first and Cortex-M7 flags later for IMX; GCC uses the last.
        for target in ("plus", "mini"):
            u = self.data["targets"][target]["compile_units"]["core (c++)"]
            self.assertIn("-mcpu=cortex-m7", u["codegen"])
            self.assertIn("-mfpu=fpv5-sp-d16", u["codegen"])
            self.assertEqual(u["overridden_flags"].get("-mcpu="), ["-mcpu=cortex-m4"])


@unittest.skipUnless(MAKE, "needs make")
class Mutation(unittest.TestCase):
    def test_new_define_appears_for_exactly_its_branch(self):
        m = Mirror(["Makefile", "version.mk", "core", "platforms"])
        try:
            m.edit("core/makefile", "DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH",
                   "DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH -DMUTANT_GUARD")
            data = build.build_config(m.root)
            has = {t: "MUTANT_GUARD" in data["targets"][t]["compile_units"]["core (c++)"]["defines"]
                   for t in build.TARGETS}
            self.assertEqual(has, {"original": False, "plus": True, "mini": True})
        finally:
            m.close()

    def test_duplicate_defines_resolve_last_wins(self):
        eff = build._effective(["-DA=1", "-DB", "-DA=2", "-UB", "-mcpu=cortex-m4", "-mcpu=cortex-m7"])
        self.assertEqual(eff["defines"], {"A": "2"})
        self.assertEqual(eff["codegen"], ["-mcpu=cortex-m7"])
        self.assertEqual(eff["overridden"], {"-mcpu=": ["-mcpu=cortex-m4"]})


if __name__ == "__main__":
    unittest.main()
