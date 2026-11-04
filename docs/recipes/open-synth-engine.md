# Recipe: add an open synth engine (all three Trackers)

How I add a sound engine under `core/synth/open/`, the engine layer that runs on the Original Tracker as well as Tracker+ and Mini, either written fresh or ported from OpenTracker.

## Before you start

- **Pick the right recipe.** This one is for engines that must run on all three devices. An engine that only needs Tracker+ and Mini, built on Polyend's SDK, follows the `synth-engine` recipe instead. Background: [`docs/design/open-synth-host.md`](../design/open-synth-host.md).
- **Know where this stands.** Only the engine layer exists yet (stage 1 of that plan). An engine added here builds and is tested on the host, but nothing plays it until the CE host lands (stage 2). Don't wire it into `InstrumentTypeId` or the Model menu on your own; that's the host's job.
- Read `core/synth/AGENTS.md` and the contract at the top of `core/synth/open/OpenSynthEngine.h`.
- **An engine that also has an SDK build** (TORO is the example): share the DSP from `core/synth/open/engines/`, mirror the SDK voice's note, glide and envelope code in the open engine, and add a parity test like `tests/toro/open_parity_test.cpp` that requires identical samples from both.
- The worked example is ACID: `core/synth/open/engines/Acid.cpp`, its table in `core/synth/open/OpenSynthParams.cpp` and its tests in `tests/opensynth/`. Grep `kOpenEngineAcid`: every hit is a place your engine needs too.

## Steps

1. **ID.** Append your engine to `OpenEngineId` in `core/synth/open/OpenSynthEngine.h`. Never renumber one that exists; once the host saves patches, these numbers end up in files.
2. **The engine.** Add `core/synth/open/engines/<Name>.h` and `.cpp` implementing `ISynthEngine`. Keep per-voice state in `SynthVoice::state` (128 bytes) and per-slot state in `SynthSlotScratch` (384 bytes); `as<T>()` has a `static_assert` that fails the build if yours is bigger. Mark only the per-sample kernels `OPENSYNTH_HOT`.
3. **Porting from OpenTracker?** Copy the file and change only names, include paths and OpenTracker's fast-RAM macro (to `OPENSYNTH_HOT`), so `diff` against upstream stays the proof that the DSP is unchanged. Put a header at the top naming the upstream file and commit and listing every change, like `Acid.cpp`'s. Use the CE name from the design doc (SWARM, PRISM, GLINT or KIT), because Polyend's own models already own the original names.
4. **Parameters.** Add the table to `OpenSynthParams.cpp` (name, default, min, max, in the engine's index order), with a `static_assert` that its length equals the engine's parameter count. When porting, check every number against upstream mechanically, not by eye.
5. **Register it.** Add one line to `openSynthEngine` in `core/synth/open/OpenSynthRegistry.cpp`, and the file to `CE_SRCS` in `tests/opensynth/Makefile`.
6. **Tests.** The scenarios in `tests/opensynth/scenarios.h` are written for ACID's parameters today. Add your engine's own scenarios: every section of the engine, glide or retrigger modes if it has them, parameter ramps and random patches. Then run the same contract checks ACID has (every parameter matters except Volume, linear gain, finite output, every voice ends), and record goldens with `make -C tests/opensynth print-goldens`.
7. **Attribution.** If it's ported, add it to the OpenTracker entry in `NOTICE.md`.

## Pitfalls

- **No Polyend code here.** No `PolySynth` includes, no `FEATURE_SYNTH` guard: this directory has to build for the Original Tracker, where neither exists.
- **Everything runs in the audio interrupt.** No heap, no SD, no `double`, no UI, no locks.
- **Don't apply Volume or velocity.** The host applies them through the gain ramp passed to `render()`. The contract test fails an engine that does.
- **The sample rate isn't the same everywhere.** `kSampleRate` is 44,100 Hz on IMX and 44,117.647 Hz on MK66. Derive coefficients from it; never hard-code 44100.
- **Fast code RAM is small on IMX.** `OPENSYNTH_HOT` puts code in ITCM, which had about 22 KB free when the developer guide measured it. Check the CI memory report.
- **The Original's CPU is the tight one.** OpenTracker measured 4 worst-case synth voices plus 8 tracks at 62–73 % of the audio block budget. Keep block-rate work out of the per-sample loops.

## Verify

- Rung 1: `make -C tests/opensynth syntax` compiles the engines through the firmware's real `config.h`, for MK66 and IMX.
- Rung 2: `make -C tests/opensynth` runs the goldens and the contract checks. Plant a small change in the engine once and check the goldens fail.
- Rung 3: the PR's CI build for all three devices, including the ITCM line of the memory report.
- Listening (rung 6) has to wait until the host plays engines.
