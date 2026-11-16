# Tape Projects: Plan

> Status: **plan only, nothing built.** Branch `work/keen-babbage-uvnhez`, from `main` at `377c56f`. Every phase below is its own PR with a fence, a blast radius, an invariant and a verification rung, per [`AGENTS.md`](../../AGENTS.md) §1.

> **UX first.** How this works in your hands is in [`tape-ux.md`](tape-ux.md) (version 2), with screens in [`tape-ux-mockups.html`](tape-ux-mockups.html). Version 2 puts every idea in the Tracker screen that already does that job, so parts of this plan are out of date until I fold them in:
> - **No Tape module.** The tape is play mode Tape in Sample Playback. Grain, Ripple and Echo are a send effect per tape (Tape A FX, Tape B FX), set up in Master → Tape FX like Reverb and Delay; instruments send in from Params → More. Color is dropped: Overdrive and Bit depth already exist on every instrument.
> - **Lanes are FX columns.** Tape tracks have FX1–FX4, swapped in pairs by pressing Fx again, and the tape and chain controls are new FX types. The lane files, lane groups and lane modes in §5 go away. Where FX3–FX4 are stored is open (a sidecar, or the step slots of the tracks a tape project hides).
> - **Modulation is Automation**, with new destinations and a Follow type. The modulator slots and depth tables in §6 go away.
> - **One voice per tape track.** Another instrument on a tape step plays instead of the tape, and the tape keeps time underneath (its position comes from the song position), so no second player is needed.
> - **Sends:** Tape A and Tape B sends on every instrument and synth part, beside Reverb and Delay. Scenes and the synth Output setting are dropped.
> - **Sample strip** (`tape-ux.md` §5): a vertical waveform beside any track whose sample plays across steps, in every project type. It's independent of tape mode, so it becomes its own early phase: a peak table per instrument, the strip drawing, a Config setting, and a measured display-list cost.
> - **Recording** is the Sample Recorder with a tape Target, Bars and Dub. Bounce is Render Selection to a tape.

## What I'm building, in one paragraph

When I make a new project, I pick its type: **Default** (today's 16 tracks, no tape), **Tape 2**, **Tape 4** or **Tape 6**. A tape project has exactly that many tracks, and up to two of them are **tape tracks**: one in Tape 2, two in Tape 4 and Tape 6. A tape track owns a running tape loop that rolls with the transport and can be recorded over while the song plays, a fixed effects chain after it (GRAIN → RIPPLE → COLOR → ECHO), four modulators, and **lanes**, columns of step values beside the pattern's note and FX columns that sequence the tape and its effects. The other tracks are ordinary tracker tracks, playing samples and synth engines, and they're what usually gets printed onto the tapes. Capping both numbers is the point: fewer tracks and at most two tapes means longer tapes, and the CPU, memory and screen width the missing tracks would use go to the two chains and their lanes. Patterns, song mode, performance mode and the synth editor work the way they do in any project. It runs on the Original Tracker, Tracker+ and Tracker Mini, scaled to each one.

The design comes from [Bryo](https://github.com/thoughtgrain/bryo), a four-track sound-sculpting firmware for the M-VAVE FM-1. I take its ideas, not its code, and I don't copy it one to one: it's an instrument played live with keys and knobs, and this is a tracker. Bryo is GPL-3.0-only and this repository is MIT, so everything here is written fresh in C++ for this audio engine (float, 128-sample blocks) and this UI. Where the right starting point is a public algorithm (Karplus-Strong, a TPT state-variable filter), I work from the algorithm or an MIT original (DaisySP, Rings), with an entry in `NOTICE.md` for anything ported.

---

## 1. How it maps from Bryo to a tracker

Bryo has no song structure: you play it. A tracker is written into patterns and played back. So I keep what Bryo is *for* (a loop you keep re-shaping through a fixed chain, moved by modulators) and change how you drive it.

| Bryo | Tape projects | Why it changed |
| :--- | :--- | :--- |
| 4 tracks, every one with a tape and a chain | 2, 4 or 6 tracks; 1 or 2 of them tape tracks, the rest ordinary tracker tracks | The ordinary tracks write the parts (drums, bass, synth lines); the tape tracks catch, loop and mangle them. Two chains is what all three devices can run well. |
| Source per track: TAPE, SYNTH, POLY, DRUM | A tape track always has its tape. What also plays into its chain is decided per step by the instrument column: any sample instrument, a theseus drum instrument (§7a), or (Tracker+/Mini) a synth part bound to it | The instrument column already does "what plays here". POLY is the existing sampler. DRUM becomes theseus, ported into the firmware, because the Original has no synth engines to fill that role. |
| 16 white keys = 16 slices | A note on a tape step jumps the head to a slice (C = slice 1, chromatic up) | Same idea as Beat Slice, but the head keeps rolling after the jump. |
| REC prints the other tracks | REC IN picks the ordinary tracks' mix, one track, the tape track itself or the audio input | Same, and it's the main way the ordinary tracks reach a tape. |
| SEQ modulator + p-locks | **Lanes** in the pattern (§5), plus a SEQ engine kept for polymeters | P-locks are what a tracker grid already is. Lanes make them first-class and visible. |
| Hold slot, turn knob | Hold a modulator pad, turn the encoder on a parameter (Original/+). A depth list on the Mini (§9) | One encoder and 48 pads; the Mini has no pads or encoder. |
| Scenes (4, glide or morph) | 4 scenes a project, recalled from a step FX, from performance mode, or by hand with a glide | Scenes become something a song can call. |
| Two shared rooms | The existing master reverb send is the room | It exists and works on all three devices. A second room can come later. |
| Channel strip, master compressor | Not in the first pass | Tracker has volume, pan, the instrument filter and the master limiter/enhancer already. |
| ADPCM tapes at 22.05 kHz, 20 s shared by four | 16-bit PCM at the engine rate, in normal sample memory, at most two tapes | With two tapes and the Tracker's sample memory, each tape can be long. A tape is a sample, so saving, the sample editor and export keep working. |

---

## 2. Project types: how they're chosen and stored

| Type | Tracks | Tape tracks (max) | Default tape tracks |
| :--- | :---: | :---: | :--- |
| **Default** | 16 (8 sample + 8 MIDI, as today) | 0 | — |
| **Tape 2** | 2 | 1 | track 1 |
| **Tape 4** | 4 | 2 | tracks 1 and 2 |
| **Tape 6** | 6 | 2 | tracks 1 and 2 |

Any track in a tape project can be switched between tape and ordinary on the TAPE screen, as long as the limit holds. Switching a tape track to ordinary keeps its tape and chain settings in the project, so switching back loses nothing.

**UI.** `cProjectEditor::createNewProject()` (`core/interface/projectEditor/fProjEdit.cpp`) gets a type popup before `newFileManager.createNewProjectInWorkspace()`. Default is the first and highlighted choice, so muscle memory still makes the project people make today. Boot (`Interface::openNewProject()`) and MOD/IT import always make Default.

**The type byte.** `strMtProjectRemote` is asserted at 2,304 bytes, so there's no room to add a field without a format change. The `strMtValues::midiInstrument[].reserved[7]` bytes are zeroed on create (`fileManagerProject.cpp:320`) and on v10 migration (`mtFileManagerOldVersionRecovery.cpp:257`), and nothing else reads them. I'll take one as `projectType` (0 = Default, 1 = Tape 2, 2 = Tape 4, 3 = Tape 6), give it a named accessor so nobody reads it as "MIDI reserved", and add a `static_assert` that pins its offset. Old projects read 0, which is Default, so they load exactly as before and the struct size doesn't move. Bumping `PROJECT_FILE_VERSION` to grow `strMtValues` would break the size contract and stop older firmware opening the file, which is the wrong trade for one byte.

**Everything else goes in a sidecar file**, `tape.ptt` in the project folder, with its own magic, version and CRC: which tracks are tape tracks, their tape and chain settings, modulators, depths, lane targets, synth binding, and the 4 scenes. Lanes themselves are per pattern and go in their own files (§5). `ProjectSave` and `ProjectOpen` copy it the way synth patches are copied (`copyWorkspacePatches`). It's read field by field with counts and clamping, never as a struct dump, so I can add fields later without a version change.

**The tracks a type doesn't have.** The sequencer still has its 8 sample tracks and the MIDI tracks internally, and the pattern format doesn't change. In a tape project, tracks past 2, 4 or 6, and the MIDI tracks, are hidden in every screen, their pattern data stays empty and their voices aren't rendered. That's where the chains' CPU comes from, especially on the Original. An ordinary track can still play MIDI instruments, as any track can.

**Opened on older firmware**, a tape project looks like a Default project (the type byte is ignored): the tracks play, the tapes play as plain samples, and the sidecar is ignored. Nothing breaks; the tape behaviour and effects are just missing.

---

## 3. The tape

**A tape is an instrument.** A tape project reserves the last two sample instrument slots (indices 46 and 47, or just 47 in Tape 2) as tapes A and B. That buys a lot for free: the tape saves as a normal `.pti`, the sample editor can trim, normalise or run a `.ptfx` over it, sample and stem export see it, and the memory bar counts it.

The instrument gets a new play mode, `playModeTape`, appended to `instrumentPlayMode` (`core/source/mtStructs.h:387`, append-only). Its playback is a new `core/libs/Audio/play_memory_tape.cpp` next to the existing per-mode files:

- **The head always rolls** while the transport plays, looping the window START…START+LENGTH. With no notes in the pattern, the tape still plays. That's the difference between a tape and Beat Slice.
- **SYNC** (off, 1–16 bars) sets the loop to N bars at the project tempo, and the tape's speed follows the tempo like a tape machine. It's a soft lock: the head is nudged a little each block toward where the beat says it should be, with a crossfaded jump if it's far out. Step 1 of the pattern is the loop's start.
- **Notes jump** the head to a slice (16 slices of the window), with a short crossfade, and the head keeps rolling. A note-off doesn't stop a tape. An explicit note-off step (the existing `OFF`) mutes it.
- **Parameters:** START, LENGTH, SPEED (±200 %, negative is reverse), PITCH, REVERSE, RPT (hold the head on a short loop, 10 ms–1 s, for stutters), RLEN, WOW, FLUTTER, ROTATE (shift the loop against the beat), XFADE (seam crossfade), SYNC, LEVEL.
- **Never streamed.** `isPlayModeStreamable(playModeTape)` returns false, so the tape stays in RAM where the record head can reach it.

**Length and memory.** A tape's length is fixed when it's made: the default is 8 bars at the project tempo, and the most is set per device by phase 0 measurements. Two tapes is the reason the cap can be generous: my starting figures are up to 60 s stereo per tape on Tracker+ and Mini (about 10.6 MB each, from a pool of about 39 MB) and up to 20 s mono on the Original (about 1.8 MB each, from 7.5 MB). Tapes come from the normal sample pool like any sample, with no change to `SAMPLE_MEMORY_SIZE`, so Default projects lose nothing. A tape that doesn't fit is refused with a popup, not silently shortened.

**Recording onto tape.** The existing `Recorder` overwrites sample memory from its base and stops every player while it runs, so it can't record while the song plays. Tape tracks get their own write path, a small `TapeWriter` inside the tape player. It writes into the instrument's own sample memory at the record head, one block at a time, in the interrupt: RAM writes only, no SD and no allocation.

- **REC IN** per tape track:
  - **ORDINARY**: the mix of the project's ordinary tracks. This is the default, and it's how the written parts reach a tape.
  - **One track**: any track, ordinary or the other tape track.
  - **SELF**: resampling through its own chain.
  - **INPUT**: line or mic, plus USB on Tracker+ and Mini.

  The tap is one block late, which is fine for a loop.
- **DUB** (−100…+100) sets how much of the old loop survives a pass: 0 is sound-on-sound, +100 keeps the loop, −100 replaces it.
- **Takes** are bar-exact: armed, they start on the next bar line and stop after the set number of bars.
- **Bounce** records an ordinary track's output onto a tape for the set bars.
- **Freeze to tape** copies the GRAIN live buffer (§4) onto the tape.
- **Saving:** a take marks the instrument dirty, and saving writes it in the main loop like any edited sample.

---

## 4. The chain

A tape track's audio runs `tape + source → GRAIN → RIPPLE → COLOR → ECHO → track amp/pan → mix`, plus the existing delay and reverb sends. Every block starts out of the way (WET 0, or no drive), so a fresh tape track sounds exactly like its tape. Each of GRAIN, RIPPLE and COLOR is an insert or a send (ROUT). ECHO is always a send. Ordinary tracks don't get chains of their own (two chains is the CPU cap), but they can **feed** one. Every ordinary track has two new sends, **FX A** and **FX B**, into tape track A's or B's chain. That's how drums, or anything else on an ordinary track, flow into the effects live, without printing them to tape first:

- **What a send feeds.** The send enters the chain after the tape, so it's granulated, rung through RIPPLE, coloured and echoed along with whatever the tape track plays. It's pre-fader by default, so a drum track can be muted in the mix and still heard only through the effects.
- **Hits drive the chain.** A note on an ordinary track that sends to a chain can, per send, pluck RIPPLE (so the kick strikes the strings), retrigger the chain's ADSR modulators, and feed a FOLLOW modulator. The send level, and the pluck on or off, are lane-able on the tape track and settable by step FX on the ordinary track.
- **Cost.** A send is one gain and one mixer input per track. The chains run once whatever feeds them, so sending all four ordinary tracks costs about the same CPU as sending one.

Ordinary tracks also keep the instrument filter, overdrive, bit depth and the master delay and reverb sends they have today.

**Where it lives.** A new `TapeTrackChain` in `core/AudioEngine/`, one per tape track, so at most two. It's built only in tape projects, and constructed after the voices and synth nodes, because `AudioStream::update_all` runs nodes in construction order. Its input is an `AudioMixer` of the track's voice filter output and, on Tracker+ and Mini, the synth part bound to that track. Its output takes the place of the voice's own connection into the amp and the send mixers. Default projects never build or connect it.

The DSP nodes are new `AudioStream` classes in `core/libs/Audio/`, following `core/libs/Audio/AGENTS.md` (NULL-safe `allocate()`, every block released or sent on every path, `AUDIO_CORE_RAM` only for the inner loops that earn it):

| Block | What it does | Parameters | Memory |
| :--- | :--- | :--- | :--- |
| **GRAIN** | Granulates the track's last 1–4 bars from a live buffer recorded on the bar line. SCAN picks where grains read: the tape window, a moving cursor (time stretch), a fixed spot, or trailing the write head (a granular delay). FREEZE holds the buffer. | SIZE, RATE, PITCH, SPREAD, SPRAY, CONTOUR, REV chance, PATTERN, SCALE, PRAND, SCAN, WARP, OFFSET, FEEDBACK, GRAINS, SYNC, ROUT, WET | The live buffer (up to 4 bars) + a grain pool shared by the two tape tracks |
| **RIPPLE** | 4 tuned Karplus-Strong strings, a comb, or 4 resonators on a chord's partials. Notes on the track retune and pluck it. | PITCH, DECAY, TONE, SCALE (chord), CUT/RES, DRIVE, SPREAD, PLUCK, ROUT, WET | 4 short delay lines |
| **COLOR** | Drive (soft clip), crush (bits, rate or both), noise that follows the input's envelope, tilt EQ | DRIVE, CRUSH, CMODE, NOISE, NDECAY, NTONE, TILT, ROUT, WET, LEVEL | None |
| **ECHO** | A tape-style delay per track, then a send into the master reverb | TIME (or a SYNC division), FEEDBACK, DLY level, TONE, SPREAD, DUCK, AGE, DIFFUSE, VERB send | One delay line |

**The effects arena.** GRAIN, RIPPLE and ECHO buffers come from one block of sample memory reserved when a tape project opens, shown on the memory bar as "Tape FX". Buffers are carved when a block is switched on (WET above 0) and handed back when it's off. Only the main loop allocates. The interrupt only reads pointers it was handed, so to free a buffer the main loop unpublishes the pointer, waits a block, then frees. With at most two chains, my starting guess is about 2 MB on Tracker+ and Mini and about 0.5 MB on the Original; phase 0 and phase 6 set the real figures.

**Reused, not rebuilt:** the instrument filter, overdrive and bit-depth node (still on the sample source going into the chain), the master reverb, the master delay send, the limiter and the master enhancer.

---

## 5. Lanes

Lanes are what make this a tracker instrument rather than a box with knobs. Each tape track has **5 lane groups of 4 lanes**, one group per part of the instrument:

| Group | Default lanes (each can be re-targeted within its group) |
| :--- | :--- |
| **TAPE** | SPEED, RPT, START, WOW |
| **GRAIN** | WET, SIZE, PITCH, SCAN position |
| **RIPPLE** | WET, PITCH, DECAY, TONE |
| **FX** (COLOR + ECHO) | DRIVE, CRUSH, ECHO send, VERB send |
| **MOD** | M1–M4 AMT |

Each lane is bound to one target, and each step either holds a value or is empty. Tying each group to one block means a lane's meaning is readable from where it sits. A group's column is the same width as a track column, which is what makes the screen layout below work.

**Per-lane behaviour**, set on the lane:
- **STEP**: the value applies on that step only, then the parameter goes back to its base. That's a p-lock.
- **HOLD**: the value stays until the next value in the lane.
- **GLIDE**: ramps from this value to the next one across the steps between.

**Values.** On screen, a lane value shows in the parameter's own units (ms, %, semitones, a division). It's stored as one byte: 0–254 mapped across the target's range, and 255 meaning empty.

**Storage.** `Sequencer::strPattern` is a size contract (16,432 bytes), so lanes don't go in it. Each pattern that has lanes gets `lanes_NNN.ptl` next to its pattern file:
- a header: magic, version, and the lane targets and modes for each tape track;
- then, for each tape track, only the groups that hold a value: 4 lanes × 128 steps, 512 bytes a group;
- so a pattern's file is at most 5 KB, with both tape tracks using every group.

Copy, paste, insert, delete and the pattern length act on lane columns too, so moving steps around keeps lanes lined up with the notes.

**Applying them.** The sequencer's step handling runs in the interrupt, so lane values are applied there from a RAM copy: no file reads, no allocation. Lane files are read in the main loop when a pattern is loaded or *queued*. The next pattern in song mode or performance mode is read ahead into a second buffer, and the interrupt flips one pointer when the pattern changes. I'm deliberately not copying the SD read that `Sequencer::loadNextPattern` does in the interrupt, which `core/source/AGENTS.md` already lists as a violation.

**Order of evaluation**, each block, per target:
1. the base value (the knob, or the active scene);
2. the lane value, if the lane has one in force;
3. plus each modulator's swing × depth;
4. clamped to the target's range.

**On screen.** The pattern editor today shows 4 full track columns (note, instrument, 2 FX), or 8 narrow ones with one parameter each (`patternViewMode`, `core/interface/patternEditor/fPatternEdit.cpp`). Tape projects add a **lane view** next to these. A pattern-editor button switches between the normal view and the lane view (an Fn binding on the Mini):

| Type | Normal view | Lane view (4 columns) |
| :--- | :--- | :--- |
| Tape 2 | 2 track columns | tape track · lane group · lane group · the ordinary track |
| Tape 4 | 4 track columns, as today | tape 1 · its lane group · tape 2 · its lane group |
| Tape 6 | 6 tracks, scrolling as Default's 8 do | tape 1 · its lane group · tape 2 · its lane group |

A screen button cycles the group under the cursor: TAPE → GRAIN → RIPPLE → FX → MOD. Each column header names the group and its 4 targets.

A lane column holds 4 lanes of 2-character values, the same character count as a track column. Its display-list cost should be about the same as the track column it stands in for, which keeps the frame inside the 8,172-byte budget (`core/libs/NewDisplay/AGENTS.md`); I'll count the bytes in phase 4. Step FX keep working on every track.

**One new step FX:** `SCENE` (value 1–4, recalled with the scene glide), taken from one of the free `FX_TYPE_R*` placeholders in `core/source/mtSequencer.h`. It's the one thing a lane can't do, because scenes span tracks. It works from any track, so an ordinary drum track can call the scene changes.

---

## 6. Modulators

Each tape track has **4 slots**. Ordinary tracks keep the instrument envelopes and LFOs they have today. Each slot runs one engine:

- **LFO**: SIN, TRI, SQR, SAW or RANDOM. Free (0.02–20 Hz) or synced (16 bars to 1/32), free-running or retriggered by notes, with phase, skew, smoothing and per-cycle variation.
- **ADSR**: triggered by the track's notes and, optionally, by the tape's loop seam.
- **FOLLOW**: an envelope follower on this track, any other track or the input, auto-levelled, with an optional sample-and-hold. Following an ordinary track (the kick, say) to move a tape track's chain is the obvious use. I'll build it on the RMS node each voice already has (`AudioVoice::getRMS`).
- **SEQ**: a 1–16 step sequence with its own length and rate. Lanes already sequence at the pattern's length, so SEQ is kept for polymeters (a 5-step SEQ over a 16-step pattern).

**Depths.** A depth (−100…+100 %) connects a slot to a target, and at 100 % the swing covers the target's whole range. An LFO swings both ways around the base; ADSR, SEQ and FOLLOW push one way, set by the depth's sign. Limits: 24 depths a track, at most 8 per slot. Each slot also has AMT and OFFSET, both lane-able, so a lane can fade a modulator in over a bar.

**Cost and context.** Modulators run once per audio block (128 samples, about 2.9 ms) in the interrupt, before the chain renders. Each depth is a precomputed `range × depth` entry in a fixed array, double-buffered: the main loop rebuilds the array when a depth changes, and the interrupt flips one byte to pick it up. No allocation, no `double`, and every literal is `f`-suffixed.

---

## 7. Synth engines (Tracker+ and Mini)

**On ordinary tracks, synths work exactly as they do today:** synth instruments in the instrument column, the 3 parts, the existing synth editor, patch and macro screens, and synth step FX.

**On a tape track,** a part can be **bound** to the track. While it's bound, that part's output goes into the track's chain input instead of straight to the mixer, so a synth runs through GRAIN, RIPPLE, COLOR and ECHO. Notes for it go on the tape track as usual, with the synth instrument in the instrument column. There are at most two tape tracks and three parts, so a binding never runs out of parts, and at least one part stays free for the ordinary tracks. The binding is the only new synth setting, and it lives on the TAPE screen.

The synth's real-time budget guard (`kMaxSynthRealtimeBudgetUs` in `synth_PolySynth.cpp`) stays as it is. The chains' cost comes out of what's left, and phase 0 says how much that is.

**On the Original Tracker** there are no synth parts (`FEATURE_SYNTH` is IMX only). A tape track's sources there are its tape and the sample instruments, including the wavetable and granular play modes.

---

## 7a. Drum engine: theseus (all three devices)

The Original Tracker has no synth engines, so a drum source can't be a `.psyn` plugin (those are IMX only). It has to be firmware code that runs on all three devices, the way the sample player does. The engine is **theseus**, my DFAM-style percussion voice from [`thoughtgrain/operators-xy`](https://github.com/thoughtgrain/operators-xy) (`machines/theseus/engine.cpp`). That tree is MIT and my own work, so this is a straight port with a `NOTICE.md` entry, not a redesign.

**Why it fits a tracker.** Theseus is one voice where **the note picks the drum**: low notes are kicks (a sine with a deep pitch drop), the middle is toms and snare, and high notes lean on noise and FM into hats and metal. The signal path is VCO 1 FM-ing an inharmonic VCO 2, mixed with noise, through a 2-pole state-variable filter that the hit opens (crossfading low-pass to high-pass across the upper zones), then a decay envelope. On a tracker, the note column already is "which drum", so a whole kit is one instrument on one track. Its hit LFO steps once per note instead of with a clock, so it's locked to the pattern: at Rate 2 it alternates every other hit, and a pattern breathes the same way every time it plays.

**Why it fits all three CPUs.** It's integer DSP with no `double`, no tables and no allocation, and its only divides are two per block. The OP-XY port estimates about 13–15 k instructions per 64-sample voice block at its heaviest, roughly 230 a sample. That's cheap on the Tracker+/Mini's M7, but on the Original's 180 MHz M4 a voice is around 5 % of the CPU, so the voice count there is set by phase 0's measurements (my guess is 2, against 4 on Tracker+ and Mini).

**How it plugs in.**
- A **drum instrument** is a sample instrument with a new play mode, `playModeDrum`, appended to `instrumentPlayMode`. It has no sample data, so it costs no sample memory, and it works in any project type, Default included, on any track, ordinary or tape.
- The player is a new `core/libs/Audio/play_memory_drum.cpp`. It renders a small pool of theseus voices inside the track's one voice, so a kick's tail isn't cut by the next hat. A new hit takes a silent voice, else the quietest one. The output is Q15, which is the `int16` the audio blocks already carry, so there's no conversion.
- The instrument's filter, overdrive, bit depth, volume, pan, envelopes and sends still apply after it, like any instrument. On a tape track it feeds that track's chain. On an ordinary track its FX A and FX B sends feed the tape tracks' chains (§4), and each hit can pluck RIPPLE and retrigger the chain's envelopes, so a drum track is the most direct way to play the effects.

**Parameters.** Theseus's 8 controls, kept as they are: **Decay** (30 ms–1.5 s, high drums shorter), **Punch** (pitch-envelope depth), **Cutoff**, **FM**, **Noise**, **LFO** (hit-LFO depth on cutoff, FM and a little pitch), **Rate** (one LFO cycle every 32…2 hits) and **Tune** (±24 semitones, shifts which drum a note picks). Every one can be moved by step FX, and on tape tracks by lanes and modulators.

**What changes from the OP-XY build, and why:**
- **Velocity comes back.** The XY doesn't give engines velocity, so the port fixed it at full. On the Tracker the step's volume column becomes the hit's velocity, which deepens Punch and the filter's opening, as the OP-1 original did. Without a volume value a hit plays at full, which matches the XY.
- **Block size.** The Tracker's blocks are 128 samples. I render each block as two 64-sample halves, so the envelope ramps and the sound stay the same as the XY build.
- **Sample rate.** The note-to-drum zone maths assumes the XY's rate. I recompute its offset for the Tracker's 44,117.6 Hz, so the same note picks the same drum.
- **Pitch.** The XY hands the engine a phase increment per sample. Here it comes from the note and the instrument's finetune, through the existing pitch code.

**Where the 8 values are stored** is the one open detail. `strInstrument` is a 372-byte contract, but a drum instrument doesn't use the granular, wavetable or loop-point fields. If those fields can hold the 8 values without changing what any other play mode reads, the kit saves and loads as a normal `.pti`, and older firmware just sees an empty instrument. Otherwise they go in a small project sidecar keyed by instrument slot. I'll decide in phase 7a, after grepping every reader of those fields.

**Tests.** The theseus repo has a host sound test (`machines/theseus/tests/sound.cpp`: 5,832 control and pitch combinations; peak level, every hit decaying to silence, kick longer and darker than hat, the LFO stepping once per hit, Tune moving the zone). I'll port it to this repo's host-test layout and add golden hashes for the Tracker build.

---

## 8. Scenes

A project has 4 scenes, stored in `tape.ptt`. A scene holds each tape track's base parameter values, chain settings, synth binding and modulator AMTs. It doesn't hold tapes, patterns, lanes, depths, ordinary tracks or the tempo; in a tracker the tempo belongs to the song.

- **Recall** glides over a set time (default 300 ms). Choices such as a mode or a scale switch at the halfway point.
- **From the pattern:** the `SCENE` step FX.
- **From performance mode:** a scene row, so scenes can be thrown live next to the existing performance FX.
- **Capture:** Shift + a scene slot saves the current sound into it.
- **Morph** (an encoder sweep between two scenes) is a later phase.

---

## 9. UI and the three devices

New screens follow `docs/recipes/ui-screen.md`. Tape projects add one module, **TAPE**, with pages switched by the screen buttons: TRACKS (which tracks are tape tracks, and their synth binding) · TAPE · GRAIN · RIPPLE · COLOR · ECHO · MOD · LANES · SCENES · REC. Each page shows its parameters in the existing parameter-bar style, and every page is a list the Mini can step through. It adds a module slot, a menu entry, a new-project popup and a pattern-editor view, which `AGENTS.md` §7 says to discuss first upstream. This is a fork, so that waits until the work is offered upstream; a draft post is at the end of this plan.

| | Original | Tracker+ | Mini |
| :--- | :--- | :--- | :--- |
| Project types | Default, Tape 2/4/6 | Same | Same |
| Audio | Mono chain (`SAMPLE_CHANNELS = 1`); GRAIN and RIPPLE stereo spread collapses to mono | Stereo | Stereo |
| Synth binding | — | Yes | Yes |
| Assign a depth | Hold a modulator pad + turn the encoder on a parameter | Same | MOD page: pick a slot, then a target from a list, then set the depth with ±1/±10 |
| Lanes | Lane view; a screen button picks the group | Same | Same, with the groups cycled by an Fn key |
| REC IN sources | Ordinary tracks, one track, self, line, radio, mics | Same + USB | Ordinary tracks, one track, self, line, mic, USB |
| Expected limits | Fewer grains and strings; **set by phase 0** | Bound by the synth budget | Same as Tracker+ |

---

## 10. Rules this has to keep

- **Interrupt context.** The tape player, `TapeWriter`, the chain nodes, modulators and lane application all run in the audio DMA interrupt. That means no SD, no allocation, no UI calls, no blocking and no `double`. Inner loops also avoid library calls that look cheap, because `core/` builds with `-fno-builtin`.
- **Main loop only:** reading and writing `tape.ptt` and lane files, carving and freeing effects buffers, rebuilding depth tables, saving tapes.
- **Default projects are untouched.** No change to the sizes of `strInstrument`, `strPattern` or `strMtProjectRemote`, to `SAMPLE_MEMORY_SIZE`, or to any existing enum value. Appended only: `playModeTape` and the `SCENE` FX ID.
- **Memory.** No new static `EXTERNAL_RAM` buffers on IMX (it's full). Everything tape-sized comes from sample memory, and only in tape projects. Additions to fast RAM (grain pool state, modulator tables, lane RAM copies) are counted per phase against the ARM build's memory report.
- **Display.** Every new or changed control's display-list bytes are counted against the 8,172-byte frame budget.

---

## 11. Phases

Each phase is one PR. The fences are a starting point; I re-grep the blast radius right before each phase, because code moves.

### Phase 0: Measure and prepare (no user-visible change)
- **Do:** measure idle and busy audio load per device with `Profiler` on fixed reference projects: 6 sample tracks, plus 3 synth parts on IMX. Bring in, or depend on, the `tests/audio` host harness from `perf/optimization-plan`, so every new node can get golden-hash tests.
- **Fence:** this doc (the numbers), host-test scaffolding under `tests/`.
- **Invariant:** firmware unchanged.
- **Verify:** rung 5 measurements written into this doc. They set the tape length caps and the per-device limits in §9.

### Phase 1: Project types and sidecar (no audio)
- **Do:**
  - the `projectType` accessor and the `static_assert` pinning its offset;
  - the type popup in the new-project flow;
  - `tape.ptt` read and write, copied on save and open;
  - tape instruments created, with no audio yet.
- **Fence:** `core/source/mtStructs.h` (accessor only), `core/FileManager/fileManagerProject.cpp`, `ProjectSave.cpp`, `ProjectOpen.cpp`, a new `core/FileManager/TapeProjectFile.{h,cpp}`, `core/interface/projectEditor/fProjEdit.cpp` and `gProjEdit.cpp`.
- **Blast radius:** every reader of `midiInstrument[].reserved` (today, only the two zeroing sites), the callers of project save and open, MOD/IT import.
- **Invariant:** byte-identical files for Default projects.
- **Verify:** rung 2: a host test that loads and saves existing projects and compares the bytes, plus a `tape.ptt` round-trip. Rung 3.

### Phase 1b: Hide the tracks a type doesn't have
- **Do:** in a tape project, hide the tracks past 2, 4 or 6 and the MIDI tracks in the pattern editor, mixer, performance mode, song and export screens, and skip rendering their voices.
- **Fence:** `core/interface/patternEditor/`, the mixer and performance screens in `core/interface/`, `core/AudioEngine/mtAudioEngine.cpp` (the skip only), the export track list.
- **Blast radius:** every loop over `kSampleTracks` or `kAllTracks` in those screens, found by grep.
- **Invariant:** Default projects visually identical and bit-exact.
- **Verify:** rung 2 for the render skip; rung 4 on each screen.

### Phase 2: Tape playback
- **Do:** `playModeTape` and `play_memory_tape.cpp`: the running head, the SYNC soft lock, slice jumps on notes, RPT, wow and flutter, reverse. Plus the TRACKS and TAPE pages of the new screen.
- **Fence:** `core/source/mtStructs.h` (enum append), `core/libs/Audio/play_memory*.{h,cpp}`, `isPlayModeStreamable`, the sequencer's note path for tape instruments, a new `core/interface/tapeMode/` screen.
- **Invariant:** bit-exact audio for every existing play mode.
- **Verify:** rung 2: golden hashes for the existing modes unchanged, and a new golden for tape playback. Rung 3, then rung 4 on each device I have.

### Phase 3: Recording onto tape
- **Do:** `TapeWriter`, REC IN routing (ordinary tracks, one track, self, input), DUB, bar-exact takes, bounce, saving dirty tapes.
- **Invariant:** the existing Sample Recorder behaves as before.
- **Verify:** rung 2 for the overdub maths; rung 4 for takes against the clock.

### Phase 4: Lanes
- **Do:**
  - lane files, the RAM double buffer and the read-ahead;
  - applying lanes in the step path;
  - the lane view in the pattern editor;
  - copy, paste, insert and delete over lanes;
  - the `SCENE` FX ID reserved (it does nothing until phase 8).
- **Invariant:** timing (same ticks per block, step 1 on the downbeat with external clock); pattern files byte-identical.
- **Verify:** rung 2: a sequencer host test that steps a pattern with lanes and checks the values per tick. Rung 5 for timing. The display-list byte count for the lane view.

### Phase 5: COLOR and ECHO
- **Do:** wire up `TapeTrackChain`, the two cheapest blocks, and the effects arena.
- **Invariant:** Default projects bit-exact; with every block off, a tape track is bit-exact with the same tape played on an ordinary track.
- **Verify:** rung 2 goldens; rung 5 CPU per block per device.

### Phase 5b: FX sends from ordinary tracks
- **Do:** the FX A and FX B sends on every ordinary track, the send-level step FX, and hit triggers (RIPPLE pluck and ADSR retrigger stubs that phases 7 and 8 fill in).
- **Fence:** `core/AudioEngine/` connection code for the chain inputs, the mixer screen's send controls, `core/AudioEngine/mtAudioSequencer.cpp` for the step FX.
- **Invariant:** with both sends at 0, every track is bit-exact with no send connected.
- **Verify:** rung 2 goldens; rung 5 CPU with every ordinary track sending.

### Phase 6: GRAIN
- **Do:** the live buffer, the grain pool, the SCAN modes, freeze, and freeze to tape.
- **Verify:** rung 2 goldens with fixed random seeds; rung 5 CPU, which sets the grain counts per device.

### Phase 7: RIPPLE
- **Do:** strings, comb, resonators, and plucking on notes. Anything ported from an MIT original gets a `NOTICE.md` entry.
- **Verify:** rung 2; rung 5.

### Phase 7a: Drum engine (theseus)
- **Do:** port theseus into `play_memory_drum.cpp` with its voice pool, `playModeDrum`, velocity from the volume column, the zone offset for 44.1 kHz, the 8 parameters and their storage, step FX for them, and the drum page in the instrument editor.
- **Fence:** `core/source/mtStructs.h` (enum append), new `core/libs/Audio/play_memory_drum.cpp` and its header, the play-mode dispatch in `play_memory.cpp`, the instrument editor's play-mode list, `NOTICE.md`.
- **Blast radius:** every reader of the `strInstrument` fields the parameters reuse, if they're reused.
- **Invariant:** bit-exact audio for every existing play mode; `.pti` files of other play modes byte-identical.
- **Verify:** rung 2: the ported theseus sound test, plus goldens. Rung 5: CPU per voice on the Original, which sets its voice count.

It doesn't depend on any tape phase, so it can go first if drums on the Original matter more than tapes.

### Phase 8: Modulators and scenes
- **Do:** the 4 slots, the depth tables, the assign gesture and the Mini list, scenes, the `SCENE` FX, and a scene row in performance mode.
- **Verify:** rung 2 for modulator output and depth maths; rung 4 for the gesture; rung 6 for scene glides.

### Phase 9: Synth binding (Tracker+ and Mini)
- **Do:** route a bound synth part into a tape track's chain.
- **Fence:** the `core/AudioEngine/` connection code under `FEATURE_SYNTH`, `SynthVoice`, and the binding on the TRACKS page.
- **Invariant:** unbound synths bit-exact.
- **Verify:** rung 2; rung 5 under the synth budget guard.

### Phase 10: Device tuning, Mini pass, manual
- **Do:** per-device limits from the measurements, the Mini's Fn bindings, and a user manual page in `docs/` in the same voice as the others.
- **Verify:** rung 4 on all three devices; rung 6 write-ups.

Later, once the numbers are in: morph, a second room, a channel strip, GRAIN's WARBLE and VINYL forms.

---

## 12. Decisions made

- **Four project types:** Default (16 tracks, no tape), Tape 2 (1 tape track), Tape 4 and Tape 6 (up to 2 tape tracks each). The caps keep tapes long and give the chains the CPU, memory and screen room.
- **Ordinary tracks are full tracker tracks.** They play samples, synths and drums as they do today, get printed onto the tapes, and feed the effects live through their FX A and FX B sends.
- **Fixed tape length**, chosen when a tape is made. Tapes that grow while recording fit badly with the Tracker's sample memory and make save sizes unpredictable.
- **Tape length:** fixed, default 8 bars, with per-device caps set by phase 0.
- **theseus is the drum engine** (§7a), ported from my MIT `operators-xy` tree, because the Original has no synth engines. It works on every track, in every project type, on every device.
- **This is a fork** (`thoughtgrain/tracker-ce`), so the upstream "discuss first" step isn't a gate for now. The draft post below is kept for if and when this goes upstream.
- **Synth editing stays as it is.** Engines, patches and macros are edited in the existing synth screens. Tape projects add only the binding of a part to a tape track.
- **Patterns, song mode and performance mode work as usual.** Lanes travel with patterns, and scenes are one more thing performance mode and a step FX can recall.

## Appendix: draft GitHub Discussion post

> **Proposal: tape projects (2, 4 or 6 tracks, with up to two tape tracks that have granular, resonator and effects lanes)**
>
> I'd like to add project types. When you make a new project you'd pick Default (what exists today, still the first choice), Tape 2, Tape 4 or Tape 6. A tape project has that many tracks. Up to two of them (one in Tape 2) are tape tracks: a tape loop that rolls with the transport and can be recorded over while the song plays, an effects chain after it (granular, resonator, colour, echo), four modulators, and lanes, which are columns beside the pattern's notes that sequence the tape and its effects step by step. The other tracks are ordinary tracker tracks with samples and synths, and they're what you usually print onto the tapes.
>
> Why the caps: with fewer tracks and at most two tapes, each tape can be long, and the CPU and screen room go to the two effects chains and their lanes.
>
> What changes for people who don't use it: nothing. Default projects read and write byte-for-byte as before, and no sample memory is taken unless a tape project is open.
>
> What's new on screen: a type choice when you make a project, a TAPE screen (which tracks are tape tracks, the tape, each effect, the modulators, lanes, scenes and recording), and a lane view in the pattern editor in tape projects. Patterns, song mode, performance mode and the synth editor work as they do now.
>
> It's planned for all three devices: the Original Tracker (mono chain, no synth binding), and Tracker+ and Mini (a synth part can be run through a tape track; the Mini uses its Fn keys and list pages instead of pads).
>
> The full plan, with the phases and how each one is verified, is in `docs/design/tape-mode-plan.md` on the `work/keen-babbage-uvnhez` branch. I'd especially like opinions on the lane view layout and on where the TAPE screen should sit in the module buttons.
