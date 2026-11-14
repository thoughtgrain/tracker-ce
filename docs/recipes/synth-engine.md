# Recipe: add a synth engine (`.psyn` plugin for Tracker+ and Tracker Mini)

How I add a synth engine since engines became SD-card plugins: write a voice, build it into a `.psyn`, and copy it to `/Synths`. No firmware change, no menu table, no bitmaps.

## Before you start

- Read [`docs/custom_synth_sdk.md`](../custom_synth_sdk.md) (plugin ABI, voice lifecycle, parameter metadata) and `core/synth/AGENTS.md`.
- The worked example is the CHIP engine, `synth-plugin-sdk/engines/chip.cpp`: a `ChiptuneSynthVoice` derived from `CustomSynthVoice` and exported with `PSYN_REGISTER_CPP_VOICE`. For a plain C engine, start from `synth-plugin-sdk/examples/simple_sine.c`.

## Steps

1. **Voice.** Create `synth-plugin-sdk/engines/<name>.cpp`. Derive the voice from `CustomSynthVoice` (`synth/include/PolySynth/sdk/SynthVoiceSDK.h`), with a parameter ID enum and static parameter tables like `ChipParamId` in `chip.cpp`. End the file with `PSYN_REGISTER_CPP_VOICE(<YourVoice>)`.
2. **Engine ID.** Pick a fixed ID and pass it to `psyn_builder.py` as `--type-id-play-tracker`. Don't leave it at 0: dynamic IDs are given out in file-name order, so they move when someone adds a plugin, and projects store the number. A built-in engine also gets an entry at the end of `InstrumentTypeId` (`synth/include/PolySynth/config.h`), never renumbering the ones before it.
3. **Build.** In `synth-plugin-sdk/Makefile`, copy CHIP's two rules (`.elf`, then `.psyn` through `psyn_builder.py`) with your `--code-name`, `--display-name`, ID and polyphony, and add the `.psyn` to `all`. `make psyn` from the repo root builds the plugins and copies them, with the prebuilt factory ones, to build/Synths in the repo root.
4. **Install.** Copy `<NAME>.psyn` to `/Synths` on the card. `PsynLoader::scanAndRegister` finds it at the next boot and the Model menu lists it.

## Pitfalls

- **Your render code runs in the audio interrupt.** `voice_render_audio` is called from `Synth::renderAudioBuffer` every block. No heap, no SD (`PsynHostApi` file and WAV calls are for set-up only), no `double`, no UI.
- **Plugins are Cortex-M7 only.** The Original Tracker can't load them. The plugin is built with `-ffast-math` and `-fsingle-precision-constant`, so float results can differ from a host build of the same code.
- **The arena is shared.** All loaded engines share `PsynLoader::kArenaSize` (1,920 KB); an engine that doesn't fit evicts idle ones, and one bigger than the arena never loads. Keep tables `constexpr`.
- **Parameter IDs are stored in patches.** Once released, append new parameters; never reorder them.

## Verify

- Rung 2: a host test for the DSP core and the voice (see the `host-test` recipe). Build the voice on the host without the bridge, the way the TORO suite on the `feat/toro-synth` branch tests its voice.
- Rung 3: `make psyn` with the GNU Arm toolchain, plus `make plus` if you touched `synth/source` or `core/synth`.
- Rung 4: on a Tracker+ or Mini, copy the `.psyn` to `/Synths`, reboot, select the engine, play notes, save and reload the project, then add another plugin and check the project still opens your engine.
