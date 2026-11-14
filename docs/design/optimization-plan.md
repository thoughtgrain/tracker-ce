# Optimization Plan: Small, Fenced Chunks

> Status: **plan only**. Nothing here is implemented yet. Each chunk below is sized to be one small PR.
>
> Re-checked on 2026-11-14 against the fork's `main` after merging upstream `ed51301`. Upstream fixed C1, C2 and C3 and rewrote the display pipeline, which fixes D1 and makes D2 moot. A second review added C6–C14, A9–A13, D5–D7 and F8–F11. Line numbers below are from that merged tree.

I ran a read-only review of four areas: audio rendering, display and UI, sequencer and MIDI, and file I/O and memory. Then I re-read the source behind every finding that made it into this plan. This doc turns the results into a work queue.

The goal isn't "make it faster" in one sweep. It's a series of narrow changes, each with:
- a clear fence around what it touches,
- a list of the code that depends on it,
- the behaviour it must not change,
- and a way to prove it didn't.

---

## Re-check after the upstream merge (2026-11-14)

| Item | Status | Why |
| :--- | :--- | :--- |
| C1 | **Fixed upstream** | `handleMidiInStart` sets the song position and plays instead of calling `playSongFromPosition()`, so no SD save from the interrupt (upstream 98468f0) |
| C2 | **Fixed upstream** | Every interrupt-side caller now calls `stop(StopContext::audioCallback)`, which defers the pattern reload to the main loop (upstream PR #54) |
| C3 | **Fixed upstream** | Both playlist loops in `findSlotWithPattern` are bounded by `SONG_MAX` (upstream 1e7ebbb) |
| C4, C5 | Open | Code unchanged. C4 was found again independently by the second review |
| D1 | **Fixed upstream** | Display lists are built in the MCU now; the coprocessor is only polled after it actually drew (`cDisplay::readyForFrame`) |
| D2 | **Moot** | `synchronizeRefresh()` is empty: every queued control is built before a frame is composed |
| D3a–c | Open | Display-list bytes still count against the 8,172-byte frame (`kFrameControlBytes`), and now also cost MCU build time |
| Everything else in Phases 2–3 | Open | The cited files are unchanged by the merge, or changed elsewhere in the file |

---

## How I want every chunk to work

1. **One chunk, one PR, one concern.** If a chunk starts pulling in a second file that isn't listed in its fence, stop and split it.
2. **Every chunk has a fence.** I list the files it may touch. Anything outside is out of scope, even if it's tempting.
3. **Every chunk lists its blast radius.** These are the callers and shared state that could notice the change, found by grepping, not by memory. Re-run the grep before starting, because code moves.
4. **Every chunk names its invariant.** For most audio chunks that's *bit-exact output*. For sequencer chunks it's *tick timing and MIDI clock offsets*. For file chunks it's *byte-identical files*. If a chunk can't keep its invariant, it gets reclassified as a behaviour change and reviewed as one.
5. **Measure before and after.** Gains in this doc are reasoned estimates, not measurements. Phase 0 sets up the measurements, and no performance chunk merges without a before/after number, or a stated reason it can't have one.
6. **Correctness first.** Phase 1 fixes real bugs the review uncovered, including two that touch the SD card from inside the audio interrupt. Those outrank any speedup.
7. **Respect open PRs.** PR #1 (bug fixes) and PR #2 (TORO) already touch `display.cpp`, `display.h`, `bar.cpp`, `displayStructs.h`, `fSampleEdit.cpp`, `sampleImporter.h`, `SynthEditor.cpp` and `SynthPatchEditor.cpp`. Chunks that touch those files wait until those PRs merge. That avoids rebasing conflicts and doesn't change what the PRs are being reviewed for.

---

## Phase 0: Measure first

Without numbers, half of these chunks are guesses. This phase adds no optimizations, only the instruments to judge them.

| ID | What | Fence | Why |
| :--- | :--- | :--- | :--- |
| **M1** | Record a baseline per platform: `Profiler` audio-block load (already wraps every block, `core/source/Tracker.cpp:178-199`) for three reference projects: sparse, dense, and dense with external clock at 300 BPM | No code change; results go in this doc | Every audio and sequencer chunk compares against this |
| **M2** | Display baseline: frame words per screen from `cDisplay::composedFrame` and the controls left out in `displayList::overflows` (`lastMemoryUsed` is no longer set since the display rewrite), plus a main-loop iteration counter in a DEBUG build | DEBUG-only lines in `Tracker.cpp` | Display chunks need frame cost and loop rate, not a feel |
| **M3** | File baseline: time project load, Save As, a 10 MB stereo export and a WAV import, on the same SD card | No code change | File chunks are judged on wall-clock time |
| **M4** | Host harness for audio units, like `tests/toro`: run the limiter, state-variable filter and envelope multiply loops through a test so "bit-exact" is checked rather than assumed | New `tests/audio/` only | Gates A1–A3 and A5. **Done for the limiter and filter:** [`tests/audio/`](../../tests/audio/), 80 golden hashes, mutation-tested. The envelope (A3) still needs adding. |
| **M5** | Generate `.map` files for both IMX targets and record ITCM/DTCM free space and where hot functions actually land | Build artifact, no code | Gates A8 and S3, and confirms the ITCM headroom the developer guide quotes |

---

## Phase 1: Correctness fixes found during the review

These come first because they're bugs, and some of them stall or corrupt things in ways that would also distort any performance measurement.

### C1. MIDI Start in Song mode touches the SD card inside the audio interrupt (fixed upstream)
- **Status:** fixed upstream in 98468f0; kept here for the record. The interrupt-side popup it mentions is still there (now C12).
- **What I verified:**
  - `updateAudioBuffers` (`Tracker.cpp:171-198`) runs `midiUpdate()` and `sequencer.tick()` inside the audio interrupt.
  - A MIDI Start or Continue with the Song screen open goes `receiveStart` (`mtMidi.cpp:476`) → `handleMidiInStart(…, songMode)` → `playSongFromPosition()` (`mtSequencer.cpp:1429-1443`). That calls `saveWorkspacePatternNow()` and `loadWorkspacePatternNow()`, both synchronous SD writes and reads.
  - The SD write happens whenever the pattern has unsaved edits. The read happens whenever the song position is on a different pattern.
- **Why it matters:** SD access from the interrupt can pre-empt the main loop's own SD streaming and file operations in the middle of a FatFs call. That risks filesystem corruption and certainly overruns the audio block.
- **Fence:** `core/source/mtSequencer.cpp`, `mtSequencer.h`.
- **Change:** when the request comes from interrupt context, latch it (start step, song mode, song position) and finish it in `Sequencer::update()`, which already runs in the main loop. The clock-sync resets in `handleMidiInStart` (`externalClockPulses`, `clockSignalsToCompensate`) stay synchronous with the 0xFA byte; only the pattern save and load move.
- **Blast radius:** `handleMidiInStart` callers (`mtMidi.cpp:476`, `:482`); `playSongFromPosition` callers (`grep -n playSongFromPosition core`); the `mtPopups.show` at `mtSequencer.cpp:3598` (also an interrupt-to-UI call; move it with the rest).
- **Invariant:** step 1 still lands on the downbeat with an external clock, which the 1.0.0 changelog advertises.
- **Verify:** on hardware, have a DAW send Start while the Song screen is open, with an edited pattern and a song position on a different pattern. Listen for glitches, re-open the project to check SD integrity, and measure the interrupt duration with the profiler.
- **Risk:** medium.

### C2. `stop()` can load a pattern from SD inside the audio interrupt (fixed upstream)
- **Status:** fixed upstream (PR #54, `StopContext::audioCallback` and `Sequencer::reloadPatternAfterStop`); kept for the record.
- **What I verified:** at `mtSequencer.cpp:1567-1574`, when performance mode is set to turn off at pattern end, `stop()` calls `loadWorkspacePatternNow()` and `switchRamPatternsNow()`. `stop()` is called from interrupt context at `:1719`, `:1748`, `:2287`, and `:3637` (a MIDI Stop from the DAW).
- **Fence:** `mtSequencer.cpp`, `mtSequencer.h`.
- **Change:** set a pending flag and perform the load in `Sequencer::update()`.
- **Blast radius:** 22 `sequencer.stop()` call sites in the UI. These run in the main loop and must behave exactly as now. It's acceptable to perform the load immediately when not in interrupt context (detect the interrupt via the IPSR register).
- **Verify:** on hardware: in performance mode, leave the screen so it's armed to turn off at pattern end, then send MIDI Stop.
- **Risk:** medium.

### C3. `findSlotWithPattern` reads one byte past the playlist and can loop forever (fixed upstream)
- **Status:** fixed upstream in 1e7ebbb; kept for the record.
- **What I verified:** `playlist` is `uint8_t[SONG_MAX]` with `SONG_MAX = 255` (`mtStructs.h:146, 971`). The loop index is `uint8_t` with no upper bound (`mtSequencer.cpp:3339-3373`), so it reads `playlist[255]`. With no 0 terminator and no match, the first loop never exits. This runs in the interrupt at every song-mode pattern switch.
- **Fence:** `mtSequencer.cpp` (this function only).
- **Change:** bound both loops by `SONG_MAX`.
- **Verify:** host-testable as a pure function: a full playlist, no terminator, pattern absent → returns -1.
- **Risk:** low.

### C4. NewList keeps redrawing forever after one scrolled item
- **Status (2026-11-14):** still open. Under the new display code it means the list is rebuilt for every frame, though identical frames are no longer sent.
- **What I verified:** `newList.cpp:507` sets `autoRefresh = true` and nothing in NewList ever clears it. `cList` clears it (`list.cpp:221`), and `display.cpp:607-610` re-queues any control with `autoRefresh` after every render.
- **Effect:** on the synth editor screens, after one long name scrolls, that list re-renders and the whole screen rebuilds every few loops, indefinitely. It's a bug and a performance drain.
- **Fence:** `core/libs/NewDisplay/newList.cpp`.
- **Change:** `autoRefresh = false;` at the top of `NewList::update()`. The scroll path sets it again when needed.
- **Blast radius:** lists created with `scrollText = true`: `SynthEditor.cpp:824`, `SynthPatchEditor.cpp:1631`, `:1804`.
- **Verify:** long names still scroll, and with a short item selected the display goes idle (`display.isIdle()`).
- **Risk:** low.

### C5. Small latent display and limiter bugs (one chunk, all one-liners)
- `displayControls.cpp:62-63`: `string2Bitmaps` returns on a NUL after `SAVE_CONTEXT` and `BEGIN`, skipping `END`/`RESTORE`. No current caller triggers it.
- `effect_limiter.cpp:16`: `memset(inputStorageBuffer, 0, 10)` never clears `outputStorageBuffer`. Use `sizeof` on both.
- **Fence:** those two files. **Risk:** low. **Verify:** visual check; limiter output unchanged after init.

### C6. Master WAV export truncates its file on every chunk
- **What I verified:** `saveMasterBufferToSD()` (`mtAudioExport.cpp:325`) runs once per 70-block packet (called from `:607` and `:682`) and opens the file with `kWriteCreateNew` each time (`:331`), which is `FA_CREATE_ALWAYS` on both platforms (`FileSystemImpl.cpp`). It truncates the file, seeks past the end and writes. The file only comes out right because FatFs reuses the freed clusters and their old data is still on the card.
- **Cost:** every packet frees and rebuilds the whole cluster chain, so a long song export does work that grows with the square of its length. The stems path opens once and is fine.
- **Fence:** `core/Exporter/mtAudioExport.cpp`.
- **Change:** open once in `start()`, keep the handle, write the header and close in `finishRender`.
- **Invariant:** byte-identical WAV, header written last.
- **Verify:** export the same song before and after, compare bytes; time a 5-minute export (M3).

### C7. `saveMemoryToWav` keeps a stale stereo flag and overruns its buffer
- **What I verified:** `conversionInPlace` is set to true for stereo (`fileTransfer.cpp:1304`) and never back to false; `copySample` (`:602`) can also leave it set. The next mono sample is then written as half-length stereo with a stereo header. The interleave loop runs `memStep / 2` frames of 4 bytes (`:1326`), writing 2 × `memStep` bytes into the 32,640-byte `externalRamFileManagerSrcBuff` and reading past the end of the sample on the last chunk.
- **Fence:** `cFileTransfer::saveMemoryToWav`. **Blast radius:** caller `fileManagerExport.cpp:461`.
- **Change:** set `conversionInPlace` explicitly in both branches; loop `memStep / 4` frames.
- **Invariant:** output equals a correct mono or stereo export. This is a bug fix, so mono exports after a stereo one change (to correct).
- **Verify:** export a stereo then a mono instrument in one run; check both files' headers, lengths and samples.

### C8. External-clock pulses pile up during selection play
- **What I verified:** `tick()` takes the internal-clock branch whenever `player.selectionMode` is set (`mtSequencer.cpp:96`), and only the external branch clears `externalClockPulses`. `updateExternalClockTempo` (`:3720`) replays every pulse so far, a 48-float `std::copy` each, on every block, and its tempo estimate drifts. A pad stop doesn't clear the backlog, so the external branch then plays all of it in one interrupt. `playSelection` (`:1352`) doesn't check the clock source.
- **Fence:** `mtSequencer.cpp`.
- **Change:** in selection mode with an external clock source, drop the pulses and reset the tempo state instead of recording them.
- **Invariant:** outside selection mode, identical ticks and MIDI output. Inside it, a small behaviour change: the tempo estimate restarts after the selection.
- **Verify:** sync to a DAW, play a selection for a minute, stop; profiler audio load and no dropout.

### C9. Every MIDI note rebuilds the pattern view inside the interrupt
- **What I verified:** in sequencer voice mode, the note-on and note-off handlers call `patternEditor.refreshPatternFromOutsideModule()` unconditionally (`mtMidi.cpp:119`, `:163`, `:209`, `:253`). With the pattern screen open that runs `refreshPattern()`: 16 tracks × 15 rows with `snprintf` per FX, then a display refresh, from the interrupt.
- **Fence:** `mtMidi.cpp`, and `handleNoteOn`/`handleNoteOff` in `mtSequencer.cpp` if they need to report a change.
- **Change:** refresh only when the pattern or cursor changed, via a flag the main loop handles.
- **Invariant:** identical timing and MIDI; the screen shows the same thing.
- **Verify:** play chords from a keyboard with the pattern screen open; profiler audio load before and after.

### C10. The display refresh queue double-queues controls
- **What I verified:** `cDisplay::refreshControl` (`display.cpp:1184`) skips the bottom queue slot when looking for duplicates. Since the display rewrite, `renderFrame` pops an entry *before* building it (`:766-770`), so the bottom slot is never the control being built: two refreshes of one control in one handler queue it twice (for example `gSongEdit.cpp` refreshes `label[7]` twice every 100 ms).
- **Fence:** `cDisplay::refreshControl`.
- **Change:** delete the bottom-slot special case and search the whole queue.
- **Invariant:** visually identical; fewer control builds.
- **Verify:** queue length after a double refresh is 1; song screen looks the same.

### C11. The Revolver layout's playhead check is nearly always true
- **What I verified:** `refreshPlayheadOnScreen` (`fPatternEdit.cpp:1186-1192`) tests `playheadPosition <= patternLength + 7 - middleVisibleStep`, which is at least 7 and usually far larger. It should mirror the other wrap test. So the heavy tracker control is rebuilt on most steps in edit mode even with the playhead off screen.
- **Fence:** that function. **Invariant:** identical whenever the playhead is visible.
- **Verify:** Revolver layout, playhead off screen: no rebuild per step.

### C12. A popup shown from the interrupt on MIDI Start
- **What I verified:** `handleMidiInStart` (interrupt) calls `mtPopups.show(0, "Load pattern from song first")` (`mtSequencer.cpp:3672`).
- **Change:** latch a flag and show it from `Sequencer::update()`. **Fence:** `mtSequencer.cpp`. **Risk:** low.

### C13. Every note-on while stopped resets all synth FX (IMX)
- **What I verified:** the TRK-518 workaround `engine.endAllFx(true)` (`mtSequencer.cpp:2632`) runs on every note-on while stopped, not once at stop: map lookups for 3 synths × 10 FX × 2 slots, then a full resend of their macro CCs.
- **Change:** call it once in `stop()`, or guard it with a flag cleared on play. **Invariant:** synth state at the first note after stop unchanged. Not yet checked: whether anything sets synth FX while stopped.

### C14. A second MIDI NoteOff after notes that already ended
- **What I verified:** a gate-length or roll note is ended (`noteOpen = 0`) while `stepOpen` stays set, so the next note start (`mtSequencer.cpp:818`) and an OFF step send `sendNoteOff` again.
- **Change:** guard the MIDI and synth NoteOff with `noteOpen`; leave sample tracks alone. **Invariant:** MIDI output only loses the redundant NoteOffs. **Verify:** MIDI monitor.

---

## Phase 2: Low-risk optimizations

Each one keeps output bit-exact, byte-identical, or visually identical.

### Audio (both platforms unless noted)

**A1. Limiter: skip the divide when not limiting; keep state in locals**
- **Where:** `effect_limiter.cpp:95-114` (sidechain) and `:158-180` (main).
- **Cost now:**
  - A float divide on every sample, on both L and R.
  - The 5-tap delay shifts through a member array every sample.
  - `uint16_t` members get reloaded because the `int16_t` output store may alias them.
  - This costs the same whether 1 or 8 voices play, so it's a worst-case cost.
- **Change:**
  - Copy members into locals and write them back after the loop.
  - Rotate the delay through local variables.
  - Use `f = (threshold < xpeak) ? threshold / (float)xpeak : 1.0f`.
- **Why it's bit-exact:** both values are integers ≤ 65535, so `t/x < 1` whenever `t < x`. The original's `x = 0` case gives `+inf` or `NaN`, and both resolve to 1.0, the same as the new branch.
- **Fence:** `effect_limiter.cpp`. **Blast radius:** only `AudioEffectLimiter::update`; the instances are `AudioOutput.h:44` and `mtAudioEngine.cpp:162`.
- **Verify:** M4 harness byte comparison, then the Profiler. **Gain (estimate):** about 1–2 % of MK66 audio time.

**A2. Filter: lift the per-sample type switch out of the loop**
- **Where:** `filter_variable.cpp:160-175`. GCC at `-O2` won't unswitch the loop.
- **Change:** one specialized loop per type (`template<int Type>`), chosen once per block.
- **Fence:** `filter_variable.cpp/.h`. **Blast radius:** callers at `:125` (`stabilize`, 8 samples, in place) and `:232`, so in-place operation must keep working.
- **Verify:** M4 harness. **Gain:** about 2–4 of ~25–30 cycles per sample, per voice with the filter on.

**A3. Envelope multiply: temporaries in locals**
- **Where:** `effect_envelope.cpp:728-786`. `sample12..78` and `tmp1/tmp2` are struct members (`play_memory.h:497-498`) that `uint32_t*` stores may alias.
- **Change:** use locals. Optionally cache `mult_hires`, `inc_hires` and `count`, writing them back before any `envelopeSwitchPhase()`, `stop()` or `play()`.
- **Fence:** `effect_envelope.cpp`. **Blast radius:** those temporaries are used only in this file (grep). The cached fields are also written by UI-side setters under `AudioNoInterrupts`.
- **Verify:** M4 harness. **Gain:** 1–3 cycles per sample per voice channel.

**A4. Panning gain uses a double divide**
- **Where:** `mtAudioEngine.cpp:1742,1748`, `/ 50.0`. Both FPUs are single-precision only (`-mfpu=fpv4-sp` / `fpv5-sp`), so this becomes a software double divide.
- **Change:** `/ 50.0f`.
- **Verify:** check all 101 inputs on the host for the same final integer multiplier. **Fence:** that function. **Risk:** low.

**A5. WAV preview resampler does the same work twice**
- **Where:** `play_sd_wav.cpp:145-146` and `:159-160` compute the identical `inPos`/`inIdx` twice per sample, including `floorf` as a library call (`-fno-builtin`).
- **Change:** delete the second pair. `inPos ≥ 0`, so `(uint32_t)inPos` replaces `floorf`.
- **Fence:** `play_sd_wav.cpp`. **Scope:** file-browser preview only. **Verify:** compare preview output on the host.

**A6. Avoid 256-byte copies in the voice chain** (three separate chunks, in this order)
- **A6a, mixer** (`mixer.cpp:38`): the first input is received writable, which copies when the block is shared, but only read. Receive it read-only and reuse the block if `ref_count == 1`. Instances: `mixer.cpp:80-90`; users: `MixerAndFxs.h:57-60`, `AudioOutput.h:46`.
- **A6b, amplifier** (`amplifier.cpp:42-49`): its input has 3 readers, so it always copies. Add an `applyGain(dst, src, …)` overload and leave the existing one alone (`monoToStereo.cpp:52` and `effect_dynamics.cpp:140` share it).
- **A6c, MonoToStereo (MK66 only)** (`monoToStereo.cpp:30-56`): with centred pan, both gains are unity, yet each voice copies twice. Pass the same block to both outputs. Land after A6a, or the downstream mixers just copy instead.
- **Verify:** stem export renders identical bytes before and after; Profiler.


**A9. Synced envelopes divide in double every block**
- **Where:** `mtEnvelopeGenerator.cpp:581` (`currentSeqSpeed / 15.0`), and `/ 1000.0` at `:279`, `:315`, `:643` on the random path. Reached from `playerEngine::update` in the interrupt for every synced envelope while playing.
- **Change:** `15.0f`, `1000.0f`. **Why it's bit-exact:** a float divided by an exact constant, done in double and rounded back, equals the float divide. **Verify:** host check over all `currentSeqSpeed` values used.

**A10. MK66 runs a USB input graph that never gets data**
- **Where:** `AudioInput.cpp:21-39` wires `usbStream` into two amplifiers, two RMS meters and two record queues on both platforms, but only IMX calls `setInBuffer` on it (`mtAudioEngine.cpp:331`). On MK66 every block allocates and zero-fills two blocks and pushes them through six nodes nobody reads.
- **Change:** wrap those objects and connections in `#ifdef PLATFORM_IMX`. **Invariant:** bit-exact (nothing on MK66 reads them). **Verify:** MK66 build and profiler.

**A11. Polyverb copies its input block every update**
- **Where:** `effect_polyverb.cpp:40-46`. Both inputs are the same mono block from the send mixer, so `receiveWritable(0)` always copies 256 bytes.
- **Change:** take input 1 read-only and release it first, so input 0 needs no copy; read the dry sample before writing either output. **Invariant:** bit-exact. **Verify:** host test.

**A12. PolySynth mono parts copy a block they could share (IMX)**
- **Where:** `synth_PolySynth.cpp:183-188`: with `reuseUnmodifiedMono`, the post-fader block is a memcpy of the send block.
- **Change:** transmit the same block to both, relying on reference counting (downstream nodes use `receiveWritable`). **Invariant:** bit-exact.

**A13. Skip delay and reverb blocks at an exact silent fixed point**
- **Where:** `effect_shortDelay.cpp` `process()`, `Polyverb/reverb.h` `Process()`. Both run in full every block, even with no sends.
- **Why not the existing detectors:** measured on the host with the real code, the reverb's `IsActive()` goes false 200–650 ms before the last non-zero output, and the delay's `isSilent()` ignores the lines that aren't heard. Cutting at either changes the sound.
- **Change:** skip only when the state is an exact fixed point:
  - Delay: zero input, zero filter state, and zero reads, filter outputs and writes on all three lines for at least the delay time (time 0 reads the oldest slot, so it needs the whole line). A skipped block outputs zeros, still writes zeros into the slots it would overwrite, and advances `headIdx`, `silentSamplesCount` and an idle counter, so `isSilent()` and export behaviour don't change.
  - Reverb: a full 16,384-sample rotation of zero writes (tracked with an OR per write), lowpass states at most subnormal, zero input. A skipped block only moves both write pointers and steps the LFO exactly as `Start()` would; `Start()` and the skip should share one helper.
- **Evidence:** a scratch prototype matched the original sample for sample over 2,000,000 random blocks, with parameter changes mid-silence. Six planted bugs; three caught at random, two (time 0, damping 1.0) only by targeted scenarios, one (filter reads not tracked) needs a white-box test.
- **Tests to commit:** golden hashes recorded on the current code before the change; targeted edge cases; white-box predicate checks; engagement checks; the six planted bugs must fail. On hardware: a byte-identical song export before and after, and profiler load with no sends, sends but stopped, and sends while playing.
- **Fence:** `effect_shortDelay.{h,cpp}`, `Polyverb/fx_engine.h`, `Polyverb/reverb.h` (MIT code from Mutable Instruments; name the edit in the PR). **Gain (estimate):** several % of MK66 audio time with no sends; not measured.

### Sequencer

**S1. Round the MIDI-clock offset in float, keep the accumulator double**
- **Where:** `mtSequencer.cpp:96-108`. This runs once per nano-step, inside the interrupt: about 4.6k times per second at 120 BPM and up to about 30k at 800 BPM. Each iteration makes about 6 software double calls.
- **Change:** keep `internalTickAccumulatorUs` and its subtraction in double, exactly as they are, because they're the drift fix. Compute only `offsetUs` and its rounding in float. The offset is always shorter than one block (~2.9 ms), where float resolution is ~0.0002 µs.
- **Fence:** `Sequencer::tick()`.
- **Blast radius:** `midiSetClockSubBlockOffsetUs` (`:105, :112, :3613`), read by `sendMidiClock` (`mtMidi.cpp:379-393`). The accumulator is reset in `play()` (`:1326`) and `stop()` (`:1565`).
- **Invariant:** identical tick count per block; MIDI clock offsets within ±1 µs.
- **Verify:** extract the loop as a pure function. Simulate hours of blocks at 44100 and 44117.647 Hz, 40–800 BPM, swing on and off, and assert identical tick counts and offsets ≤ 1 µs apart. Then check clock jitter on hardware with a MIDI monitor.
- **Gain (estimate):** about half of 1–7 % of MK66 at 120–800 BPM.

### Display (after PR #1 merges, since it shares files)

**D1. Skip the coprocessor poll when there's nothing to draw**
- **Status:** fixed by the upstream display rewrite; kept for the record.
- **Where:** `display.cpp:470` polls `isCoProEmpty()`, 2–3 SPI register reads, on every main loop *before* checking whether anything is queued.
- **Change:** return early when `updateStep == 0`, the queue is empty and `!stopAppend`.
- **Fence:** `cDisplay::update()`.
- **Blast radius:** the `cmdOffset` resync and error recovery after the poll now run only when work is pending, which is still before any coprocessor use. The `loadImage` block above line 470, `stream.cpp:68` and `processDisplayUpdateImmediate` keep their own polling.
- **Verify:** screens refresh after idle; the main-loop rate (M2) goes up.
- **Risk:** low.

**D2. Batch refreshes in Master screen value handlers**
- **Status:** moot since the display rewrite: `synchronizeRefresh()` does nothing, and all queued controls are built before a frame is composed.
- **Where:** `gMasterParams.cpp:570-602, 729-771, 917-926, 1180-1186, 1200-1219, 1302-1346`. Each handler refreshes a bar and a label separately, so every encoder tick causes two full-screen rebuilds and swaps.
- **Change:** add `display.synchronizeRefresh()` after each pair. 142 other call sites already use this pattern.
- **Fence:** `gMasterParams.cpp` only. Other screens get their own chunk if M2 shows the same pattern.
- **Verify:** values still update while turning fast; rebuild count per tick (M2) halves.

**D3. Display-list byte trims** (three separate chunks, all visually identical)
- **D3a, `cLabel`:** emit `SAVE_CONTEXT`/`RESTORE_CONTEXT` only when something is drawn between them (`label.cpp:318, 401`). Saves 8 B per label.
- **D3b, `cList`:** only send COLOR when it changes, the way `NewList::setRowTextColor` already does (`list.cpp:277-288`, `509-517`, `558-566`, `603-611`). Saves about 4 B per row.
- **D3c, `cBar`:** drop the repeated `LINE_WIDTH(16)` after the first in each update (`bar.cpp`). Saves about 8–16 B per bar.
- **Verify:** compare `ramSize` per control in a DEBUG build; screenshots unchanged.
- **Why it matters:** these leave headroom in the 8,172-byte frame budget, which the parameter-visualization redesign will need.


**D5. Pad LEDs are cleared and relit on every playback step**
- **Where:** `fPatternEdit.cpp:302` calls `lightUpPadBoard()` every step, which starts with `clearGlobalPads(1, 1, 0)`. The off-then-on marks the LEDs changed, so the MK66 driver's change check (`platforms/mk66/3rdparty/Leds/mtLED.cpp`) still sends a full frame over I2C each step; the IMX driver has no check at all.
- **Change:** compute the target state, then write only differences. **Invariant:** the same pads lit. **Fence:** `fPatternEdit.cpp`, `mtPadsBacklight.cpp`.

**D6. The song screen redraws every 100 ms when nothing changed**
- **Where:** `fSongEdit.cpp:132-133` calls `refreshSongPlayer()` and `refreshTempoField()` on a timer, which re-runs `sprintf` and refreshes the player, `label[7]` and the bar even while stopped.
- **Change:** keep the last values and refresh only on a difference. **Invariant:** visually identical; tap-tempo text still updates.

**D7. The performance screen refreshes 8 labels 4× too often**
- **Where:** `fPerformance.cpp:760-798`: `blinkState` flips every 4th call, but all eight track labels are refreshed on every 100 ms call.
- **Change:** refresh a label only when its colour changes. **Invariant:** visually identical, including shift and mute colours.

### File I/O (both platforms)

**F1. Bounce-buffer misaligned sample transfers**
- **What I verified:** both NXP SD drivers drop to one block per command, plus a memcpy, when the buffer isn't 4-byte aligned (`platforms/imx/sdmmc/src/fsl_sd.c:1684`). `noInteralAlign` is never set.
- **When it happens:** sample addresses are `int16_t*`, so after an odd-length **mono** sample (`fileManagerInstrument.cpp:427-432`), or on the right channel of an odd-length stereo sample (`:181-183`), the address is only 2-byte aligned. That instrument's whole load or save then goes one sector at a time.
- **Change:**
  - In `loadInstrumentRawSampleDataToMemory` (`fileTransfer.cpp:872`), `saveSampleRawDataToFile` (`:958`) and the pre-cache read (`VoiceStreamManager.cpp:300`), read or write through `externalRamFileManagerBuffL` when `((uintptr_t)dst & 3) != 0`.
  - Add `__attribute__((aligned(32)))` to the two file-manager buffers (`fileManager.cpp:17-18`).
  - **Do not** change the sample memory layout.
- **Fence:** those three functions plus the buffer declarations.
- **Blast radius:** callers `fileManagerInstrument.cpp:315`, `SampleImport.cpp:536`, the 4 `saveSampleRawDataToFile` sites. The bounce buffer is file-manager-only, not `externalRamScratchBuff`.
- **Verify:** log the alignment per instrument on a project with odd-length mono samples; time the load (M3).

**F2. Streamed-instrument copy uses a 4 KB stack buffer**
- **Where:** `ProjectSave.cpp:462`, `fileManagerSamples.cpp:82`.
- **Change:** use `externalRamFileManagerBuffL` (32 KB, idle in both paths). That's 8× fewer calls and frees 4 KB of the tight main stack.
- **Verify:** the output `.pti` is byte-identical; time Save As.

**F3. Exporting a streamed sample opens the file twice per KB**
- **Where:** `fileManagerExport.cpp:425-436`, 512-frame chunks through `readInstrumentFramesBlocking`, which opens, seeks, reads and closes on every call.
- **Change:** use 4,080-frame chunks in the file-manager buffers, and remove the 4 KB of stack arrays.
- **Verify:** the exported WAV is byte-identical; time it.

**F4. Workspace load computes instrument sizes twice**
- **Where:** `WorkspaceProjectLoad.cpp:146` and `:239`. The first result is overwritten before it's read; only the progress bar uses it.
- **Change:** set `memoryToLoadTotal = 0` at `:146`.
- **Verify:** load time and that the progress bar still works.

**F5. Re-opening a file just to get its size**
- **Where:** `fileManagerPattern.cpp:71, 107`, right after `loadFileToMemory` already had the size.
- **Change:** remember the size in `cFileTransfer` and add a `getLastFileSize()` accessor.
- **Fence:** pattern load only. The instrument-side sites (`isPtiFileValid` overload) get their own chunk.

**F6. Copy-loop chunks that end mid-sector**
- **Where:** `READ_WRITE_BUFOR_SIZE` is 32,640, which isn't a multiple of 512, so each chunk costs an extra partial-sector read and write.
- **Change:** a local `kCopyChunk = 32256` in the plain copy and save loops only (`fileTransfer.cpp:334, 410, 755, 928, 1010`). Leave the conversion paths alone.
- **Verify:** byte-identical output.


**F8. Recording save writes 512 frames per main-loop pass, with `new` each time**
- **Where:** `RecordingSave.cpp:87-120`: 1–2 KB per pass from file offset 44, so writes straddle sectors; stereo allocates and frees a buffer every pass (`:107`). The `writeFile` result is unchecked, so a full card goes unnoticed (a bug).
- **Change:** large chunks through an existing static buffer; check the result. **Invariant:** byte-identical file.

**F9. IT and MOD import read sample data by reopening the file per chunk**
- **Where:** `fileManagerImportIt.cpp:562-574` (2 KB chunks) and `:620-635` (512 B), each a `loadFileToMemory` with an offset: an open, a seek from cluster 0 and a read. Writes go out 2 KB per pass (`:1076-1080`, `fileManagerImportMod.cpp:684-688`).
- **Related bugs:** the offset variant of `loadFileToMemory` returns `fileTransferInProgress` on a short read even with `fileWholeOnce` (`fileTransfer.cpp:159-162`), leaving the file open at a stale position; `importModFile_ImportWave` has an empty `else` (`fileManagerImportMod.cpp:631-647`), so an open failure retries every pass forever.
- **Change:** one open, sequential large reads. **Invariant:** identical sample memory and `.pti` files.

**F10. IT export rewrites its offset table with two seeks per item**
- **Where:** `fileManagerExportToMod.cpp:279-330`: per instrument, sample and pattern, seek back to the header, write 4 bytes, seek to the end (which walks the cluster chain from the start).
- **Change:** keep offsets in RAM and write the table once at the end. **Invariant:** byte-identical file.

**F11. Files checked and opened two to five times each**
- **Where:** `fileManagerInstrument.cpp:83-85` then `isPtiFileValid` (`fileManager.cpp:805-810`) per instrument per load; `SampleImport.cpp:309-359`; `WorkspaceProjectLoad.cpp:285-287`; `ProjectSave.cpp`. `exists ? getFileSize : 0` is redundant because `getFileSize` returns 0 for a missing file; `exists` + `remove` before `kWriteCreateNew` is redundant because it truncates.
- **Change:** one chunk per call site. **Invariant:** byte-identical files.

---

## Phase 3: Medium risk, needs hardware and careful review

| ID | Chunk | Why it's medium | Gate |
| :--- | :--- | :--- | :--- |
| **D4a** | **i.MX: write display words in one SPI transaction.** Today `EVE_Write32` makes 4 calls to `transferByte`, each a full `LPSPI_MasterTransferBlocking` (`EVE_Layer.cpp:92-100` → `SpiBus.cpp:68-84`). Add `SpiBus::write(buf, len)` with `rxData = nullptr`, and use it in `EVE_Write32` and `EVE_AddrForWr` only. | It touches every display byte on Tracker+ and Mini. The DMA image stream shares LPSPI4, and the SDK's `Busy` return is currently ignored. | Boot, fonts, every screen and the image viewer on both IMX devices. Likely the biggest display win. |
| **D4b** | Same for `API_SendString` and the RAM_G bulk uploads | Font and bitmap uploads at boot | After D4a has soaked |
| **S2** | **One undo snapshot per audio block, not per recorded note.** Each recorded MIDI or pad note copies the 16 KB pattern into the undo ring inside the interrupt (`mtSequencer.cpp:2587, 2721, 2836`). | Undo granularity is user-visible: a chord would undo as one step. | Record chords, undo/redo on hardware |
| **F7** | WAV imports whose data starts at an offset ≡ 2 mod 4 (e.g. an 18-byte `fmt` chunk) go one sector at a time: read to `buf + (offset & 3)` | Needs `convertAudioData` checked for halfword-aligned input | Host test with crafted WAVs |
| **A7** | Sample player scratch fields to locals, and hoist the streamed/RAM check (`play_memory_*.cpp`) | The gain depends on codegen I couldn't inspect | Only if objdump shows spills (M5 toolchain) |
| **A8** | Move hot IMX audio code into fast RAM (ITCM): `updateWavetable`, the granular updaters, `evalWavetableEnvelopeFixed30`, the input gate, newlib `memset` | ITCM has ~22.5 KB free and must be checked first | M5 map, then Profiler |
| **S3** | Put the MIDI clock-out interrupt path in ITCM (`sendMidiClockImmediate`, `TMR4_IRQHandler`) | Only matters if the map shows they're in flash | M5 map, then clock jitter on a logic analyser |

---

## Deferred: behaviour changes and long shots

These are real, but they change what you hear or see, or the gain is speculative. Each needs its own proposal and a listening or visual test, not a quiet optimization PR.

- **Skip zero blocks from idle voices.** This is the biggest audio saving (an estimated 5–10 % of MK66 in sparse songs), but the filter state would no longer decay during silence, which risks clicks or resonant bursts at the next note-on. It needs a filter reset or an N-block grace period, plus a listening test. `synth_PolySynth.cpp:35-41` is a precedent.
- **Granular per-sample integer divide** (`play_memory_granular.cpp`): bit-exact only with careful remainder stepping.
- **Dropping `volatile` from `dspinst.h` asm:** it touches every DSP loop.
- ~~**Running step3 in the same pass as step2**~~: moot, the display rewrite removed the step machine.
- **Removing the 3 µs coprocessor delays:** the reason for them is unknown; needs a hardware soak.
- **Tracker grid `VERTEX_TRANSLATE_X`:** relies on an FT81x behaviour I haven't confirmed.
- **Several file chunks per main-loop pass during load:** possibly the biggest load-time win, but it changes UI responsiveness during load. Measure M3 first.

## Reviewed and left alone

So nobody re-reviews these:
- `getFxMin`/`getFxMax` and the per-step `memcpy` in the sequencer.
- The external-clock inner loop (integer only).
- `getTimerPeriod_us()` double literals. Changing them would shift tempo by about 1e-7, and drift was just fixed.
- `mtRandomNameGenerator` tables (already `const` in flash).
- ThemeManager parsing; `wavHeaderReader`; `VoiceStreamManager` refills (already aligned, 8 KB, by LBA).
- Reverb modulo and divides (compile to AND and multiply).
- `applyGain*` (already SIMD).
- Float math in draw code (SPI time dominates).
- `sprintf` in UI handlers (event rate, not frame rate).

## Behaviour notes found along the way, not performance

- **Roll retriggers ignore performance MIDI tune.** `tuneMidi` goes to `stepToSend.note` but `stepContRoll` is what gets sent (`mtSequencer.cpp:1132-1144`).
- **`abs()` on a float probably resolves to the int version** in `reverb.h:139,156`, and likely in `mtAudioEngine.cpp:287,292`. The 0.5 BPM threshold would then effectively be ≥ 1. Confirm with the ARM toolchain.
- **`list.cpp:336-338`:** the unsigned `size_t` comparison makes `textMaxWidth > 0` almost always true.

Found by the second review (2026-11-14), each its own small fix:
- **Stutter adds the MIDI tune again on every repeat** (`mtSequencer.cpp:1025-1031`): `stepToSend` is copied from `stepSent`, which already includes the tune.
- **A looped selection resets the absolute position to 0, not its first step** (`mtSequencer.cpp:1792` is overwritten at `:1797`).
- **`effect_bitDepth.cpp:35`** builds the mask from `val`, not the clamped `bitDepth`, so values outside 4–16 give a wrong mask.
- **Copy-paste mode check** in `mtAudioUpdateHandle.cpp:112-120`: the amp LFO rate tests `performanceMode` twice and never `sequencerMode`.
- **`setLP2` tests `loop1 > end`** where it means `loop2` (`play_memory.cpp:550`). Harmless today, because `loop2` is clamped below `end` just above it.
- **`play_memory_single_shot.cpp:279`:** reported as `iPitchCounter - castPitchControl` having the wrong sign in reverse playback. Not confirmed from the code alone; needs a host test before anyone changes it.
- **`NewList` headline font is never applied** (`newList.cpp:474-488`): the font pointer is passed by value.
- **Stereo waveform preview seeks one frame short per step** (`fileManagerSampleWaveform.cpp:143-150`).
- **Reverb LFO mask is `& 30`** (`Polyverb/fx_engine.h:407`), so the chorus LFO steps twice per 32 samples; the original engine likely used `& 31`. Fixing it changes the sound.
- **Filter `connect()`/`disconnect()` overwrite `lastEnabled`** on every call (`filter_variable.h:98-107`), so two calls in one block lose the click-suppressing crossfade.
- **Gain setters set `previous = multiplier`** (`AudioAmplifier`, `AudioMixer`, `MonoToStereo`), so two calls between updates make the ramp jump.
- **Streaming fade-in may skip the right channel** (`VoiceStreamManager.cpp` around `:1063`): reported before the merge; the code moved, so it needs re-tracing before anyone acts on it.

---

## Suggested order

```text
Phase 0  M1 M2 M3 M5 ─┬─ M4 ──────────────► A1 A2 A3 A4 A5
                      │
Phase 1  C4 C5 C6 C7 C8 C9 C10 C11 C12 C13 C14  (independent; C1–C3 done upstream)
                      │
Phase 2  S1 ─ F1 F2 F3 F4 F5 F6 F8 F9 F10 F11 ─ A6a → A6b → A6c
         A9 A10 A11 A12 ─ A13 (needs its golden tests first)
         D3a D3b D3c D5 D6 D7  (D1 done upstream, D2 moot)
                      │
Phase 3  D4a → D4b,  S2,  F7,  A7 / A8 / S3 (gated on M5)
```

Phase 1 comes first because those are bugs, and C8/C9 can produce interrupt overruns that would mess up Phase 2's measurements.
