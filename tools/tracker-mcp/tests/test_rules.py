"""rules: served verbatim, split losslessly, and scoped to the directories above a path."""

import unittest

from support import ROOT, Mirror, check_provenance, line_text
from tracker_mcp import rules


class Rules(unittest.TestCase):
    def test_root_sections_are_lossless_and_anchored(self):
        data = rules.rules(ROOT)
        self.assertEqual(check_provenance(data), [])
        sections = data["root_rules"]["sections"]
        text = (ROOT / "AGENTS.md").read_text()
        self.assertEqual("".join(s["text"] for s in sections).replace("\n", ""), text.replace("\n", ""))
        for s in sections[1:]:
            self.assertTrue(line_text(s["where"]).startswith("#"), s["where"])
            self.assertIn(s["heading"], line_text(s["where"]))
        headings = [s["heading"] for s in sections]
        self.assertIn("2. Real-time and interrupt rules (these cause audible bugs)", headings)

    def test_area_files_apply_to_their_subtree_only(self):
        m = Mirror(["AGENTS.md", "core/source/Tracker.cpp", "core/libs/NewDisplay/display.cpp"])
        try:
            (m.root / "core/AGENTS.md").write_text("# core\n\nCore rule.\n")
            (m.root / "core/source/AGENTS.md").write_text("# source\n\nSource rule.\n")
            data = rules.rules(m.root, "core/source/Tracker.cpp")
            self.assertEqual([a["file"] for a in data["area_rules"]], ["core/AGENTS.md", "core/source/AGENTS.md"])
            data = rules.rules(m.root, "core/libs/NewDisplay/display.cpp")
            self.assertEqual([a["file"] for a in data["area_rules"]], ["core/AGENTS.md"])
            data = rules.rules(m.root, "core/source")
            self.assertEqual(len(data["area_rules"]), 2)
            self.assertEqual({a["file"] for a in data["all_area_files"]}, {"core/AGENTS.md", "core/source/AGENTS.md"})
        finally:
            m.close()

    def test_recipes_are_listed_and_served_losslessly(self):
        listed = {r["recipe"]: r for r in rules.rules(ROOT)["recipes"]}
        self.assertTrue({"synth-engine", "open-synth-engine", "ui-screen", "step-fx", "ptfx-effect", "host-test"} <= set(listed), listed)
        for name, entry in listed.items():
            with self.subTest(recipe=name):
                self.assertTrue(entry["summary"])
                data = rules.rules(ROOT, recipe=name)
                self.assertEqual(check_provenance(data), [])
                text = (ROOT / entry["file"]).read_text()
                self.assertEqual("".join(x["text"] for x in data["sections"]).replace("\n", ""), text.replace("\n", ""))
        with self.assertRaises(ValueError):
            rules.rules(ROOT, recipe="no-such-recipe")

    def test_bad_paths_raise(self):
        for bad in ("core/no/such/file.cpp", "../outside"):
            with self.subTest(path=bad), self.assertRaises(ValueError):
                rules.rules(ROOT, bad)


if __name__ == "__main__":
    unittest.main()
