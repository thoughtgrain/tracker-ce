# Tape Projects: UI and UX

> Status: **design only, nothing built.** This is the second version. The first transplanted Bryo's interface onto the Tracker: a new Tape module, modulator slots on pads, lane banks and scenes. That gave a Tracker user a second instrument to learn. This version starts from the Tracker instead: **every idea goes into the screen that already does that job, and where a step FX already does a Bryo thing, I use the FX.** It's the companion to [`tape-mode-plan.md`](tape-mode-plan.md); where they differ on anything a person sees or touches, this file wins. Mockups: [`tape-ux-mockups.html`](tape-ux-mockups.html).

---

## 0. How I decided where things go

I asked one question for each Bryo idea: **where would a Tracker user look for this today?**

| A Tracker user wants to… | They go to… | So in a tape project… |
| :--- | :--- | :--- |
| Set where a sample starts, loops and slices | **Sample Playback** | the tape is a play mode there |
| Trim, normalise or process audio | **Sample Editor** | the tape's audio is edited there, unchanged |
| Shape an instrument's sound, set its sends | **Params** (and its **More** window) | Grain, Ripple and Echo are there, beside Overdrive and Bit depth |
| Move a parameter with an envelope or LFO | **Params** pressed again: **Automation** | the chain's values are new automation destinations |
| Change something on a step | an **FX column** in the pattern | tape tracks get more FX columns, and the tape FX are FX |
| Record audio | **Sample Recorder** | it can record onto a tape while the song plays |
| Turn a pattern into a sample | **Render Selection** | it can render onto a tape |
| Play it live | **Performance mode** | a few tape FX join the list |
| Arrange | **patterns and Song** | unchanged |

That leaves **two** new pieces of UI: the project-type choice, and paging through more FX columns on a tape track. Everything else is a new value, mode or list entry in a screen people already know. It also means the Mini needs almost nothing of its own, because every one of those screens already works on the Mini.

**What I took from Bryo is the sound and the workflow, not the controls.** That means a loop that keeps rolling and gets recorded over, a chain you push sound through, sends that feed it live, and drums that hit it. I didn't take hold-a-pad-and-turn, pages with pictures, scenes, or the four modulator slots, because the Tracker has its own answer to each (§10).

---

## 1. The model, in Tracker terms

- **A tape is an instrument** (Tape A, Tape B) with a new play mode, **Tape**. While the transport runs it keeps rolling in sync, whether or not a note is playing on it, so coming back to it lands where the music is.
- **A tape track** is the track the tape lives on. It's an ordinary track in every way you already know, one voice and the same columns, with two additions:
  - **more FX columns** (FX1–FX4 instead of FX1–FX2);
  - **its own chain**, Grain → Ripple → Echo, through which its sound and every send into it pass.
- **Its fader is the chain's output.** Muting Tape A mutes Tape A's chain, including what other tracks send into it. There's no separate return channel to learn.
- **Ordinary tracks** are exactly today's tracks. Their instruments have two new sends, **Tape A send** and **Tape B send**, beside Reverb send and Delay send. That's how drums, synths and samples flow into the effects.

| Type | Tracks | Tape tracks | Ordinary |
| :--- | :---: | :--- | :--- |
| Default | 16 | — | today's 8 sample + 8 MIDI |
| Tape 2 | 2 | track 1 | track 2 |
| Tape 4 | 4 | tracks 1–2 | tracks 3–4 |
| Tape 6 | 6 | tracks 1–2 | tracks 3–6 |

---

## 2. Making a tape project (new)

**File → New Project**, after today's stop and save prompts, opens a list of the four types, each with a one-line summary. It looks like the file and Config lists: no pictures, just the choice and what it means. Turn the encoder or use Up/Down to pick, then press **Create** (button 8); **Cancel** is button 7.

| Type | Summary in the list |
| :--- | :--- |
| Default | 16 tracks: 8 sample + 8 MIDI. No tape. Today's project. |
| Tape 2 | 2 tracks. Track 1 is a tape track; track 2 is ordinary. |
| Tape 4 | 4 tracks. Tracks 1–2 are tape tracks; 3–4 are ordinary. |
| Tape 6 | 6 tracks. Tracks 1–2 are tape tracks; 3–6 are ordinary. |

- **Default is highlighted when the list opens**, so the old path is one extra press.
- Create makes the tape instruments (blank, 8 bars at the project tempo) and writes **`C-4 TA`** on step 1 of pattern 1 for each tape. That note starts the tape, the same way a looping break starts in any Tracker project. It's an ordinary note you can see, move or delete.
- **On the Mini,** ±1 or Up/Down moves through the list and button 8 creates.

---

## 3. The tape: Sample Playback, play mode Tape

Choose Tape A as the instrument and press **SamplePlay**. The screen is the one you know: the waveform with the markers, zoom, and Play Mode on buttons 7–8. **Tape** appears in the Play Mode list, but only for the tape instruments; a tape can't be switched to another mode and stay a tape.

| | 1 | 2 | 3 | 4 | 5 | 6 | 7–8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Tape mode | Preview | Loop Start | Loop End | Speed | Zoom | More | Play Mode |
| More | Sync (off, 1–16 bars) | Wow | Flutter | Rotate | Xfade | Slices (16) | Back |

- **The waveform** shows the loop window, the slice markers and the play head, as Slice mode already draws them. Tape mode adds a **bar ruler** under it and, while recording, a red record head.
- **Notes jump slices.** On a tape step, the note picks the slice (C-4 = slice 1, chromatic up), as in Beat Slice. The difference is that the tape keeps rolling after the jump.
- **Instruments take turns on a track, as they always do.** Another instrument on a tape track plays instead of the tape; the tape keeps time underneath, and the next TA note picks it back up in place.
- **The Tracker's existing FX already do most of Bryo's tape moves.** They work on a tape exactly as their names say:

| Bryo | The Tracker FX |
| :--- | :--- |
| Jump to slice | **Slice** FX, or the note |
| Reverse | **Reverse** FX |
| Move the head | **Position** FX, **Position LFO** |
| Pitch | **Tune**, **Slide up / down**, **Finetune LFO** |
| Stutter (RPT) | **Roll**: on a tape, it repeats a short stretch at the head. I'll check how Roll behaves on a rolling tape before promising this. |
| Tape stop, half speed, varispeed | **Speed** (new FX, §5) |

---

## 4. The chain: Params → More

**Params** on Tape A is the instrument editor, unchanged: Volume, Panning, Tune, Finetune, Filter Type, Cutoff, Resonance, More. The **More** window, where Overdrive, Bit depth, Reverb send and Delay send already live, gains three buttons on a tape: **Grain**, **Ripple** and **Echo**. Each opens a sub-page of vertical bars with Back on button 8, the way Master's Reverb and Delay buttons open theirs.

| Sub-page | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Grain** | Wet | Size | Density | Pitch | Spray | Spread | Scan | More |
| Grain, More | Position | Warp | Feedback | Freeze | Route (insert, send) | | | Back |
| **Ripple** | Wet | Mode (strings, comb, reso) | Pitch | Decay | Tone | Chord | Pluck (own notes, + sends) | More |
| Ripple, More | Cutoff | Resonance | Drive | Spread | Route | | | Back |
| **Echo** | Level | Time | Sync | Feedback | Tone | Spread | Age | More |
| Echo, More | Duck | Diffuse | Verb send | | | | | Back |

- **Wet is on button 1 and selected when the sub-page opens**, so Grain, then a turn of the encoder, brings it in. That's the one Bryo habit worth keeping, and it costs nothing on the Tracker.
- **Colour isn't a separate block.** The Tracker already has Overdrive and Bit depth on every instrument, which is what Bryo's COLOR mostly did. Each source gets coloured by its own settings before it reaches the chain.
- **Pluck (own notes, + sends)** decides whether notes from tracks that send into this chain also pluck the strings. That's how a kick on track 3 strikes Tape A's strings, set in one place.

---

## 5. Lanes: more FX columns on tape tracks

A tape track has **four FX columns** where an ordinary track has two. They *are* FX columns: the same FX list, the same entry, the same Fill, Copy and Undo, with no new rules. That's the lanes you asked for, in the form a Tracker user already writes.

**Paging.** Pressing **Note** again already swaps a track's FX columns for Micro-move and Volume. On a tape track, pressing **Fx** again swaps the two visible FX columns between FX1·2 and FX3·4. The track's header shows which pair is up (`FX 3·4`). Nothing changes width, so the 4-track view, scrolling and the 8,172-byte frame budget behave as they do now.
- **Tape 2** has width to spare. Track 1 shows all four FX columns at once, and track 2 sits beside it.

**The tape strip.** Beside each tape track's columns, in the space to the right of FX2 (about 20 px, so no column moves), a thin waveform runs **top to bottom with the steps**. Each row shows the stretch of tape that plays on that step, so you can read the tape the way you read the notes:
- **It follows what's written.** A note that jumps to a slice draws a short orange line where the jump happens, and the strip shows the slice's audio from there. Speed, Reverse and Position FX bend it: a slowdown narrows the waveform as fewer seconds pass per step, a stop goes flat, and reverse runs the audio backwards.
- **The playhead row** is drawn brighter, like the rest of the playing row.
- **It's quiet when there's nothing to say.** A blank tape draws an empty strip, and Config's "Pattern info" Off hides it along with the track names.
- **Cost.** It's four short lines per row, about 60 lines per tape, drawn from a peak table I keep per tape (min/max per 1/64 of the loop), not from the audio. That's well under a kilobyte of display list for two tapes, but it comes out of the 8,172-byte frame budget, so I'll count it in phase 4 and fall back to two lines per row if it's tight.
- **Mini:** the same; it's part of the pattern editor.

**New FX types**, appended to the FX list. They're listed only on tape tracks except for the two sends, which work on any track:

| FX | Value | What it does |
| :--- | :--- | :--- |
| Speed | −200…+200 % | tape speed; 0 is a tape stop, negative plays backwards |
| Wow | 0–100 | wow and flutter depth |
| Grain Wet / Size / Position | 0–100 | |
| Grain Freeze | on / off | hold the grain buffer |
| Ripple Wet / Pitch / Decay | 0–100, semitones | |
| Echo Level / Feedback | 0–100 | |
| Tape A send / Tape B send | 0–100 | **any track**: this note's send into a chain, like Reverb send |

**Ramps and sweeps** use **Fill**, which already offers an FX Type with From and To. Fill Speed from 100 to −100 across steps 9–16 and the tape slows into reverse.

**How long an FX lasts** follows the Tracker's own rule: an FX applies from its step until the track's next note. On a tape track, notes are usually few (a slice jump now and then), so tape FX naturally hold across steps until you change them or the next note arrives. To end a stutter, write the FX again with 0, the way you'd end any FX today.

---

## 6. Modulation: Automation

Press **Params** again on Tape A for **Automation**, the existing page: Destination, Type, then that type's values and Amount.

- **New destinations** for tapes: Speed, Grain Wet, Grain Size, Grain Position, Grain Pitch, Ripple Wet, Ripple Pitch, Ripple Decay, Echo Time, Echo Feedback. They join Volume, Panning, Cutoff and the rest, so an LFO on grain size is chosen the same way as an LFO on cutoff.
- **The existing types all apply:** Envelope, LFO (including Random), WT Env and WT LFO, with tempo sync.
- **One new type, Follow:** an envelope follower. Its values are Source (a track, or the input), Rise, Fall and Amount. It's the one thing Bryo's modulators did that the Tracker can't: let the kick on track 3 pump Tape A's grains.
- Bryo's step-sequencer modulator isn't needed. FX columns already sequence any destination per step, and Random-shape LFOs cover the rest.

---

## 7. Recording onto a tape: Sample Recorder

**SampleRec** opens the Sample Recorder as it is now: Source, Level, Gain, Metronome, Record. In a tape project it gains:

- **Source** adds **Tracks** (resample the mix of the ordinary tracks), **Track 3…6**, and **Grain A / B** (freeze to tape: the grain buffer copied at once), next to Line, Mic, Radio and USB.
- **Target**: **New instrument** (today's behaviour), **Tape A** or **Tape B**.
- **Bars** (Free, 1, 2, 4, 8, 16) and **Dub** (−100…+100), shown only when the target is a tape.

**Recording onto a tape doesn't stop the song.** Press Record and the label blinks until the next bar line, the way a queued pattern blinks. It records for the set bars and stops itself, and the tape loops the take in time. Bar takes skip the trim step, because they're already the right length; **Undo** brings back the tape as it was. A **Free** take goes through the trim screen you know (Preview, Start, End, Zoom, Crop, Undo, Cancel, Save), and Save writes it onto the tape.

**Dub** works as it did in Bryo: 0 adds the new sound to the old (sound on sound), +100 keeps the old loop, −100 replaces it, and values between fade the old loop each pass.

**Bounce** is **Render Selection** (pattern editor, page 2), which already renders a selection to a sample. It gains a destination: New instrument, **Tape A** or **Tape B**. Select an ordinary track's steps, render to Tape A, and the part is on tape.

---

## 8. Drums: theseus as a play mode

- **Making one:** in the Sample Loader, with an empty slot selected, **New Drum** makes an instrument with play mode **Drum** and no sample, so it takes no sample memory.
- **SamplePlay** on a drum instrument shows parameter bars, like every Tracker parameter page, since there's no waveform. The title bar names the drum the last note played (`D-3 → Snare`), the way it already shows the current instrument. Its values:

| | 1 | 2 | 3 | 4 | 5 | 6 | 7–8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Drum mode | Preview | Decay | Punch | Cutoff | FM | More | Play Mode |
| More | Noise | LFO | Rate | Tune | | | Back |

- **Params** is the usual instrument editor (volume, pan, filter, envelopes), and **More** has its Tape A and Tape B sends. That's how drums flow into the effects.
- **The note picks the drum,** the 48 pads are the kit, and the volume column is velocity. It works on every track in every project type, on all three devices.

---

## 9. Synths, performance mode and song

- **Synths (Tracker+ and Mini):** the synth part settings already have Reverb and Delay sends. They gain **Tape A send** and **Tape B send**. There's no new routing concept: a synth reaches a tape the same way a sample does.
- **Performance mode** gains these FX: Speed (rows: 50 %, −100 %, 200 %, 0 %), Grain Freeze, Grain Wet, Ripple Wet, Echo Throw (feedback). The FX it already has (filter, volume and the rest) work on tape tracks as on any track.
- **Song mode** is unchanged. Patterns carry their FX columns, so the tape's moves arrange like any other part.

---

## 10. What changed from the first version, and why

| First version | Now | Why |
| :--- | :--- | :--- |
| A new **Tape** module with six pages and pictures | Sample Playback (the tape) and Params → More (the chain) | Those are the screens a Tracker user already opens for "how a sound plays" and "how it's shaped". |
| Lanes in four banks, with Step / Hold / Glide modes and a target popup | Four more **FX columns**, new FX types, and **Fill** for ramps | Lanes *are* FX columns in a tracker. Separate rules would be a second grid to learn. |
| Modulator slots M1–M4, hold a pad to set depth, Assign mode | **Automation**, with new destinations and a **Follow** type | The Tracker's model is destination-first; adding destinations fits it. |
| A deck that plays alongside the track's notes | One voice per track; the tape keeps time underneath | One voice per track is how every Tracker track works. |
| **Scenes** (4, glide, a Scene FX) | Removed | Patterns, FX on step 1, song mode and performance mode already do section changes on a tracker. Bryo needed scenes because it has no song. |
| A separate COLOR block | Each instrument's Overdrive and Bit depth | They already exist on every instrument. |
| Master → Tape Sends page, HIT toggle | Tape A / B send in each instrument's More, plus Ripple's Pluck setting | Sends are per instrument on the Tracker, beside Reverb and Delay. |
| Synth **Output** routing | Tape A / B sends on the synth part | Same reason. |
| Tape Recorder screen | Sample Recorder, with a tape Target | It's the screen people already record in. |
| Top-row pads for pages, freeze and stutter | Pads play notes, as everywhere else | Live moves belong in performance mode. |
| A Mini-specific assign flow | Nothing extra | The screens used already work on the Mini. |

---

## 11. Journeys

**J1. Tape 4, first sound**
1. **File → New Project → Tape 4 → Create.** Pattern 1 has `C-4 TA` on Tape A and `C-4 TB` on Tape B.
2. On track 3, write a beat with a drum instrument (§8). **Play.**
3. **SampleRec**: Source Tracks, Target Tape A, Bars 4. **Record.** It starts on the next bar, stops after 4, and Tape A loops it.
4. Cursor to Tape A. **Params → More → Grain**, then turn the encoder: Wet is already selected.

**J2. A tape break in the pattern**
1. On Tape A, press **Fx** twice for FX3·4.
2. Steps 9–16, FX3: **Fill**, FX Type Speed, From 100, To −100. The tape slows into reverse.
3. Step 13, FX4: **Roll** 1/16, and step 15: Roll 1/32. On step 17, write Speed 100 and Roll 0 to come out of it.

**J3. The kick plucks the strings**
1. On track 3's drum instrument, **Params → More → Tape A send** up.
2. Tape A: **Params → More → Ripple**, Wet up, Chord Min7, and **Pluck: + sends**.

**J4. The kick pumps the grains**
1. Tape A: **Params**, **Params** again (Automation). Destination **Grain Size**, Type **Follow**, Source **Track 3**, Amount −40.

**J5. A synth through Tape B (Tracker+ and Mini)**
1. Synth part B's settings: **Tape B send** up. Its notes stay on whichever track they're on.

**J6. Live**
1. **Perform**: put Speed, Grain Freeze and Echo Throw in three slots, then hold their pads over the song.

---

## 12. Questions for you

1. **One voice on a tape track.** Another instrument on a tape step plays *instead of* the tape, with the tape keeping time underneath, which is how every Tracker track works. Bryo plays a source and its tape at once. Is the Tracker way right here, given that the sends cover "play something into the chain while the tape rolls"?
2. **Scenes dropped.** Patterns and performance mode cover them on a tracker. Do you still want them?
3. **Colour folded into Overdrive and Bit depth.** That drops Bryo's noise and tilt. Acceptable, or should Color be a fourth chain sub-page?
4. **Tape FX only on tape tracks** (except the sends). Or should they be available, and do nothing, on any track?
5. **New Drum in the Sample Loader.** Or would you rather drums were a choice in Sample Playback's Play Mode list on an empty slot?
