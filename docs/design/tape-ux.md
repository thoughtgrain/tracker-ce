# Tape Projects: UI and UX

> Status: **design only, nothing built.** This is the companion to [`tape-mode-plan.md`](tape-mode-plan.md). The plan says what gets built and in what order; this says how it works in your hands. Where they disagree about anything a person sees or touches, this file wins, and I'll bring the plan in line before building. The screen mockups are drawn at the Tracker's 800×480 on a separate page that I keep beside this file.

The question this answers: Bryo is an instrument you *play* (hold a pad, turn a knob, no menus). The Tracker is an instrument you *write* (a grid of steps, a screen button and an encoder, popups). How does a person who already knows the Tracker get Bryo's tape, chain, modulators and scenes without learning a second instrument?

---

## 0. Design rules

I took five rules from the Tracker and five from Bryo. Every decision below comes from one of them, and where they conflict the Tracker's rule wins, because that's the muscle memory people already have.

**From the Tracker (kept exactly):**
1. **A screen button selects, the encoder changes.** Up and Down are ±1, Left and Right move between columns, and Delete resets a value to its default. On the Mini, ±1 and ±10 stand in for the encoder.
2. **Hold a column button for a list.** Holding Instr or Fx opens a list on the right of the screen, and letting go writes the choice.
3. **Press a module button again to cycle its modes.** Params goes from parameters to automation today, and Master goes from master to mixer to sends. On the Mini, Middle does the same in the menu.
4. **Shift is the second layer**, shown on the bottom labels while it's held, the way the pattern editor shows mute labels.
5. **Confirm on buttons 7 and 8.** Cancel on 7 and the action on 8. There are no modal windows.

**From Bryo (adapted):**
6. **Every page shows a picture of what it does.** The tape and its loop window, grains on their buffer, the strings, the echo's taps. A screen full of bars says less than a picture of the sound.
7. **One press, one turn, and you hear it.** Every effect's first page has Wet on button 1, already selected when you arrive. Open the effect, turn the encoder, and it's in.
8. **Nothing hidden.** If a lane or a modulator is moving a value, the value's label shows it, with a mark and a rail.
9. **Destructive actions confirm, and the last one undoes.** Clearing a tape asks first, and the previous take comes back with Undo until it's recorded over.
10. **Colour is never the only cue.** Every colour that means something also has a glyph or a word.

**One rule about written versus played.** Things you write go in the pattern: lanes, the Scene step FX, slice jumps. Things you play go in performance mode: stutter, freeze, scene throws. Both reach the same parameters, so a move you find live can be written down, and a written move can be played over.

---

## 1. The mental model

A tape project is a small studio with two tape machines.

```
 ordinary tracks (samples, drums, synths)             tape track A               tape track B
 ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐       ┌───────────────────┐     ┌───────────────────┐
 │ track 3│ │ track 4│ │ track 5│ │ track 6│──REC──▶ DECK  (the tape)  │     │ DECK              │
 └───┬────┘ └───┬────┘ └───┬────┘ └───┬────┘       │ + its own notes   │     │ + its own notes   │
     │  FX A / FX B sends (live)      │            │ + sends in        │     │ + sends in        │
     └────────────────┬───────────────┘            │ GRAIN → RIPPLE →  │     │ GRAIN → RIPPLE →  │
                      └───────────────────────────▶│ COLOR → ECHO      │     │ COLOR → ECHO      │
                                                   │ 4 modulators      │     │ 4 modulators      │
                                                   │ lanes A–D         │     │ lanes A–D         │
                                                   └─────────┬─────────┘     └─────────┬─────────┘
                                                             └──────────── mix ────────┘
```

- **Ordinary tracks** are tracker tracks, exactly as today. They write the parts.
- **A tape track** has three things an ordinary track doesn't:
  - **a deck**, a loop of tape that rolls whenever the transport does;
  - **a chain**, GRAIN → RIPPLE → COLOR → ECHO;
  - **lanes**, columns of values beside its notes.
- **Into a tape track's chain** go its deck, the notes on its own steps (any instrument), the FX A or FX B sends of the ordinary tracks, and, on Tracker+ and Mini, any synth part routed to it.
- **Onto a tape** you record the ordinary tracks, one track, the tape track itself, or the input.

| Type | Tracks | Tape tracks | Ordinary tracks |
| :--- | :---: | :--- | :--- |
| Default | 16 | — | 8 sample + 8 MIDI, as today |
| Tape 2 | 2 | track 1 (Tape A) | track 2 |
| Tape 4 | 4 | tracks 1–2 (Tape A, Tape B) | tracks 3–4 |
| Tape 6 | 6 | tracks 1–2 (Tape A, Tape B) | tracks 3–6 |

---

## 2. Where everything is

The Tracker's printed buttons keep their meaning. In a tape project a few of them do a tape thing **when the pattern cursor is on a tape track**, and their usual thing on an ordinary track. Outside the pattern editor, "on a tape track" means the last track the cursor was on.

| I want to… | Original and Tracker+ | Mini |
| :--- | :--- | :--- |
| Edit a deck and its chain | **Params** on a tape track → the Tape screen. **Shift + Params** from anywhere. | Menu → **Tape**, or bind it to an Fn key |
| Edit the instrument a tape track plays | **Params** again: Tape → Instrument → Automation → Tape | **Middle** in the menu cycles the same |
| Record onto a tape | **SampleRec** on a tape track → the Tape Recorder | Menu → Sample Rec on a tape track |
| Record a sample as usual | **SampleRec** on an ordinary track, or the Tape Recorder's **Sampler** button | Same |
| Edit the tape's audio (trim, normalise, `.ptfx`) | **SampleEdit** with Tape A or B as the instrument | Same |
| Write lanes | Pattern editor, **Fx** again on a tape track | Same, with the Fx button |
| Send an ordinary track into a chain | **Master**, pressed until **Tape Sends** | Menu → Master, then Middle |
| Scenes | **Perform** pressed a second time → **Scenes** | Menu → Perform, then Middle |
| Throw a stutter, freeze or scene live | **Perform**: the new tape FX | Same, with ±1/±10 for the rows |
| Play a drum kit | A drum instrument (§9) on any track | Same |
| Route a synth through a tape | Synth editor → **Output** | Same |

Two Tracker functions that a tape project frees up get reused. Perform's second press used to swap between tracks 1–8 and 9–16; with at most 6 tracks there's no second page, so it opens Scenes. The Mini's menu grid has 12 boxes and 11 modules, so Tape takes the free box.

---

## 3. Making a tape project

**File → New Project** works as it does now: a playing project asks to stop, and an unsaved one shows Cancel / Don't Save / Save. Then a new step, **Project type**, set out the way the Export choices already are: a page of screen-button choices, with a picture of the tracks above them.

| Button | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Label | **Default** / 16 tracks | **Tape 2** / 1 tape | **Tape 4** / 2 tapes | **Tape 6** / 2 tapes | | | Cancel | Create |

- **Default is selected when the page opens**, so pressing Create (button 8) twice makes the project you'd have got before. That keeps the old path two presses long.
- The picture shows the tracks the type will have: tape tracks drawn as reels, ordinary tracks as plain columns, and a line of text such as "4 tracks. Tracks 1–2 are tape tracks with effects and lanes; 3–4 are ordinary. Tapes up to 60 s each."
- Create makes the project and opens the pattern editor with the cursor on track 1. In a tape project, each tape starts blank at 8 bars of the project tempo.
- On the Mini, ±1 moves between the four types and button 8 creates.

**Changing which tracks are tape tracks** later is on the Tape screen's Shift layer (§5): **Make tape** or **Make ordinary** for the track under the cursor, as long as the limit holds. Making a tape track ordinary keeps its deck and chain settings, so switching back loses nothing. A project's type can't be changed after it's made. Converting a Default project's 16 tracks down to 4 would delete data, and **Save As** into a new tape project is the safe way to do it.

---

## 4. The pattern editor in a tape project

### 4.1 What's different on screen

- **Fewer tracks.** Tape 2 shows 2, Tape 4 shows 4, and Tape 6 shows 4 at a time and scrolls, as Default's 8 do. MIDI tracks aren't shown.
- **Tape tracks are marked.** Their header reads `TAPE A` or `TAPE B` with a small reel glyph. With Config's "Pattern info" set to Off, the column's top edge carries the reel glyph instead, so the mark never depends on colour alone.
- **The tape instrument.** On a tape track the instrument popup starts with **TA** (or **TB**), the track's own deck. A note with TA jumps the deck to a slice, and OFF with TA pauses the deck until the next TA note. Any other instrument on a tape track plays through the chain while the tape keeps rolling.
- **Ordinary tracks that send into a chain** show `→A` or `→B` in their header, so the routing is visible from the grid.

### 4.2 Lanes

A lane is **an FX column whose FX type is fixed for the whole pattern**. That's the whole idea, and it means everything people already know about entering FX values works on lanes.

**Where they are.** Each tape track has 16 lanes in four banks of four:

| Bank | Starts as | So that… |
| :--- | :--- | :--- |
| **A** | Tape: Speed · Rpt · Start · Wow | the first bank you open moves the tape |
| **B** | Grain: Wet · Size · Pitch · Position | |
| **C** | Ripple: Wet · Pitch · Decay · Tone | |
| **D** | Color/Echo: Drive · Crush · Echo · Verb | |

Any lane can be re-targeted to anything on the Tape screen, including a modulator's Amount or Offset. The bank names follow what's in them; a bank holding a mix shows its letter only.

**Showing them.** On a tape track, press **Fx** again while the cursor is in an FX column. The track's two FX columns are replaced by bank A's four lanes. Each press after that steps to the next bank, and the press after bank D brings the FX columns back: FX → A → B → C → D → FX. The FX columns are 8 characters wide and four 2-character lanes fill exactly the same width, so nothing on the screen moves.

- **Tape 2** has room to spare: tape A shows its FX columns *and* two lane banks in the columns track 3 and 4 would use. Fx then steps the pair (A+B, then C+D).
- The column header names the bank (`A · TAPE`). The title bar's right label, which already shows the selected step's value, names the lane and its value in real units: `A2 Rpt  1/16  Hold`.

**Entering values** (red mode, as for FX):
- **Encoder or Up/Down** sets the value. The cell shows `00`–`99`, and the title bar shows the real value (ms, %, semitones, a division).
- **Pads** set the value scaled across the 48 pads, the way pads set FX values.
- **Delete** clears the step. Copy, Shift+Copy (paste), Insert, Backspace, selections, Undo and Redo work on lanes exactly as on FX.
- **Fill** gains two Fill Types for lanes: **Ramp** (From → To across the selection) and **Random** (between From and To). That's how you write a sweep in one move.
- **Mini:** ±1 and ±10 set the value, and Middle writes the last value used, as it does for FX.

**Choosing what a lane moves.** Hold **Fx** with the cursor on a lane. The list on the right that normally holds FX types holds lane targets instead, grouped Tape / Grain / Ripple / Color / Echo / Mod. Letting go of Fx sets the target. While the list is open, buttons 5–7 set the lane's mode:

| Mode | What a value does | How the following empty steps look |
| :--- | :--- | :--- |
| **Step** | applies on its step only, then the value goes back to where it was (a p-lock) | blank `··` |
| **Hold** (default) | stays until the next value in the lane | the value repeated, dimmed |
| **Glide** | ramps to the next value across the steps between | the in-between values, dimmed |

The dimmed values are drawn in the same colour at lower brightness, so you can see where the tape is going without moving the cursor. That idea comes from Bryo's p-lock view (bright where locked, dim where inherited).

**Recording lanes live.** In live recording (Rec + Play), turning a value on the Tape screen writes it into the lane that targets it, at the step that's playing. If no lane targets it, the first free lane in the bank whose device it belongs to takes it, and a popup says so: `Grain Size now on lane B2`. This is the tracker version of Bryo's knob moves, and it's a later phase (§16).

---

## 5. The Tape screen

The Tape screen is one module with six pages, one per part of the tape track:

**Tape · Grain · Ripple · Color · Echo · Mod**

### 5.1 Anatomy

- **Title bar:** `Tape A · Grain` on the left, with the page dots (`●○○`) when the device has more than one page. On the right, the transport position in bars and the memory the deck uses.
- **The picture**, y = 31–420: what the page does, drawn live (§5.3).
- **The bottom labels:** 8 parameters, name over value, like every Tracker parameter screen. The selected one has the red frame. Button 8 is **More** when the device has a second page.
- **Marks on a label:**
  - `~` and a pink rail along the label's bottom edge: a modulator moves this value. The rail shows how far it reaches, and a dot shows where it is now.
  - `L` and an orange tick: a lane in this pattern targets this value.

### 5.2 Moving around

- **Pages.**
  - **Original and Tracker+:** the top row of pads, pads 1–6, jumps straight to a page. Their LEDs show which page is up, bright for the current page and dim for the rest.
  - **Every device:** Shift + Left/Right steps through the pages.
  - More (button 8) steps within a device.
- **Which tape.** The screen edits the tape track the cursor was on. Hold Shift and buttons 1 and 2 become **Tape A** and **Tape B**.
- **The Shift layer:**

  | Button | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
  | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
  | With Shift held | Tape A | Tape B | Make tape / ordinary | | | Clear mods on page | Reset page | |

  Delete still resets the selected value on its own. Reset page and Clear mods confirm on 7/8.
- **Pads** (Original and Tracker+): the bottom three rows play the track. With the deck as the instrument they jump slices, otherwise they play notes, so you can hear a change without going back to the pattern. The top row is the deck's controls:

  | Top-row pad | 1–6 | 7–10 | 11 | 12 |
  | :--- | :--- | :--- | :--- | :--- |
  | Does | Jump to a page | Hold for M1–M4 (assign depth, §6) | Grain Freeze: hold, Shift to latch | Rpt: hold to stutter |
  | LED | current page bright | dim when the slot has depths | lit while frozen | lit while held |

### 5.3 The pages

Every effect's first page starts with **Wet** on button 1, selected when you arrive. **Route** (Insert or Send) is on page 2 for Grain, Ripple and Color.

**Tape**: *a strip of the tape's waveform with the loop window lit and the rest dimmed, 16 slice ticks, the play head, a bar ruler under it, and the record head in red while recording.*

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | Start | Length | Speed ±200 % | Pitch ±24 st | Rpt (off, 1/4 … 1/64) | Rpt len | Sync (off, 1–16 bars) | More |
| Page 2 | Wow | Flutter | Rotate | Xfade | Reverse | Level | | More |

**Hold Shift on Start or Length to zoom**: the picture shows the waveform around that edge, and the encoder moves in fine steps. That's Bryo's HOME-held zoom, on the Tracker's Shift.

**Grain**: *the live buffer as a timeline of the last bars, grains as small marks that light while they sound, the read cursor, `12 OF 24 GRAINS`, and `FROZEN` when frozen.*

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | **Wet** | Size | Rate | Pitch | Spray | Spread | Scan (Tape, Stretch, Fixed, Delay) | More |
| Page 2 | Position | Warp | Feedback | Contour | Reverse | Pattern | Scale | More |
| Page 3 | Rnd pitch | Grains | Sync | Route | Freeze | | | More |

**Ripple**: *the four strings drawn as lines that move while they ring, each labelled with its note, and the chord's name.*

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | **Wet** | Mode (Strings, Comb, Reso) | Pitch | Decay | Tone | Chord | Spread | More |
| Page 2 | Cutoff | Resonance | Drive | Pluck | Keys (Pluck, Tune) | Route | | More |

**Color**: *the drive's transfer curve, the crush's stair-step, and a noise band that follows the input.*

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | **Wet** | Drive | Crush | Crush mode (Bits, Rate, Both) | Noise | Noise decay | Tilt | More |
| Page 2 | Noise tone | Route | Level | | | | | More |

**Echo**: *the repeats as decaying taps along a time axis with the beat grid behind them, and the reverb send as a level.*

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | **Echo** | Time | Sync | Feedback | Tone | Spread | Verb | More |
| Page 2 | Duck | Age | Diffuse | | | | | More |

**Mod**: covered in §6.

---

## 6. Modulators

### 6.1 The Mod page

The Mod page shows **M1–M4** as four small plots across the picture (an LFO's wave, an ADSR's shape, a follower's trace, a SEQ's steps), each with its engine name and how many depths it has.

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Mod page | M1 · LFO | M2 · ADSR | M3 · LFO | M4 · SEQ | **Assign** | Depths | | |

- Pressing a slot's button again opens that slot's own page, with **Engine** on button 1 (a list: LFO, ADSR, Follow, SEQ) and **Back** on button 8.
- The slots start as Bryo's do: M1 a slow LFO, M2 an envelope, M3 a random LFO, M4 a SEQ.
- **Depths** lists every depth the slot has (target and amount) for editing, or deleting with Delete.

| Engine | Page 1 | Page 2 |
| :--- | :--- | :--- |
| LFO | Shape · Rate · Sync · Trig · Phase · Skew · Smooth · More | Variation · Fade · Amount · Offset · · · · Back |
| ADSR | Attack · Decay · Sustain · Release · Trig (notes, loop seam, both) · Amount · Offset · Back | |
| Follow | Source (self, a track, input) · Gain · Rise · Fall · Hold · Amount · Offset · Back | |
| SEQ | Steps · Length · Rate · Slew · Direction · Amount · Offset · Back | the picture is 16 bars: select a step with Left/Right, set it with the encoder (pads 1–16 on the Original and Tracker+) |

### 6.2 Giving a value a depth

There are two ways in. Both end in the same place: a pink signed depth on the label, and a rail after you let go.

**Hold and turn (Original and Tracker+).** This is the Bryo gesture on the Tracker's controls.
1. On any page, select a value with its screen button.
2. Hold top-row pad 7–10 (M1–M4). The bottom labels turn into that slot's depths, in pink, signed: `Size +40 %`.
3. Turn the encoder to set the selected value's depth.
4. Let go of the pad.

A depth turned back to 0 **catches there**: it stays at 0 until the encoder rests for a quarter second, so 0 is easy to find. That's Bryo's catch, word for word.

**Assign mode (every device, and the Mini's only way).**
1. On the Mod page, select a slot and press **Assign**.
2. The screen goes back to the last device page with a pink `ASSIGN M1` badge in the title bar, and the labels show depths.
3. Select a value with its button and set its depth with the encoder, or ±1/±10 on the Mini. Repeat for as many values as you like, across pages.
4. Press **Assign** again (button 5, relabelled **Done**) to leave.

**Limits and messages.** A track has up to 24 depths, at most 8 per slot. Going past the limit shows `M1 has 8 depths. Remove one in Mod > Depths.`. A depth on a value that has a lane is allowed, and the order is the knob, then the lane, then the modulators.

---

## 7. Recording onto tape

**SampleRec** on a tape track opens the **Tape Recorder**. The picture shows both tapes as strips, with the take's bars marked on the one that's armed, an input meter, and a count-in.

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Page 1 | Source | Bars (Free, 1, 2, 4, 8, 16) | Dub | Gain | **Arm** | Bounce | Freeze → tape | More |
| Page 2 | Tape length | Undo take | Clear tape | Sampler | | | | More |

- **Source** (REC IN): Ordinary tracks (the default), Track 3–6, Tape A or B, Self, Line, Mic, Radio (Original and Tracker+), USB (Tracker+ and Mini).
- **Arm** waits for the next bar line, blinking red the way a queued pattern blinks. Then it records for the set bars, and stops itself and loops in time. With Bars on Free, it records until you press Arm again. While stopped, Arm starts the transport on the next press of Play.
- **Dub** goes from −100 to +100. At 0, the new sound is added to the old (sound on sound). At +100 the old loop is kept and recording changes nothing; at −100 the old loop is replaced. Between those, each pass fades the old loop, which is how you get a loop that decays.
- **Bounce** records a track's output onto this tape for the set bars, from the next bar line. Pick the track on Source, then press Bounce.
- **Freeze → tape** copies what Grain is holding onto the tape at once. It's for the moment you hear something good and it's already gone.
- **Undo take** brings back the tape as it was before the last take. **Clear tape** asks Cancel / Clear on 7/8. A cleared tape can be undone until the next take.
- **Sampler** opens the ordinary Sample Recorder, for recording a sample into an instrument slot as usual.
- **Messages:** `Tape A: 4 bars recorded`, `Tape A armed: starts on bar 9`, `Not enough memory for 16 bars. 9.2 MB free; 8 bars fit.`

---

## 8. Sending ordinary tracks into a chain

Press **Master** until **Tape Sends**, a fourth mode after master, mixer and sends, shown only in tape projects. There's one column per ordinary track.

- **Press a column's button** to select its send to Tape A. **Press it again** to switch to its send to Tape B. That follows the Tracker's rule that pressing a selected list again steps it.
- The label shows both: `Trk 3` on top, `→A 64 · →B 0` below.
- **Shift + a column's button** toggles **Hits** for that send. With Hits on, every note on the track plucks the chain's Ripple strings and restarts its ADSR modulators, and the label shows `HIT`. That's what makes a kick strike the strings on the beat.
- Sends are taken before the track's fader by default. A drum track pulled all the way down is then heard only through the effects.
- On the pattern grid, a track that sends shows `→A` or `→B` in its header (§4.1). A step FX, **Send A** or **Send B**, sets the level on a step.

---

## 9. Drums (theseus, every device)

**Making a kit.** In the Sample Loader, with an empty instrument slot selected, a new button **New Drum** makes a drum instrument. It has no sample, so it takes no sample memory.

**Playing it.** The note picks the drum:
- low notes are kicks;
- the middle is toms and snare;
- high notes are hats and metal.

On the Original and Tracker+, the 48 pads are the kit, bottom-left low. The note popup labels the zones (`KICK`, `TOM`, `SNARE`, `HATS`, `METAL`) under the keys, so the map is visible while you write. The step's volume column is the hit's velocity, and harder hits punch deeper and open the filter further.

**Params** on a drum instrument shows exactly 8 values, one per screen button, so the kit fits one page:

| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Decay | Punch | Cutoff | FM | Noise | LFO | Rate | Tune |

- The picture is the zone strip, showing which notes make which drum and how Tune shifts them, plus the hit LFO's position.
- **Params** pressed again opens the instrument automation page as usual, and the instrument's filter, envelopes and sends still apply.
- Every value can be set from a step FX on any track, and from lanes and modulators on a tape track.

---

## 10. Synths through a tape (Tracker+ and Mini)

The synth editor gets one new setting, **Output**: Mix (the default, as today), Tape A or Tape B. It's in the synth editor because routing belongs with the part's other settings (gain, pan, sends), which is where people already look.

- Notes for the part can be on any track. Its sound goes through the chosen tape track's chain.
- The Tape page's picture lists what's feeding the chain: `IN  deck · Synth B · trk 3, 4`.
- Choosing a tape for a part shows the popup `Synth B now plays through Tape A`.

---

## 11. Scenes

**Perform** pressed a second time opens **Scenes**. The picture shows four boxes: filled for the current scene, framed for a stored one, and dotted for an empty one. Each box lists what differs from the others.

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Scenes | Scene 1 | Scene 2 | Scene 3 | Scene 4 | Glide (0–4 s) | | | Back |

- **Recall:** press a scene. Its values glide in over the Glide time, and choices (a mode, a scale) switch halfway.
- **Store:** Shift + a scene. A stored slot asks Cancel / Overwrite on 7/8.
- **Clear:** Delete + a scene, which confirms.
- **What a scene holds:** both tape tracks' values, the chains, the synth outputs, and the modulators' amounts. It doesn't hold tapes, patterns, lanes, depths, ordinary tracks or the tempo.
- **From the pattern:** the step FX **Scene** (1–4) recalls a scene on that step, from any track. An ordinary drum track can call the changes.
- **Live:** the performance FX **Scene** (§12).

---

## 12. Performance mode

Performance mode works as it does now: 12 slots, 4 stored values per slot on the pads, a slot held while you play it. Tape projects add these FX types, which act on the tape tracks selected in performance mode:

| FX | The 4 rows | What it's for |
| :--- | :--- | :--- |
| Tape Speed | 50 % · −100 % · 200 % · 0 (tape stop) | half speed, reverse, double, and a brake |
| Tape Rpt | 1/4 · 1/8 · 1/16 · 1/32 | stutter while held |
| Grain Freeze | on (any row) | hold the grains while held |
| Grain Wet | 4 levels | throw the grains in |
| Ripple Wet | 4 levels | throw the strings in |
| Echo Throw | 4 feedback levels | a dub throw |
| Scene | 1 · 2 · 3 · 4 | that scene while held, back to the one before on release |

On the Mini, ±10 / ±1 / −1 / −10 play rows 1–4, as they do for every performance FX today.

---

## 13. Song mode

Song mode doesn't change. Patterns carry their lanes, so a song made of tape patterns plays its tape moves. Scene step FX in the patterns change the sound section by section. The song editor's blocks show a small reel on patterns where a tape track has lanes.

---

## 14. The Mini

The Mini has no pads, no encoder and no LEDs. Everything above has a path without them:

| Function | Original and Tracker+ | Mini |
| :--- | :--- | :--- |
| Open the Tape screen | Params on a tape track, Shift + Params | Menu → Tape, or an Fn binding |
| Change a value | Encoder, Up/Down | ±1, ±10, Up/Down |
| Change Tape screen page | Top-row pads 1–6, Shift + Left/Right | Shift + Left/Right |
| Give a value a depth | Hold pad 7–10 + encoder, or Assign | Assign |
| Freeze, Rpt | Top-row pads 11–12 | Performance FX, or the Grain and Tape pages |
| Lane value | Encoder, pads | ±1, ±10, Middle writes the last value |
| Lane target | Hold Fx, choose, let go | Hold Fx, ±1 to choose, let go |
| Scenes | Perform, press again | Menu → Perform, Middle |
| Drum kit | Pads are the kit | Mini keyboard (Shift + Note); the zones are labelled on its keys |

---

## 15. Feedback and accessibility

- **Colours are theme tokens.** Lanes get orange and modulation gets pink. They're new keys in the theme JSON, so custom themes can set them, and each comes with a glyph (`L`, `~`) so it never depends on colour. I avoided the pattern's FX purple and FX cyan for these, because a lane isn't an FX and a depth isn't a lane.
- **Real units on screen.** The grid holds two-digit cells; the title bar and labels say `120 ms`, `−50 %` and `1/16`.
- **Messages** use the existing 400×80 popup and say what happened and what to do next: `Tape B is full. Shorten the take or clear Tape B.`. They never just say "error".
- **Motion.** The pictures animate only when the sound moves. When Config's animation setting is off (I'll add one if it doesn't exist), every picture is drawn still, with the play head and grains as static marks.
- **Blinking** means waiting, as it already does for queued patterns: an armed take, a queued scene.
- **Confirmations** on 7/8 for Clear tape, Reset page, Clear mods, Overwrite scene and shortening a tape that holds audio. One level of undo for takes and clears.
- **Text sizes** are the Tracker's existing fonts. Nothing on the Tape screen goes below Font1 (14 px), the size of the bottom labels' second line today.

---

## 16. Journeys

Each journey is the shortest path, on the Original or Tracker+, with the Mini differences after it.

**J1. A tape project making sound in a minute**
1. **File → New Project → Tape 4 → Create.**
2. On track 3, enter a few notes with a sample (or a drum instrument, §9), then press **Play**.
3. Cursor to track 1 (Tape A), then **SampleRec**. Source is Ordinary tracks and Bars is 4. Press **Arm**.
4. The take starts on the next bar and stops after 4. The popup says `Tape A: 4 bars recorded`, and Tape A loops it in time.
5. **Params**, top-row pad 2 (Grain), then turn the encoder. Wet was already selected, so the grains come in.

**J2. A stutter break written into the pattern**
1. On Tape A, press **Fx** twice: bank A, the Tape lanes.
2. Cursor to lane A2 (Rpt) at step 13. Turn the encoder to `1/16`, then down to steps 14–16 for `1/32`.
3. Hold **Fx**, set A2's mode to **Step**, and let go. Only those steps stutter.
4. Lane A1 (Speed): select steps 9–16, **Fill → Ramp**, from 100 % to −100 %, and **Fill**. The tape slows into reverse over the bar.

**J3. The kick plucks the strings**
1. **Master** until **Tape Sends**. Press track 3's button and turn its send to Tape A up.
2. **Shift** + track 3's button: `HIT`.
3. **Params** on Tape A, pad 3 (Ripple). Turn Wet up, then set Chord to Min7 and Decay to taste.
4. Every kick on track 3 plucks Tape A's strings in Min7.

**J4. An LFO on grain size**
1. **Params** on Tape A, pad 2 (Grain). Press **Size**.
2. Hold top-row pad 7 (M1, a slow LFO). Turn the encoder to `+40 %` and let go.
3. Size now shows `~` and a rail. To change the LFO, go to Mod (pad 6), select M1, and press it again for its page.
   - **Mini:** Mod page → M1 → **Assign** → Grain page → Size → +10 four times → **Done**.

**J5. Two sections with scenes, in song mode**
1. Set Tape A the way the verse should sound. **Perform**, **Perform** again (Scenes), then **Shift + Scene 1**.
2. Change it for the chorus, then **Shift + Scene 2**.
3. In the chorus pattern, on any track, step 1: step FX **Scene** = 2. In the verse pattern, Scene = 1.
4. **Song**: arrange the patterns as usual. The sound glides between scenes at each section.

**J6. Performing over it**
1. **Perform.** Put Tape Rpt, Grain Freeze, Tape Speed and Scene in four slots.
2. While the song plays, hold a Tape Rpt pad to stutter, a Grain Freeze pad to hold the cloud, and the Tape Speed 0 % pad to brake.

**J7. A synth through Tape B (Tracker+ and Mini)**
1. Play synth part B's notes on track 4 as usual.
2. Open the synth editor for part B and set **Output → Tape B**. The popup says `Synth B now plays through Tape B`.
3. **Params** on Tape B: the Tape page's picture says `IN  deck · Synth B`. Bring in Echo, then Grain.

---

## 17. What I left out of Bryo, and why

| Bryo | Why it isn't here |
| :--- | :--- |
| The arpeggiator | A tracker writes its notes. Fill, roll and chance step FX already make moving parts. It could come back later as a step FX. |
| DRUM's written rhythms and SEED | The pattern is the rhythm. Chance and random step FX do what SEED did, and theseus's hit LFO keeps a pattern alive. |
| Hold-to-peek mixer, OCT keys, black-key shortcuts | They're FM-1 hardware. The Tracker's equivalents are Master, Shift and performance mode. |
| Two shared rooms, the master compressor | The Tracker's master reverb, limiter and enhancer already do this job. |
| Six project slots and eight track presets in flash | The Tracker has files. Saving a tape track's chain as a preset file is an open question (§18). |
| Morph between scenes | Kept for later. It's the most CPU-hungry part, so it waits for measurements. |

---

## 18. Questions for you

1. **Params on a tape track opens the Tape screen.** This feels right to me because Params already means "edit what this track plays". The cost is one extra press to reach the instrument editor from a tape track. Would you rather the Tape screen sat on Shift + Params only?
2. **Scenes on Perform's second press.** I'm reusing the press that a tape project frees up. Is that discoverable enough, or should Scenes also be a page of the Tape screen?
3. **Lane cells show `00`–`99`.** Two digits keep four lanes in the width of two FX columns. Hex (`00`–`FF`) would give more resolution in the same width, but most Tracker values read in decimal. Which do you want?
4. **Tape track presets.** Should a tape track's chain and modulators save as a preset file you can load into another project?
5. **Arpeggiator.** Drop it, or keep it as a later step FX?
