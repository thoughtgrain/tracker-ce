# core/synth: synth engines (Tracker+ and Tracker Mini)

This adds to the root [AGENTS.md](../../AGENTS.md); it doesn't replace it.

- **Everything here is `FEATURE_SYNTH` only.** The Original Tracker never compiles it, so wrap new files in `#ifdef FEATURE_SYNTH` like `registry.cpp` does.
- **Voice code runs in the audio interrupt.** The PolySynth library is precompiled. Inside `Synth::renderAudioBuffer` (called from `PolySynth::update` every block), it calls your voice's `renderVoiceAudio()`, `onNoteOn()`, `onNoteOff()` and the other SDK overrides. Write them like any audio `update()`: no heap, no SD, no `double`, no UI. `interrupt_context` follows this through the library and shows the path.
- **Engine IDs are stored in projects.** Add new engines to the end of `InstrumentTypeId` (`synth/include/PolySynth/config.h`) and never renumber. To add one, follow the `synth-engine` recipe ([`docs/recipes/synth-engine.md`](../../docs/recipes/synth-engine.md)); the background is in the [developer guide §4](../../docs/developer_guide.md#4-synthesizer-engine-architecture--guidelines-tracker--tracker-mini) and [`docs/custom_synth_sdk.md`](../../docs/custom_synth_sdk.md).
- **Inactive engines should cost nearly no RAM.** Every registered engine exists from boot. Derive voices from `CustomSynthVoice` (static parameter tables), and put big tables in flash (`constexpr`) or `EXTERNAL_RAM`.
