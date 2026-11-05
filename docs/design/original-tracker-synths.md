# Synths on the Original Tracker: stage 3 plan

> Status: **planning, waiting on the hardware measurement.** Stage 3 of [`open-synth-host.md`](open-synth-host.md). Nothing here changes the firmware yet. Decided: open-engine patches on the Original use a CE patch format (below).

Tracker+ and Mini get their synth slots from Polyend's precompiled library. The Original Tracker can't run that library (it's built for the Cortex-M7 and licensed for Tracker+ and Mini), so today the Original has no synths at all. The open engines (TORO, ACID, and the ones to come) run on its Cortex-M4, so what the Original is missing is a **host**: the code that plays engines in the synth slots, applies macros, mixes them, saves their settings and shows them in the editor. That host is CE's own, written for CE. Only the sound engines come from elsewhere, and none of OpenTracker's UI or framework.

---

## Gate: is there CPU for it?

Before any host code goes in, I want a number from a real Original. The audio interrupt does all the sample work, and synth voices would add to it.

**How to measure** (rung 5, about ten minutes):
1. Put an empty file named `.develop` in the root of the SD card. That turns on developer mode (`Tracker.cpp` checks for it at boot).
2. Flash the current CE firmware on an **Original Tracker** and boot.
3. Load the heaviest project you have: all 8 tracks playing at once, long stereo samples, delay and reverb on, the master effects on.
4. Open **Config**. In developer mode it shows the audio load as `min: X%, max: Y%`, from the `Profiler` that times every audio block.
5. Let the song play for a minute and note the **max**. Repeat with a second heavy project.

**What the number means:**
- **Under about 55 % max:** room for 4 synth voices, the default I'd ship.
- **55–70 %:** 2 voices by default, and the host's CPU guard (below) does real work.
- **Over 70 %:** synths on the Original need optimizations from the `perf/optimization-plan` branch first.

These thresholds are estimates, not measurements. OpenTracker counted a worst-case voice of these engines at 17,000–30,000 instructions per audio block (its `tests/perf_baseline.txt`, simulated Cortex-M4). Against the block's 522,240 cycles at 180 MHz, that's roughly 3–6 % per voice before flash wait states, and running from flash on CE's Original adds to it. The measurement is what decides.

---

## Where the host plugs in

CE's synth code talks to Polyend's library through a small number of seams. On the Original, CE's host takes each one.

| Seam | Today (Tracker+ / Mini) | On the Original |
| :--- | :--- | :--- |
| **Notes** | The sequencer sends MIDI to the synth through `Synth::getVirtualMidiPort()` (`core/source/mtMidi.cpp`, one channel per slot) | The host implements that same `::Midi` port: note on / off, pitch bend, CCs |
| **Step FX** | `audioEngine::seqSynthFx` turns synth FX into parameter changes (`core/source/mtSequencer.cpp`, `core/AudioEngine/mtAudioEngine.cpp`) | The same entry point, writing into the host's parameters |
| **Audio** | `PolySynth::update` (`core/libs/Audio/synth_PolySynth.cpp`) renders the library into the mixer's synth inputs | A CE audio node renders the open engines into the same inputs |
| **Slot settings** | `Synth` (`core/synth/Synth.h`): model, gain, pan, sends, voices, macros | A second implementation of the same class interface |
| **Patches and the editor** | The editor, macros and file manager use Polyend's `Patch` API: metadata, get and set values, edit-mapped values, macro editing | A CE "patch view" interface with two implementations: one wrapping Polyend's `Patch`, one wrapping the open engines' tables and values |

`FEATURE_SYNTH` means two things today, "this device has synth slots" and "Polyend's library is here", and only the second is false on the Original. Stage 3 splits them. `FEATURE_SYNTH` keeps meaning the library. A new guard, defined on all three devices, means "synth slots exist". Each of the roughly 45 guarded places moves to whichever meaning it actually has, one by one, with the reason in the PR. That split also gives the Original's mixer its synth inputs (`kSynthParts` is 0 there today).

---

## What CE writes (not taken from anywhere)

- **Voice allocation.** A fixed pool, 4 voices by default on the Original, shared by the three slots, with per-slot polyphony. When the pool is full, steal the quietest voice in release first, then the oldest. The engines' `SynthVoice::index` comes from the pool slot, so noise seeds stay per voice.
- **Macros.** Macros 1–6 add their modifiers to the parameters, and Velocity scales Volume, the same model the Tracker+ / Mini editor already shows. They're evaluated once per block and handed to the engine as a start and end ramp.
- **CPU guard.** If `Profiler` reports the audio block above a threshold, new notes get fewer voices before anything clicks. Sample tracks are never touched.
- **The audio node.** It converts the engines' float output to the mixer's int16 blocks, with per-slot gain, pan and sends.
- **The editor.** CE's own Tracker+ / Mini synth screens (`SynthEditor.cpp`, `SynthPatchEditor.cpp`, `SynthMacros.cpp`), moved onto the patch-view interface so they run on the Original too. The section layouts and icons already exist (`core/synth/open/OpenSynthLayout.cpp`). The section icons and engine badges move out of `FEATURE_SYNTH`, about 2.2 KB of flash plus the badges, in the same change that turns the editor on.
- **The Model menu on the Original** lists only the open engines. A project from a Tracker+ that uses a Polyend model (ACD, FAT…) still opens. That slot keeps its settings, shows the model as not available on this Tracker, and stays silent.

---

## Patch files (decided: a CE format)

Synth patches are saved as `.patch` files. On Tracker+ and Mini, only Polyend's library reads and writes them, and the format isn't published. The Original's host needs to save open-engine patches somewhere. There are three ways:

1. **Ask Polyend for the `.patch` format** (they maintain CE) and implement it in CE. Patches then move freely between all three devices. This is the best result, but it depends on them.
2. **A CE patch format for open engines**, documented in this repo. The Original reads and writes it. Tracker+ and Mini keep saving through the library, so an open-engine patch made on a Tracker+ wouldn't load on the Original, and the reverse.
3. **No patch files on the Original at first.** A slot's settings live in the project (they already do on every device), so projects work and move between devices. Saving a patch to the library comes later, with option 1 or 2.

**Decision: option 2, a CE patch format for open engines.** On the Original, open-engine patches are saved and loaded in a format defined and documented in this repo. The trade-off: an open-engine patch saved on a Tracker+ (through Polyend's library, as `.patch`) won't load on the Original, and the reverse. Projects still move between devices, because slot settings live in the project file. A later change can close the gap by letting Tracker+ and Mini export and import open-engine patches in the CE format as well.

**Format sketch** (a proposal to review before anyone writes a reader):
- **File:** `Patches/<ENGINE>/<name>.cepatch`, for example `Patches/ACID/Squelch.cepatch`. A distinct extension, so neither firmware mistakes one format for the other.
- **Header:** the magic `CEPT`, a format version (`uint16`), the engine as its `InstrumentTypeId` (`uint16`), and the parameter count (`uint16`). Little-endian throughout.
- **Parameters:** one `float32` per parameter, in the engine's parameter order. A file with fewer parameters than the engine has (an older patch) gets the defaults for the rest; extra values are ignored. That's the same append-only rule the parameter IDs already follow.
- **Macros:** for each of the 6 macros, a name (up to 20 bytes, the editor's limit), a bipolar flag, and up to 6 modifiers of (parameter `uint16`, amount `float32`).
- **Name:** the patch's display name.
- **Forward compatibility:** sections after these are tagged with a type and a length, so a newer file loads in older firmware with the unknown parts skipped.
- **Tests:** round trip byte for byte, old files with fewer parameters, damaged files rejected without crashing, all as host tests.

---

## Order of work

Each step is its own PR.

1. **Measurement** (hardware, above). It decides whether the rest goes ahead and with how many voices.
2. **Split `FEATURE_SYNTH`** into "library" and "synth slots", with no behaviour change on any device. The proof is that the IMX build is byte-identical where possible, plus the PR's CI build for all three devices.
3. **Patch-view interface** on Tracker+ and Mini, wrapping Polyend's `Patch`, with the editor moved onto it. No behaviour change, checked by a hardware smoke test on Tracker+ or Mini.
4. **The host core** (voice pool, macros, MIDI port, FX entry, audio node), built and tested on the host first. The same sound-parity approach as TORO and ACID: through the host vs the engine driven directly.
5. **Turn it on for the Original:** the guard, the mixer inputs, the editor and icons, the Model menu. Rungs 3 and 4 on an Original, then a listening test.
6. **Patches:** the CE format above, its reader and writer as host-tested code shared by all devices, then the Original's patch browser and Save / Save As in the editor.

---

## Risks

- **CPU** (the gate above). Even when it passes, the CPU guard has to be good. A synth must never make sample tracks click.
- **Fast RAM on the Original.** The MK66 has no ITCM; `AUDIO_CORE_RAM` lands in flash there. The engines were written for this chip and ran from its fast RAM in OpenTracker, so flash execution speed is part of what step 1 measures.
- **The editor can't be compiled on a 64-bit host.** `strInstrument`'s size check assumes 32-bit pointers, so the UI moves in steps 3 and 5 rely on the CI ARM build to compile, and on a device to check. Those PRs should stay small.
- **Projects.** The project format doesn't change: synth slot settings are already saved on every device. A pattern step that names a synth instrument already loads on the Original today; it just stays silent.
