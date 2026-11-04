# Recipe: add a step FX

How I add a pattern step FX: a new entry in the FX menu that the sequencer stores per step and plays back.

## Before you start

- Read `core/source/AGENTS.md`: FX are applied by the sequencer, inside the audio interrupt.
- Pick the existing FX closest to yours. A sound one like `FX_TYPE_PANNING`, or a sequencer one like `FX_TYPE_ROLL`. **Grep its name: every hit is a place yours probably needs too.** That's more reliable than any list, including the one below.

## Steps

1. **ID.** Choose the stored ID in `Sequencer::strFxConsts::enFxType` (`core/source/mtSequencer.h`). Patterns store this number. The most recent FX (`FX_TYPE_AMP_ENV`, `FX_TYPE_FILTER_ENV`, `FX_TYPE_GRANULAR_LENGTH`) reuse reserved placeholders (`FX_TYPE_R1`, `FX_TYPE_R30`, `FX_TYPE_R31`) instead of appending. `FX_TYPE_R2` and a few others aren't used by any current FX, but patterns saved by old firmware may still contain them, and those steps would start triggering yours. Whichever you choose, say why in the PR, and never renumber an existing value.
2. **Range.** Add cases to `Sequencer::getFxMin`, `Sequencer::getFxMax` and `Sequencer::getFxDefault` (`core/source/mtSequencer_patternEdit.cpp`). Add the `forRandom` bounds if randomizing the full range would sound wrong.
3. **Value label.** If the raw value isn't what people should see (an offset, a note, a ratio), handle it in `Sequencer::getFxValueCorrection` and `Sequencer::makeFxValLabelWithAlgin`.
4. **Menu.** In `core/interface/core/interfaceDefs.h`, add the ID to `fxIDs` and the names to `fxNames` and `fxShortNames`, at the same menu position, then bump `FX_COUNT` (`core/source/mtStructs.h`). Add the help text to `fxDescriptions` (`core/interface/core/interfaceFxDesc.h`) at that same position.
5. **Behaviour.**
   - A sound FX on sample instruments: add a case to `playerEngine::seqFx`, and to its end-of-FX counterpart, in `core/AudioEngine/mtAudioSequencer.cpp`.
   - Synths: map the FX to a synth parameter in `core/AudioEngine/mtAudioEngine.cpp`.
   - Timing or step FX: add it to the step handling in `core/source/mtSequencer.cpp`.

## Pitfalls

- **All of step 5 runs in the audio interrupt.** No SD, heap, `double` or UI. Ask `interrupt_context` about each function you change.
- **The FX tables are parallel arrays.** `fxIDs`, `fxNames`, `fxShortNames` and `fxDescriptions` must stay in the same order. One off-by-one shifts every name after yours.
- **Devices can differ.** Some FX names and behaviour are under `FEATURE_SYNTH`; check the `#ifdef`s around the code you copy.

## Verify

- Rung 1: syntax check. Rung 3: the ARM build for all three devices.
- Rung 4 and 6: put the FX on steps, play, save, reload and check the value survives. Also load a pattern saved before your change and check nothing else moved.
