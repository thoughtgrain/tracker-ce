"""list_screens / find_screen accuracy: independent re-derivation, cross-checks, mutation, loud failure."""

import re
import unittest

from support import ROOT, Mirror, check_provenance, line_text
from tracker_mcp import screens
from tracker_mcp.source import ParseError


class Rederivation(unittest.TestCase):
    def setUp(self):
        self.data = screens.list_screens(ROOT)
        self.real = [s for s in self.data["screens"] if not s.get("empty")]

    def test_every_module_class_appears_exactly_once(self):
        # Independently: grep every `class cX : public ModuleBase` under core/interface.
        classes = set()
        for path in (ROOT / "core/interface").rglob("*.h"):
            classes |= set(re.findall(r"class\s+(c\w+)\s*:\s*public\s+ModuleBase", path.read_text(errors="replace")))
        listed = [s["class"] for s in self.real]
        self.assertEqual(sorted(listed), sorted(classes))
        self.assertEqual(len(listed), len(set(listed)))

    def test_listed_files_exist_and_follow_the_g_f_convention(self):
        for s in self.real:
            d = ROOT / s["directory"]
            for f in s["draw_files"] + s["input_files"] + s["other_files"]:
                self.assertTrue((d / f).is_file(), f"{s['directory']}/{f}")
            self.assertTrue(s["draw_files"], f"{s['class']} has no g*.cpp")
            self.assertTrue(s["input_files"], f"{s['class']} has no f*.cpp")

    def test_menu_and_button_tables_agree(self):
        # Two independent tables: the Mini menu row says "slot N, button X"; modulesButtons says
        # "button X opens slot M". They must name the same slot.
        button_to_slot = {b["button"]: s["slot"] for s in self.real for b in s["buttons"]}
        for s in self.real:
            for entry in s["mini_menu"]:
                self.assertEqual(button_to_slot.get(entry["button"]), s["slot"], entry)

    def test_menu_has_every_name_once(self):
        names = [m["name"] for s in self.real for m in s["mini_menu"]]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(self.data["warnings"], [])

    def test_provenance_points_at_real_lines(self):
        self.assertEqual(check_provenance(self.data), [])
        for s in self.real:
            self.assertIn(s["instance"], line_text(s["where"]))
            self.assertIn(s["class"], line_text(s["declared"]))


class FindScreen(unittest.TestCase):
    def test_finds_by_menu_name_class_and_button(self):
        for q, cls in [("Sample Editor", "cSampleEditor"), ("cMasterParams", "cMasterParams"),
                       ("interfaceButtonSong", "cSongEditor"), ("perform", "cPerformanceMode")]:
            got = [m["class"] for m in screens.find_screen(ROOT, q)["matches"]]
            self.assertIn(cls, got, q)

    def test_no_match_is_empty_not_an_error(self):
        self.assertEqual(screens.find_screen(ROOT, "no such screen")["matches"], [])


class Mutation(unittest.TestCase):
    PATHS = ["core/interface"]

    def test_renamed_menu_entry_is_found_by_new_name(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(screens.MENU_CPP, '"Sample Editor"', '"Wave Lab"')
            got = [x["class"] for x in screens.find_screen(m.root, "wave lab")["matches"]]
            self.assertEqual(got, ["cSampleEditor"])
        finally:
            m.close()

    def test_restructured_table_fails_loudly(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(screens.INTERFACE_CPP, "&masterParams,", "getMasterParams(),")
            with self.assertRaises(ParseError):
                screens.list_screens(m.root)
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
