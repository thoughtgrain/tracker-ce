# Backstage bug fix plan (Tracker, Tracker+, Tracker Mini)

This is my working plan for the bugs reported on Polyend Backstage under [Bug + `tracker`](https://backstage.polyend.com/tags/c/bug/19/all/tracker/3). It's a triage, not a set of fixes: nothing in the firmware has been changed. Every fix below gets a human review of its entry here *before* I write code, and every fix ships as its own PR.

Snapshot: forum read on 2026-11-23, source at `b38a325` (fork `main` after merging upstream `c1713de`).

## How I got here

1. I pulled all 197 topics in the category. 111 are already closed out by Polyend as by-design, duplicate, cannot-reproduce, RMA, declined or resolved. That leaves **86** that are open or carry the `logged` tag (Polyend's "confirmed, on our list").
2. I read all 86 threads in full, including staff replies, because the useful detail is usually in reply 7, not the opening post.
3. For the 73 that could be firmware, I traced the feature through the current source to answer three things: is the bug still there, where does it live, and what's the most likely cause. The reports are one to three years old and were filed against closed firmware, so "already fixed" is a real outcome.
4. I spot-checked seven of the cited code locations by hand and all seven matched.

What this plan is **not**: nothing here was built, flashed or measured. Every status is from reading code. Where a cause is inferred instead of read, I say so. A reference like `mtSequencer.cpp:917` is where to start reading, and I'll re-grep it before touching anything because code moves.

## How each fix will go

Per `AGENTS.md`, one concern per PR, and each PR description states its fence, blast radius, invariant and verification rung. My loop for each bug:

1. I post the scoped proposal (the entry here, expanded with the grep'd blast radius).
2. You review and pick a behaviour where there's a choice.
3. I write the fix plus its test, then open the PR.

Three things apply to almost everything below:

- **Most sequencer code runs in the audio interrupt.** `sequencer.tick()` and every MIDI handler are called from `TrackerImpl::updateAudioBuffers`. So no SD, no heap, no UI calls and no `double` in those fixes.
- **Nothing here needs a file-format change.** No fix reorders an enum or resizes a struct. Where a fix changes how an existing project *sounds*, I flag it as a behaviour change, which means a listening test.
- **I can't run hardware.** Rungs 4 to 6 of the verification ladder need someone with the device. Each entry says which device.

Device shorthand: **OG** = Original Tracker (MK66), **Plus** = Tracker+, **Mini** = Tracker Mini (both IMX).

## Summary

| Bucket | Count | What it means |
| :--- | ---: | :--- |
| A. Ready to fix | 22 | Cause read from the code, small fix, no real product decision |
| B. Needs your decision first | 32 | Cause known, but there are two reasonable behaviours |
| C. Needs hardware before any code | 9 | Couldn't pin the cause from source |
| D. Probably already fixed | 13 | Current source doesn't show the mechanism; needs a quick device check |
| E. Not planned | 13 | Hardware, docs, already fixed by Polyend, or not enough to go on |

The buckets add up to 89 because three reports are split across two buckets each: 11218 (A and B), 5504 (B and C) and 15503 (A and D).

---

## Step 0: a sequencer host test harness

About twenty of these fixes sit in `core/source/mtSequencer.cpp`, and today nothing tests the sequencer off-device. `tests/` only has the two ptfx suites. Without a harness, every one of those fixes can only be verified by ear on hardware.

So the first PR I'd propose isn't a bug fix. It's a host test under `tests/sequencer/` (outside the compiled `core/` directories, per the `host-test` recipe) that drives `Sequencer` with synthetic ticks and records note-on, note-off and FX events. It changes no firmware code. Once it exists, most of bucket A can prove its invariant at rung 2 with a before/after event log.

**I'd like your call on this:** harness first, or start with the fixes that don't need it (the UI ones)?

---

## A. Ready to fix

Ordered roughly by value against risk. "Check" is the cheapest rung that proves the fix.

### Sequencer and pattern FX

| ID | Report | Cause | Fix | Size | Check |
| :--- | :--- | :--- | :--- | :--- | :--- |
| [16101](https://backstage.polyend.com/t/16101) | Random Note FX lands octaves outside its range with scale lock | `mtSequencer.cpp:917`: the retry loop that looks for an in-scale note re-centres on each failed candidate, so it random-walks away from the original note | Keep the original note as the fixed centre. If nothing in range is in scale, play the original note | S | Host test. All devices |
| [18894](https://backstage.polyend.com/t/18894) | Perform step repeater adds the Tune offset again on every repeat (+3, +6, +9...) | `mtSequencer.cpp:1056`: the repeat copies the already-transposed note back and transposes it again | Drop the second transpose. Also fixes MIDI tracks, which share the path | S | Host test or MIDI monitor. All devices |
| [15539](https://backstage.polyend.com/t/15539) | A micro-moved step ignores its Gate FX | `mtSequencer.cpp:395`: gate length is a local that's only set on the step's first tick. A micro-moved note starts on a later tick, by which time it's back to "infinite" | Read the gate from the buffered step when the note actually starts | S | Host test. All devices |
| [24081](https://backstage.polyend.com/t/24081) | Chance FX stops the other lane's FX working on a step with no note | `mtSequencer.cpp:440` and `:732`: Chance marks the step "FX handled", which disables the only path that sends an FX without a note | Don't let Chance set that flag, and skip the Chance lane in the send. Narrow fix only (see side findings for the wider case) | S | Host test. All devices |
| [2411](https://backstage.polyend.com/t/2411) | Continue Song does nothing until the pattern has played once after visiting Perform | Leaving Perform arms a hidden "reload this pattern" cue (`fPerformance.cpp:279`). That cue wins over the song and switches song mode off | Cancel the hidden cue when Continue Song is pressed (`fSongEdit.cpp:308`) | S | Hardware. All devices |
| [11218](https://backstage.polyend.com/t/11218) (part A) | In Perform, tracks of different lengths collapse to one length after visiting Master | `fPerformance.cpp:157`: re-entering the Perform screen always re-initialises every track's length | Only initialise when performance mode isn't already on | S | Hardware. All devices |
| [15503](https://backstage.polyend.com/t/15503) (leftover) | Song + Perform plays short patterns twice | The reported song path looks fixed (bucket D). The same length refresh is missing when a *cued* pattern switch fires in Perform (`mtSequencer.cpp:2056`) | Copy the refresh into the cue branch | S | Hardware. All devices |

### Pattern editor and UI

| ID | Report | Cause | Fix | Size | Check |
| :--- | :--- | :--- | :--- | :--- | :--- |
| [23412](https://backstage.polyend.com/t/23412) | Pasting only Note, FX1 or FX2 wipes the other columns | `mtSequencer_patternEdit.cpp:1548`: `copyStep` clears the whole destination step before pasting one element. Shared code, so OG has it too in this source | Clear only the elements being pasted | S | Host test on `copyStep`. All devices |
| [13077](https://backstage.polyend.com/t/13077) | Shift+Insert on the Song page moves 5 slots, not to the top | `fSongEdit.cpp:1696` asks for -300 slots, then `:934` clamps that to -5 | Add a real jump-to-first that skips the clamp | S | Hardware smoke. All devices |
| [17369](https://backstage.polyend.com/t/17369) | Shift stays "held" after cancelling the overwrite prompt in Sample Loader | The prompt blocks every button but two, so the Shift *release* is thrown away (`fSampleImport.cpp:1208`, `modulesBase.cpp:234`) | Re-read the physical Shift key when the prompt closes | S | Hardware. All devices |
| [13076](https://backstage.polyend.com/t/13076) | Big +/- on Cutoff goes 100, 90, 79; Resonance goes 0, 9 | The value is a float and the label truncates it (`gInstrEdit.cpp:1658`), so 79.99 shows as 79. The stored value is fine | Round the label to nearest | S | Host check of the label math. All devices |
| [18904](https://backstage.polyend.com/t/18904) | Keyboard guide lights vanish while naming a pattern during playback | The pattern editor clears and redraws all pad lights on every playhead move, without checking the keyboard is open (`fPatternEdit.cpp:301`) | Skip the pad redraw while the on-screen keyboard is up | S | Hardware. OG, Plus |
| [15635](https://backstage.polyend.com/t/15635) | Mini: holding Menu shows nothing, but the menu still responds | The frame runs out of its 8,172-byte display budget and the menu is the control that gets dropped. The code that blanks pattern cells to make room uses an absolute track number, so it blanks nothing once the view is scrolled (`fPatternEdit.cpp:395`) | Make the blanked range relative to the first visible track | S | `displayList::overflows` on a Mini |
| [12506](https://backstage.polyend.com/t/12506) | Wavetable/granular preview sticks if you change play mode while holding Preview | `fSamplePlay.cpp:767`: the handler returns early in slice modes before it ever looks at the button release | Handle the release first | S | Hardware. All devices |

### Synth, samples, export

| ID | Report | Cause | Fix | Size | Check |
| :--- | :--- | :--- | :--- | :--- | :--- |
| [21153](https://backstage.polyend.com/t/21153) | Raising polyphony loses everything mapped to the Velocity macro | `InstrumentImpl.cpp:84`: when voices are added, parameters are re-sent without the per-voice (velocity, pressure) part. It isn't re-sent later because pad velocity is constant | Re-send per-voice values when voices are recreated | S-M | Host test feasible. Plus, Mini |
| [23751](https://backstage.polyend.com/t/23751) + [17139](https://backstage.polyend.com/t/17139) | PERC: a macro step FX lands one hit late; changing Perc Type on a note step silences that note | One cause, one PR. The step's note-on reaches the synth *before* the macro change, which is only sent later in the same audio block (`mtSequencer.cpp:507`, `mtAudioEngine.cpp:1255`) | Flush that part's pending macro values before its note-on | S | Hardware listening. Plus, Mini. Behaviour change: macros on a note step now shape that note |
| [12948](https://backstage.polyend.com/t/12948) | Synth tracks render dry, with no reverb or delay | Export mutes every send first (`mtAudioExport.cpp:87`). Sample tracks get theirs back on the next note; synth sends are never restored | Refresh the send levels right after the mute | S | Hardware listening. Plus, Mini |
| [12797](https://backstage.polyend.com/t/12797) | Sample plays at different pitches in Loader, Editor and Playback | A Low Quality import marks the slot "-12 semitones". Importing a normal sample over that slot never clears the mark (`SampleImport.cpp:533`) | Reset it on every WAV import | S | Host test or listening. All devices. Explains one octave; the report says two (see open items) |
| [10248](https://backstage.polyend.com/t/10248) | Importing a .mod with symbols in instrument names loses those instruments on save | The raw name becomes a filename (`fileManagerImportMod.cpp:256`). Characters FAT doesn't allow mean the file is never written | Replace illegal characters at import. Also fixes an unterminated string copy in the same lines | S | Host test of the sanitiser, then import/save/reopen. All devices |
| [4892](https://backstage.polyend.com/t/4892) | Slice markers don't line up with the waveform when zoomed out | `graphicProcessing.cpp:454`: the waveform uses whole samples per pixel and drops the remainder, the markers don't. On a very short sample that's about 55 px | Use a fractional samples-per-pixel step | S-M | Host test (pure function). Visual change on three screens |
| [16951](https://backstage.polyend.com/t/16951) | Exporting to a full SD card writes 0-byte files and reports nothing | Short writes are never compared against what was asked for, the stems path ignores every result, and an error ends up reported as a normal finish (`mtAudioExport.cpp:357`, `:496`) | Treat short writes as errors and show a "card full" message | M | Hardware with a nearly full card. All devices |

One caveat on the PERC pair: PERC and the other factory engines ship as binaries (`synth/prebuilt/*.psyn`), so I can only fix the host's ordering. If PERC still misbehaves after that, the rest is inside the plugin and out of reach here.

---

## B. Needs your decision first

The cause is known for each of these. What's missing is which behaviour you want.

### External clock timing (one cause, two reports)

| ID | Report |
| :--- | :--- |
| [6149](https://backstage.polyend.com/t/6149) | Notes with Gate below 18 are silent under external clock |
| [11736](https://backstage.polyend.com/t/11736) | MIDI Offset is coarse (14 to 15 is a big jump) under external sync |

With external clock, each incoming MIDI clock pulse runs 8 micro-steps back to back inside one audio interrupt (`mtSequencer.cpp:111`). Anything finer than one pulse collapses to the same instant. Gate 17 is 7 micro-steps, so the note starts and stops before any audio is rendered. Offset 14 and 15 sit either side of the same boundary.

- **Option 1 (small):** stop a note-off landing in the burst that started the note. Fixes 6149 only.
- **Option 2 (proper):** spread each pulse's micro-steps across the measured pulse interval. Fixes both, and tightens micro-move, rolls and swing under external sync. It also changes playback timing for every externally clocked project, so it needs a discussion and a logic-analyser measurement.

My recommendation is option 1 now and option 2 as its own discussed project.

### LFO phase (one cause, two reports)

| ID | Report |
| :--- | :--- |
| [24338](https://backstage.polyend.com/t/24338) | LFOs go off after a few pattern loops |
| [17021](https://backstage.polyend.com/t/17021) | A 128-step saw LFO restarts at step 65 |

Synced LFO phase is computed from a tick counter that never resets when the pattern loops, plus the note's step position added on top (`mtEnvelopeGenerator.cpp:566`, `mtAudioSequencer.cpp:2269`). A note on step *s* starts at phase 2*s*, which is why a 128-step ramp wraps at step 65. Separately, three LFO rates round down by one tick and drift.

- The rounding fix is safe by itself and I'd do it first.
- For the phase, pick one: **retrigger at note-on** (phase 0 at each note) or **free-run locked to the pattern** (same phase at the same step, every loop).
- The Amp LFO is a different mechanism (a looping envelope restarted per note). Should it follow the same rule?

This changes how existing projects with synced LFOs sound.

### Song, perform and pattern cue

| ID | Report | Choice |
| :--- | :--- | :--- |
| [23769](https://backstage.polyend.com/t/23769) | A cued pattern is lost after visiting Perform, but still blinks red | (a) keep the cue alive across screens, or (b) keep cancelling it but stop the misleading red blink. (b) is smaller |
| [11218](https://backstage.polyend.com/t/11218) (part B) | Swapping a track in Perform changes the pattern's length, and the mixed pattern can stay in memory | (a) never copy the length into the live pattern, or (b) reload on leaving Perform only if a swap happened. Must not undo the recent fix that stopped reloading from SD and losing unsaved edits |
| [8277](https://backstage.polyend.com/t/8277) | Pattern + arrow can't leave a 1 or 2 step pattern | The cue refuses to arm on a pattern's last step, and every step of a 1 or 2 step pattern counts as last. Relaxing that leaves one step to load the next pattern from SD. If it misses, playback currently *stops*. Loop one more pass instead? |
| [13149](https://backstage.polyend.com/t/13149) | Deleting the playing song slot makes the playhead jump | Proposed: finish the current pattern, then play the slot that followed. What should deleting slot 0 or the last slot do? |
| [13078](https://backstage.polyend.com/t/13078) | Saving during song playback disrupts it | In current source it now *stops* at the next pattern boundary, because the pattern load is dropped while saving. (a) loop the current pattern until the save finishes, (b) load patterns between save steps, or (c) stop playback explicitly on Save |
| [20510](https://backstage.polyend.com/t/20510) | MIDI START resumes the last song slot instead of restarting | Start from slot 0 (the MIDI spec) or from the selected slot (matches the Play button)? Medium to large: the handler runs in the interrupt and can't touch the SD card, so the target pattern has to be preloaded |
| [4531](https://backstage.polyend.com/t/4531) | A Roll is cut when the song moves to the next pattern | Every pattern switch cancels rolls, arps and Tempo FX. Carry over rolls only, or arps and tempo as well? |

8277, 13078 and 20510 all turn on the same question: **when the next pattern isn't loaded in time, loop the current one or stop?** One answer settles all three.

### Step FX semantics

| ID | Report | Choice |
| :--- | :--- | :--- |
| [24080](https://backstage.polyend.com/t/24080) | OFF! does nothing after Slide Up/Down | A slide is a one-step pitch ramp and has no "end" handler. Should OFF (a) freeze the pitch where it is, or (b) return to the note's original pitch? Also: OFF only ends FX in its own lane. Keep that? |
| [17928](https://backstage.polyend.com/t/17928) | A synth macro FX is ignored when it moves from FX1 to FX2 in the next pattern | Each lane stores its own value and FX1 always wins. Should the newest write win, or is fixed priority the documented behaviour? Plus, Mini |
| [23360](https://backstage.polyend.com/t/23360) | Micro-tune bends synths twice as far as samples (M50 = 100 cents) | `mtSequencer.cpp:657` maps to the full bend range, which is 2 semitones on synths. Halving it is one line, but **existing projects using M on synth tracks will bend half as far**. Accept that, or keep the old range behind a setting? |
| [23896](https://backstage.polyend.com/t/23896) | Pasting a sampler track onto a MIDI track pastes FX but not notes | This is deliberate code: notes with a sample instrument are dropped on MIDI tracks. Remap them to the last used MIDI instrument, or to a fixed channel? |

### Transport, metronome and effects

| ID | Report | Choice |
| :--- | :--- | :--- |
| [5906](https://backstage.polyend.com/t/5906) | Stopping the external clock makes a synced delay jump to a very long time | After 1 s without clock the tempo becomes 0 and the delay clamps to its 3.5 s maximum. (a) hold the last known tempo (smallest), or (b) fall back to the project tempo |
| [17394](https://backstage.polyend.com/t/17394) | Pressing Play during count-in makes the metronome race | Two click generators run at once, and the pattern restarts when the count-in ends. Should Play skip the count-in and record immediately (one-line fix), or be ignored until it ends? |
| [5516](https://backstage.polyend.com/t/5516) | Recording starts on the second count-in click | The code is doing what it was written to do: count-in is one bar of the metronome time signature, and the reporter had 1/4. (a) reword the setting, or (b) enforce a minimum count-in |
| [15438](https://backstage.polyend.com/t/15438) | Stop no longer clears reverb and delay tails (it did in 1.6) | Nothing clears them on transport any more. Clear on every Stop, on Play, or only on a quick double press like 1.6? |
| [16533](https://backstage.polyend.com/t/16533) | Reverb on the audio input switches on and off with level | The input's sends run through a noise gate hard-coded at -32 dBFS (`effect_dynamics.h:39`). Lower it, remove it, or add a setting? |
| [11917](https://backstage.polyend.com/t/11917) | Kick transients are softened | With the Amp envelope on (the default), every note gets 8 muted samples then an 8-sample ramp, even at attack 0 (`effect_envelope.cpp:81`). Make attack 0 truly instant, or keep a shorter ramp? Changes the start of every enveloped note |

### Render and export

| ID | Report | Choice |
| :--- | :--- | :--- |
| [11877](https://backstage.polyend.com/t/11877) + [4752](https://backstage.polyend.com/t/4752) | Render Selection during playback renders from the playhead; the naming screen glitches | Refusing to render while playing (as Export to MOD already does) fixes both in one PR. Stopping playback automatically fixes only the first |
| [20803](https://backstage.polyend.com/t/20803) | Song export adds up to a minute of near-silence | The tail runs until the output drops below about -78 dBFS, or 60 s. A slow reverb never gets there. Raise the threshold, lower the cap, or trim? |
| [5504](https://backstage.polyend.com/t/5504) (tail) | Rendered selection clicks at the end | Selection render stops dead on the last step with no tail, cutting anything still sounding. Keep the exact length (loopable) with a short fade, or let tails ring? |

### Smaller UI choices

| ID | Report | Choice |
| :--- | :--- | :--- |
| [11539](https://backstage.polyend.com/t/11539) | Tracker+: pad lit for an instrument is off by 3 between two views | The Instrument popup lists the 3 synths first. Shift the step view to match, or move the synths in the popup (changes muscle memory)? |
| [2455](https://backstage.polyend.com/t/2455) | C#10 shows as C#1 | The note cell draws 3 characters. A 4th touches the instrument column. Tighten spacing or use a compact form? |
| [9641](https://backstage.polyend.com/t/9641) | Timestretch Length bar is blank for samples over 8 s | Default to 8 s for long samples, or raise the 8 s limit? |
| [5079](https://backstage.polyend.com/t/5079) | Recorder's auto start point lands inside the sound | The detector lags more than it backs off, and its real threshold is nearer -48 dB than the intended -70. Fixing the maths makes noisy inputs trigger at sample 0, so the threshold and pre-roll need picking |
| [18876](https://backstage.polyend.com/t/18876) | Mini: beat slices start at C2 but the note keys start at C3 | Reachable today by setting Root note to C2. Is that enough, or should slice instruments get their own key layout? |
| [13161](https://backstage.polyend.com/t/13161) | Mini: red selection outline is hard to see outdoors | A contrast request more than a bug. The colour (`Themes.cpp:4`, `0xcd1818`) and the 1.5 px line width are shared by all three devices, and custom themes can already override them. Brighten the default, thicken the line, or ship a high-contrast theme? Per `AGENTS.md` this one wants a Discussion first |
| [13214](https://backstage.polyend.com/t/13214) | Reverb/delay sends do nothing on some recorded instruments | My best reading (inferred, I don't have the files): the send bus is mono, and a recording with left and right out of phase cancels to silence there. First step is to inspect the reporter's `.pti`. If that holds: document it, or change how stereo voices feed the sends? |

---

## C. Needs hardware before any code

I couldn't pin these from source. Each needs a measurement before a fix can be scoped.

| ID | Report | What I found | What's needed |
| :--- | :--- | :--- | :--- |
| [17703](https://backstage.polyend.com/t/17703) | Tracker+ sends 0xFF on MIDI out at cold power-up | No code sends it. It looks like a glitch on the TX line between the pin being assigned at boot and the UART being set up (`MidiUart.cpp:52`). Could also be the bootloader, which isn't in this repo | Logic analyser on TRS out during a cold boot |
| [23941](https://backstage.polyend.com/t/23941) | Mini as master below 108 BPM: Play+ starts 8 steps late | The only 108 BPM threshold I found on the Mini side doesn't change what's sent on the wire. The symptom points at the Play+, whose source isn't here | Capture Mini MIDI out at 107 and 108 BPM |
| [17540](https://backstage.polyend.com/t/17540) | Delay stops with PERC in all three synth slots | Nothing disables the delay by engine. Suspect: the audio block pool running out, since PERC uses 4 blocks per part. Idle parts are now skipped, so the reported case may be gone | Retest; read `AudioMemoryUsageMax()` against 80 |
| [14300](https://backstage.polyend.com/t/14300) | More than 6 granular voices lag or crash the OG | No single cause. Recent bounds fixes may cover the crash half | Profile the reporter's project on an OG |
| [14355](https://backstage.polyend.com/t/14355) | OG mixer meters update slowly since 1.9 | Meters update every 10 main-loop passes, so a slower main loop means slower meters. Can't show the regression without the 1.8 source | Measure main-loop period and meter rate on an OG |
| [20826](https://backstage.polyend.com/t/20826) | Freeze when hammering Preview on the import screen | Two real defects in the path: the audio interrupt closes the SD file while the main loop may be reading it (`play_sd_wav.cpp:252`), and a buffer index race in `SdStream.cpp`. Neither is proven to be this hang | Retest on a current build with the reporter's file |
| [23822](https://backstage.polyend.com/t/23822) | RDS station name missing in FM radio | The chain looks intact. Two smaller defects found (name not republished after retuning to the same station; label position on OG) | On an OG: does "Seeking..." appear in that label? |
| [5504](https://backstage.polyend.com/t/5504) (spike) | A full-scale spike at the end of a render, even of an empty pattern | Not explained by the tail cut in bucket B | Render an empty selection, inspect the last 128 samples of the WAV on a computer |
| [5312](https://backstage.polyend.com/t/5312) | Rendered samples can't be normalized | Probably a consequence of 5504: one full-scale sample makes Normalize a no-op | Wait for 5504 |

---

## D. Probably already fixed in this source

The current code doesn't show the reported mechanism. Each needs a short device check, after which the forum thread could be answered.

| ID | Report | Why I think it's fixed | Device check |
| :--- | :--- | :--- | :--- |
| [7277](https://backstage.polyend.com/t/7277) | Step repeater sends no MIDI note-off | Each repeat now stops the open note first (`mtSequencer.cpp:1065`) | MIDI monitor, repeater on a MIDI track |
| [9829](https://backstage.polyend.com/t/9829) | Cursor stuck after entering a note with external clock | Note preview now runs on the internal clock | Report's steps with Clock In = USB |
| [17459](https://backstage.polyend.com/t/17459) | Metronome Config shortcut opens USB config | Now jumps to the Metronome group's real position | Press the shortcut from Sample Recorder |
| [15503](https://backstage.polyend.com/t/15503) | Song + Perform plays short patterns twice | Track lengths are refreshed on each song pattern switch | Reporter's project (leftover gap is in bucket A) |
| [4441](https://backstage.polyend.com/t/4441) | Render & Load loads mono | Stereo import is always on | Hard-panned tracks on Plus or Mini |
| [6122](https://backstage.polyend.com/t/6122) | Clicks when a filter FX changes with filter type Disabled | Consecutive filter FX no longer reset the filter | Listen with the report's steps |
| [14783](https://backstage.polyend.com/t/14783) | Granular Position/Length over MIDI CC freezes the Mini | The playhead is now folded back into the loop | Sweep CC16/CC17 fast |
| [3148](https://backstage.polyend.com/t/3148) | Editor preview pitched up after Low Quality import | Preview now applies the resample offset | LQ import, apply an effect, Preview |
| [10080](https://backstage.polyend.com/t/10080) | Freeze while scanning radio frequencies | The unbounded wait loops in the tuner driver now have timeouts. Polyend is actively testing this one in the thread | Scan the full band, EU and US regions |
| [13160](https://backstage.polyend.com/t/13160) | Mini: synth editor icons invisible in Monochrome | Icon and background colours no longer match in any theme | Look at the patch editor in Monochrome |
| [15558](https://backstage.polyend.com/t/15558) | Mini note keys wrap to the wrong octave | Fixed for the default Notes layout; other layouts still wrap | Shift the key row past a row boundary |
| [15592](https://backstage.polyend.com/t/15592) | Mini: C2 key plays C5 on beat-slice instruments | That branch is compiled out on Mini | Root note C2, record the first key |
| [12523](https://backstage.polyend.com/t/12523) | Mini has no scale setting | "Notes scale" is in Project Settings with no Mini guard | Open Config, Project Settings |

---

## E. Not planned

| ID | Report | Why |
| :--- | :--- | :--- |
| [7025](https://backstage.polyend.com/t/7025) | Mini / Play+ not recognised over USB on AMD machines | Works through a hub, fails direct. Electrical or USB PHY behaviour; not something I can reason about from source |
| [20880](https://backstage.polyend.com/t/20880), [7331](https://backstage.polyend.com/t/7331), [13321](https://backstage.polyend.com/t/13321) | Mini mic whine, mic crackle at high brightness, sweep noise on line-in | Analog interference inside the unit |
| [13923](https://backstage.polyend.com/t/13923) | Manual lists the wrong USB audio tracks | The manual isn't in this repo |
| [23709](https://backstage.polyend.com/t/23709), [12509](https://backstage.polyend.com/t/12509), [13542](https://backstage.polyend.com/t/13542), [16987](https://backstage.polyend.com/t/16987), [19849](https://backstage.polyend.com/t/19849) | Anniversary theme note display, chord velocity, effect preview overwriting instrument 1, OFF not pasted to MIDI tracks, reverb dead after a game | Polyend closed each as fixed in a release. I didn't re-verify them |
| [23833](https://backstage.polyend.com/t/23833) | Tracker+ freezes on MIDI sync stop | Staff couldn't reproduce and the reporter no longer has the setup |
| [21288](https://backstage.polyend.com/t/21288), [9882](https://backstage.polyend.com/t/9882) | Samples missing after reload; SD card corrupted after 1.8 | Data loss with no repro steps. In both threads a reformat cured it. Serious, but there's nothing concrete to trace yet. Worth its own investigation into the FatFs write paths if you want it prioritised |

---

## Order and overlap

One bug per PR is the rule, but many of these touch the same file, so order matters. Before each PR I'll also check the open PRs upstream for overlap, which I haven't been able to do from this session.

- **`core/source/mtSequencer.cpp`** carries about twenty of these. I'd land them one at a time, rebasing each on the last. Within it:
  - `play_microStep`: 16101, 18894, 15539, 24081, then 23360 and the PERC macro pair.
  - Pattern cue code: 2411, then 8277 and 23769 once decided.
  - Count-in: 17394 before 5516.
  - External clock: 5906 before any of the 6149 / 11736 timing work, because the bigger timing change rewrites the code 5906 touches.
- **`core/Exporter/mtAudioExport.cpp`**: 12948, 16951, 20803 and the 5504 tail. The last two edit the same block and should be designed together.
- **`mtSequencer_patternEdit.cpp` `copyStep`**: 23412 first, then 23896.
- **`fPatternEdit.cpp`**: 18904 and 11539 share one function; 15635 is in another.
- **Radio driver**: exists twice (`platforms/mk66` and `platforms/imx`) and both copies must change together. Polyend is working in it right now, so I'd leave it alone until their fix lands.

## Suggested first batch

If you want a place to start, these five are small, independent of each other's decisions, and cover a spread of reports:

1. The sequencer host test harness (no firmware change).
2. **16101** Random Note range.
3. **23412** partial paste wiping columns.
4. **18894** step repeater re-applying Tune.
5. **13076** Cutoff / Resonance label rounding.

## Side findings

These came up while reading and aren't forum reports. I'm noting them, not fixing them; each would be its own change.

- **Interrupt-rule violations already in the tree:**
  - `handleMidiInStart` shows a popup from the interrupt (`mtSequencer.cpp:3850`).
  - `seqSynthFx` uses `std::map::operator[]`, which can allocate (`mtAudioEngine.cpp:1069`).
  - Granular MIDI CC handling calls `sprintf` and `display.*` from the interrupt (`fSamplePlay.cpp:3340`).
  - `envelopeGenerator::calc` uses `double`.
  - `AudioPlaySdWav::update` closes an SD file from the interrupt (see 20826).
- **Wider version of 24081:** the "FX handled" flag is shared across both lanes for every transport-type FX, so OFF, Tempo, Swing, Micro-move or MIDI CC in one lane also drops a sample FX in the other lane on a step with no note.
- **Possible FX1/FX2 swap in partial paste:** Shift+FX1 paste appears to act on the lane shown as FX2 (`fPatternEdit.cpp:3092` against `:830`). Needs a device to confirm.
- **Render & Load memory check** doubles a length that's already stereo, so it refuses at half the real limit (`fPatternEdit.cpp:1984`).
- **Timestretch (SOLA)** has a comparison result stored where an offset is meant, and an end-of-input copy that can read past the selection (`mtEffectTimestretch.cpp:205`). A plausible source of the "spike after timestretch" mentioned in 5504, and a good host-test candidate with AddressSanitizer.
- **Normalizer** divides by zero on a silent selection and applies separate gain per channel, which shifts stereo balance (`mtEffectNormalizer.cpp:12`).
- **Synced LFO** divides by zero when external tempo is unknown (`mtAudioEngine.cpp:2519`).
- **Resonance isn't applied** to a filter step FX when the instrument's own filter is disabled.
- **`play()` sets the playing flag before sending MIDI START**, so a clock byte could in principle go out ahead of START.
- **In the Instrument popup on Tracker+**, samples 46 to 48 can't be picked by pad because of the 3-slot synth offset.
- **Stale references in the fork's own docs** after the upstream merge: `ImageViewerJpegAddress` in `core/libs/NewDisplay/AGENTS.md` and `fxDescriptions` in `docs/recipes/step-fx.md`.

## Open items I couldn't close

- **12797:** the reporter measured two octaves; I can only explain one. Their 1.8 project would settle it.
- **11917:** the report says some hits lose the transient and others don't. The fade I found applies to every hit, so there's a second effect I haven't found.
- **24080:** OFF after *reverse* works in source for samples held in memory. Long streamed samples on Plus and Mini only take their direction at note-on, so that case needs a device.
