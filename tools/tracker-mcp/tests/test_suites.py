"""test_suites accuracy, using the real suites from other branches.

tests/audio (perf/optimization-plan) and tests/toro (feat/toro-synth) are checked out of git into a
mirror, so the tool is tested against the suites it will actually see once those branches merge: the
firmware sources each one compiles must be exactly the ones its Makefile names, found independently.
"""

import re
import subprocess
import tarfile
import io
import unittest

from support import ROOT, Mirror
from tracker_mcp import suites


def _branch_has(ref, path):
    return subprocess.run(["git", "-C", str(ROOT), "cat-file", "-e", f"{ref}:{path}"],
                          capture_output=True).returncode == 0


def _extract(ref, path, dest):
    data = subprocess.run(["git", "-C", str(ROOT), "archive", "--format=tar", ref, path], capture_output=True,
                          check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(dest, filter="data")


def _sources_named_in_makefile(directory):
    """Independent read: firmware .cpp paths written in the Makefile (after expanding $(ROOT) / $(AUDIO))."""
    text = (directory / "Makefile").read_text()
    variables = dict(re.findall(r"^(\w+)\s*:?=\s*(.+)$", text, re.M))
    def expand(s, depth=0):
        return re.sub(r"\$\((\w+)\)", lambda m: expand(variables.get(m.group(1), ""), depth + 1), s) if depth < 5 else s
    found = set()
    for token in re.findall(r"[\w$().\-/]+\.cpp", text):
        p = (directory / expand(token)).resolve()
        rel = str(p.relative_to(directory.parents[1].resolve())) if str(p).startswith(str(directory.parents[1].resolve())) else None
        if rel and rel.startswith(("core/", "synth/")):
            found.add(rel)
    return sorted(found)


REFS = {
    "audio": ("origin/perf/optimization-plan", "tests/audio"),
    "toro": ("origin/feat/toro-synth", "tests/toro"),
}


class RealSuites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mirror = Mirror(["core", "synth", "util", "tools/tracker-mcp/Makefile", "tools/tracker-mcp/README.md",
                              "tools/tracker-mcp/requirements.txt"])
        cls.available = {}
        for name, (ref, path) in REFS.items():
            if _branch_has(ref, path):
                _extract(ref, path, cls.mirror.root)
                cls.available[name] = path
        # The TORO suite compiles engine sources that only exist on its branch.
        if "toro" in cls.available:
            for f in ("core/synth/engines/ToroCore.h", "core/synth/engines/ToroSynthVoice.cpp",
                      "core/synth/engines/ToroSynthVoice.h"):
                _extract(REFS["toro"][0], f, cls.mirror.root)
        cls.data = suites.test_suites(cls.mirror.root)

    @classmethod
    def tearDownClass(cls):
        cls.mirror.close()

    def test_branches_present(self):
        if not self.available:
            self.skipTest("neither suite branch is fetched")
        self.assertTrue(self.available)

    def test_coverage_matches_the_makefiles(self):
        by_path = {s["path"]: s for s in self.data["suites"]}
        for name, path in self.available.items():
            with self.subTest(suite=name):
                self.assertIn(path, by_path)
                expected = _sources_named_in_makefile(self.mirror.root / path)
                self.assertTrue(expected, "independent read found no sources")
                self.assertEqual(by_path[path]["firmware_sources_compiled"], expected)

    def test_run_commands_and_descriptions(self):
        by_path = {s["path"]: s for s in self.data["suites"]}
        for path in self.available.values():
            s = by_path[path]
            self.assertEqual(s["run"], f"make -C {path} test")
            heading = next(l for l in (self.mirror.root / path / "README.md").read_text().splitlines()
                           if l.startswith("#")) if (self.mirror.root / path / "README.md").exists() else None
            if heading:
                self.assertEqual(s["description"], heading.lstrip("#").strip())
            self.assertIn("g++", s["commands_used"])


class ThisCheckout(unittest.TestCase):
    def test_finds_its_own_suite_and_leaves_the_tree_alone(self):
        before = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--ignored"], capture_output=True,
                                text=True).stdout
        data = suites.test_suites(ROOT)
        after = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--ignored"], capture_output=True,
                               text=True).stdout
        self.assertEqual(before, after)
        mcp = next(s for s in data["suites"] if s["path"] == "tools/tracker-mcp")
        self.assertEqual(mcp["run"], "make -C tools/tracker-mcp test")
        self.assertEqual(mcp["firmware_sources_compiled"], [])
        self.assertNotIn("PYTHONPATH=.:tests", mcp["commands_used"])


if __name__ == "__main__":
    unittest.main()
