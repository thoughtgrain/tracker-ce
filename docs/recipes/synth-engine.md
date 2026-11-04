# Recipe: add a synth engine (Tracker+ and Tracker Mini)

How I add an open-source synth engine to the Model menu, in the order that keeps the tree building at every step.

## Before you start

- **Need it on the Original Tracker too?** Use the `open-synth-engine` recipe instead: SDK engines can't run there.
- Read [`docs/custom_synth_sdk.md`](../custom_synth_sdk.md) (the voice lifecycle and parameter tables) and `core/synth/AGENTS.md`.
- The closest worked example is the chiptune engine (`core/synth/engines/ChiptuneSynthVoice.cpp`). The TORO engine on the `feat/toro-synth` branch is a second one, with a host test suite. Grep `kChiptuneSynth` and `createChiptuneSynth`: every hit is a place your engine needs too.

## Steps

1. **Engine ID.** In `synth/include/PolySynth/config.h`, add your entry to `InstrumentTypeId` right after the last engine, and point `kLastSynth` at it. Projects and patches store this integer, so never reorder or renumber what's already there.
2. **Voice.** Create `core/synth/engines/<Name>SynthVoice.h` and `.cpp`, wrapped in `#ifdef FEATURE_SYNTH`. Derive the voice from `CustomSynthVoice`, whose static parameter tables keep inactive engines near zero RAM. Expose a factory like `createChiptuneSynth`.
3. **Register it.** Call your factory in `InstrumentTypeRegistry::registerInstrumentTypes` (`core/synth/registry.cpp`).
4. **Model menu.** Add your ID to `Synth::kSynthTypes` (`core/synth/Synth.h`). The menu order is this array's order, and the UI arrays below are sized from it.
5. **Names and icons.** Add the name to `synthModelNames` and the mini icon to `synthModelListImages` in `core/interface/instrumentEditor/SynthEditor.cpp`. Add the medium icon to `synthModelListImages` in `core/interface/instrumentEditor/SynthPatchEditor.cpp`.
6. **Bitmaps.** Add both icon IDs to `enDisplayBitmaps` (`core/libs/NewDisplay/displayStructs.h`) and the bitmaps to `bitmaps` in `core/libs/NewDisplay/display.cpp`. They live in graphics memory below `controlsRamStartAddress`, so check the space first (see `core/libs/NewDisplay/AGENTS.md`).

## Pitfalls

- **Everything in your voice runs in the audio interrupt.** The precompiled library calls `renderVoiceAudio()`, `onNoteOn()` and `onNoteOff()` from inside the render. No heap, no SD, no `double`, no UI.
- **The Original Tracker never compiles this.** Anything outside `#ifdef FEATURE_SYNTH` that names your engine breaks the MK66 build.
- **Parameter IDs are stored in patches too.** Once released, append new parameters; never reorder them.

## Verify

- Rung 2: a host test for the DSP core and the voice, the way the TORO suite on the `feat/toro-synth` branch does it (see the `host-test` recipe).
- Rung 3: the ARM build for plus and mini. Check the ITCM and DTC lines in the memory report.
- Rung 4: on a Tracker+ or Mini, select the engine, play notes, save and reload the project.
