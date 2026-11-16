# Tape Projects: Implementation Plan

> Status: **plan only, nothing built.** Branch `work/keen-babbage-uvnhez`. This replaces the first plan, which predated the UX design. **What it should feel like** is in [`tape-ux.md`](tape-ux.md) and the mockups ([`tape-ux-mockups.html`](tape-ux-mockups.html)); this file is **how I build it**, in what order, and what can go wrong, with the Original Tracker (MK66) getting its own review in §3. Every phase is one PR with a fence, a blast radius, an invariant and a verification rung, per [`AGENTS.md`](../../AGENTS.md) §1.

Numbers in this file are from the developer guide's memory table and the code as of `377c56f`, or are marked **estimate**. Nobody has measured the audio load on any device yet; phase 0 does that, and several limits below are placeholders until it does.

---

## 1. What gets built, as engineering units

| Unit | What it is | Where | Runs in |
| :--- | :--- | :--- | :--- |
| **Project type** | One byte: 0 Default, 1 Tape 2, 2 Tape 4, 3 Tape 6 | `strMtValues::midiInstrument[0].reserved[0]`, behind a named accessor and an offset `static_assert` | main loop |
| **Hidden tracks** | Tracks beyond the type's count, and the MIDI tracks, hidden in every screen and skipped by the sequencer | interface + `Sequencer` | both |
| **Tape instrument** | Sample instruments 46/47 (TA/TB) with new `playModeTape`; never streamed | `core/libs/Audio/play_memory_tape.cpp`, `isPlayModeStreamable` | ISR |
| **Tape FX columns** | FX3/FX4 for each tape track, stored in hidden tracks' FX slots (§2.1) | `Sequencer`, pattern editor | ISR + UI |
| **New step FX** | Speed, Wow, Tape Rec, Tape A send, Tape B send, Grain Wet/Size/Position/Freeze, Ripple Wet/Pitch/Decay (12) | `mtSequencer.h` enum (append), `interfaceDefs.h` names, `mtAudioSequencer.cpp` | ISR |
| **TapeWriter** | Records into a tape's own sample memory at the record head, with Dub | inside the tape player | ISR |
| **Tape FX bus ×2** | Send bus per tape: Grain → Ripple, return into the mix | `core/AudioEngine/TapeFxBus.{h,cpp}`, nodes in `core/libs/Audio/` | ISR |
| **Effects arena** | Grain buffer carved from sample memory, only in tape projects | `core/AudioEngine/` | main loop allocates, ISR reads |
| **Sample strips** | Peak table per instrument, captured peaks per track, a strip drawn in the pattern editor | `core/source/`, `core/interface/patternEditor/` | main loop (capture: one compare per block in ISR) |
| **Automation additions** | New destinations for tape FX values, new type Follow | `envelopesLabels`, `playerEngine::update` | ISR |
| **theseus** | Drum synth: `.psyn` engine on IMX; on MK66 see §3.1 | `synth-plugin-sdk/engines/theseus.cpp` (port) | ISR |
| **Patch editor pads** | Hold Parameter / Shift + pads | `SynthPatchEditor.cpp` | main loop |
| **UI** | Project type list, FX column paging, page-3 tape settings, Master Tape page and effect pages, Global Mixer column, Sample Playback Tape mode, perf FX | `core/interface/…` | main loop |

## 2. Decisions the design forces

### 2.1 Where FX3/FX4 live: hidden tracks' FX slots

`Sequencer::strPattern` is a 16,432-byte size contract with `fx[kStepFxSlots = 2]` per step, and every pattern stores all 16 tracks on every device. A tape project never shows or plays tracks 9–16 (MIDI), so **Tape A's FX3/FX4 are track 9's `fx[0..1]`, and Tape B's are track 10's**, step for step.

- **For:** no new file, so song mode's pattern loading, copy and duplicate, and undo all carry the columns for free. I add no new SD read in the interrupt (`Sequencer::loadNextPattern` already reads in the ISR, a listed violation I don't extend). Pattern files stay byte-compatible.
- **Against:** an older firmware opening a tape project sees FX on MIDI tracks 9–10. New FX IDs it doesn't know are ignored. Existing FX in FX3/FX4 (Roll, Reverse and so on) would act on MIDI tracks 9–10, which send MIDI out and are harmless without a MIDI device on those channels. I'll document it rather than engineer around it.
- **Rule:** in a tape project the sequencer must never play tracks 9–16 as MIDI. Hiding them is phase 2's job and a precondition for phase 4.
- **Editing:** every pattern-editor operation on a tape track (copy, paste, insert, delete, fill, selection, render, expand, shrink) applies to its mapped track too. I'll find every such operation by grep on `selection`, `copy`, `insert`, `fill` in `fPatternEdit.cpp` and `mtSequencer_patternEdit.cpp`.

### 2.2 The FX list outgrows the pads

FX are picked by holding Fx and pressing a pad, where pad = list index (`interfaceGlobals.fxIDs[pad]`, `fPatternEdit.cpp:4589`), and `FX_COUNT` is 46. Fourteen new FX make 60.
- **Plan:** the list shows tape FX only on tape tracks, except the two sends, which show everywhere. That's 48 on ordinary tracks and 60 on tape tracks. On a tape track the tape FX get a second pad page: holding Fx + **Shift** + pad picks from them. The FX popup list itself just gets longer.
- **Also:** `FX_MAX` (49) is used as a validity bound in a few UI places (`fPatternEdit.cpp:4320`, `gPatternEdit.cpp:1734`, `fProjEdit.cpp:496`). It becomes the new last ID + 1, and every `FX_MAX` and `FX_COUNT` use gets checked by grep.

### 2.3 One voice per tape track; where the decks play

The UX keeps the Tracker's rule that a track has one voice, so the tape plays on its own track's `instrumentPlayer`. When another instrument plays on that track, the tape is silent but its position keeps following the song position, so it comes back in time. No second player is needed. That position is computed from the sequencer's tick count, not from the player, so it survives being interrupted.

### 2.4 The FX bus input

Each tape bus gets its input from a new send mixer fed from every voice's filter output, the same tap the reverb and delay sends use (`preFxMixer`), plus synth parts on IMX. Each voice and synth part needs two more gain stages. The return goes into `tracksAndSendsMixer` like the reverb return.

### 2.5 Echo is dropped

The Tracker already has Delay and Reverb as send effects on every device, so the tape buses don't get an echo of their own. Each bus is **Grain → Ripple**, with one buffer, Grain's. That removes an effect from every phase, a delay line per bus from memory and about 5 k cycles a block per bus from the CPU estimate. A tape's own Delay and Reverb sends put echoes and space on its sound.

### 2.6 No streamed recording

I looked at streaming recordings to the SD card ("reels") to get past RAM limits, and dropped it: a take would depend on the card keeping up in real time, alongside streamed playback and autosave, and a slow or worn card would end takes. That's a new way to lose a performance, and stability matters more than length. **Tapes record into RAM only.** The length caps per device (§3, #4) are the trade.

### 2.7 Synths: print offline, not by streaming

The synth parts share a hard real-time budget (`kMaxSynthRealtimeBudgetUs`, 2.1 ms of the 2.9 ms block) that the tape buses compete with. The way to free it is **Render Selection to a tape, in real time** (phase 5): the selection plays and is recorded straight into the tape's RAM from the per-track taps stem export already uses. It isn't limited by the 768 KB offline render buffer, it can keep the second pass so loop seams carry their tails, and it writes nothing to the card while it runs. Then the synth part can be turned off.

Streamed sample slots for sample-based engines (WAVS, GRAIN, DWA) remain a possible separate project, because streaming *playback* is already proven in `VoiceStreamManager`. It's a plugin ABI change and outside tapes.

---

## 3. The Original Tracker (MK66): review

The Original is where this design can fail. Here is every issue I found, how bad it is, and what I'd do.

| # | Issue | Severity | What I'd do |
| :--- | :--- | :--- | :--- |
| 1 | **No synth host.** `FEATURE_SYNTH` and `GUI_HANDLE_SYNTH` are IMX only. The host (`synth/source`, about 10,600 lines) plus the `.psyn` loader need the M7 and a 6 MB pool. The MK66 has about 153 KB of flash and about 1.1 MB of external RAM free. | **Blocking** for "theseus in the synth screens" on the Original | §3.1: a small built-in host for one engine, reusing the synth screens if they compile without the full host; otherwise a drum instrument with screens copied from them. A decision gate in phase 10. |
| 2 | **CPU is unmeasured.** 180 MHz M4F, 2.9 ms blocks (about 522 k cycles). There's no idle or busy figure today. My estimates per tape bus: Grain with 12 grains about 30–45 k cycles a block, Ripple about 10 k. One theseus voice is about 30 k. **All estimates.** | **High** | Phase 0 measures first. Per-device caps: grain count and theseus voices, set from measurements. A tape project's at most 6 tracks also frees 2 voices' worth compared with Default. |
| 3 | **No ITCM.** On MK66, `AUDIO_CORE_RAM` and `AUDIO_CORE_FLASH` both land in flash, so hot DSP runs from flash through the cache. Grain reads land at random addresses in external SDRAM, which misses the cache. | High | Keep inner loops small. Copy each grain's window into a stack buffer per block, the way Bryo decodes into a 128-sample window, so the SDRAM reads are sequential. Measure in phase 7. |
| 4 | **Memory.** The sample pool is about 7.5 MB, mono. Two 20 s tapes are 3.5 MB. A 4-bar grain buffer at 90 BPM is about 10.7 s, 0.94 MB each, so 1.9 MB for two. That's about 5.4 MB, which leaves about 2.1 MB for samples. | **High** | Tapes 12 s each (2.1 MB together) and one 2-bar grain buffer per bus (about 0.94 MB together, only while Grain is on). That's about 3 MB in a full tape project, leaving about 4.5 MB for samples. Fixed caps, shown on the memory bar, refused with a popup when they don't fit. |
| 5 | **Internal RAM.** About 128 KB free. Ripple's 4 strings × about 1,056 floats × 2 buses is about 34 KB; peak tables for strips are 48 × 128 B, about 6 KB; captured strip peaks are 512 B × 6 tracks. | Low | Fits. Ripple lines go in internal RAM on MK66 for speed. On IMX, where internal RAM is tight, they come from the arena. |
| 6 | **Mono.** `SAMPLE_CHANNELS = 1`; voices use `monoToStereo`. Grain Spread and Ripple's panned strings have no effect. | Low | The bus processes mono on MK66, and the stereo-only parameters are shown dimmed with "mono" as the value, not hidden, so pages line up across devices. |
| 7 | **Display and SPI time.** The developer guide measures 20–35 ms per step on MK66 for FT812 SPI pattern redraws. Sample strips add lines to that redraw. | **Medium-high** | Measure the frame bytes and the redraw time with strips on (phase 1). If the redraw grows past what fits between steps at 180 BPM 1/16 (about 83 ms), draw strips at two lines per row on MK66, or default to Long samples there. Strips are the first thing dropped (UX §5). |
| 8 | **Audio block pool.** `AudioMemory(70)` on MK66. Each bus node that holds or transmits blocks draws from it. | Medium | Count blocks per bus (about 6 at peak) and raise the pool by the measured need; each block is about 260 B on MK66, so 12 blocks are about 3 KB. |
| 9 | **Recording input.** Line in, radio and mic come through `updateInputBuffers` (`platforms/mk66/source/TrackerMain.cpp:28`). There's no USB audio. | Low | Rec Source lists only what the device has, as the Sample Recorder already does. |
| 10 | **No slice streaming** on MK66 (`isPlayModeStreamable` false for Slice and Beat Slice). | None | Tapes are never streamed on any device. |
| 11 | **Render Selection to a tape** renders through `externalRamScratchBuff` (768 KB, shared). On MK66 the render buffer is the same size. | Low | Render in chunks straight into the tape's memory; the length is capped by the tape's length anyway. |
| 12 | **Flash budget.** About 153 KB free. New code: tape player and writer, three effects, the bus, strips, UI, perhaps theseus and a small host. **Estimate 60–110 KB.** | Medium | Check the ARM build's memory report every phase. A big, cold UI table can live in external RAM if needed. |
| 13 | **Sequencer in the ISR.** Each step now also handles FX3/FX4 on two tracks, tape positions and Tape Rec. | Low | That's a few more branches per step. Check with the timing invariant (ticks per block unchanged, MIDI clock jitter within ±1 µs). |
| 14 | **Old firmware.** Many Original owners stay on older releases. A tape project opened there plays as Default, with FX3/FX4 acting on MIDI tracks 9–10 (§2.1). | Low | Say so in the release notes. Default projects are untouched. |

### 3.1 theseus on the Original: the decision

1. **A built-in host for one engine (preferred if it fits).** I'd write a `BuiltinSynthHost` for MK66 that implements the part of `Synth`'s interface the synth screens actually call: model list, patch metadata (sections, parameters, values), set and get a parameter, note on and off, render. It would have one part and theseus compiled in, with no PsynLoader, no 6 MB pool and no SamplePool. If `SynthEditor.cpp` and `SynthPatchEditor.cpp` compile against it with `GUI_HANDLE_SYNTH` turned on for MK66, the Original gets the same screens. The project file already stores `synthPart[3]` on every device, so patches and parts load across devices.
   - **Cost to check first:** what else `GUI_HANDLE_SYNTH` pulls in (macros, sample slots, `.psyn` file browsing), and the flash size of the two screens. If they need more of the host than the subset above, this option is too expensive.
2. **Otherwise, a drum instrument.** theseus runs as a play mode on the sample track's voice, as in the earlier plan. Its two screens are new code that copies the synth part screen and the patch editor (section grid, parameter list, value bar, pad shortcuts) button for button. Same muscle memory, more code to keep in sync. Patches save as a small file per instrument.

The decision gate is in phase 10, after measuring the flash cost of option 1 with a stub build.

---

## 4. Phases

Each phase is one PR. Fences are a starting point; I re-grep the blast radius right before each phase.

### Phase 0: Measure and set up tests (no user-visible change)
- **Do:**
  - Audio load (`Profiler`) on reference projects per device: idle, 6 sample tracks busy, plus 3 synth parts on IMX.
  - Frame display-list bytes and redraw time of the pattern editor on MK66.
  - Free memory from the ARM build's memory report.
  - Bring in, or depend on, the `tests/audio` host harness from `perf/optimization-plan` so every new node gets golden-hash tests.
- **Fence:** this doc (numbers), `tests/`.
- **Invariant:** firmware unchanged.
- **Verify:** rung 5. The numbers go into §3 and replace the estimates.

### Phase 1: Sample strips (every project type)
- **Status:** 1a (the model and its host tests) and 1b (the pattern editor, the drawing, the Config setting) are written and syntax-checked for MK66, Tracker+ and Mini; not built for ARM or run on hardware yet. Still to do: captured strips for synths and other generated sound (1c), and the rung-5 measurements of frame bytes and MK66 redraw time.
- **Do:**
  - Peak table per instrument, built at load.
  - Captured peaks per track: one compare per block in the ISR, written at quarter-step boundaries.
  - The strip drawn in the pattern editor; Config **Sample strips: Off / Long samples / All notes**.
- **Fence:** `core/interface/patternEditor/`, `core/libs/NewDisplay/` (only if the tracker control needs a strip primitive), `core/source/` sample load, `core/interface/configEditor/`, `mtConfig` (append a field within the config's reserved space or bump `CONFIG_STRUCT_VER`).
- **Blast radius:** every pattern-editor redraw path; `cTracker` control data; config load and save.
- **Invariant:** with strips Off, visually identical and the same display-list bytes. Audio bit-exact either way.
- **Verify:** rung 2 for peak tables and capture; rung 5 for frame bytes and MK66 redraw time; rung 4 on all three devices.
- **MK66 risk:** #7. This phase settles it, and it's useful on its own if the rest stalls.

### Phase 2: Project types and hidden tracks (no audio)
- **Do:**
  - The type byte and its accessor.
  - The New Project type list.
  - Tape instruments TA/TB created blank, and `C-4 TA` written on step 1.
  - Tracks beyond the type, and MIDI tracks, hidden in every screen and skipped by the sequencer.
- **Fence:** `mtStructs.h` (accessor only), `fileManagerProject.cpp`, `fProjEdit.cpp`/`gProjEdit.cpp`, the pattern editor, mixer, performance, song and export track lists, `Sequencer` (skip).
- **Blast radius:** the readers of `midiInstrument[].reserved` (the two zeroing sites); every loop over `kSampleTracks` and `kAllTracks` in those screens; MOD/IT import.
- **Invariant:** Default projects byte-identical on load and save, visually identical, bit-exact.
- **Verify:** rung 2, a load/save byte comparison over a set of real projects; rung 4.

### Phase 3: Tape playback
- **Do:**
  - `playModeTape` (appended).
  - `play_memory_tape.cpp`: rolling head, Sync soft lock to song position, slice jumps on notes, OFF and resume in time.
  - Speed and Wow FX; Roll checked on a rolling tape.
  - Sample Playback's Tape mode.
- **Fence:** `mtStructs.h` (enum append), `core/libs/Audio/play_memory*.{h,cpp}`, `isPlayModeStreamable`, `mtAudioSequencer.cpp`, `samplePlayback/`.
- **Invariant:** every existing play mode bit-exact.
- **Verify:** rung 2 goldens; rung 5 CPU per device.

### Phase 4: FX3/FX4 on tape tracks and the new FX list
- **Do:**
  - The mapping of §2.1 in the sequencer and in every editing operation.
  - Fx-again paging, and the `FX 3·4` header.
  - FX list entries, with tape FX only on tape tracks and the second pad page (§2.2).
- **Fence:** `mtSequencer*.cpp/.h`, `fPatternEdit.cpp`, `gPatternEdit.cpp`, `interfaceDefs.h` FX names and IDs.
- **Invariant:** pattern files byte-identical for Default projects; timing (ticks per block, MIDI clock jitter within ±1 µs).
- **Verify:** rung 2, a sequencer host test that steps a tape pattern and checks applied FX per tick; rung 5 timing on MK66.

### Phase 5: Recording in the pattern editor
- **Do:**
  - `TapeWriter` with Dub.
  - Tape tracks in the Shift + Rec arm row; Rec + Play punch in and out; bar-quantised takes.
  - The Tape Rec FX.
  - Page 3 settings (Rec Source, Bars, Dub, Undo Take).
  - Render Selection → Tape A/B **in real time**: it plays the selection (twice with Loop seam on, keeping the second pass), records into the tape through `TapeWriter`, and stops by itself. **Dry** sums the selected voices (and synth parts) from the same nodes stem export taps (`connectVoiceToExportQueue`, `connectSynthToExportQueue`), through new connections into the writer, not through the export queues, which buffer for the main loop. **Mix** takes the final output. Reverb and Delay are only available as whole returns (`stem_reverb`, `stem_delay`), so there's no per-track share of them.
  - Saving dirty tapes.
- **Fence:** play_memory tape files, `Sequencer` (Tape Rec), `fPatternEdit.cpp`/`gPatternEdit.cpp`, the render code, the platform input tap (read only).
- **Invariant:** the Sample Recorder unchanged; live note recording unchanged.
- **Verify:** rung 2 for Dub maths and take boundaries; rung 4 for takes against the clock on each device.

### Separate fix: SD access from the interrupt (not part of tapes)
Song-mode pattern loading and the MIDI Start and Stop paths read the card from the interrupt (C1, C2 in `core/source/AGENTS.md`). Tapes don't make this worse, since they never touch the card while playing, but it's a real bug for every user. I'd fix it as its own PR, outside this plan: read the next pattern ahead in the main loop and swap it in from the interrupt.

### Phase 6: Tape FX bus, Ripple, sends, Master pages
- **Do:**
  - `TapeFxBus` and the effects arena.
  - Ripple (cheaper than Grain, so it proves the bus first).
  - Tape A/B sends in Params → More, and their step FX.
  - Master Tape 4/4 and its pages; the Global Mixer Tape FX column.
- **Fence:** `core/AudioEngine/` (connections, new bus), `core/libs/Audio/` (Ripple node), `instrumentEditor` More window, `masterParams/`.
- **Invariant:** Default projects bit-exact; with all sends at 0, tape projects bit-exact with the bus absent.
- **Verify:** rung 2 goldens; rung 5 CPU per device and block-pool high water (MK66 #8).

### Phase 7: Grain
- **Do:** the bus's grain buffer, grain pool with per-device grain counts, the Scan modes, Freeze, freeze to tape via Rec Source.
- **Verify:** rung 2 goldens with fixed seeds; rung 5 CPU, which sets the MK66 grain count (#2, #3).

### Phase 8: Ripple's pluck from sends
- **Do:** Pluck by sends (notes on ordinary tracks that send into the bus pluck its strings). The strings themselves arrive in phase 6. Anything ported from an MIT original gets a `NOTICE.md` entry.
- **Verify:** rung 2; rung 5.

### Phase 9: Automation
- **Do:** new destinations for the tape FX values; the Follow type (Source, Rise, Fall, Amount), built on each voice's existing RMS node.
- **Fence:** `gInstrEdit.cpp` (labels), `mtAudioUpdateHandle.cpp`, `envelopeGenerator`.
- **Invariant:** `strInstrument` stays 372 bytes. New destinations live in the tape project sidecar data, not in the instrument struct, unless spare bytes are proven unused.
- **Verify:** rung 2.

### Phase 10: theseus and patch-editor pads
- **Do:**
  - Port theseus to the `.psyn` SDK with seven sections and per-drum trims; extend its host tests.
  - Pad shortcuts in `SynthPatchEditor.cpp` for every engine.
  - **The MK66 decision gate (§3.1):** stub build, measure flash, pick option 1 or 2, then build it.
- **Verify:** rung 2 (the ported sound tests); rung 5 CPU per voice on MK66, which sets its voice count.

### Phase 11: Performance FX, Mini pass, manual
- **Do:** the five perf FX; Mini Fn and menu checks; a user manual page in `docs/`.
- **Verify:** rung 4 on all three devices; rung 6 write-ups.

## 5. Order and what ships when

Phases 1 (strips) and 10's theseus engine work don't depend on tape projects. Phase 1 can ship to everyone first, and theseus can ship as a Tracker+ and Mini engine as soon as it's ported. The tape path runs 0 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 11, with a usable tape project after phase 5 (tapes, FX columns, recording) before any effect exists.

## 6. Open items


1. **theseus on the Original**: option 1 or 2 (§3.1), decided by a measured stub build.
2. **Original memory caps** (#4): 12 s tapes and a 2-bar grain buffer. Acceptable as starting caps?
3. **Old firmware and FX3/FX4** (§2.1): document it, or store the columns in a sidecar and accept the extra file handling?
4. **Automation destinations' storage** (phase 9): sidecar or spare instrument bytes, decided after a grep of every reader.
