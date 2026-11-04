"""interrupt_context: roots, paths and negatives, each checked against the source independently.

Every edge on a returned path is re-read from the cited line: a call edge's line must contain the callee's
name followed by `(`, and a MIDI dispatch edge must cite a registration line naming the handler. The
positives are the two interrupt SD-card findings from the optimization review (C1, C2). The negatives are
main-loop work that must not show up. Mutations prove the answer follows the source.
"""

import re
import unittest

from support import ROOT, Mirror, check_provenance, line_text
from tracker_mcp import interrupts
from tracker_mcp.source import ParseError
from tracker_mcp.scopes import SCAN


def _ic(fn="", root=ROOT):
    return interrupts.interrupt_context(root, fn)


def _chain(path):
    return [path["root"]["function"]] + [s["function"] for s in path["steps"]]


class Roots(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = _ic()

    def test_audio_callbacks_on_both_platforms(self):
        for kind, call in (("audio output DMA interrupt", "->updateAudioBuffers("),
                           ("audio input DMA interrupt", "->updateInputAudioBuffers(")):
            roots = [r for r in self.data["roots"] if r["kind"] == kind]
            self.assertEqual({r["evidence"].split("/")[1] for r in roots}, {"imx", "mk66"}, kind)
            for r in roots:
                self.assertIn(call, re.sub(r"\s+", "", line_text(r["evidence"])))

    def test_every_root_cites_its_definition_and_evidence(self):
        self.assertEqual(check_provenance(self.data), [])
        for r in self.data["roots"]:
            with self.subTest(root=r["function"]):
                self.assertIn(r["function"].split("::")[-1], line_text(r["where"]))
                ev = line_text(r["evidence"])
                if r["kind"] == "MIDI clock timer interrupt":
                    self.assertRegex(ev, rf"scheduleMidiClockUs\(.*\b{r['function']}\b")
                elif r["kind"].startswith("hardware interrupt ("):
                    self.assertRegex(ev, rf"attachInterrupt\(\s*&?{r['function']}\s*\)")

    def test_irq_handlers_found_inside_extern_c(self):
        names = {r["function"] for r in self.data["roots"] if r["kind"] == "hardware interrupt handler"}
        self.assertTrue({"TMR4_IRQHandler", "GPIO2_Combined_16_31_IRQHandler"} <= names, names)

    def test_agents_md_claim_direct_calls(self):
        """AGENTS.md: updateAudioBuffers calls midiUpdate(), sequencer.tick() and the engine."""
        for fn in ("midiUpdate", "Sequencer::tick", "audioEngine::updateAudioBuffers"):
            paths = _ic(fn)["paths"]
            audio = [p for p in paths if p["root"]["kind"] == "audio output DMA interrupt"]
            self.assertTrue(audio, fn)
            self.assertEqual(_chain(audio[0])[1:3], ["TrackerImpl::updateAudioBuffers", fn])


class Paths(unittest.TestCase):
    def _assert_real_edges(self, path):
        for step in path["steps"]:
            name = step["function"].split("::")[-1]
            if step["how"].startswith("dispatched by the MIDI port driver"):
                reg = re.search(r"registered at (\S+)\)", step["how"]).group(1)
                self.assertRegex(line_text(reg), rf"=\s*&?{name}\s*;|Action\(\s*&?{name}\s*\)")
            elif step["how"].startswith("called back by the precompiled PolySynth library"):
                self.assertIn("renderAudioBuffer(", line_text(step["called_at"]))
            else:
                self.assertRegex(line_text(step["called_at"]), rf"\b{name}\s*\(", step)

    def test_c1_midi_start_reaches_sd_save(self):
        data = _ic("cFileManager::saveWorkspacePatternNow")
        self.assertTrue(data["reachable"])
        midi = [p for p in data["paths"] if "receiveStart" in _chain(p)]
        self.assertTrue(midi, [_chain(p) for p in data["paths"]])
        self.assertIn("Sequencer::playSongFromPosition", _chain(midi[0]))
        self.assertEqual(midi[0]["compiled_for"], ["mini", "original", "plus"])
        for p in data["paths"]:
            self._assert_real_edges(p)
        self.assertEqual(check_provenance(data), [])

    def test_c2_stop_and_its_sd_load(self):
        for fn in ("Sequencer::stop", "cFileManager::loadWorkspacePatternNow"):
            data = _ic(fn)
            self.assertTrue(data["reachable"], fn)
            for p in data["paths"]:
                self._assert_real_edges(p)

    def test_synth_voice_render_is_imx_only(self):
        data = _ic("ChiptuneSynthVoice::renderVoiceAudio")
        self.assertTrue(data["reachable"])
        p = data["paths"][0]
        self.assertIn("PolySynth::update", _chain(p))
        self.assertEqual(p["compiled_for"], ["mini", "plus"])
        self._assert_real_edges(p)

    def test_main_loop_work_is_not_reachable(self):
        for fn in ("cDisplay::update", "TrackerImpl::update", "WorkspaceProjectLoad::update", "Sequencer::update",
                   "cFileManager::update", "midiInit"):
            with self.subTest(fn=fn):
                data = _ic(fn)
                self.assertFalse(data["reachable"], [_chain(p) for p in data["paths"]])
                self.assertIn("count", data["unresolved_calls_in_interrupt_code"])

    def test_unknown_function_raises(self):
        with self.assertRaises(ValueError):
            _ic("noSuchFunctionAnywhere")


class Mutations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = Mirror(SCAN)

    @classmethod
    def tearDownClass(cls):
        cls.m.close()

    def setUp(self):
        self.mtmidi = (self.m.root / "core/source/mtMidi.cpp").read_text()

    def tearDown(self):
        (self.m.root / "core/source/mtMidi.cpp").write_text(self.mtmidi)

    def test_planted_call_becomes_reachable(self):
        self.m.edit("core/source/mtMidi.cpp", "void receiveStart()\n{", "void receiveStart()\n{\n    display.update();")
        data = _ic("cDisplay::update", self.m.root)
        self.assertTrue(data["reachable"])
        self.assertIn("receiveStart", _chain(data["paths"][0]))
        self.assertIn("display.update();", line_text(data["paths"][0]["steps"][-1]["called_at"], self.m.root))

    def test_moved_clock_callback_raises(self):
        self.m.edit("core/source/mtMidi.cpp", "scheduleMidiClockUs(", "scheduleMidiClockLater(")
        with self.assertRaises(ParseError):
            _ic("", self.m.root)


if __name__ == "__main__":
    unittest.main()
