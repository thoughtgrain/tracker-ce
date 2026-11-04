"""Factual claims in the area AGENTS.md files, each checked against the tools or the source.

Like test_guidance.py for the root file: each claim is located by a regex (so editing it without updating
this test fails), then compared with the code. The known interrupt-rule violations are checked with
interrupt_context, so fixing one fails here too, as a reminder to drop it from the guidance.
"""

import re
import unittest

from support import ROOT
from tracker_mcp import display, interrupts


def claim(rel, pattern):
    m = re.search(pattern, (ROOT / rel).read_text(), re.S)
    if not m:
        raise AssertionError(f"{rel} no longer contains the claim matched by {pattern!r}; update this test with it")
    return m


def _reachable_via(fn, via):
    data = interrupts.interrupt_context(ROOT, fn)
    chains = [[p["root"]["function"]] + [s["function"] for s in p["steps"]] for p in data["paths"]]
    return any(via in c for c in chains), chains


def _body(rel, signature):
    text = (ROOT / rel).read_text().replace("\r\n", "\n")
    start = text.index(signature)
    return text[start : text.index("\n}\n", start)]


class CoreSource(unittest.TestCase):
    rel = "core/source/AGENTS.md"

    def test_known_interrupt_violations_are_still_there(self):
        claim(self.rel, r"MIDI Start in Song mode reaches `Sequencer::playSongFromPosition`")
        ok, chains = _reachable_via("Sequencer::playSongFromPosition", "receiveStart")
        self.assertTrue(ok, chains)
        claim(self.rel, r"a MIDI Stop reaches `Sequencer::stop`")
        # The shortest path from the audio root goes through tick(), so check the MIDI route on its own.
        model = interrupts.build_model(ROOT)
        start = interrupts._targets(model, "receiveStop")
        self.assertTrue(start)
        self.assertIsNotNone(interrupts._path(model, start[0], set(interrupts._targets(model, "Sequencer::stop"))))
        self.assertTrue(interrupts.interrupt_context(ROOT, "receiveStop")["reachable"])
        claim(self.rel, r"`Sequencer::loadNextPattern` calls `cFileManager::loadWorkspacePattern`\. Its `stopAutosave\(\)` "
                        r"can spin on `WorkspaceProjectSave::update`")
        ok, chains = _reachable_via("WorkspaceProjectSave::update", "Sequencer::loadNextPattern")
        self.assertTrue(ok, chains)
        self.assertIn("while (operationStep == WorkspaceProjectSaveStep::saveInstrumentSample)",
                      _body("core/FileManager/WorkspaceProjectSave.cpp", "void WorkspaceProjectSave::stop()"))
        claim(self.rel, r"note-off on the Sample Recorder screen reaches `cSampleRecorder::hidePreviewValue`, which calls "
                        r"`display\.setControlText\(\)`")
        ok, chains = _reachable_via("cDisplay::setControlText", "cSampleRecorder::hidePreviewValue")
        self.assertTrue(ok, chains)
        self.assertIn("display.setControlText", _body("core/interface/sampleRecorder/gSampleRec.cpp",
                                                      "void cSampleRecorder::hidePreviewValue()"))

    def test_struct_size_contracts(self):
        claim(self.rel, r"`strInstrument` \(`mtStructs\.h`\), `strEnv` \(`mtEnvelopeGenerator\.h`\) and "
                        r"`Sequencer::strPattern` \(`mtSequencer\.h`\) carry `static_assert`s")
        for rel, name in (("core/source/mtStructs.h", "strInstrument"), ("core/source/mtEnvelopeGenerator.h", "strEnv"),
                          ("core/source/mtSequencer.h", "strPattern")):
            self.assertRegex((ROOT / rel).read_text(), rf"static_assert\(sizeof\({name}\)", rel)

    def test_tick_accumulator_is_double(self):
        claim(self.rel, r"`Sequencer::internalTickAccumulatorUs` is `double`")
        self.assertRegex((ROOT / "core/source/mtSequencer.h").read_text(), r"\bdouble\s+internalTickAccumulatorUs\b")

    def test_pattern_versions_are_translated_on_load(self):
        claim(self.rel, r"`cFileManager::checkVersionAndTranslate` upgrades every older pattern on load")
        body = _body("core/FileManager/fileManagerPattern.cpp", "void cFileManager::checkVersionAndTranslate")
        self.assertIn("< PATTERN_FILE_VERSION", body)


class Audio(unittest.TestCase):
    rel = "core/libs/Audio/AGENTS.md"

    def test_update_all_and_export(self):
        claim(self.rel, r"`artificial_isr` calls every node's `update\(\)` when audio export runs, while "
                        r"`AudioStream::renderMode` makes `AudioStream::update_all` return early")
        update_all = _body("core/libs/Audio/AudioStream.cpp", "AUDIO_CORE_RAM void AudioStream::update_all")
        self.assertRegex(update_all, r"if \(AudioStream::renderMode\)\s*return;")
        self.assertIn("p->update();", _body("core/libs/Audio/AudioStream.cpp", "void artificial_isr(void)"))
        self.assertRegex((ROOT / "core/Exporter/mtAudioExport.cpp").read_text(), r"\bartificial_isr\(\);")

    def test_allocate_can_return_null(self):
        claim(self.rel, r"`AudioStream::allocate` can return `NULL`")
        self.assertIn("return NULL;", _body("core/libs/Audio/AudioStream.cpp", "AUDIO_CORE_RAM audio_block_t* AudioStream::allocate"))


class Display(unittest.TestCase):
    rel = "core/libs/NewDisplay/AGENTS.md"

    def test_budget_loop_order_and_cutoff(self):
        m = claim(self.rel, r"reverse creation order and stops at the first one that doesn't fit the ([\d,]+)-byte budget")
        budget = display.display_controls(ROOT)["budgets"]["frame_display_list_budget_bytes"]["value"]
        self.assertEqual(int(m.group(1).replace(",", "")), budget)
        step3 = _body("core/libs/NewDisplay/display.cpp", "void cDisplay::step3()")
        self.assertRegex(step3, r"for \(int8_t i = controlsCount - 1; i >= 0; i--\)")
        self.assertRegex(step3, rf"(?s)< {budget}\)\s*\{{.*?\}}\s*else\s*\{{\s*break;")
        self.assertIn("step3();", _body("core/libs/NewDisplay/display.cpp", "void cDisplay::update()"))


class Synth(unittest.TestCase):
    rel = "core/synth/AGENTS.md"

    def test_feature_synth_only(self):
        claim(self.rel, r"wrap new files in `#ifdef FEATURE_SYNTH` like `registry\.cpp` does")
        self.assertRegex((ROOT / "core/synth/registry.cpp").read_text(), r"#ifdef FEATURE_SYNTH")

    def test_voice_render_runs_in_interrupt_on_imx_only(self):
        claim(self.rel, r"Inside `Synth::renderAudioBuffer` \(called from `PolySynth::update` every block\)")
        data = interrupts.interrupt_context(ROOT, "ChiptuneSynthVoice::renderVoiceAudio")
        self.assertTrue(data["reachable"])
        self.assertEqual(data["paths"][0]["compiled_for"], ["mini", "plus"])
        self.assertIn("PolySynth::update", [s["function"] for s in data["paths"][0]["steps"]])


class FileManager(unittest.TestCase):
    rel = "core/FileManager/AGENTS.md"

    def test_update_runs_in_the_main_loop(self):
        claim(self.rel, r"a state machine driven by `cFileManager::update` from the main loop")
        self.assertIn("newFileManager.update();", _body("core/source/Tracker.cpp", "void TrackerImpl::update()"))
        self.assertFalse(interrupts.interrupt_context(ROOT, "cFileManager::update")["reachable"])


class Platforms(unittest.TestCase):
    rel = "platforms/AGENTS.md"

    def test_same_name_in_both_trees(self):
        claim(self.rel, r"the same function name can exist in both \(`dataTransferComplite` does\)")
        for plat in ("imx", "mk66"):
            self.assertRegex((ROOT / f"platforms/{plat}/source/DisplayDriverImpl.cpp").read_text(),
                             r"(?m)^void dataTransferComplite\(\)\s*$")

    def test_clock_timer_roots(self):
        claim(self.rel, r"`TMR4_IRQHandler` on IMX, an IntervalTimer in `platforms/mk66/source/TimerImpl\.cpp` on MK66")
        roots = {r["function"] for r in interrupts.interrupt_context(ROOT)["roots"]}
        self.assertIn("TMR4_IRQHandler", roots)
        self.assertIn("IntervalTimer", (ROOT / "platforms/mk66/source/TimerImpl.cpp").read_text())

    def test_platform_interfaces_are_pure_virtual(self):
        claim(self.rel, r"Nearly every method there is pure virtual")
        bodies = re.findall(r"virtual[^;{]*?(=\s*0\s*;|\{)", (ROOT / "core/include/Platform.h").read_text())
        pure = sum(1 for b in bodies if b.startswith("="))
        self.assertGreaterEqual(pure / len(bodies), 0.95, f"{pure} of {len(bodies)}")


if __name__ == "__main__":
    unittest.main()
