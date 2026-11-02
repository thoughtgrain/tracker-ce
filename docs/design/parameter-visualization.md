# Parameter Visualization Overhaul: Inventory & Proposal

> Status: **draft / pre-discussion**. Nothing here is implemented yet. This is the groundwork I want in hand before opening a GitHub Discussion or Backstage topic, which `CONTRIBUTING.md` asks for before any UI redesign.

## Why I'm doing this

Almost every value on the Tracker is drawn the same way: a vertical bar that fills up from the bottom, 0 to 100. That's fine for volume. It's misleading for most other things:

- **Bipolar values look unipolar.** An EQ band at 0 dB draws as a half-full bar, which reads as "something is turned up halfway." Pan, tune, micro-tune and EQ gain all have a meaningful centre, and the UI hides it.
- **Log-scale values get linear bars.** The master delay filter runs 100 Hz to 20 kHz on a linear bar, so everything musically interesting is crammed into the bottom 10%.
- **Enums get bars.** The Sample Editor Overdrive "Type" is a 0-2 enum shown as a fill bar plus a bare number.
- **Some screens show no picture at all.** Performance mode has 12 slots of signed offsets and shows them only as text.

The goal is one consistent visual language where the shape of the widget tells you what kind of parameter it is before you read the number.

---

## How drawing works today (and the constraints that come with it)

### The pipeline
1. Every screen (`core/interface/<screen>/g*.cpp`) creates controls with `display.createControl<T>(&props)`. `T` is one of about 30 classes in `core/libs/NewDisplay/`.
2. The screen pushes values with `setControlValue` / `setControlText2` / `setControlData`, then calls `refreshControl`.
3. `cDisplay` renders each dirty control into its own display-list fragment. It copies that fragment into an 8 KB slot in FT812 graphics RAM (`controlsRamAddressStep = 8192`, `displayStructs.h:23`).
4. Each frame, `cDisplay::step2()` (`display.cpp:630-650`) `CMD_APPEND`s every visible fragment into the real display list.

Both MK66 and IMX drive the same FT812 graphics chip through `DisplayDriver`. **A widget I write once renders identically on all three devices.**

### Constraints that shape the design

| Constraint | Where | What it means for me |
|---|---|---|
| **8,172-byte frame budget** | `display.cpp:640` | Controls are appended newest first. Once the budget is hit, **every remaining control is silently skipped**. A heavy knob doesn't fail loudly; something else on screen vanishes. I have to measure, and I want a debug assert. `lastMemoryUsed` (`display.h:206`) already records usage in `DEBUG` builds. |
| **Only 2 free style bits** | `controlsStyle`, `displayStructs.h:89-138` (bits 30-31 free) | I can't keep adding `controlStyleValueXxx` flags. Parameter semantics have to travel in a typed struct through the existing `data` pointer. |
| **65 control slots** | `controlsCount`, `displayStructs.h:21` | Replacing a bar+label pair with one widget is neutral. Adding extra decoration controls per parameter is not. |
| **MK66 flash: ~153 KB free** | `docs/developer_guide.md` §1 | Widgets have to be drawn from primitives (lines, points, rects), not bitmaps. |
| **Theme colors are fixed arrays** | `core/source/Themes.h`, parsed in `ThemeManager.cpp:469` | New colors (track, centre detent, default tick) need theme fields **and** JSON keys **and** fallbacks, so existing custom themes still load. |
| **No on-device simulator** | – | Everything gets verified on hardware. Display-list byte cost can be reasoned about offline, though: each `VERTEX2F` is 4 bytes. |

### What FT812 gives me
- `API_CMD_DIAL`, `API_CMD_GAUGE`, `API_CMD_SLIDER`, `API_CMD_PROGRESS` and `API_CMD_TOGGLE` are already wrapped in `core/libs/FT812/FT812.cpp`, and nothing uses them. They're cheap, but they have a fixed, beveled look and only take FG/BG colors, so they won't match the themes.
- There's no arc primitive. A themed arc has two options:
  - (a) a `LINE_STRIP` of N segments: about 4 bytes per vertex, so a 270° arc at 24 segments is roughly 100 bytes;
  - (b) a stencil trick: a filled `POINTS` circle, an inner circle cut out, then the sweep masked with an `EDGE_STRIP`. That's fewer bytes but more state changes.
  
  I'd benchmark both.

---

## The inventory

Three passes over every screen in `core/interface/`. File:line references point to where the value is pushed to its control.

### Summary by screen

| Screen | How values are drawn today | Biggest problems |
|---|---|---|
| **Instrument Params** (`gInstrEdit.cpp:1576-1690`) | 8 `cBar` columns; Pan/Tune/Finetune are already `LeftRight_100_100` | Cutoff/Resonance show 0-100 with no Hz or Q. Filter type is a list (fine). |
| **Instrument Effects** (`gInstrEdit.cpp:1703-1738`) | `Value_0_100` bars | Bit depth (4-16, discrete) drawn as a continuous fill |
| **Automation** (`gInstrEdit.cpp:1193-1495`) | Bars plus lists | A/D/R use a hand-rolled two-segment mapping (0-1 s → lower half, 1-10 s → upper half). **WT Warp X/Y smuggles two values through one int (`1000 + packed`) and `bar.cpp:176` special-cases it.** |
| **MIDI params** (`gInstrEdit.cpp:1515-1561`) | Bars plus CC lists | Velocity 0-127 rescaled to 0-100 (fine) |
| **Synth Params** (`SynthEditor.cpp:907-979`) | Bars | Gain −96..+12 dB drawn linear in dB. 0 dB isn't marked. |
| **Synth Macros** (`SynthMacros.cpp:385-389`) | Bars; bipolar macros get `LeftRight` | Already the most correct screen |
| **Synth Patch Editor** (`SynthPatchEditor.cpp:1567-1680`) | One bar (col 7) or a list | **The bar ignores `editCurve`**, so exponential params jump around the bar while the encoder turns evenly. Bipolar is only detected for exactly symmetric ranges. Macro Amount (−2..+2) drawn unipolar. |
| **Sample Playback** (`gSamplePlay.cpp`) | Text plus waveform markers | Waveform overlays already do the visual work. Low priority. |
| **Sample Editor FX** (`gSampleEdit.cpp:338,606`; tables in `effector/mtSampleEditorEngine.cpp`) | `Value_0_100` bars for everything | **EQ gains ±12 dB drawn unipolar.** Hz params (EQ freqs, bitcrusher rate, flanger rate) are linear. Overdrive Type enum drawn as a bar. |
| **`.ptfx` plugins** (`fSampleEdit.cpp:2186-2273`) | Same generic bar path; `PARAM_LIST` → `cList` | `PtfxParamDef` (`effector/tracker_plugin.h:33-42`) has **no bipolar/centre, taper, or unit-category field**, so plugins can't ask for the right widget. |
| **Sample Recorder** (`gSampleRec.cpp`) | Bars for gain, radio freq, level meter | Gain shows a raw 0-100 with no dB. The level meter is fine. |
| **Sample Importer** (`gSampleImport.cpp:265-300`) | Preview vol bar plus a memory compare-bar | Fine |
| **Master 1/3, Reverb, Delay, Limiter** (`gMasterParams.cpp`) | Bars; the gain-reduction meter uses `Reverse_0_100` | **Delay filter 100 Hz-20 kHz linear.** Delay time uses a piecewise mapping. Predelay shows no ms. |
| **Master EQ** (`gMasterParams.cpp:925`) | `Value_0_100` bars | **5 bipolar dB bands drawn unipolar** (0 dB = half full) |
| **Mixer 2/3, 3/3** (`gMasterParams.cpp:1727-1851`) | Half-width volume bars plus full meters | Fine conceptually. This is the **densest screen** (16+ bars), so it's the budget stress test. |
| **Song Editor** (`gSongEdit.cpp:407`) | Tempo bar 0-800 → 0-100 | The bar starts at 0 BPM, not 40 |
| **Pattern Fill popup** (`gPatternEdit.cpp:770-1086`) | Bars plus lists | **Pan/Micro-tune/Tune FX drawn unipolar.** Density 1-128 drawn as a fill whether it's %, a step count or an event count. |
| **Performance mode** (`gPerformance.cpp:300`) | **Text only**, 12 slots × 5 rows | Every value is a signed offset (`%+d`) with no visual centre |
| **Config** (`gConfigEdit.cpp`) | String lists | Metronome volume is a 101-entry list. Clock sync correction (−6..+6) is text. |
| **Popups** (`interfacePopups.cpp:1511`) | `cHorizontalBar` for master volume | Fine |

### Summary by parameter kind
This table is where the widget set comes from. Every parameter I found falls into one of these kinds:

| Kind | Examples | Count (approx.) | Drawn correctly today? |
|---|---|---|---|
| Unipolar continuous (0..max) | Volume, sends, cutoff, overdrive, reverb size | ~45 | Mostly yes |
| **Bipolar** (has a centre) | Pan, tune, finetune, EQ gains, macro amount, perf offsets, clock sync | ~40 incl. perf slots | **About a third** |
| **Level in dB** | Track/master/preview volume, synth gain, sends | ~20 | Bar yes; 0 dB never marked |
| **Log-scale** (Hz, long times) | Delay filter, EQ freqs, bitcrusher rate, A/D/R, delay time | ~15 | **No** (linear or hand-rolled piecewise) |
| **Enum** (small, named) | Filter type, LFO shape, saturation, overdrive type, ping-pong | ~30 | Lists mostly; a few are bars |
| **Discrete int** (stepped) | Bit depth, polyphony, voices, slices, space | ~15 | Drawn as smooth fills |
| **Tempo-sync divisions** | LFO speed, delay rate, step repeater | ~8 | Lists (fine) |
| **2-D** | WT Warp X/Y | 1 | **Encoded via a magic-number hack** |
| Toggle | Monitor, sync, play-thru, mute | ~20 | Text / two-item lists |
| Read-only meters | Level, gain reduction, memory | ~20 | Yes. They should stay visually distinct from editable controls. |
| Compound/special | Chance, Roll, Arp, w/y packed nibbles | ~5 | Text. Leave alone. |

---

## Proposal

### 1. One descriptor: `ParamSpec`
**Why:** I counted more than 40 separate hand-written "map this to 0..100" expressions across the screens. Each one re-decides scaling, centre and units, and that's how the inconsistencies crept in. I want that decision made once, from data.

```cpp
// core/libs/NewDisplay/paramSpec.h
enum class ParamKind : uint8_t { Unipolar, Bipolar, Level_dB, LogScale, Discrete, Enum, Toggle, XY, Meter };
enum class ParamUnit : uint8_t { None, Percent, dB, Hz, ms, s, Semitones, Cents, BPM, Ratio };

struct ParamSpec {
    ParamKind kind;
    ParamUnit unit;
    float min, max;
    float center;          // used for Bipolar; usually 0
    float defaultValue;    // draws a tick so "where it was" is always visible
    uint8_t steps;         // Discrete/Enum segment count (0 = continuous)
    const char* const* labels; // Enum labels, or nullptr
};

float  paramNormalize(const ParamSpec&, float value);  // → 0..1 along the widget's travel
float  paramCenterNorm(const ParamSpec&);             // → 0..1 position of the centre detent
int    paramFormat(const ParamSpec&, float value, char* buf, size_t len); // bounded, unit-aware
```

Adapters build a `ParamSpec` from the metadata each subsystem already has, so most screens don't need hand-written specs:

| Source | Adapter | Mapping |
|---|---|---|
| Synth `Patch::Metadata::Param` (`synth/include/PolySynth/Patch.h:114`) | `specFromSynthParam()` | `isEnum` → Enum; symmetric range → Bipolar; `editCurve != kLinear` → LogScale. **This fixes the curve mismatch for free.** |
| `.ptfx` `PtfxParamDef` | `specFromPtfx()` | `'l'` → Enum; others inferred from unit string ("Hz" → LogScale, "dB" + negative min → Bipolar/Level). See the format note below. |
| Built-in effect tables (`mtSampleEditorEngine.cpp`) | static `ParamSpec` tables next to the existing ones | Hand-tagged once |
| Step FX (`getFxMin`/`getFxMax`, `mtSequencer_patternEdit.cpp:1745`) | `specFromStepFx()` | Pan/Microtune/Tune → Bipolar; LFO speeds → Enum |

### 2. One control: `cParamWidget`
A single new `cDisplayControl` subclass that takes a `ParamSpec*` through `data` and picks a rendering by `kind`. It sits alongside `cBar` and doesn't replace it, so screens migrate one at a time.

| Kind | Rendering | Why |
|---|---|---|
| Unipolar | Arc (270° sweep, opening at the bottom) **or** bar, chosen by a layout flag | An arc matches the physical encoder. A bar fits the existing tall columns. |
| Bipolar | Fill **from the centre detent outward**, with a centre tick that's always drawn | Zero is the most important value on a bipolar control |
| Level_dB | Bar or arc with a **0 dB tick**, mapped through the real dB curve | "Unity" is the reference point every mixer uses |
| LogScale | Arc/bar with log mapping and decade ticks (100, 1k, 10k) | Spends equal travel on equal musical intervals |
| Discrete | Segmented (N blocks, up to ~16; continuous fill beyond that) | Shows that the value moves in steps |
| Enum | Segmented strip with the active segment filled, plus the name as text | Shows how many options there are and where you are |
| Toggle | Two-state pill | Clear at a glance |
| XY | Small 2-D pad with a dot | **Replaces the `1000 + packed` hack** in `bar.cpp:176` |
| Meter | Keeps the current bar look, no knob affordance | Read-only things shouldn't look editable |

Every kind also draws a **default-value tick**, so you can see when you've moved off the default.

### 3. Accessibility rules (applied to every kind)
- **Never encode state by color alone.** Inactive/disabled state today is only "grey colors" (`localInactiveBarColors`). I'd add a shape change too: an outline-only track with no fill.
- **The number always stays.** The widget adds to the value text; it never replaces it. Units come from `paramFormat`, so "Cutoff 54" can become "Cutoff 2.4 kHz" where the engine exposes it.
- **Contrast checks on themes.** Custom themes are loaded from JSON. I'd add a contrast check in `ThemeManager::parseThemeJson` that warns (doesn't reject) when the fill, track and background fall below 3:1 (the WCAG non-text contrast threshold).
- **Consistent direction.** Up/clockwise = more, everywhere. The gain-reduction meter's `Reverse_0_100` stays a Meter, which is the one sanctioned exception.

### 4. Budget plan
- A current `cBar` costs roughly 60-120 bytes of display list.
- My rough estimate for an arc knob (track arc + value arc + centre/default ticks + state changes) is 250-400 bytes with `LINE_STRIP`. Eight of those is 2-3 KB. That fits on most screens, but **not necessarily on the mixer**, which already has 16+ bars, labels and meters.
- So: I measure first. I'd add a `DEBUG`-only on-screen readout of `lastMemoryUsed`, record every screen's baseline, and add an assert where `step2()` currently `break`s silently.
- The mixer and other dense screens keep bar-style widgets. Arcs go where the columns have room.

### 5. Format change for `.ptfx`
Plugins can't express bipolar or log params today. `PtfxParamDef` is a fixed 32-byte struct, and its last byte (`+0x1F`) is padding. I'd use that byte as a **flags field**:
- bit 0: bipolar
- bit 1: log taper
- bit 2: show default tick

Old plugins have it zeroed, so they keep today's behaviour, and there's no header version bump. The SDK side needs matching macros in `editor-fx-sdk/tracker_plugin.h` and support in `ptfx_builder.py`.

---

## Proposed file layout

```text
core/libs/NewDisplay/
├── paramSpec.h            # ParamSpec, ParamKind, ParamUnit, normalize/format declarations
├── paramSpec.cpp          # normalize (lin/log/dB/centre), bounded unit-aware formatting
├── paramSpecAdapters.h/.cpp  # from synth metadata, .ptfx defs, step FX, built-in effect tables
├── paramWidget.h          # cParamWidget : cDisplayControl
├── paramWidget.cpp        # dispatch by kind, layout (bar vs arc)
└── paramWidgetPrims.cpp   # arc / segment / tick / pill / xy primitives on the FT812 API
core/libs/NewDisplay/displayStructs.h   # + controlIdParamWidget
core/libs/NewDisplay/display.cpp        # + DEBUG budget assert & readout
core/source/Themes.h / Themes.cpp       # + paramWidgetColors[] with defaults in the 3 built-in themes
core/source/ThemeManager.cpp            # + JSON key, fallback to activeBarColors, contrast warning
editor-fx-sdk/tracker_plugin.h, core/effector/tracker_plugin.h, editor-fx-sdk/ptfx_builder.py  # flags byte
```

---

## Rollout

Each phase is shippable on its own:

0. **Fix the bugs I found** (list below). This has nothing to do with the redesign and is worth doing anyway.
1. **`ParamSpec` + adapters, no visual change.** Route existing `cBar` values through `paramNormalize`. It's pure refactoring and easy to review, and it already fixes the synth curve mismatch.
2. **`cParamWidget` in bar layout.** Bipolar centre-fill, 0 dB ticks, log mapping, segments, default ticks. This fixes the biggest *correctness* problems without changing the look of any screen.
3. **Measure the budget on every screen**, then add the arc layout and the XY pad, and delete the `1000 + packed` hack.
4. **Migrate screens** in order of payoff: Master EQ → Sample Editor FX/`.ptfx` → Synth Patch Editor → Instrument → Fill popup → Performance mode → the rest.
5. **`.ptfx` flags byte** plus the SDK update.

**Workflow continuity:** the README's guiding principle is to respect muscle memory. Nothing here changes navigation, button mapping or column positions, only how a value is drawn. I'd still put the arc layout behind a `Config > General > Parameter style` (Classic / Modern) setting, at least at first.

---

## Bugs found along the way

| # | Where | What | Confidence |
|---|---|---|---|
| 1 | `SynthPatchEditor.cpp:1579` | `paramIdx < 0 && paramIdx >= size` can never be true. It should be `\|\|`, so the out-of-range guard is dead code. | Verified |
| 2 | `SynthPatchEditor.cpp:847`, `:1721` | Same `&&`/`\|\|` mistake for `macroIdx`, in two places | Verified |
| 3 | `fSampleEdit.cpp:1047` | The bar value is computed as a float and stored in `uint8_t`. If the value ever sits below its minimum, that's a negative float → unsigned conversion, which is undefined behaviour. The TS Beat/TS Note defaults are `0` (`sampleEditor.h:171-172`) while their minimums are 40 and 0.1. | Defaults verified. Whether they're overwritten before display is not yet checked. |
| 4 | `sampleEditor.h:426`, `fSampleEdit.cpp:1020-1024` | `parameterValueLabel[16]` is filled with unbounded `sprintf("%.2f")` plus a unit `strcat`. Built-in ranges fit, but a `.ptfx` plugin declares its own min/max, and a large float there overflows the buffer. Plugin-supplied data can corrupt memory. | Verified by reading |
| 5 | `bar.cpp:271` | The `LeftRight_24_24` branch computes the fill as if the range were 0..100, so negative values would draw outside the bar. It's currently unused, so this is latent. | Verified by reading |
| 6 | `display.cpp:640` | When the display-list budget is exceeded, the remaining controls are silently dropped | Verified (design issue, not a typo) |
| 7 | `fSamplePlay.cpp:1922` | The play-mode list value is `playMode + sample.type`. For wavetable samples this may select the Granular index. | Uncertain. Needs hardware or a deeper read. |

---

## Open questions

1. **Hardware:** which of the three devices do I have for testing? Display-list budget results apply to all three, but performance on the MK66 needs to be checked separately.
2. **Arc vs bar per screen:** the columns are about 97 × 394 px. An arc only uses about the top quarter. I could fill the rest with a larger value readout, or keep bars as the default and use arcs only where columns are short (performance mode, popups).
3. **Upstream or fork:** if this is meant to go upstream to `polyend/tracker-ce`, the Discussion needs to happen before phase 2. Phases 0-1 are uncontroversial and could go up as normal PRs.
