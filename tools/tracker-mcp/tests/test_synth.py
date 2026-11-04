"""list_synth_engines accuracy: compiler ground truth, cross-checks, mutation, loud failure."""

import unittest

from support import ROOT, Mirror, check_provenance, compile_and_run, line_text
from tracker_mcp import synth
from tracker_mcp.source import ParseError


class CompilerGroundTruth(unittest.TestCase):
    def setUp(self):
        self.data = synth.list_synth_engines(ROOT)

    def test_enum_is_complete_and_values_match_the_compiler(self):
        # A switch over every parsed (non-alias) enumerator, compiled with -Werror=switch against the real
        # config.h: if the parser missed an enumerator, the compiler rejects the probe. Printed values must
        # equal the parsed values exactly, aliases included.
        engines = self.data["engines"]
        aliases = self.data["aliases"]
        cases = "\n".join(f"        case E::{e['id']}: return {e['value']};" for e in engines)
        prints = "\n".join(
            f'    std::printf("{x["id"]}=%d\\n", (int)E::{x["id"]});' for x in engines + aliases
        )
        probe = f"""
#include "PolySynth/config.h"
#include <cstdio>
using E = polysynth::InstrumentTypeId;
int all(E e) {{
    switch (e) {{
{cases}
    }}
    return -1;
}}
int main() {{
{prints}
}}
"""
        compiled = dict(line.split("=") for line in compile_and_run(probe))
        parsed = {x["id"]: str(x["value"]) for x in engines + aliases}
        self.assertEqual(parsed, compiled)

    def test_menu_order_matches_the_compiler(self):
        # Synth::kSynthTypes evaluated by the compiler (Synth.h with its heavy project-struct dependency
        # forward-declared away) must equal the parsed menu, in order.
        probe = """
#include "PolySynth/config.h"
#include <cstdio>
#define MT_STRUCTS_H
struct strMtValues;
struct SynthPartSettings;
#include "Synth.h"
int main() { for (auto t : Synth::kSynthTypes) std::printf("%d\\n", (int)t); }
"""
        compiled = [int(v) for v in compile_and_run(probe)]
        parsed = [e["value"] for e in sorted(
            (e for e in self.data["engines"] if e["menu_index"] is not None), key=lambda e: e["menu_index"])]
        self.assertEqual(parsed, compiled)


class CrossChecks(unittest.TestCase):
    def setUp(self):
        self.data = synth.list_synth_engines(ROOT)

    def test_repo_is_consistent(self):
        self.assertEqual(self.data["warnings"], [])

    def test_provenance_points_at_real_lines(self):
        self.assertEqual(check_provenance(self.data), [])
        for e in self.data["engines"]:
            self.assertIn(e["id"], line_text(e["declared"]), e["declared"])
            if e["registration"]:
                self.assertIn("addInstrumentType", line_text(e["registration"]["where"]))

    def test_every_menu_entry_is_registered_and_named(self):
        for e in self.data["engines"]:
            if e["menu_index"] is not None:
                self.assertTrue(e["registered"], e["id"])
                self.assertTrue(e["menu"]["ui_name"], e["id"])
                self.assertTrue(e["menu"]["mini_badge"].startswith("kBitmapMini"), e["id"])
                self.assertTrue(e["menu"]["medium_badge"].startswith("kBitmap"), e["id"])

    def test_open_source_engines_point_at_their_factory(self):
        for e in self.data["engines"]:
            impl = e["implementation"]
            if impl["kind"] == "open-source":
                self.assertTrue((ROOT / impl["file"]).is_file())
                self.assertIn(e["registration"]["factory"], line_text(impl["where"]))


class Mutation(unittest.TestCase):
    PATHS = [synth.CONFIG_H, synth.REGISTRY, synth.SYNTH_H, synth.SYNTH_EDITOR, synth.PATCH_EDITOR,
             synth.ENGINES_DIR]

    def test_a_new_engine_shows_up(self):
        m = Mirror(self.PATHS)
        try:
            before = synth.list_synth_engines(m.root)
            last = [e for e in before["engines"] if e["category"] == "instrument"][-1]
            m.edit(synth.CONFIG_H, f"{last['id']},", f"{last['id']},\n    kMutantSynth,")
            m.edit(synth.CONFIG_H, f"kLastSynth = {last['id']}", "kLastSynth = kMutantSynth")
            m.edit(synth.REGISTRY, "extern std::unique_ptr<InstrumentType> createMidiOut();",
                   "extern std::unique_ptr<InstrumentType> createMidiOut();\n"
                   "extern std::unique_ptr<InstrumentType> createMutant();")
            m.edit(synth.REGISTRY, "createDrumSynth());",
                   "createDrumSynth());\n    registry.addInstrumentType("
                   "static_cast<int>(InstrumentTypeId::kMutantSynth), createMutant());")
            after = synth.list_synth_engines(m.root)
            mutant = next(e for e in after["engines"] if e["id"] == "kMutantSynth")
            self.assertEqual(mutant["value"], last["value"] + 1)
            self.assertTrue(mutant["registered"])
            self.assertEqual(mutant["implementation"]["kind"], "precompiled")
            self.assertIsNone(mutant["menu_index"])
            # Registered but not in kSynthTypes: the tool must warn that the UI can't select it.
            self.assertTrue(any("kMutantSynth" in w for w in after["warnings"]), after["warnings"])
            alias = next(a for a in after["aliases"] if a["id"] == "kLastSynth")
            self.assertEqual(alias["alias_of"], "kMutantSynth")
        finally:
            m.close()

    def test_mismatched_ui_tables_are_reported(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(synth.SYNTH_EDITOR, '"CHIP",', "")
            self.assertTrue(any("synthModelNames" in w for w in synth.list_synth_engines(m.root)["warnings"]))
        finally:
            m.close()

    def test_restructured_source_fails_loudly(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(synth.CONFIG_H, "enum class InstrumentTypeId", "enum class EngineKind")
            with self.assertRaises(ParseError):
                synth.list_synth_engines(m.root)
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
