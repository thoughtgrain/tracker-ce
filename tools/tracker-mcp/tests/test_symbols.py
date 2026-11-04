"""where_is accuracy, cross-checked against locations the other tools derive independently (themselves
checked against the compiler, linker and make), plus per-target compilation and negatives."""

import unittest

from support import ROOT, Mirror, check_provenance, line_text
from tracker_mcp import build, display, fx, project_map, screens, synth
from tracker_mcp.symbols import where_is
from tracker_mcp.targets import RELEASE_UNDEFINED


def _has(symbol, kind, where):
    return any(h["kind"] == kind and h["where"] == where for h in where_is(ROOT, symbol, limit=500)["hits"])


class CrossTool(unittest.TestCase):
    def test_display_control_classes(self):
        for c in display.display_controls(ROOT)["controls"]:
            with self.subTest(cls=c["class"]):
                self.assertTrue(_has(c["class"], "type definition", c["declared"]))

    def test_screen_instances_and_classes(self):
        for s in screens.list_screens(ROOT)["screens"]:
            if s.get("empty"):
                continue
            with self.subTest(screen=s["class"]):
                self.assertTrue(_has(s["instance"], "extern variable", s["declared"]))
                defs = [h for h in where_is(ROOT, s["class"])["hits"] if h["kind"] == "type definition"]
                self.assertTrue(any(h["where"].startswith(s["directory"] + "/") for h in defs), defs)

    def test_open_source_engine_factories(self):
        for e in synth.list_synth_engines(ROOT)["engines"]:
            impl = e["implementation"]
            if impl["kind"] == "open-source":
                self.assertTrue(_has(e["registration"]["factory"], "function definition", impl["where"]))

    def test_enumerators(self):
        for e in synth.list_synth_engines(ROOT)["engines"]:
            self.assertTrue(_has(e["id"], "enumerator", e["declared"]), e["id"])
        for e in fx.list_step_fx(ROOT)["effects"]:
            self.assertTrue(_has(e["id"], "enumerator", e["declared"]), e["id"])

    def test_project_map_method_anchors(self):
        for area in project_map.project_map(ROOT)["areas"]:
            for k in area["key_files"]:
                sym = k["symbol"]
                if "::" in sym and " " not in sym and k["path"].endswith(".cpp"):
                    # A qualified anchor is either a method body or a static member definition; the anchored
                    # line says which, independently of where_is.
                    path, line = k["where"].rsplit(":", 1)
                    text = (ROOT / path).read_text(errors="replace").split("\n")[int(line) - 1]
                    after = text[text.index(sym.split("::")[1], text.index(sym.split("::")[0])):]
                    kind = "function definition" if after.split("[")[0].find("(") >= 0 else "variable definition"
                    with self.subTest(symbol=sym):
                        self.assertTrue(_has(sym, kind, k["where"]), kind)


class Targets(unittest.TestCase):
    def test_feature_synth_only_code(self):
        hit = next(h for h in where_is(ROOT, "createChiptuneSynth")["hits"] if h["kind"] == "function definition")
        self.assertEqual(hit["compiled_for"], ["mini", "plus"])

    def test_code_no_build_compiles(self):
        hit = next(h for h in where_is(ROOT, "cArrangement::cArrangement")["hits"]
                   if h["kind"] == "function definition")
        self.assertEqual(hit["compiled_for"], [])

    def test_release_undefined_macros_are_never_defined(self):
        data = build.build_config(ROOT)
        for target, t in data["targets"].items():
            for unit in t["compile_units"].values():
                for name in RELEASE_UNDEFINED:
                    self.assertNotIn(name, unit["defines"], f"{target}: {name}")


class Precision(unittest.TestCase):
    def test_qualified_lookup_is_specific(self):
        hits = where_is(ROOT, "cDisplay::update")["hits"]
        self.assertEqual({h["where"].split(":")[0] for h in hits},
                         {"core/libs/NewDisplay/display.cpp", "core/libs/NewDisplay/display.h"})

    def test_uses_are_not_definitions(self):
        kinds = {h["kind"] for h in where_is(ROOT, "masterParams")["hits"]}
        self.assertEqual(kinds, {"variable definition", "extern variable"})
        defs = [h for h in where_is(ROOT, "midiUpdate")["hits"] if h["kind"] == "function definition"]
        self.assertEqual([h["where"] for h in defs], ["core/source/mtMidi.cpp:63"])

    def test_hit_lines_are_the_cited_source_lines(self):
        for sym in ("cDisplay::update", "masterParams", "InstrumentTypeId", "AUDIO_CORE_RAM", "strInstrument"):
            data = where_is(ROOT, sym)
            self.assertTrue(data["hits"], sym)
            self.assertEqual(check_provenance(data), [], sym)
            for h in data["hits"]:
                self.assertEqual(h["line"], line_text(h["where"]).strip()[:200], h["where"])
                self.assertIn(sym.split("::")[-1], h["line"], h["where"])

    def test_comments_and_strings_dont_count(self):
        m = Mirror(["core/source/Profiler.h"])
        try:
            path = m.root / "core/source/Profiler.h"
            path.write_text(path.read_text() + '\n// void ZzCommentOnly() {}\nconst char* s = "struct ZzInString {";\n')
            self.assertEqual(where_is(m.root, "ZzCommentOnly")["hits"], [])
            self.assertEqual(where_is(m.root, "ZzInString")["hits"], [])
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
