# Tape Mode: Plan

> Status: **plan only, nothing built.** Branch `work/keen-babbage-uvnhez`, from `main` at `377c56f`. Every phase below is its own PR with a fence, a blast radius, an invariant and a verification rung, per [`AGENTS.md`](../../AGENTS.md) §1.

## What I'm building, in one paragraph

When I make a new project, I can pick **Classic**, **Tape 2** or **Tape 4**. A tape project turns the first 2 or 4 sample tracks into **tape tracks**: each one owns a running tape loop that rolls with the transport, a fixed effects chain after it (GRAIN → RIPPLE → COLOR → ECHO), four modulators, and a set of **lanes**, rows of step values beside the pattern's note and FX columns that sequence the tape and its effects. The remaining tracks stay normal tracker tracks. Notes in a tape track's pattern still do what notes do (they jump the tape to a slice, play a sample instrument or play a synth part *through* the track's chain), so the pattern editor, song mode, performance mode and the synth engines all keep working. It runs on the Original Tracker, Tracker+ and Tracker Mini, scaled to each one.

The design comes from [Bryo](https://github.com/thoughtgrain/bryo), a four-track sound-sculpting firmware for the M-VAVE FM-1. I take its ideas, not its code: Bryo is GPL-3.0-only and this repository is MIT, so everything here is written fresh in C++ for this audio engine (float, 128-sample blocks) and this UI. Where the right starting point is a public algorithm (Karplus-Strong, Freeverb-style rooms, a TPT state-variable filter), I work from the algorithm or an MIT original (DaisySP, Rings), with an entry in `NOTICE.md` for anything ported.

---

## 1. How it maps from Bryo to a tracker

Bryo is played live with keys and knobs and has no song structure. A tracker is written into patterns and played back. So I keep what Bryo is *for* (a loop you keep re-shaping through a fixed chain, moved by modulators) and change how you drive it.

| Bryo | Tape mode | Why it changed |
| :--- | :--- | :--- |
| 4 tracks, all special | 2 or 4 tape tracks + the remaining normal tracks | The tracker workflow stays for drums, bass lines, MIDI and so on. Nothing is taken away from a project by choosing the mode. |
| Source per track: TAPE, SYNTH, POLY, DRUM | Every tape track **has** a tape. What plays into its chain is decided per step by the instrument column: the tape itself, any sample instrument, or (Tracker+/Mini) a synth part | The instrument column already does "what plays here". POLY is the existing sampler. DRUM is dropped: a slice kit, the PERC engine or a normal track already does that job. |
| 16 white keys = 16 slices | Note column on a tape-instrument step = jump to slice (C = slice 1, chromatic up) | Same idea as Beat Slice, but the head keeps rolling after the jump. |
| SEQ modulator + p-locks | **Lanes** in the pattern (§5), plus a SEQ engine kept for polymeters | P-locks are what a tracker grid already is. Lanes make them first-class and visible. |
| Hold slot, turn knob | Hold a modulator button/pad, turn the encoder on a parameter (Original/+). A depth list on the Mini (§9) | One encoder, 48 pads; the Mini has no pads or encoder. |
| Scenes (4, glide or morph) | 4 scenes a project, recalled from a step FX, from performance mode, or by hand with a glide | Scenes become something a song can call. |
| Two shared rooms | Room A = the existing master reverb send. Room B deferred | The reverb send exists and works on all three devices. |
| Channel strip LOW/HIGH/DJ filter, master compressor | Not in the first pass | Tracker has volume, pan, the instrument filter and the master limiter/enhancer already. Revisit after measurements. |
| ADPCM tapes at 22.05 kHz, 20 s shared | 16-bit PCM at the engine rate, inside normal sample memory | Memory is the Tracker's strength. A tape is just a sample, so saving, the sample editor and export keep working. |

---

## 2. Project mode: how it's chosen and stored

**UI.** `cProjectEditor::createNewProject()` (`core/interface/projectEditor/fProjEdit.cpp`) gets a popup before `newFileManager.createNewProjectInWorkspace()`: **Classic / Tape 2 / Tape 4**. Classic is the default and the highlighted choice, so muscle memory still makes a classic project. Boot (`Interface::openNewProject()`) and MOD/IT import always make Classic.

**The mode flag.** One byte. `strMtProjectRemote` is asserted at 2,304 bytes, and the `strMtValues::midiInstrument[].reserved[7]` bytes are zeroed on create (`fileManagerProject.cpp:320`) and on v10 migration (`mtFileManagerOldVersionRecovery.cpp:257`) and read nowhere else. I'll take one of them as `projectMode` (0 = Classic, 1 = Tape 2, 2 = Tape 4), give it a named accessor so nobody reads it as "MIDI reserved", and add a `static_assert` that pins its offset. Old projects read 0, which is Classic, so they load exactly as before and the struct size doesn't move.

The alternative is bumping `PROJECT_FILE_VERSION` to 18 and growing `strMtValues`, which breaks the 2,304 contract and every older firmware's ability to open the file. For one byte, that's the wrong trade.

**Everything else goes in a sidecar file**, `tape.ptt` in the project folder, with its own magic, version and CRC: per-tape-track state (chain settings, modulators, depths, lane targets, source binding) and the 4 scenes. Lanes themselves are per pattern and go in their own files (§5). The sidecar is copied by `ProjectSave` and `ProjectOpen` the way synth patches are (`copyWorkspacePatches`). Loading it is field-by-field with counts and clamping, never a struct dump, so I can add fields later without a version dance.

What an older firmware does with a tape project: it opens it as a classic project (the flag byte is ignored), plays the tape instruments as plain samples and ignores the sidecar. Nothing breaks, the tape effects are just missing. I think that's the right failure mode.

---

## 3. The tape

**A tape is an instrument.** Tape mode reserves the last 2 or 4 sample instrument slots (indices 46–47 or 44–47) as the tapes for tracks 1–2 or 1–4. That buys a lot for free: the tape saves as a normal `.pti`, the sample editor can trim, normalise or run a `.ptfx` over it, sample export and stem export see it, and the memory bar counts it.

The instrument gets a new play mode, `playModeTape`, appended to `instrumentPlayMode` (`core/source/mtStructs.h:387`, append-only). Its playback is a new `core/libs/Audio/play_memory_tape.cpp` next to the existing per-mode files:

- **The head always rolls** while the transport plays, looping the window START…START+LENGTH. With no notes in the pattern, the tape still plays. That's the difference between a tape and Beat Slice.
- **SYNC** (off, 1–16 bars) sets the loop to N bars at the project tempo, and the tape's speed follows the tempo like a tape machine. It's a soft lock: the head is nudged a little each block toward where the beat says it should be, with a crossfaded jump if it's far out. Step 1 of the pattern is the loop's start.
- **Notes jump** the head to a slice (16 slices of the window), with a short crossfade, and keep rolling. A note-off doesn't stop a tape. An explicit note-off step (the existing `OFF`) mutes it.
- **Parameters:** START, LENGTH, SPEED (±200 %, negative is reverse), PITCH, REVERSE, RPT (hold the head on a short loop, 10 ms–1 s, for stutters), RLEN, WOW, FLUTTER, ROTATE (shift the loop against the beat), XFADE (seam crossfade), SYNC, LEVEL.
- **Never streamed.** `isPlayModeStreamable(playModeTape)` returns false, so the tape stays in RAM where the record head can reach it.

**Length and memory.** The tape's length is fixed when it's created (default 8 bars at the project tempo, capped per device: about 32 s stereo on Tracker+/Mini, about 20 s mono on the Original) and allocated from the normal sample pool like any sample. No change to `SAMPLE_MEMORY_SIZE`, so classic projects lose nothing. A tape that doesn't fit is refused with a popup, not silently shortened.

**Recording onto tape.** The existing `Recorder` overwrites sample memory from its base and stops every player while it runs, so it can't record while the song plays. Tape mode gets its own write path, a small `TapeWriter` inside the tape player: it writes into the instrument's own sample memory at the record head, a block at a time, in the interrupt. That's just RAM writes, no SD, no allocation.

- **REC IN** per tape track: OTHERS (the mix of every track but this one, one block late), a single track, SELF (resampling through its own chain), or the audio input (line/mic, plus USB on Tracker+/Mini).
- **DUB** (−100…+100): how much of the old loop survives a pass. 0 is sound-on-sound, +100 keeps the loop, −100 replaces it.
- **Takes** are bar-exact: armed, they start on the next bar line and stop after the set bars.
- **Bounce** records a track's own output onto its tape and switches the track's steps to the tape.
- **Freeze to tape**: the GRAIN live buffer (§4) is copied onto the tape.
- The take marks the instrument dirty, and saving writes it in the main loop like any edited sample.

---

## 4. The chain

A tape track's audio runs `source → GRAIN → RIPPLE → COLOR → ECHO → track amp/pan → mix`, plus the existing delay and reverb sends. Every block starts out of the way (WET 0, or no drive), so a fresh tape track sounds exactly like its source. Each of GRAIN, RIPPLE and COLOR is an insert or a send (ROUT). ECHO is always a send.

**Where it lives.** A new `TapeTrackChain` in `core/AudioEngine/`, one per tape track, built only in tape mode (constructed after the voices and synth nodes, since `AudioStream::update_all` runs in construction order). Its input is an `AudioMixer` of the track's voice filter output and, on Tracker+/Mini, the synth part bound to that track. Its output takes the place of the voice's own connection into the amp and the send mixers. Classic projects never build or connect it.

The DSP nodes are new `AudioStream` classes in `core/libs/Audio/`, following `core/libs/Audio/AGENTS.md` (NULL-safe `allocate()`, every block released or sent on every path, `AUDIO_CORE_RAM` only for the inner loops that earn it):

| Block | What it does | Parameters | Memory |
| :--- | :--- | :--- | :--- |
| **GRAIN** | Granulates the track's last 1–4 bars from a live buffer recorded on the bar line. SCAN picks where grains read: the tape window, a moving cursor (time stretch), a fixed spot, or trailing the write head (a granular delay). FREEZE holds the buffer. | SIZE, RATE, PITCH, SPREAD, SPRAY, CONTOUR, REV chance, PATTERN, SCALE, PRAND, SCAN, WARP, OFFSET, FEEDBACK, GRAINS, SYNC, ROUT, WET | The live buffer (up to 4 bars, from the tape arena) + a fixed grain pool shared across the tape tracks |
| **RIPPLE** | 4 tuned Karplus-Strong strings, or a comb, or 4 resonators on a chord's partials. Notes on the track retune and pluck it. | PITCH, DECAY, TONE, SCALE (chord), CUT/RES, DRIVE, SPREAD, PLUCK, ROUT, WET | 4 short delay lines per track |
| **COLOR** | Drive (soft clip), crush (bits, rate or both), noise that follows the input's envelope, tilt EQ | DRIVE, CRUSH, CMODE, NOISE, NDECAY, NTONE, TILT, ROUT, WET, LEVEL | None |
| **ECHO** | A tape-ish delay per track, then a send into Room A (the master reverb) | TIME (or SYNC division), FEEDBACK, DLY level, TONE, SPREAD, DUCK, AGE, DIFFUSE, VERB send | One delay line per track |

**The tape arena.** GRAIN, RIPPLE and ECHO buffers come from one block of sample memory reserved at project open, only in tape mode, and shown on the memory bar as "Tape FX". Buffers are carved when a block is switched on (WET above 0) and handed back when it's off, and only the main loop allocates. The interrupt only reads pointers it was handed, so to free a buffer the main loop unpublishes the pointer, waits a block, then frees. Sizes come from phase 0 measurements; my starting guess is about 4 MB on Tracker+/Mini and about 1 MB on the Original.

**What's reused instead of rebuilt:** the instrument filter and overdrive and the bit-depth node (still there on sample sources), the master reverb (Room A), the master delay send, the limiter and the master enhancer.

---

## 5. Lanes

Lanes are what makes this a tracker instrument rather than a box with knobs. A tape track shows, beside its note/instrument/volume/FX columns, up to **8 lanes**. Each lane is bound to one target (any tape or chain parameter, a modulator's AMT, or a modulator depth), and each step either holds a value or is empty.

**Per-lane behaviour**, set on the lane:
- **STEP**: the value applies on that step only, then the parameter goes back to its base. That's a p-lock.
- **HOLD**: the value stays until the next value in the lane.
- **GLIDE**: ramps from this value to the next one across the steps between.

Lane values are written as the parameter's own units on screen (ms, %, semitones, a division) but stored as one byte, 0–254 mapped across the target's range, 255 meaning empty. Bryo's p-lock limit of 8 knobs a track maps nicely onto 8 lanes.

**Storage.** `Sequencer::strPattern` is a size contract (16,432 bytes), so lanes don't go in it. Each pattern that has lanes gets `lanes_NNN.ptl` next to its pattern file: a header (magic, version, per-track lane targets and modes) and 4 tracks × 8 lanes × 128 steps of bytes, about 4 KB, written only if any lane has a value. Copy, paste, insert, delete and the pattern length all act on lane columns too, so moving steps around keeps lanes in line with notes.

**Applying them.** The sequencer's step handling runs in the interrupt, so lane values are applied there from a RAM copy: no file reads, no allocation. Lane files are read in the main loop when a pattern is loaded or *queued* (the next pattern in song mode or performance mode is read ahead into a second buffer, and the interrupt flips one pointer on the pattern change). I'm deliberately not copying `Sequencer::loadNextPattern`'s SD read in the interrupt, which `core/source/AGENTS.md` already lists as a violation.

**Order of evaluation** each block, per target: base value (the knob, or the active scene) → lane value (if the lane has one in force) → plus each modulator's swing × depth → clamp to the target's range.

**On screen.** In the pattern editor, a tape track gets a view toggle (Shift + the FX column button on Original/+, an Fn binding on the Mini) between **FX columns** and **lanes**. The lane view uses the same column width as an FX column, so the frame cost stays inside the 8,172-byte display budget (`core/libs/NewDisplay/AGENTS.md`). Lane values print as two characters, like an FX value. Step FX keep working on tape tracks as they do everywhere.

**One new step FX:** `SCENE` (value 1–4, recall with the scene glide), taken from one of the free `FX_TYPE_R*` placeholders in `core/source/mtSequencer.h`. It's the only thing a lane can't do, because scenes span tracks.

---

## 6. Modulators

Each tape track has **4 slots**. Each slot runs one engine:

- **LFO**: SIN, TRI, SQR, SAW, RANDOM, free (0.02–20 Hz) or synced (16 bars to 1/32), FREE or note-retriggered, with phase, skew, smoothing and per-cycle variation.
- **ADSR**: triggered by the track's notes and, optionally, by the tape's loop seam.
- **FOLLOW**: an envelope follower on this track, another track or the input, auto-levelled, with optional sample-and-hold. I'll build it on the RMS node each voice already has (`AudioVoice::getRMS`).
- **SEQ**: a 1–16 step sequence with its own length and rate. Lanes already sequence at the pattern's length; SEQ is kept for polymeters (a 5-step SEQ over a 16-step pattern).

**Depths.** A depth (−100…+100 %) connects a slot to a target, and at 100 % the swing covers the target's whole range. An LFO swings both ways around the base; ADSR, SEQ and FOLLOW push one way, set by the depth's sign. Limits: 24 depths a track, at most 8 per slot. Each slot also has AMT and OFFSET, both lane-able, so a lane can fade a modulator in over a bar.

**Cost and context.** Modulators run once per audio block (128 samples, about 2.9 ms) in the interrupt, before the chain renders. A depth is a precomputed `range × depth` entry in a fixed array, double-buffered: the main loop rebuilds it when a depth changes and the interrupt flips one byte. No allocation, no `double`, all `f`-suffixed. Instrument-level envelopes and LFOs (`strInstrument::envelope/lfo`) keep working on the tape instrument and sample sources; these slots are on top of them.

---

## 7. Synth engines in a tape track (Tracker+ and Mini)

Synth parts don't go through any track's voice today: `SynthVoice` feeds the mixer directly. Tape mode adds a **binding**: each tape track can be bound to one synth part (A, B or C, or none). While bound, that part's output is routed into the track's chain input instead of straight to the mixer, so a synth goes through GRAIN, RIPPLE, COLOR and ECHO like any source. Notes for it are written on the tape track as usual, with the synth instrument in the instrument column.

There are 3 parts and up to 4 tape tracks, so in Tape 4 at most 3 tracks have a synth. The binding screen shows which part is taken. The synth's real-time budget guard (`kMaxSynthRealtimeBudgetUs` in `synth_PolySynth.cpp`) stays as it is; the chain's cost comes out of what's left, and phase 0 tells me how much that is.

On the Original Tracker there are no synth parts (`FEATURE_SYNTH` is IMX only), so a tape track's sources are the tape and sample instruments, including wavetable and granular play modes, which cover much of the "synth" ground.

---

## 8. Scenes

4 scenes per project, stored in `tape.ptt`. A scene holds every tape track's base parameter values, chain settings, the source binding and each modulator's AMT. It doesn't hold tapes, patterns, lanes, depths or the tempo (in a tracker the tempo belongs to the song).

- **Recall** glides over a set time (default 300 ms). Choices (a mode, a scale) switch at the halfway point.
- **From the pattern:** the `SCENE` step FX.
- **From performance mode:** a scene row, so scenes can be thrown live next to the existing performance FX.
- **Capture:** Shift + a scene slot saves the current sound into it.
- **Morph** (an encoder sweep between two scenes) is a later phase. It's the most CPU-hungry part (every parameter interpolated every block), so it waits for real numbers.

---

## 9. UI and the three devices

New screens follow `docs/recipes/ui-screen.md`. Tape mode adds one module, **TAPE**, with pages switched by the screen buttons: TAPE · GRAIN · RIPPLE · COLOR · ECHO · MOD · LANES · SCENES · REC. Each page shows its parameters in the existing parameter-bar style, and every page is a list a Mini can step through. This is a new screen, not a change to existing ones, but it adds a module slot and a menu entry, which `AGENTS.md` §7 says to discuss first. I'll open that discussion before phase 4's PR.

| | Original | Tracker+ | Mini |
| :--- | :--- | :--- | :--- |
| Tape tracks | 2 or 4 | 2 or 4 | 2 or 4 |
| Audio | Mono chain (`SAMPLE_CHANNELS = 1`); RIPPLE and GRAIN pan collapse to mono | Stereo | Stereo |
| Synth binding | — | Yes | Yes |
| Assign a depth | Hold a modulator pad (pads 1–4 of the TAPE screen's pad row) + turn the encoder on a parameter | Same | MOD page: pick a slot, pick a target from a list, set depth with ±1/±10 |
| Lanes | Pattern view toggle | Same | Same, via an Fn key binding |
| REC IN sources | Line, radio, mics, other tracks | + USB | Line, mic, USB, other tracks |
| Expected limits | Fewer grains and strings; maybe ECHO only on 2 tracks in Tape 4. **Set by phase 0** | Bound by the synth budget | Same as Tracker+ |

---

## 10. Interrupt, memory and compatibility rules this has to keep

- **Interrupt context.** The tape player, `TapeWriter`, the chain nodes, modulators and lane application all run in the audio DMA interrupt. No SD, no allocation, no UI calls, no blocking, no `double`, no "cheap" library calls in the inner loops (`core/` builds with `-fno-builtin`).
- **Main loop only:** reading and writing `tape.ptt` and lane files, carving and freeing arena buffers, rebuilding depth tables, saving tapes.
- **Classic projects are untouched.** No change to `strInstrument`, `strPattern`, `strMtProjectRemote` sizes, `SAMPLE_MEMORY_SIZE` or any existing enum value. Appended only: `playModeTape`, the `SCENE` FX ID, and new `InstrumentTypeId` values if any appear (I don't expect any).
- **Memory.** No new static `EXTERNAL_RAM` buffers on IMX (it's full). Everything tape-sized comes from sample memory, and only in tape mode. Fast RAM additions (grain pool state, modulator tables, lane RAM copies) are counted per phase against the ARM build's memory report.
- **Display.** Every new or changed control's display-list bytes are counted against the 8,172-byte frame budget.

---

## 11. Phases

Each phase is one PR. Fences are a starting point; I re-grep the blast radius right before each phase, because code moves.

### Phase 0: Measure and prepare (no user-visible change)
- **Do:** measure idle and busy audio load per device with `Profiler` on a fixed reference project (8 sample tracks + 3 synth parts on IMX). Bring in or depend on the `tests/audio` host harness from `perf/optimization-plan`, so every new node can get golden-hash tests.
- **Fence:** `docs/design/tape-mode-plan.md` (numbers), host-test scaffolding under `tests/`.
- **Invariant:** firmware unchanged.
- **Verify:** rung 5 measurements written into this doc. This sets the per-device limits in §9.

### Phase 1: Project mode and sidecar (no audio)
- **Do:** `projectMode` accessor + offset `static_assert`; the new-project popup; `tape.ptt` read/write and copy on save/open; tape instruments created, with no audio yet.
- **Fence:** `core/source/mtStructs.h` (accessor only), `core/FileManager/fileManagerProject.cpp`, `ProjectSave.cpp`, `ProjectOpen.cpp`, a new `core/FileManager/TapeProjectFile.{h,cpp}`, `core/interface/projectEditor/fProjEdit.cpp` and `gProjEdit.cpp`.
- **Blast radius:** every reader of `midiInstrument[].reserved` (today: the two zeroing sites), project save/open callers, MOD/IT import.
- **Invariant:** byte-identical files for classic projects.
- **Verify:** rung 2, a host test that loads and saves a set of existing projects and compares bytes, plus a `tape.ptt` round-trip. Rung 3.

### Phase 2: Tape playback
- **Do:** `playModeTape`, `play_memory_tape.cpp`, the running head, SYNC soft lock, slice jumps on notes, RPT, wow/flutter, reverse; the TAPE page of the new screen.
- **Fence:** `core/source/mtStructs.h` (enum append), `core/libs/Audio/play_memory*.{h,cpp}`, `isPlayModeStreamable`, the sequencer's note path for tape instruments, the new `core/interface/tapeMode/` screen.
- **Invariant:** bit-exact audio for every existing play mode.
- **Verify:** rung 2, golden hashes for existing modes unchanged and a new golden for tape playback. Rung 3, then rung 4 on each device I have.

### Phase 3: Recording onto tape
- **Do:** `TapeWriter`, REC IN routing (others, one track, self, input), DUB, bar-exact takes, bounce, saving dirty tapes.
- **Invariant:** the classic Sample Recorder behaves as before.
- **Verify:** rung 2 for the overdub maths; rung 4 for takes against the clock.

### Phase 4: Lanes
- **Do:** lane files, the RAM double-buffer and read-ahead, application in the step path, the pattern-editor view toggle, copy/paste/insert/delete over lanes, the `SCENE` FX ID reserved (it does nothing until phase 8).
- **Invariant:** timing (same ticks per block, step 1 on the downbeat with external clock); pattern files byte-identical.
- **Verify:** rung 2, a sequencer host test that steps a pattern with lanes and checks values per tick. Rung 5 for timing. The display-list byte count for the lane view.
- **Discuss first:** pattern editor change.

### Phase 5: COLOR and ECHO
- **Do:** `TapeTrackChain` wiring and the two cheapest blocks, plus the tape arena.
- **Invariant:** classic projects bit-exact; with every block off, a tape track is bit-exact with the same instrument on a classic track.
- **Verify:** rung 2 goldens; rung 5 CPU per block per device.

### Phase 6: GRAIN
- **Do:** live buffer, grain pool, SCAN modes, freeze, freeze to tape.
- **Verify:** rung 2 goldens with fixed random seeds; rung 5 CPU, which sets the grain counts per device.

### Phase 7: RIPPLE
- **Do:** strings, comb, resonators, pluck on notes. Port from the MIT originals if I use them, with `NOTICE.md` entries.
- **Verify:** rung 2; rung 5.

### Phase 8: Modulators and scenes
- **Do:** the 4 slots, depth tables, the assign gesture and Mini list, scenes, the `SCENE` FX, a scene row in performance mode.
- **Verify:** rung 2 for modulator output and depth maths; rung 4 for the gesture; rung 6 for scene glides.

### Phase 9: Synth binding (Tracker+ and Mini)
- **Do:** route a bound synth part into a tape track's chain.
- **Fence:** `core/AudioEngine/` connection code under `FEATURE_SYNTH`, `SynthVoice`, the binding UI.
- **Invariant:** unbound synths bit-exact.
- **Verify:** rung 2; rung 5 under the synth budget guard.

### Phase 10: Device tuning, Mini pass, manual
- **Do:** per-device limits from the measurements, the Mini's Fn bindings, a user manual page in `docs/` in the same voice as the others.
- **Verify:** rung 4 on all three devices, rung 6 write-ups.

Later, once the numbers are in: morph, a second room, a channel strip (LOW/HIGH/DJ filter), GRAIN's WARBLE/VINYL forms.

---

## 12. Open questions

1. **Which tracks?** I've put the tape tracks on tracks 1–2 or 1–4 and kept the rest as normal tracks. Would you rather have them on the last tracks, or have the whole project be only tape tracks?
2. **Tape length:** a fixed length per tape (chosen when it's made, default 8 bars), or growing as you record, like Bryo? Fixed is much simpler on a sample-memory model and keeps saving predictable.
3. **Drums:** I've dropped Bryo's DRUM source in favour of normal tracks, slice kits and PERC. Do you want a drum source back in a tape track?
4. **Discussion:** the new TAPE screen and the pattern-editor lane view count as workflow changes under the repository's rules. Do you want me to draft the GitHub Discussion post, or will you raise it?
