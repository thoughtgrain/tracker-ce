# Optimization Plan: Small, Fenced Chunks

> Status: **plan only**. Nothing here is implemented yet. Each chunk below is sized to be one small PR.

I ran a read-only review of four areas: audio rendering, display and UI, sequencer and MIDI, and file I/O and memory. Then I re-read the source behind every finding that made it into this plan. This doc turns the results into a work queue.

The goal isn't "make it faster" in one sweep. It's a series of narrow changes, each with:
- a clear fence around what it touches,
- a list of the code that depends on it,
- the behaviour it must not change,
- and a way to prove it didn't.

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
| **M2** | Display baseline: `lastMemoryUsed` per screen and the `droppedControlsCount` added in PR #1, plus a main-loop iteration counter in a DEBUG build | DEBUG-only lines in `Tracker.cpp` | Display chunks need frame cost and loop rate, not a feel |
| **M3** | File baseline: time project load, Save As, a 10 MB stereo export and a WAV import, on the same SD card | No code change | File chunks are judged on wall-clock time |
| **M4** | Host harness for audio units, like `tests/toro`: extract the limiter, state-variable filter and envelope multiply loops behind a tiny test so "bit-exact" is checked by a test rather than assumed | New `tests/audio/` only | Gates A1–A3 and A5 |
| **M5** | Generate `.map` files for both IMX targets and record ITCM/DTCM free space and where hot functions actually land | Build artifact, no code | Gates A8 and S3, and confirms the ITCM headroom the developer guide quotes |

---

## Phase 1: Correctness fixes found during the review

These come first because they're bugs, and some of them stall or corrupt things in ways that would also distort any performance measurement.

### C1. MIDI Start in Song mode touches the SD card inside the audio interrupt
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

### C2. `stop()` can load a pattern from SD inside the audio interrupt
- **What I verified:** at `mtSequencer.cpp:1567-1574`, when performance mode is set to turn off at pattern end, `stop()` calls `loadWorkspacePatternNow()` and `switchRamPatternsNow()`. `stop()` is called from interrupt context at `:1719`, `:1748`, `:2287`, and `:3637` (a MIDI Stop from the DAW).
- **Fence:** `mtSequencer.cpp`, `mtSequencer.h`.
- **Change:** set a pending flag and perform the load in `Sequencer::update()`.
- **Blast radius:** 22 `sequencer.stop()` call sites in the UI. These run in the main loop and must behave exactly as now. It's acceptable to perform the load immediately when not in interrupt context (detect the interrupt via the IPSR register).
- **Verify:** on hardware: in performance mode, leave the screen so it's armed to turn off at pattern end, then send MIDI Stop.
- **Risk:** medium.

### C3. `findSlotWithPattern` reads one byte past the playlist and can loop forever
- **What I verified:** `playlist` is `uint8_t[SONG_MAX]` with `SONG_MAX = 255` (`mtStructs.h:146, 971`). The loop index is `uint8_t` with no upper bound (`mtSequencer.cpp:3339-3373`), so it reads `playlist[255]`. With no 0 terminator and no match, the first loop never exits. This runs in the interrupt at every song-mode pattern switch.
- **Fence:** `mtSequencer.cpp` (this function only).
- **Change:** bound both loops by `SONG_MAX`.
- **Verify:** host-testable as a pure function: a full playlist, no terminator, pattern absent → returns -1.
- **Risk:** low.

### C4. NewList keeps redrawing forever after one scrolled item
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
- **Where:** `display.cpp:470` polls `isCoProEmpty()`, 2–3 SPI register reads, on every main loop *before* checking whether anything is queued.
- **Change:** return early when `updateStep == 0`, the queue is empty and `!stopAppend`.
- **Fence:** `cDisplay::update()`.
- **Blast radius:** the `cmdOffset` resync and error recovery after the poll now run only when work is pending, which is still before any coprocessor use. The `loadImage` block above line 470, `stream.cpp:68` and `processDisplayUpdateImmediate` keep their own polling.
- **Verify:** screens refresh after idle; the main-loop rate (M2) goes up.
- **Risk:** low.

**D2. Batch refreshes in Master screen value handlers**
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
- **Running step3 in the same pass as step2** (`display.cpp:617-618`): the original authors commented it out and the reason is unknown.
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

---

## Suggested order

```text
Phase 0  M1 M2 M3 M5 ─┬─ M4 ──────────────► A1 A2 A3 A4 A5
                      │
Phase 1  C1 C2 C3 C4 C5  (independent, can go in any order)
                      │
Phase 2  S1 ─ F1 F2 F3 F4 F5 F6 ─ A6a → A6b → A6c
         (after PR #1 merges) D1 → D2 → D3a D3b D3c
                      │
Phase 3  D4a → D4b,  S2,  F7,  A7 / A8 / S3 (gated on M5)
```

Phase 1 comes first because those are bugs, and C1/C2 can produce glitches that would mess up Phase 2's measurements.
