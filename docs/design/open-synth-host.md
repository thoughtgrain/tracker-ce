# Open synth engines on all three Trackers: plan

> Status: **stage 1** done (the engine layer, ACID and TORO); **stage 2** done on Tracker+ and Mini (open engines play through the SDK; TORO runs on its open engine; ACID is in the Model menu). On branch `feat/open-synth-host`, stacked on the TORO branch (`feat/toro-synth`).

## Why

Polyend's synth library (`synth/lib/libPolySynth.a`) is built for the Cortex-M7 with an FPv5 FPU, and its licence names Tracker+ and Tracker Mini. So the Original Tracker has no synths at all in CE, and every engine added through the SDK (Chiptune, TORO) inherits that limit.

OpenTracker ([sandlbn/polyend-tracker-re](https://github.com/sandlbn/polyend-tracker-re), Apache-2.0) is a separate firmware for the Original Tracker. It has five synth engines written in plain C++ for exactly the Original's Cortex-M4, with all state in fixed-size blocks and nothing allocated on the audio path. Those engines can run on all three devices.

## What we take, and what we don't

**Only the sound engines.** From OpenTracker I take the engine interface the engines are written against, the engines themselves, and the numbers they expect (parameter ranges and defaults). Nothing else: not its framework (voice pool, macros, mixing), patch codec, sequencer, tests, or any of its UI. Parameter names, editor layouts, value display, macros and badges are written for CE's editor.

Everything that hosts the engines in CE gets written for CE: voice allocation, macros, the audio node, patches and screens. That keeps the CE side shaped like CE, and keeps OpenTracker's own design decisions out of it.

| OpenTracker | CE name | What it is |
| :--- | :--- | :--- |
| ACD | **ACID** | Acid bass and lead: saw / pulse / sub, three filters (12 dB, 24 dB SVF, ladder), glide |
| FAT | **SWARM** | A stack of detuned saws |
| VAP | **PRISM** | Virtual-analog poly: morphing oscillators, sync, 15 filter types, a modulation matrix |
| WTFM | **GLINT** | Two-operator wavetable FM |
| PERC | **KIT** | A drum kit (its voices share one kit's state, so it comes last) |

**TORO** joins them too. It's CE's own engine (`docs/toro_synth.md`), not from OpenTracker. Its DSP, `ToroCore.h`, already had no dependencies, so it moved into `core/synth/open/engines/`. Both builds use that one file: the SDK voice on Tracker+ and Mini, and the open engine `Toro.cpp`, which can run on all three. `tests/toro/open_parity_test.cpp` drives both with the same notes and requires identical samples. That's 94 cases: every parameter, all glide modes, envelope edges and random patches. A planted 0.1 % change to the attack failed 92 of them. The parity test also caught a real bug while I wrote it: envelope-stage names shadowed the Attack, Decay, Sustain and Release parameter IDs.

They're renamed because Polyend's own ACD, FAT, VAP, WTFM and PERC are different engines. Reusing the names would make "ACD" mean two different sounds depending on the firmware.

## Editor layout and icons

Each open engine has a section layout in `core/synth/open/OpenSynthLayout.cpp`: which parameters go in which section of the patch editor's grid, and the icon each section gets. `sectionIcon` applies the patch editor's own word rules. `tests/opensynth/layout_test.cpp` reads `SynthPatchEditor.cpp` to check it uses the same words in the same order, and that every engine fits the grid with every parameter shown once. TORO's layout is its SDK layout; the parity test compares them.

The icon images are still only compiled for Tracker+ and Mini (`FEATURE_SYNTH`). They move to all three devices, about 2.2 KB of flash, in the same change that gives the Original Tracker a synth editor (stage 3), so they don't sit unused in the Original build before then.

## How a port is checked

- **The source diff.** Each ported file differs from OpenTracker's only by names, include paths and the fast-RAM marker, and its header says so. The diff is the proof that the DSP is unchanged. Re-run it when updating from upstream.
- **Host tests** (`tests/opensynth/`, plain `g++` with ASan, UBSan and `-Werror`). These don't run OpenTracker's code; they test the port.
  - **Regression goldens:** 66 render scenarios hashed bit for bit, so any change to the sound fails. A planted 2 % gain change failed all 66.
  - **Engine contract:** every parameter changes the sound except Volume, which the host applies. Host gain scales linearly. Output is finite, and every voice ends, including for random patches.
- **Rung 1:** `make -C tests/opensynth syntax` compiles the engines through the firmware's real `config.h` with `-fno-builtin`, for MK66 and IMX.

## Stages

1. **Engine layer, ACID and TORO** (this branch): `core/synth/open/` with the interface, ACID, TORO, their parameter tables and the registry, plus the tests. Nothing calls it yet, so firmware behaviour doesn't change.
2. **Tracker+ and Mini, through Polyend's SDK.** There, Polyend's library already does voice allocation, macros, patches, the Model menu and the patch editor, so an adapter (`core/synth/engines/OpenEngineVoice.h`) presents any open engine to it as an SDK voice. Writing a second host for these devices would duplicate all of that.
   - **2a (done):** the adapter, and TORO switched to its open engine. The old SDK voice's sound and note logic is deleted; its file keeps only the Tracker+/Mini metadata. Before the switch I recorded the original SDK voice's output in 103 cases (`tests/toro/sdk_regression_test.cpp`): every parameter, glide and note fallback, envelope edges, voices with their own noise seeds, Volume and Cutoff moving mid-note, pitch bend and random patches. The switched build reproduces all 103. The one bit-level difference, in silence after a voice ends, is `-0.0` becoming `+0.0`, and the hash treats those as equal. A planted bug in the adapter (every voice the same noise seed) failed the voice cases.
   - **2b (done):** ACID in the Model menu on Tracker+ and Mini: `kAcidSynth` appended to `InstrumentTypeId`, its SDK file (`AcidSynthVoice.cpp`, presentation and macros only), badges from `gen_acid_badges.py`, and `tests/acid/` (metadata against the open tables, and the SDK-hosted voice against the open engine: 122 cases, sample for sample). Two generic helpers in `OpenEngineVoice.h` (`addOpenEngineParams`, `openEngineLayout`) take names, defaults, ranges and the layout from the open tables, so the next engines only add presentation.
   - The Original Tracker still needs CE's own host (stage 3), because the SDK doesn't exist there.
3. **The Original Tracker.** First measure the audio headroom on hardware (rung 5) with all 8 tracks busy, then enable the host there. The synth screens are CE's own Tracker+ / Mini synth editor, adapted to run on the Original as well: it currently reads Polyend's `Patch` API, so it gets a data source fed by the open engines' tables (names, ranges, layouts, `sectionIcon`). No OpenTracker screens or UI are ported.
4. **SWARM, PRISM and GLINT**: each is one engine file, one table and one registry line, with its own goldens.
5. **KIT**, with a shared per-slot kit in the host.

## Risks I know about

- **CPU on the Original.** OpenTracker measured 62 to 73 % of the audio block budget with 4 worst-case synth voices plus 8 stereo tracks, in an audio engine built for that. CE's Original engine is a different design, and nobody has measured its headroom. Stage 3 starts with that measurement.
- **Fast code RAM on IMX.** The hot kernels go in ITCM (`AUDIO_CORE_RAM`), and the developer guide measured about 22 KB free. Stage 2's CI memory report has to confirm it fits.
- **Provenance.** OpenTracker describes its engines as clean-room with their own sound design. That's their claim, and the reason this port takes their engines as they are, with attribution, rather than anything derived from Polyend's library.
