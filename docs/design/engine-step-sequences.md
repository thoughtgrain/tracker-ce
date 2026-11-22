# Short step sequences inside synth engines (16–24 steps)

**Status:** research only, nothing built. Branch `research/engine-step-sequences`, off `main` at `43d39ba`.
**Devices:** Tracker+ and Tracker Mini. Synth engines are `FEATURE_SYNTH` (IMX) only. The Original has no synths (see the SynthLite plan on `work/synthlite-og`).

## The question

Could each synth engine have its own small step sequence (16 to 24 steps) that plays when a note triggers it, the way Polyend Synth and Play do? What would it take?

## Short answer

**Don't put the sequencer in the engines. Turn on the one that's already in the synth library.**

The PolySynth library this firmware links (`synth/source`, built into `libPolySynth.a`) came over from Polyend Synth. It already has a complete per-patch step sequencer and arpeggiator. They're stored in every `.pts` patch and work with any engine, factory or community, because they sit in front of the engine and send it notes. The Tracker just never switches it on: `Synth::init` (`core/synth/Synth.cpp:190`) sets the synth up with a tempo source and no sequencer.

So this is mostly wiring and UI, not DSP. I'd estimate 3 to 5 focused PRs.

## What already exists

| Piece | Where | What it does |
| :--- | :--- | :--- |
| Patch data | `Patch::Sequencer` in `synth/include/PolySynth/Patch.h` | `Mode` (Off / Arp / Seq), arp style (Up, Down, Play order, Random) and octave range (1–8), and one `Sequence` per patch: root note, scale, and up to `kMaxSequenceLength` = **64** steps. Each step has a chord (root plus chord scheme), velocity and length in steps. |
| Saved in patches | `synth/source/sequencer/Sequence.proto`, `PatchImpl.cpp:613–629` | The `.pts` file already serialises mode, arp config, root, scale and every step (`max_count = 64`). |
| Playback | `synth/source/sequencer/impl/` | `SequencerEngineImpl` with `SeqTrack` and `ArpTrack`. Steps are 16ths (`kSeqPpqn` = 96, `kPulsesPerStep` = 24). Up to `kMaxSequencerTracks` = **8** running at once. |
| Transpose | `SeqTrack::processStep`, `SequencerEngineImpl::transpose` | Plays the sequence relative to the note that triggered it, in scale or chromatically. Polyend Synth's grid does exactly this (`GridMapperImpl::handleSeqPadEvent`). |
| Sync | `TransportSyncImpl` | An internal clock from a tempo, or an external MIDI clock, plus start/stop. |

All of this is in the open `synth/source`, not the proprietary engine binaries.

## Why not inside each engine

- **Six of the eight engines can't be changed.** ACD, DIRT, FAT, PERC, VAP and WTFM are Polyend's prebuilt binaries, and their licence (`synth/prebuilt/LICENSE-PROPRIETARY.txt`) only allows redistributing them unmodified. Only open engines (CHIP here; TORO and Joseph's FM on their branches) could get a built-in sequence.
- **Every engine would re-implement the same thing.** It'd mean step storage squeezed into float patch parameters, its own clock, and its own transpose.
- **The plugin ABI has nowhere to put it.** `PsynPluginApi` (`psyn_plugin.h`) gives a voice MIDI events and a tempo. There's no step-data block, so it'd need an ABI version bump, and the patch editor would need a new kind of screen per engine.

The library route gives every engine sequences at once, including the factory ones, with nothing new to keep in sync.

## What it would take

### 1. Create and drive the sequencer (`core/synth/Synth.cpp`, `Synth.h`)
- Call `synth->createSequencer()` in `Synth::init`, before `setup`. Its step pool comes from the same allocator as the rest of the synth, which is the 6 MB `PSYN_RAM_POOL_SIZE` arena: 8 × `sizeof(StepSequence)`. Measure that, but it should be small.
- Run `updateAudio()` every block and `updateMain()` in the main loop. `SynthImpl::updateAudio` already loops over its components; check the sequencer is registered as one, and that the Tracker's synth update actually calls it.
- **Clock: lock it to the Tracker, not a second clock.** The internal clock adds `kUpdatePeriod` (a float) every block. The Tracker sequencer keeps time with a deliberate `double` accumulator. Run side by side, they'll drift apart over a long pattern. Better: switch the sync to external (`setClockInternal(false)`, `setTransportInternal(false)`) and send it clock and start/stop from the Tracker's own sequencer. `MidiAdapter::sendRealTime` exists, but nothing calls it on the synth's virtual port today (`core/source/mtMidi.cpp` only sends notes, CCs and so on there). **To verify:** that `TransportSyncImpl`'s external mode pulses on incoming 24 ppqn clocks without jitter, and where in the Tracker tick to send them.

### 2. Route Tracker notes into sequences (`core/synth/MidiAdapter.cpp`, or a small router next to it)
When a synth track plays a note and that instrument's patch has mode **Seq**:
- Note on: `startSequence(0, instrument, trackId, velocity)`, then `transpose(...)` by the played note, the same way `GridMapperImpl::handleSeqPadEvent` does.
- Note off, cut or fade: `stopSequence(trackId)`.

Mode **Arp** maps onto `addToArp` / `removeFromArp`. Off stays exactly as today.

**Interrupt rules (AGENTS.md §2):** this runs from the Tracker sequencer and MIDI input, so it's in the audio interrupt.
- `SequencerEngineImpl` holds tracks in a `static_vector` and sequences in a preallocated pool, so there's no heap on start and stop.
- `createSequence` copies up to 64 steps per start, which is fine.
- Still needed: an allocation-free check of `transpose`, `addToArp` and the `midiNotifier` path under the host-test harness.

### 3. Edit sequences on the Tracker (`core/interface/instrumentEditor/`)
- A **Sequence** page in the synth instrument screens:
  - **Mode:** Off / Arp / Seq.
  - **Arp:** style and octaves.
  - **Seq:** length 1–24, root, scale.
- **Steps on the pads:** 24 steps fill two rows of 12 pads, so all 24 fit on screen with the bottom two rows free for picking a note or value. Steps 1–16 sit on the top row plus the first four of the second, the same layout as a one-bar pattern.
- **Per step:** note (a chord later if wanted), velocity and length, edited with the encoder like pattern values. Empty steps are rests.
- **Display budget:** 24 step cells plus labels have to fit inside the 8,172-byte frame (`core/libs/NewDisplay/AGENTS.md`). Count the display-list bytes before settling the layout.

**Why cap the UI at 24 when the format allows 64:** it's what you asked for, it fits the pads, and it keeps the screen readable. Patches made on another device with longer sequences should still load and play. Show them as read-only past step 24 rather than truncating them.

### 4. Tracker step FX (optional, later)
A step FX to pick Seq/Arp per step, or to restart a sequence, would follow the step-FX recipe. **Pattern FX IDs are append-only (AGENTS.md §3).** Leave this out of the first cut.

## Compatibility

- **Patches:** nothing changes on disk. Old `.pts` files have mode Off and no steps, so they play exactly as before. A patch with a sequence saved on the Tracker uses the same protobuf layout as Polyend Synth.
- **Projects:** the sequence lives in the patch, so projects don't change. `strInstrument` and the pattern format are untouched.
- **Official firmware:** it uses the same library, so a sequenced patch should load there and simply not sequence unless it also enables the sequencer. **To check:** confirm official firmware ignores the mode field rather than rejecting the patch.

## Risks and things to measure

| Risk | How to check |
| :--- | :--- |
| Clock drift or jitter against the Tracker sequencer | Rung 5 (AGENTS.md §6): a MIDI monitor or logic analyser on a long pattern, internal versus Tracker-driven clock |
| Audio load from up to 8 running sequences plus voices | `Profiler` audio load, before and after, with 8 voices of WTFM |
| Fast RAM (`SRAM_DTC` is about 97 % full) | The CI memory report. Keep sequencer state in the PSYN pool, not in `.bss`. |
| Display budget on the step page | `cDisplay::composedFrame` words and `displayList::overflows` |
| A note-off missed when a track is cut, muted or the song stops | Stop every sequence on transport stop and on track mute; listening test |

## Proposed PR split

Each PR is one concern, per AGENTS.md §1.

1. **Sequencer on, clock locked, no UI.** Create the sequencer, drive its clock from the Tracker, and route Seq and Arp notes. Testable with a patch edited on a computer or Polyend Synth. *Invariant:* mode-Off patches sound bit-identical (host test, rung 2).
2. **Arp page.** Mode, style and octaves.
3. **Sequence editor.** 24 steps on the pads, note, velocity, length, root and scale.
4. *(Optional)* A step FX for per-step mode and restart.

## Open questions for you

1. **Trigger behaviour:** should a sequence restart on every note, or keep running and only transpose while the note is held (legato)? Polyend Synth restarts.
2. **Length range:** 16–24 only, or 1–24 so odd lengths give polyrhythms against the pattern?
3. **Steps:** single notes only, or chords per step, which the format supports?
4. **Original Tracker:** leave this out entirely, or plan a SynthLite equivalent later?
