"""step_fx accuracy.

Ground truth comes from the compiler running the real code:
- the real Sequencer::getFxMin / getFxMax / getFxDefault bodies, copied verbatim from the source into a probe
  (hosted in a struct derived from Sequencer, with the static constants they use copied verbatim from
  mtSequencer.cpp), are compiled and run for every FX id, normal and randomized, per target;
- the FX menu tables (InterfaceGlobals::fxIDs / fxNames) are printed by the compiler per target;
- a -Werror=switch probe proves the parsed enFxType list is complete.
Plus: the per-target define table is checked against build_config, mutations change answers, and
unsupported source shapes fail loudly.
"""

import re
import unittest

from support import FIRMWARE_INCLUDES, FIRMWARE_PROBE_FLAGS, ROOT, Mirror, check_provenance, compile_and_run, line_text
from tracker_mcp import fx
from tracker_mcp.source import ParseError, preprocess
from tracker_mcp.targets import TARGET_DEFINES, TRACKED_GUARDS


def _function_body(text, name):
    m = re.search(rf"Sequencer::{name}\s*\([^)]*\)\s*\{{", text)
    i, depth = m.end() - 1, 0
    for j in range(i, len(text)):
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        if depth == 0:
            return text[i : j + 1]
    raise AssertionError(f"unbalanced body for {name}")


def _probe(target, max_id):
    ranges = (ROOT / fx.RANGES_CPP).read_text()
    statics = [l for l in (ROOT / "core/source/mtSequencer.cpp").read_text().splitlines()
               if re.match(r"\s*const\s+float\s+Sequencer::\w+\s*=", l)]
    source = (
        "#define private public\n#define protected public\n"
        '#include "mtSequencer.h"\n#include "core/interfaceDefs.h"\n'
        "#undef private\n#undef protected\n#include <cstdio>\n"
        + "\n".join(statics)
        + "\nstruct FxProbe : Sequencer {\n"
        + f"  static int16_t getFxMax(uint8_t fxID, bool forRandom) {_function_body(ranges, 'getFxMax')}\n"
        + f"  static int16_t getFxMin(uint8_t fxID, bool forRandom) {_function_body(ranges, 'getFxMin')}\n"
        + f"  static int16_t getFxDefault(uint8_t fxID) {_function_body(ranges, 'getFxDefault')}\n"
        + "};\nint main() {\n"
        + f"  for (int id = 0; id <= {max_id}; id++)\n"
        + '    std::printf("R %d %d %d %d %d %d\\n", id, FxProbe::getFxMin(id, false), FxProbe::getFxMin(id, true),'
        + " FxProbe::getFxMax(id, false), FxProbe::getFxMax(id, true), FxProbe::getFxDefault(id));\n"
        + "  for (int i = 0; i < FX_COUNT; i++)\n"
        + '    std::printf("M %d %d %s\\n", i, (int)InterfaceGlobals::fxIDs[i], InterfaceGlobals::fxNames[i]);\n'
        + '  std::printf("C %d %d\\n", (int)FX_COUNT, (int)FX_COUNT_HIDDEN_FXes);\n}\n'
    )
    defines = [k if v == "1" else f"{k}={v}" for k, v in TARGET_DEFINES[target].items()]
    lines = compile_and_run(source, defines=defines, includes=FIRMWARE_INCLUDES, extra_flags=FIRMWARE_PROBE_FLAGS)
    ranges_out, menu_out, counts = {}, {}, None
    for line in lines:
        kind, _, rest = line.partition(" ")
        if kind == "R":
            v = list(map(int, rest.split()))
            ranges_out[v[0]] = v[1:]
        elif kind == "M":
            i, fid, name = rest.split(" ", 2)
            menu_out[int(i)] = (int(fid), name)
        elif kind == "C":
            counts = tuple(map(int, rest.split()))
    return ranges_out, menu_out, counts


def _for_target(entry, field, target):
    differs = entry.get("differs_by_target", {})
    return differs[field][target] if field in differs else entry.get(field)


class CompilerGroundTruth(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = fx.list_step_fx(ROOT)
        cls.max_id = max(e["value"] for e in cls.data["effects"])
        cls.probes = {t: _probe(t, cls.max_id) for t in TARGET_DEFINES}

    def test_ranges_match_the_compiled_functions(self):
        for target, (ranges, _, _) in self.probes.items():
            for e in self.data["effects"]:
                with self.subTest(target=target, fx=e["id"]):
                    mn, mx, df = (_for_target(e, k, target) for k in ("min", "max", "default"))
                    got = [mn["value"], mn.get("randomized_value", mn["value"]),
                           mx["value"], mx.get("randomized_value", mx["value"]), df["value"]]
                    self.assertEqual(got, ranges[e["value"]])

    def test_menu_matches_the_compiled_tables(self):
        for target, (_, menu, counts) in self.probes.items():
            by_index = {}
            for e in self.data["effects"]:
                idx = _for_target(e, "menu_index", target)
                if idx is not None:
                    by_index[idx] = e
            self.assertEqual(sorted(by_index), sorted(menu), target)
            for idx, (fid, label) in menu.items():
                with self.subTest(target=target, index=idx):
                    e = by_index[idx]
                    self.assertEqual(e["value"], fid)
                    letter = _for_target(e, "letter", target)
                    name = _for_target(e, "name", target)
                    self.assertEqual(f"{letter} - {name}", label)
            menu_count = self.data["menu_count"]
            menu_count = menu_count[target] if isinstance(menu_count, dict) else menu_count
            self.assertEqual((menu_count, self.data["hidden_at_end_of_menu"]), counts)

    def test_enum_is_complete(self):
        ids = sorted({e["id"] for e in self.data["effects"]} | {a for e in self.data["effects"] for a in e["aliases"]})
        primary = [e["id"] for e in self.data["effects"]]
        cases = "\n".join(f"    case F::{i}: return {next(e['value'] for e in self.data['effects'] if e['id'] == i)};"
                          for i in primary)
        prints = "\n".join(f'  std::printf("{i} %d\\n", (int)F::{i});' for i in ids)
        source = (f'#include "mtSequencer.h"\n#include <cstdio>\nusing F = Sequencer::strFxConsts;\n'
                  f"int f(F::enFxType e) {{ switch (e) {{\n{cases}\n  }} return -1; }}\n"
                  f"int main() {{\n{prints}\n}}\n")
        out = dict(line.split() for line in compile_and_run(source, defines=["PLATFORM_IMX", "FEATURE_SYNTH"],
                                                              includes=FIRMWARE_INCLUDES,
                                                              extra_flags=FIRMWARE_PROBE_FLAGS))
        parsed = {e["id"]: e["value"] for e in self.data["effects"]}
        parsed.update({a: e["value"] for e in self.data["effects"] for a in e["aliases"]})
        self.assertEqual({k: int(v) for k, v in out.items()}, parsed)


class CrossChecks(unittest.TestCase):
    def test_target_defines_match_build_config(self):
        from tracker_mcp import build
        matrix = build.build_config(ROOT)["guard_matrix"]
        for guard in TRACKED_GUARDS:
            for target in TARGET_DEFINES:
                with self.subTest(guard=guard, target=target):
                    self.assertEqual(guard in TARGET_DEFINES[target], matrix.get(guard, {}).get(target, False))

    def test_provenance_points_at_real_lines(self):
        data = fx.list_step_fx(ROOT)
        self.assertEqual(check_provenance(data), [])
        for e in data["effects"]:
            self.assertIn(e["id"], line_text(e["declared"]))
            for key in ("min", "max", "default"):
                self.assertIn("return", line_text(e[key]["where"]))

    def test_lookup(self):
        got = fx.step_fx(ROOT, "tempo")["effects"]
        self.assertEqual([e["id"] for e in got], ["FX_TYPE_TEMPO"])
        # One character is an exact, case-sensitive FX letter.
        self.assertEqual([e["id"] for e in fx.step_fx(ROOT, "C")["effects"]], ["FX_TYPE_CHANCE"])
        self.assertEqual([e["id"] for e in fx.step_fx(ROOT, "c")["effects"]], ["FX_TYPE_SEND_CC_C"])
        self.assertEqual([e["id"] for e in fx.step_fx(ROOT, "15")["effects"]], ["FX_TYPE_TEMPO"])


class Preprocessor(unittest.TestCase):
    def test_branches(self):
        code = "#ifdef A\na\n#else\nb\n#endif\n#ifndef A\nc\n#endif\n#if defined(A) && !defined(B)\nd\n#endif"
        self.assertEqual(preprocess(code, {"A": "1"}).split(), ["a", "d"])
        self.assertEqual(preprocess(code, {}).split(), ["b", "c"])
        self.assertEqual(preprocess(code, {"A": "1"}).count("\n"), code.count("\n"))

    def test_local_defines_and_unknown_values(self):
        self.assertEqual(preprocess("#define G 1\n#if G\nx\n#endif", {}).split(), ["#define", "G", "1", "x"])
        with self.assertRaises(ParseError):
            preprocess("#if SOMETHING_FROM_ANOTHER_HEADER\nx\n#endif", {})


class Mutation(unittest.TestCase):
    PATHS = ["core/source", "core/config", "core/AudioEngine/ConstantValues", "core/interface/core"]

    def test_changed_range_is_reflected(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(fx.RANGES_CPP, "case fx.FX_TYPE_SWING: return 75;", "case fx.FX_TYPE_SWING: return 80;")
            swing = next(e for e in fx.list_step_fx(m.root)["effects"] if e["id"] == "FX_TYPE_SWING")
            self.assertEqual(swing["max"]["value"], 80)
        finally:
            m.close()

    def test_unsupported_switch_shape_fails_loudly(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(fx.RANGES_CPP, "case fx.FX_TYPE_SWING: return 75;",
                   "case fx.FX_TYPE_SWING: if (forRandom) return 70; return 75;")
            with self.assertRaises(ParseError):
                fx.list_step_fx(m.root)
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
