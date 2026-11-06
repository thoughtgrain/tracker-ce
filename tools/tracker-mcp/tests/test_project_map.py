"""project_map accuracy: every curated anchor resolves to a line that really contains it, counts match an
independent walk, and a stale entry fails loudly."""

import json
import unittest
from collections import Counter
from unittest import mock

from support import ROOT, check_provenance, line_text
from tracker_mcp import project_map as pm
from tracker_mcp.source import ParseError


class Anchors(unittest.TestCase):
    def setUp(self):
        self.data = pm.project_map(ROOT)

    def test_every_anchor_line_contains_its_symbol(self):
        self.assertEqual(check_provenance(self.data), [])
        for area in self.data["areas"]:
            for k in area["key_files"]:
                text = line_text(k["where"])
                for token in k["symbol"].split():
                    self.assertIn(token, text, k["where"])

    def test_file_counts_match_an_independent_walk(self):
        for area in self.data["areas"]:
            counts = Counter()
            for p in (ROOT / area["path"]).rglob("*"):
                if p.is_file() and p.suffix in pm.COUNTED:
                    counts[p.suffix] += 1
            self.assertEqual(dict(counts), area["file_counts"], area["path"])

    def test_compiled_dirs_match_the_makefile_text(self):
        text = (ROOT / "core/makefile").read_text()
        for d in self.data["build"]["compiled_core_dirs"]:
            self.assertTrue((ROOT / d).is_dir(), d)
            self.assertIn(f"find ./{d.removeprefix('core/')}/ -name *.cpp", text)

    def test_every_top_level_source_area_is_mapped(self):
        # New top-level directories under core/ should be mapped (or this test lists them).
        mapped = {a["path"] for a in self.data["areas"]}
        missing = []
        for d in sorted((ROOT / "core").iterdir()):
            rel = f"core/{d.name}"
            if d.is_dir() and not d.name.startswith(".") and rel not in mapped and not d.name.startswith("build"):
                if not any(m.startswith(rel + "/") for m in mapped):
                    missing.append(rel)
        # These hold only headers/config/extras and are deliberately described under other areas.
        self.assertEqual([m for m in missing if m not in ("core/config", "core/include", "core/extras",
                                                           "core/Exporter")], [])


class StaleEntries(unittest.TestCase):
    def _with_entries(self, entries):
        return mock.patch.object(pm, "load_entries", return_value=entries)

    def test_missing_symbol_fails_loudly(self):
        entries = [{"path": "core/source", "purpose": "x",
                    "key_files": [{"path": "core/source/Tracker.cpp", "symbol": "NoSuchFunction", "why": "x"}]}]
        with self._with_entries(entries), self.assertRaises(ParseError):
            pm.project_map(ROOT)

    def test_missing_area_fails_loudly(self):
        with self._with_entries([{"path": "core/nope", "purpose": "x", "key_files": []}]), \
                self.assertRaises(ParseError):
            pm.project_map(ROOT)

    def test_data_file_is_valid_json_with_purposes(self):
        data = json.loads(pm.DATA.read_text())
        for area in data["areas"]:
            self.assertTrue(area["purpose"].strip(), area["path"])
            stored = json.dumps(area).split(":")[1:]
            self.assertFalse(any(part[:1].isdigit() for part in stored), "line numbers must not be stored")


if __name__ == "__main__":
    unittest.main()
