"""The root AGENTS.md is checked claim by claim.

Each factual statement in AGENTS.md is located by a regex and compared with what the tools (themselves
checked against the compiler, linker and make) or the source say. If the code changes, or someone edits a
claim, this fails and names the claim. Prose that isn't a checkable fact isn't covered here; the anchor
checks in test_anchors.py cover every path and symbol the guidance mentions.
"""

import re
import subprocess
import unittest

from support import ROOT
from tracker_mcp import build, display, memory, project_map, server

AGENTS = (ROOT / "AGENTS.md").read_text()


def claim(pattern, flags=re.S):
    m = re.search(pattern, AGENTS, flags)
    if not m:
        raise AssertionError(f"AGENTS.md no longer contains the claim matched by {pattern!r}; update this test "
                             "together with the text")
    return m


class Claims(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = build.build_config(ROOT)
        cls.memory = memory.memory_map(ROOT)
        cls.display = display.display_controls(ROOT)

    def test_display_budget(self):
        m = claim(r"display budget is ([\d,]+) bytes per frame")
        self.assertEqual(int(m.group(1).replace(",", "")),
                         self.display["budgets"]["frame_display_list_budget_bytes"]["value"])

    def test_fpu_flags(self):
        m = claim(r"\(`(-mfpu=[\w-]+)` on MK66, `(-mfpu=[\w-]+)` on IMX\)")
        for target, flag in (("original", m.group(1)), ("plus", m.group(2)), ("mini", m.group(2))):
            self.assertIn(flag, self.build["targets"][target]["compile_units"]["core (c++)"]["codegen"], target)

    def test_compiled_dirs(self):
        m = claim(r"`core/makefile` compiles every `\.cpp` under (.+?)\.\*\*")
        listed = set(re.findall(r"`([\w/]+)`", m.group(1)))
        self.assertEqual(listed, set(project_map._compiled_core_dirs(ROOT)))

    def test_audio_core_ram_placement(self):
        claim(r"`AUDIO_CORE_RAM` puts code in ITCM on IMX\. On MK66, both `AUDIO_CORE_RAM` and `AUDIO_CORE_FLASH` "
              r"land in flash")
        place = lambda t, src: next(p for p in self.memory["targets"][t]["placements"]  # noqa: E731
                                    if p["source"] == src)["placement"]["region"]
        for t in ("plus", "mini"):
            self.assertEqual(place(t, "AUDIO_CORE_RAM"), "SRAM_ITC")
        self.assertEqual(place("original", "AUDIO_CORE_RAM"), "FLASH")
        self.assertEqual(place("original", "AUDIO_CORE_FLASH"), "FLASH")

    def test_no_builtin(self):
        claim(r"`core/` builds with `-fno-builtin`")
        for target in build.TARGETS:
            cmds = build._commands(ROOT, build.dry_run(ROOT, target))
            core = [a for cwd, a in cmds if "-c" in a and str(cwd).endswith("/core")]
            self.assertTrue(core and all("-fno-builtin" in a for a in core), target)

    def test_interrupt_entry_calls(self):
        claim(r"`TrackerImpl::updateAudioBuffers` \(`core/source/Tracker\.cpp`\) runs in the audio DMA interrupt")
        text = (ROOT / "core/source/Tracker.cpp").read_text()
        body = text[text.index("void TrackerImpl::updateAudioBuffers"):]
        body = body[: body.index("\n}\n")]
        for call in ("midiUpdate();", "sequencer.tick(", "engine.update"):
            self.assertIn(call, body)

    def test_struct_size_contract(self):
        m = claim(r"`strInstrument` is (\d+) bytes")
        self.assertRegex((ROOT / "core/source/mtStructs.h").read_text(),
                         rf"static_assert\(sizeof\(strInstrument\) == {m.group(1)}")

    def test_crlf_share(self):
        m = claim(r"About (\d+) % of `core/` sources use CRLF")
        files = subprocess.run(["git", "-C", str(ROOT), "ls-files", "core"], capture_output=True, text=True,
                               check=True).stdout.split()
        sources = [f for f in files if f.endswith((".cpp", ".h"))]
        crlf = sum(1 for f in sources if b"\r\n" in (ROOT / f).read_bytes())
        self.assertLessEqual(abs(100 * crlf / len(sources) - int(m.group(1))), 5)

    def test_ci_ai_trailer_list(self):
        m = claim(r"CI fails any PR whose commits credit (.+?)\. Strip")
        named = {w.lower() for w in re.findall(r"[A-Za-z]+", m.group(1)) if w.lower() not in ("or", "a", "bot")}
        ci = (ROOT / ".github/workflows/build.yml").read_text()
        regex = re.search(r"Co-authored-by:\.\*\(([^)]+)\)", ci).group(1)
        in_ci = {w.lower() for w in regex.split("|") if w.isalpha()}
        self.assertEqual(named, in_ci)

    def test_guards(self):
        claim(r"`FEATURE_SYNTH` \(IMX only\)")
        claim(r"Never use `__MK66FX1M0__` in `core/`: it's defined on both platforms")
        g = self.build["guard_matrix"]
        self.assertEqual(g["FEATURE_SYNTH"], {"original": False, "plus": True, "mini": True})
        for target in build.TARGETS:
            self.assertIn("__MK66FX1M0__", self.build["targets"][target]["compile_units"]["core (c++)"]["defines"])
        defs = (ROOT / "core/interface/core/interfaceDefs.h").read_text()
        self.assertRegex(defs, r"#ifdef PLATFORM_IMX\s*\n#define GUI_HANDLE_SYNTH 1\s*\n#else\s*\n#define GUI_HANDLE_SYNTH 0")

    def test_toolchain_version(self):
        m = claim(r"GNU Arm toolchain `([\d.-]+)`")
        self.assertIn(m.group(1), (ROOT / ".github/workflows/build.yml").read_text())

    def test_every_tool_named_in_the_protocol_exists(self):
        section = claim(r"\*\*Change protocol\.\*\*(.+?)\n---", re.S).group(1)
        named = set(re.findall(r"`([a-z_]+)`", section))
        registered = {name for name in dir(server) if name in named and callable(getattr(server, name))}
        self.assertEqual(named, registered, "AGENTS.md names tools the server doesn't have")


if __name__ == "__main__":
    unittest.main()
