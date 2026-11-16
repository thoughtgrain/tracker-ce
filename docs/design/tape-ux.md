# Tape Projects: UI and UX

> Status: **design only, nothing built.** This is the second version. The first transplanted Bryo's interface onto the Tracker: a new Tape module, modulator slots on pads, lane banks and scenes. That gave a Tracker user a second instrument to learn. This version starts from the Tracker instead: **every idea goes into the screen that already does that job, and where a step FX already does a Bryo thing, I use the FX.** It's the companion to [`tape-mode-plan.md`](tape-mode-plan.md); where they differ on anything a person sees or touches, this file wins. Mockups: [`tape-ux-mockups.html`](tape-ux-mockups.html).

---

## 0. How I decided where things go

I asked one question for each Bryo idea: **where would a Tracker user look for this today?**

| A Tracker user wants to… | They go to… | So in a tape project… |
| :--- | :--- | :--- |
| Set where a sample starts, loops and slices | **Sample Playback** | the tape is a play mode there |
| Trim, normalise or process audio | **Sample Editor** | the tape's audio is edited there, unchanged |
| Shape an instrument's sound, set its sends | **Params** (and its **More** window) | Tape A send and Tape B send sit beside Reverb send and Delay send |
| Set up a send effect, like Reverb or Delay | **Master** | each tape's Grain and Ripple are set up there |
| Move a parameter with an envelope or LFO | **Params** pressed again: **Automation** | the tape FX values are new automation destinations |
| Change something on a step | an **FX column** in the pattern | tape tracks get more FX columns, and the tape FX are FX |
| Record audio while the song plays | **the pattern editor**: arm with Shift + Rec, record with Rec + Play | an armed tape track records onto its tape |
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
  - **its own send effect**, Tape A FX or Tape B FX: Grain → Ripple in series. It works like Reverb and Delay: instruments send into it, and it's set up in **Master**.
- **The tape sends to its own FX** at 100 by default, so a fresh tape is heard dry plus through its effects, and you turn either down independently: the track fader for the dry tape, the Tape FX level in Global Mixer for the effects.
- **Every instrument**, on any track, has two new sends, **Tape A send** and **Tape B send**, beside Reverb send and Delay send. That's how drums, synths and samples flow into the effects.

| Type | Tracks | Tape tracks | Ordinary |
| :--- | :---: | :--- | :--- |
| Default | 16 | — | today's project |
| Tape 2 | 2 | track 1 | track 2 |
| Tape 4 | 4 | tracks 1–2 | tracks 3–4 |
| Tape 6 | 6 | tracks 1–2 | tracks 3–6 |

---

## 2. Making a tape project (new)

**File → New Project**, after today's stop and save prompts, opens a list of the four types, each with a one-line summary. It looks like the file and Config lists: no pictures, just the choice and what it means. Turn the encoder or use Up/Down to pick, then press **Create** (button 8); **Cancel** is button 7.

| Type | Summary in the list |
| :--- | :--- |
| Default | 16 tracks. No tape. Today's project. |
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
**From the pattern screen**, step by step:
1. **Starting it.** A `C-4 TA` note starts the tape. A new tape project already has one on step 1 of pattern 1. With Sync on, the tape's position follows the song position, so it carries on across pattern changes and pattern loops without restarting. A `C-4 TA` on step 1 of every pattern just lines it up again; it doesn't retrigger audibly.
2. **Jumping around.** Every other TA note is a slice jump: C-4 is slice 1 up to D#5 for slice 16, as in Beat Slice. With the cursor in the Note column and TA as the instrument, the pads audition the slices, and in edit mode they write them, the same as any instrument's notes.
3. **Stopping and starting.** `OFF` on a TA step stops the tape. The next TA note starts it again where the song position says it should be, so it comes back in time.
4. **Shaping it per step.** FX in FX1–FX4 act on the tape from their step: Speed and Wow (new), Roll, Reverse, Position, Tune and Slide (existing). Fill writes ramps, such as Speed sliding down to a stop.
5. **Playing something else on the track.** Another instrument on a tape step plays instead of the tape, and the tape keeps time underneath. The next TA note picks it back up in place.
6. **Seeing it.** The sample strip beside the track (§5) shows which part of the tape each step plays, where it jumps and where it slows.
7. **Going deeper.** With the cursor on a TA step, TA is the current instrument, so **SamplePlay** opens this screen for the tape, **Params** its instrument settings. Recording onto it is in the pattern editor itself (§7).

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

## 4. The effects: send effects, set up in Master

Grain and Ripple are **send effects**, one set per tape, the same kind of thing as the Tracker's Reverb and Delay. Instruments send into them (Params → More: Tape A send, Tape B send), and you set them up in **Master**, where Reverb and Delay are set up today.

It fits the Master screens the way Reverb and Delay already do. On the Tracker today, **Master 1/3** has Reverb and Delay as buttons that open their own pages (Master Reverb, Master Delay, with Back on 8), and their return levels sit in **Global Mixer 3/3** beside Dry Mix, Line in and the synths. The tape effects copy that exactly:

- **Master Tape** is a new page in the Master cycle, in tape projects only, so pressing Master goes 1/4 → 2/4 → 3/4 → 4/4. It holds six buttons, laid out like Master 1/3's Reverb and Delay: they open pages and have no bars of their own.

  | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
  | A Grain | A Ripple | | | B Grain | B Ripple | | |

  Above each button, the page shows that effect's main values as text (Wet, and two or three settings), and a line across the top of each half reads **Tape A send → Grain → Ripple → Tape A FX level**, so the order the sound goes through is on screen.
- **Each button opens its page** (Master Tape A Grain, and so on): bars, with **More** on 7 for the rest and **Back** on 8, as the Reverb and Delay pages have Back.
- **The return levels go in Global Mixer 3/4**, beside Delay and Reverb. On Tracker+ and Mini that mixer has one free column, so it's one **Tape FX** column that shows Tape A's level and steps to Tape B's when pressed again, the Tracker's rule for a selected list. On the Original there's room for both as two columns.

The pages:

| Sub-page | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Grain** | Wet | Size | Density | Pitch | Spray | Scan | More | Back |
| Grain, More | Spread | Position | Warp | Feedback | Freeze | | More | Back |
| **Ripple** | Wet | Mode (strings, comb, reso) | Pitch | Decay | Tone | Chord | More | Back |
| Ripple, More | Pluck (own notes, + sends) | Cutoff | Resonance | Drive | Spread | | More | Back |

- **Wet is on button 1 and selected when the sub-page opens.** Inside a tape's FX the three run in series, and each block's Wet sets how much of its own sound passes on to the next, so Grain, then a turn of the encoder, brings the grains in. That's the one Bryo habit worth keeping, and it costs nothing on the Tracker.
- **No Echo.** Bryo's CHRONOS had a delay and a room. The Tracker already has both as send effects, Delay and Reverb, on every instrument and in Master, so a tape's FX are Grain and Ripple only. For echoes on the grains or strings, the tape instrument's own Delay and Reverb sends do it.
- **No insert or send switch per block.** The whole set is a send, so Bryo's ROUT goes away; the dry sound always stays on its track.
- **Automation and FX columns reach it.** The tape instrument's Automation can target its own FX's values (Grain Size and so on), and tape FX in a tape track's FX columns act on that tape's FX.
- **Colour isn't a separate block.** The Tracker already has Overdrive and Bit depth on every instrument, which is what Bryo's COLOR mostly did. Each source gets coloured by its own settings before it reaches the chain.
- **Pluck (own notes, + sends)** decides whether notes from tracks that send into this tape's FX also pluck the strings. That's how a kick on track 3 strikes Tape A's strings, set in one place.

---

## 5. Lanes: more FX columns on tape tracks

A tape track has **four FX columns** where an ordinary track has two. They *are* FX columns: the same FX list, the same entry, the same Fill, Copy and Undo, with no new rules. That's the lanes you asked for, in the form a Tracker user already writes.

**Paging.** Pressing **Note** again already swaps a track's FX columns for Micro-move and Volume. On a tape track, pressing **Fx** again swaps the two visible FX columns between FX1·2 and FX3·4. The track's header shows which pair is up (`FX 3·4`). Nothing changes width, so the 4-track view, scrolling and the 8,172-byte frame budget behave as they do now.
- **Tape 2** has width to spare. Track 1 shows all four FX columns at once, and track 2 sits beside it.

**The sample strip (every project type).** Beside a track's columns, in the space to the right of FX2 (about 20 px, so no column moves), a thin waveform runs **top to bottom with the steps**. Each row shows the stretch of sample that plays on that step, so you can read a long sample the way you read the notes. It isn't only for tapes: any track whose sample keeps playing across steps gets one, in Default projects as well.
- **When it shows:** on every track that plays audio, drums included. A drum hit draws as a short burst that dies away within its step or two, so a beat reads as a column of hits next to its notes, the same way a loop reads as a continuous waveform. Config's **Long samples** setting hides strips for notes shorter than a beat, for anyone who wants a cleaner grid.
- **It follows what's written.** A note draws a short solid line across the strip where the sample starts, or where it jumps to a slice, and the strip shows the audio from there. A looping sample wraps at its loop points, with a dashed line at each wrap, and keeps going for as long as the note does, across the pattern end and into the next pattern or the pattern's own repeat. A one-shot goes empty where the sample ends. Tune, Speed, Reverse and Position FX bend it: a slowdown narrows the waveform as less audio passes per step, a stop goes flat, and reverse runs the audio backwards.
- **Synths, drum instruments and other generated sound:** there's no stored sample to draw ahead, so the strip is the instrument's **real output, captured as it plays**: a peak per quarter-step, kept per track. Rows already heard on this pass are drawn bright; rows still ahead show the last pass, dimmed, until the playhead reaches them, so a looping pattern shows the synth's shape before it plays again. A synth note's strip swells with its attack and fades with its release after OFF. Synth parts mix outside the tracks, so a part's output is credited to the track whose note is holding it; where two tracks hold the same part, both show it. Granular and Wavetable sample instruments use the same captured view, since they don't play through a sample in order.
- **MIDI tracks** have no audio here, so no strip.
- **The playhead row** is drawn brighter, like the rest of the playing row.
- **Mini:** the same; it's part of the pattern editor.
- **It ships on its own.** Nothing in it depends on tape mode, so it can be the first pattern-editor change and be useful to everyone before tapes exist.

**How the strip stays cheap, and how to turn it off.** The strip is a primitive picture, not a waveform display. It's built to cost almost nothing and to disappear entirely when you don't want it:
- **It's a sketch, not the audio.** Each row is four short horizontal lines, one per quarter-step, each as wide as the loudest moment in that quarter. There's no curve, no fill, no colour coding and no animation beyond the playhead row getting brighter. That's about 60 lines per track for a 15-row screen.
- **Samples are read from a peak table, never from the audio.** When a sample loads, I build a table of 64 peaks across it (min and max, 1 byte each, so 128 bytes per instrument) in the main loop, alongside the load. Drawing a strip looks up that table, scaled by tune, speed and loop points; the audio interrupt is never involved.
- **Synths and other generated sound are captured as one byte per quarter-step.** The audio interrupt compares one running peak per track per 128-sample block, which is a few instructions next to the per-voice RMS node that already runs, and writes 1 byte at each quarter-step boundary. That's 512 bytes per track for a 128-step pattern, allocated only for tracks whose strip is on.
- **It's drawn only when the screen changes.** The pattern editor already redraws when the playhead moves a row or when you edit; the strip rides on that redraw and doesn't add refreshes of its own.
- **It has a budget.** Every line is display-list bytes from the 8,172-byte frame. Four strips in the 4-track view is about 2 KB by my estimate. If a busy pattern pushes the frame near its limit, strips drop first, to two lines per row and then to the cursor's track only, so notes and FX are never the thing that disappears.
- **Off means off.** Config → **Sample strips: Off / Long samples / All notes (default)**. With Off, nothing is captured, no peak tables are built, no RAM is allocated and nothing is drawn: the pattern editor costs exactly what it costs today. "Pattern info" Off also hides the strips, along with the track names. The 8-track narrow view never shows strips, because it has no room.

**New FX types**, appended to the FX list. They're listed only on tape tracks except for the two sends, which work on any track:

| FX | Value | What it does |
| :--- | :--- | :--- |
| Speed | −200…+200 % | tape speed; 0 is a tape stop, negative plays backwards |
| Wow | 0–100 | wow and flutter depth |
| Grain Wet / Size / Position | 0–100 | |
| Grain Freeze | on / off | hold the grain buffer |
| Ripple Wet / Pitch / Decay | 0–100, semitones | |
| Tape Rec | 1–16 bars, 0 = stop | start a take onto this track's tape on this step, when the track is armed (§7) |
| Tape A send / Tape B send | 0–100 | **any track**: this note's send into a tape's FX, like Reverb send |

**Ramps and sweeps** use **Fill**, which already offers an FX Type with From and To. Fill Speed from 100 to −100 across steps 9–16 and the tape slows into reverse.

**How long an FX lasts** follows the Tracker's own rule: an FX applies from its step until the track's next note. On a tape track, notes are usually few (a slice jump now and then), so tape FX naturally hold across steps until you change them or the next note arrives. To end a stutter, write the FX again with 0, the way you'd end any FX today.

---

## 6. Modulation: Automation

Press **Params** again on Tape A for **Automation**, the existing page: Destination, Type, then that type's values and Amount.

- **New destinations** for tapes: Speed, Grain Wet, Grain Size, Grain Position, Grain Pitch, Ripple Wet, Ripple Pitch, Ripple Decay. They join Volume, Panning, Cutoff and the rest, so an LFO on grain size is chosen the same way as an LFO on cutoff.
- **The existing types all apply:** Envelope, LFO (including Random), WT Env and WT LFO, with tempo sync.
- **One new type, Follow:** an envelope follower. Its values are Source (a track, or the input), Rise, Fall and Amount. It's the one thing Bryo's modulators did that the Tracker can't: let the kick on track 3 pump Tape A's grains.
- Bryo's step-sequencer modulator isn't needed. FX columns already sequence any destination per step, and Random-shape LFOs cover the rest.

---

## 7. Recording onto a tape: in the pattern editor

Recording onto a tape happens in the pattern editor, where the tape lives, using the recording controls the pattern editor already has. The Sample Recorder doesn't change: it still records samples into instrument slots.

**1. Arm the tape track.** Hold **Shift + Rec**: the bottom labels show each track's Armed or Unarmed state, as they do today for live recording, now including the tape tracks. An armed tape track records; an unarmed one never does, so playing a song back can't record over a tape by accident.

**2. Record live.** **Rec + Play** starts live recording, as it does now for notes. On an armed tape track it also records audio onto the tape, from the tape's Source, until you stop: **Rec** again punches out, and Rec once more punches back in. With **Bars** set, a take waits for the next bar line, blinking red the way a queued pattern does, records that many bars, then stops by itself and loops in time. With Bars on Free, it starts and stops on your presses.

**3. Or write the take into the pattern.** The step FX **Tape Rec** starts a take on its step, exactly there. The value is the number of bars (1–16), and `0` stops a take early. Takes can then be placed in a song: print the drums onto Tape A at bar 17, overdub at bar 33. It records only while the track is armed, and is shown dimmed when it isn't. While armed, a Tape Rec on a looping pattern records on every pass: with Dub at 0 that builds up layers, and with Dub at −100 each pass replaces the last.

**4. The take's settings** are on the pattern editor's third page, which today has Pattern Name and Track Name with its other buttons free. On a tape track it gains:

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Pattern Name | Track Name | Rec Source | Bars | Dub | Undo Take | | More |

- **Rec Source:** Tracks (the mix of the ordinary tracks, the default), Track 3–6, Tape A or B, Self (resampling through its own FX), Grain A or B (freeze to tape: the grain buffer copied at once), Line, Mic, Radio (Original and Tracker+), USB (Tracker+ and Mini).
- **Bars:** Free, 1, 2, 4, 8, 16.
- **Dub** works as it did in Bryo: 0 adds the new sound to the old (sound on sound), +100 keeps the old loop, −100 replaces it, and values between fade the old loop each pass.
- **Undo Take** brings back the tape as it was before the last take.

**5. Bounce** is **Render Selection** (pattern editor, page 2), which already renders a selection to a sample. It gains a destination: New instrument, **Tape A** or **Tape B**. Select an ordinary track's steps, render to Tape A, and the part is on tape.

**While a take runs,** the tape track's header blinks red with the bar count (`REC 2/4`), and the sample strip shows the record head as a red line moving down with the steps. A take never stops the song.

**Saving:** a take marks the tape dirty, and saving the project writes it in the main loop like any edited sample.

---

## 8. Drums: theseus is a synth engine

theseus is a synth, so it uses the synth screens a Tracker+ or Mini owner already knows, not a sample screen.

- **Choosing it:** in a synth part's screen (Model, Polyphony, Gain, Panning, Reverb send, Delay send, Load, Edit), **Model** lists **Theseus** beside the other engines. Notes for it go on any track with the synth instrument, as for any synth.
- **Its sound:** **Edit** opens the patch editor as it is today: the title **Edit Patch: <patch>** and **Synth n (Theseus)**, the engine's logo, a 4×4 grid of tiles (the engine's sections, then **Voice** and the macros **M1–M7**), the selected section's parameter list in a frame with the current parameter outlined, its value as a bar in the last column, and Save, Save As, the section's name and Back along the bottom. theseus declares seven sections: **Kit** (Decay, Punch, Cutoff, FM, Noise, Tune), **Hit LFO** (LFO, Rate), and one per drum. Patches save and load like every other engine's, and the macros map to them the same way.
- **Editing each drum:** the grid has a section for each of the five drum types, **Kick, Tom, Snare, Hats** and **Metal**, each with **Tune, Decay, Tone** and **Level** in its parameter list. The kit-wide values set the character, and the per-drum ones trim each drum within it, the way Bryo's DRUM gave each instrument its own TUNE, DEC, LVL and TONE.
  - **The pads jump to the drum.** Play a pad (or a note on the Mini keyboard) and the grid selects that drum's section, so you hit the snare and its four values are in the list. The title bar names the drum (`D-3 → Snare`).
  - **Between drums it blends.** theseus picks drums along a continuous range of notes, not in fixed slots, so a note between two types, say a high tom, gets settings blended from its two neighbours. Tuning the snare doesn't make a step at the snare's edge.
  - **Each value can be moved per step** from FX columns or Automation, like any synth parameter. That's where a per-hit snare decay or a pitched-down kick comes from.
  - **Engine change:** this adds per-type trims to theseus itself (five anchor points across its zone range, interpolated per hit). It's my own MIT code, so the change goes into the theseus source with its host tests extended to cover the trims.
- **Playing it:** the note picks the drum (low kicks, then toms and snare, then hats and metal). The 48 pads are the kit, and the step's velocity is the hit's velocity, which synth notes already carry. **Polyphony** sets how many hits overlap.
- **Into the effects:** a synth part's screen has no free button, so in tape projects **Reverb send** and **Delay send** each step on when pressed again, following the Tracker's rule that pressing a selected list again steps it: Reverb send → **Tape A send**, and Delay send → **Tape B send**. The label shows which send the encoder is setting.

**Pads make editing fast** (Original and Tracker+; this works in every engine's patch editor, not only theseus). The Tracker already lets you hold a button and press a pad to choose, as when holding Pattern and pressing a pad picks the pattern number. The patch editor uses the same habit, so you rarely need the encoder to get somewhere:

| Hold | Pads | What happens | LEDs while held |
| :--- | :--- | :--- | :--- |
| nothing | all 48 | play notes, as now; on theseus a hit also selects that drum's section | as now |
| **Parameter** (the wide label naming the section) | top row, 12 pads | select section tile 1–12 in grid order (Kit, Hit LFO, Kick… M5) | current section bright, other sections dim |
| **Parameter** | rows 1–3, 36 pads | select the current section's parameters, in list order | current parameter bright, others that exist dim, unused pads off |
| **Shift** | all 48 | set the selected parameter's value, pad 1 = minimum, pad 48 = maximum, the way pads set an FX value in the pattern editor | a bar from pad 1 to the current value |

- **Two taps and a press.** To make the snare brighter: hold Parameter, tap the Snare section pad, tap the Tone pad, let go, then hold Shift and press a pad high up. Fine-tune with the encoder afterwards. Nothing about the screen changes: the grid and the list follow what the pads selected.
- **The same rule on bar pages.** On pages of vertical bars (the Master Tape pages, Params), holding a parameter's screen button and pressing a pad sets that value across the 48 pads, exactly the hold-a-button-press-a-pad habit above.
- **List values** (a mode, a shape) step through their choices across the pads, one pad per choice, from the bottom-left.
- **Mini:** no pads, so nothing changes: Left/Right move between grid and list, and ±1/±10 set the value.

**On the Original Tracker.** There are no synth parts on the Original today (`FEATURE_SYNTH` and `GUI_HANDLE_SYNTH` are IMX only), so this needs one of two things:
1. **One synth part on the Original, for built-in engines only.** It would be the same synth screens and the same `synthPart` settings, which the project file already stores on every device, with theseus as the only engine and no `.psyn` loading (that needs the M7). This is the consistent answer. The cost is building the synth host and its screens for the M4, and I don't know yet whether they fit in the Original's spare flash (about 153 KB) and RAM. Phase 0 measures it.
2. **If they don't fit:** the Original gets theseus as a drum instrument whose screens copy the synth part and patch editor layout button for button, so the same muscle memory works. Projects move between devices with the kit's eight values intact.

## 9. Synths, performance mode and song

- **Synths (Tracker+ and Mini):** a synth part reaches a tape's FX the same way a sample does, through Tape A send and Tape B send, reached by pressing Reverb send or Delay send again (§8).
- **Performance mode** gains these FX: Speed (rows: 50 %, −100 %, 200 %, 0 %), Grain Freeze, Grain Wet, Ripple Wet. The FX it already has (filter, volume and the rest) work on tape tracks as on any track.
- **Song mode** is unchanged. Patterns carry their FX columns, so the tape's moves arrange like any other part.

---

## 10. What changed from the first version, and why

| First version | Now | Why |
| :--- | :--- | :--- |
| A new **Tape** module with six pages and pictures | Sample Playback (the tape) and Master Tape 4/4 (the effects, as send effects, like Reverb and Delay) | Those are the screens a Tracker user already opens for "how a sound plays" and "how it's shaped". |
| Lanes in four banks, with Step / Hold / Glide modes and a target popup | Four more **FX columns**, new FX types, and **Fill** for ramps | Lanes *are* FX columns in a tracker. Separate rules would be a second grid to learn. |
| Modulator slots M1–M4, hold a pad to set depth, Assign mode | **Automation**, with new destinations and a **Follow** type | The Tracker's model is destination-first; adding destinations fits it. |
| A deck that plays alongside the track's notes | One voice per track; the tape keeps time underneath | One voice per track is how every Tracker track works. |
| **Scenes** (4, glide, a Scene FX) | Removed | Patterns, FX on step 1, song mode and performance mode already do section changes on a tracker. Bryo needed scenes because it has no song. |
| A separate COLOR block | Each instrument's Overdrive and Bit depth | They already exist on every instrument. |
| Master → Tape Sends page, HIT toggle | Tape A / B send in each instrument's More, plus Ripple's Pluck setting | Sends are per instrument on the Tracker, beside Reverb and Delay. |
| Synth **Output** routing | Tape A / B sends on the synth part | Same reason. |
| Tape Recorder screen | The pattern editor: arm, Rec + Play, the Tape Rec FX, settings on page 3 | Recording a tape is part of writing the pattern, and the pattern editor already arms tracks and records live. |
| Top-row pads for pages, freeze and stutter | Pads play notes, as everywhere else | Live moves belong in performance mode. |
| A Mini-specific assign flow | Nothing extra | The screens used already work on the Mini. |

---

## 11. Journeys

**J1. Tape 4, first sound**
1. **File → New Project → Tape 4 → Create.** Pattern 1 has `C-4 TA` on Tape A and `C-4 TB` on Tape B.
2. On track 3, write a beat with a drum instrument (§8). **Play.**
3. **Shift + Rec**, arm Tape A. On the pattern editor's third page, Rec Source is Tracks and Bars is 4. **Rec + Play**: the take starts on the next bar, stops after 4, and Tape A loops it.
4. **Master** until **Master Tape 4/4**, then **A Grain**, and turn the encoder: Wet is already selected.

**J2. A tape break in the pattern**
1. On Tape A, press **Fx** twice for FX3·4.
2. Steps 9–16, FX3: **Fill**, FX Type Speed, From 100, To −100. The tape slows into reverse.
3. Step 13, FX4: **Roll** 1/16, and step 15: Roll 1/32. On step 17, write Speed 100 and Roll 0 to come out of it.

**J3. The kick plucks the strings**
1. On track 3's drum instrument, **Params → More → Tape A send** up.
2. **Master Tape 4/4 → A Ripple**: Wet up, Chord Min7, and **Pluck: + sends**.

**J4. The kick pumps the grains**
1. Tape A: **Params**, **Params** again (Automation). Destination **Grain Size**, Type **Follow**, Source **Track 3**, Amount −40.

**J5. A synth through Tape B (Tracker+ and Mini)**
1. Synth part B's settings: **Tape B send** up. Its notes stay on whichever track they're on.

**J6. Live**
1. **Perform**: put Speed, Grain Freeze and the existing Delay in three slots, then hold their pads over the song.

---

## 12. Questions for you

1. **One voice on a tape track.** Another instrument on a tape step plays *instead of* the tape, with the tape keeping time underneath, which is how every Tracker track works. Bryo plays a source and its tape at once. Is the Tracker way right here, given that the sends cover "play something into the chain while the tape rolls"?
2. **Scenes dropped.** Patterns and performance mode cover them on a tracker. Do you still want them?
3. **Colour folded into Overdrive and Bit depth.** That drops Bryo's noise and tilt. Acceptable, or should Color be a fourth Tape FX sub-page?
4. **Tape FX only on tape tracks** (except the sends). Or should they be available, and do nothing, on any track?
5. **theseus on the Original.** Is one built-in synth part on the Original worth the flash and RAM, if it fits, or is a drum instrument that copies the synth screens enough?
