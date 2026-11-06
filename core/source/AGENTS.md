# core/source: sequencer, MIDI, project structs

This adds to the root [AGENTS.md](../../AGENTS.md); it doesn't replace it.

- **The sequencer and MIDI input run in the audio interrupt.** `TrackerImpl::updateAudioBuffers` calls `Sequencer::tick` every audio block. Every MIDI handler registered in `midiInit` (`core/source/mtMidi.cpp`) is dispatched by the port drivers when `midiUpdate()` reads the ports, inside the same interrupt. So before changing anything in `mtSequencer.cpp` or `mtMidi.cpp`, trace its callers to an entry point (root `AGENTS.md`, section 2). Much of the sequencer is reachable, including code that looks like plain UI glue.
- **Some code here already breaks the interrupt rules. Don't copy it, and don't fix it in passing.** Each of these is a change of its own:
  - MIDI Start in Song mode reaches `Sequencer::playSongFromPosition`, and a MIDI Stop reaches `Sequencer::stop`. Both can read or write the SD card from the interrupt (C1 and C2 in the `perf/optimization-plan` branch's plan).
  - `Sequencer::loadNextPattern` calls `cFileManager::loadWorkspacePattern`. Its `stopAutosave()` can spin on `WorkspaceProjectSave::update` until an autosave's sample write finishes, all from the interrupt.
  - A MIDI note-off on the Sample Recorder screen reaches `cSampleRecorder::hidePreviewValue`, which calls `display.setControlText()` from the interrupt.
- **Struct sizes are file formats.** `strInstrument` (`mtStructs.h`), `strEnv` (`mtEnvelopeGenerator.h`) and `Sequencer::strPattern` (`mtSequencer.h`) carry `static_assert`s on their size, because projects store them byte for byte. A failing assert means the change is wrong, not the number.
- **`PATTERN_FILE_VERSION` only goes up.** `cFileManager::checkVersionAndTranslate` upgrades every older pattern on load. A format change bumps the version and adds a translation step there, so old projects keep loading.
- **`Sequencer::internalTickAccumulatorUs` is `double` on purpose.** It accumulates microseconds across blocks, and float would drift against an external clock. It's the one `double` allowed on the interrupt path.
