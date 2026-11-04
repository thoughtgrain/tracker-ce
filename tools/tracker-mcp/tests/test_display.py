"""display_controls accuracy.

- Compiler ground truth: enControlId (complete via -Werror=switch, values), the slot/budget constants, the
  union of controlsStyle bits (so style_bits_free is what's really free), and sizeof() of every static image
  array a bitmap entry uploads.
- Re-derivation: classes, constructors, draw functions, createControl sites and image loads each point at a
  line that really says so; create-site counts match an independent grep.
- Mutation: changing a constant, a table row or disabling a class with #if changes the answer.
"""

import re
import unittest

from support import FIRMWARE_INCLUDES, FIRMWARE_PROBE_FLAGS, ROOT, Mirror, check_provenance, compile_and_run, line_text
from tracker_mcp import display


class CompilerGroundTruth(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = display.display_controls(ROOT)

    def _run(self, body, includes=('#include "displayStructs.h"',), defines=("PLATFORM_IMX", "FEATURE_SYNTH")):
        source = "\n".join(includes) + "\n#include <cstdio>\n" + body
        return compile_and_run(source, defines=defines, includes=FIRMWARE_INCLUDES, extra_flags=FIRMWARE_PROBE_FLAGS)

    def test_control_ids_and_constants(self):
        ids = self.data["control_ids"]
        cases = "\n".join(f"    case {i['id']}: return {i['value']};" for i in ids)
        prints = "\n".join(f'  std::printf("{i["id"]} %d\\n", (int){i["id"]});' for i in ids)
        consts = ("controlsCount", "controlsRamStartAddress", "controlsRamAddressStep")
        prints += "\n" + "\n".join(f'  std::printf("{c} %u\\n", (unsigned){c});' for c in consts)
        out = dict(line.split() for line in self._run(
            f"int f(enControlId e) {{ switch (e) {{\n{cases}\n  }} return -1; }}\nint main() {{\n{prints}\n}}\n"))
        for i in ids:
            self.assertEqual(int(out[i["id"]]), i["value"], i["id"])
        b = self.data["budgets"]
        self.assertEqual(int(out["controlsCount"]), b["control_slots"]["value"])
        self.assertEqual(int(out["controlsRamAddressStep"]), b["slot_bytes"]["value"])
        layout = self.data["ram_g_layout"]["plus"]["regions"]
        slots = next(r for r in layout if r["name"] == "control display-list slots")
        self.assertEqual(int(out["controlsRamStartAddress"]), slots["start"])

    def test_free_style_bits(self):
        out = self._run('int main() { unsigned u = 0;\n'
                        + "".join(f"  u |= (unsigned){m};\n" for m in self._style_names())
                        + '  std::printf("%u\\n", u); }\n')
        used = int(out[0])
        self.assertEqual(self.data["budgets"]["style_bits_free"], [b for b in range(32) if not used >> b & 1])

    def _style_names(self):
        text = (ROOT / display.STRUCTS_H).read_text()
        body = text[text.index("enum controlsStyle"):]
        body = body[body.index("{") + 1 : body.index("}")]
        body = re.sub(r"//[^\n]*", "", body)
        return [item.split("=")[0].strip() for item in body.split(",") if item.strip()]

    def test_bitmap_bytes_match_sizeof_image_arrays(self):
        for target in ("original", "plus"):
            bitmaps = [b for b in self.data["ram_g_layout"][target]["bitmaps"] if b["image_kind"] == "static data"]
            headers = sorted({b["image"].rsplit(":", 1)[0] for b in bitmaps})
            includes = ["#include <cstdint>"] + [f'#include "{ROOT / h}"' for h in headers]
            prints = "\n".join(f'  std::printf("{b["name"]} %zu\\n", sizeof({b["name"]}));' for b in bitmaps)
            out = dict(line.split() for line in self._run(f"int main() {{\n{prints}\n}}\n", includes=includes,
                                                           defines=()))
            for b in bitmaps:
                with self.subTest(target=target, bitmap=b["name"]):
                    self.assertEqual(int(out[b["name"]]), b["image_array_bytes"])


class Rederivation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = display.display_controls(ROOT)

    def test_provenance(self):
        self.assertEqual(check_provenance(self.data), [])
        for c in self.data["controls"]:
            self.assertIn(c["class"], line_text(c["declared"]))
            if c.get("constructor"):
                text = line_text(c["constructor"]) + line_text(c["constructor"].rsplit(":", 1)[0] + ":"
                                                               + str(int(c["constructor"].rsplit(":", 1)[1]) + 1))
                self.assertIn(c["control_id"], text, c["class"])
            if c.get("draws_in"):
                self.assertIn(f"{c['class']}::update", line_text(c["draws_in"]))

    def test_create_sites_match_grep(self):
        counts = {}
        for path in (ROOT / "core").rglob("*.cpp"):
            if "libs/NewDisplay" in str(path):
                continue
            for line in path.read_text(errors="replace").splitlines():
                code = line.split("//", 1)[0]
                for m in re.finditer(r"createControl\s*<\s*(\w+)\s*>", code):
                    counts[m.group(1)] = counts.get(m.group(1), 0) + 1
        reported = {c["class"]: c["instances_created_in_code"] for c in self.data["controls"]
                    if c["instances_created_in_code"]}
        self.assertEqual(reported, counts)

    def test_arrangement_is_declared_but_not_built(self):
        arr = next(c for c in self.data["controls"] if c["class"] == "cArrangement")
        self.assertEqual(arr["implemented_for"], [])

    def test_bitmap_rows_match_their_source_lines(self):
        # Each row's fields, in table order, must be exactly what that line of display.cpp says. This pins
        # the table parse itself (the sizeof check above only covers the image arrays).
        for target in ("original", "plus", "mini"):
            for b in self.data["ram_g_layout"][target]["bitmaps"]:
                rel, line = b["where"].rsplit(":", 1)
                lines = (ROOT / rel).read_text().splitlines()
                text = " ".join(lines[int(line) - 1 : int(line) + 8])
                row = re.search(r"\{[^}]*\}", text).group(0)
                fields = [f.strip() for f in row.strip("{} \n").split(",") if f.strip()]
                expected = [b["name"], str(b["address"]), str(b["bytes"]), str(b["width"]), str(b["height"]),
                            b["format"], str(b["stride"])]
                self.assertEqual(fields, expected, b["where"])

    def test_real_bitmap_table_is_consistent(self):
        for target, layout in self.data["ram_g_layout"].items():
            self.assertEqual(layout["warnings"], [], target)

    def test_image_loads_point_at_calls(self):
        for ld in self.data["ram_g_layout"]["plus"]["image_loads"]:
            self.assertIn("readImgFromSd", line_text(ld["where"]))


class Mutation(unittest.TestCase):
    PATHS = ["core"]

    def test_constant_change_moves_the_layout(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(display.STRUCTS_H, "controlsCount = 65", "controlsCount = 40")
            d = display.display_controls(m.root)
            hits = d["ram_g_layout"]["plus"]["control_slots_overlapped_by_image_loads"]
            self.assertEqual(max(max(v) for v in hits.values() if v), 39)
            self.assertEqual(d["budgets"]["control_slots"]["value"], 40)
        finally:
            m.close()

    def test_bad_bitmap_row_is_reported(self):
        m = Mirror(self.PATHS)
        try:
            m.edit(display.DISPLAY_CPP, "{ playIcon20x20, 110000, 200,", "{ playIcon20x20, 110000, 400,")
            warnings = display.display_controls(m.root)["warnings"]
            self.assertTrue(any("playIcon20x20" in w and "past the end" in w for w in warnings), warnings)
        finally:
            m.close()


if __name__ == "__main__":
    unittest.main()
