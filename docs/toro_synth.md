# TORO Synth Engine (Tracker+ & Tracker Mini)

TORO (Spanish for "bull") is a two-oscillator voice with a wavefolder, a resonant low-pass and **APHX**, a key-tracked drift that starts as old-polysynth character and ends in chaos. I originally built it as a plugin for the OP-1 and then ported it to the OP-XY ([`thoughtgrain/labyrinth`](https://github.com/thoughtgrain/labyrinth)). This is the third home for it.

The DSP is the same integer code as the OP-XY build, not a re-creation. With the OP-XY's 64-sample blocks it renders **bit-identical** output, and the test suite checks that on every build (see [Testing](#testing)). On the Tracker's 128-sample blocks, the only difference is that the cutoff glides across 128 samples instead of 64.

---

## Signal path

```text
osc 1 ─┐
       ├─(saw → tri morph)─┬─► wavefolder ─► DC blocker ─► 2-pole SVF low-pass ─► soft-clip drive ─► amp env ─► out
osc 2 ─┘                   │
noise ─────────────────────┘
```

- **Two oscillators.** Osc 2 runs at 1.0–1.5× osc 1 (**Spread**), so the low end beats slowly and the top end is a fifth. They start a quarter cycle apart so they never cancel at unison.
- **Wavefolder.** 1–8× gain, then the signal reflects off the rails (up to five folds). Triangles fold more audibly than saws, so **Shape** and **Fold** work together.
- **DC blocker.** An asymmetric wave folds into an offset, so a ~15 Hz high-pass sits right after the folder.
- **Low-pass.** A Chamberlin state-variable filter with its coefficient capped against the damping, so it can't blow up at any setting. Cutoff has a squared response for feel.
  The cap is looser when the filter rings, so the top of the range is 5.3 kHz with no resonance and 6.5 kHz at full. The middle of the knob, where the default sits, is unaffected (about 2 kHz). The Cutoff readout includes this cap, so the number on screen is the real frequency.
- **Drive.** 1–4× into an `x − x³/3` soft clip, then scaled to 0.75 of full scale for headroom across voices.

---

## Parameters

| Section | Parameter | Range | What it does |
| :--- | :--- | :--- | :--- |
| **Osc** | Spread | `1.000x` – `1.500x` | Osc 2 frequency ratio |
| | Shape | `Saw` – `Triangle` | Morphs both oscillators |
| | Noise | `0 %` – `100 %` | Noise into the folder (100 % = a quarter of the mix) |
| **Shp** | Fold | `1.0x` – `8.0x` | Wavefolder gain |
| | Drive | `1.00x` – `4.00x` | Soft-clip drive after the filter |
| | APHX | `Off` – `100 %` | Key-tracked drift → chaos (below) |
| **Filter** | Cutoff | `64 Hz` – `5.3`–`6.5 kHz` | Base cutoff. The top end depends on resonance (see Signal path), and the readout accounts for it |
| | Resonance | `0 %` – `100 %` | |
| **Amp** | Attack / Decay / Sustain / Release | ADSR | Amp envelope. The OP-1 and OP-XY supplied their own, so this is new on the Tracker |
| **Voice** | Glide Mode | `Always` / `Overlap` / `Legato` | When glide happens, and whether legato notes retrigger the envelope |
| | Glide Time | `0` – `2 s` | |
| | Volume | `0 %` – `200 %` | Also what the Velocity macro drives |

Polyphony comes from the Tracker's own **Polyphony** setting on the Synth Parameters page, like every other engine.

### Glide modes

- **Always**: every note glides from the last one.
- **Overlap**: only notes played while another is still held glide. Separate notes jump.
- **Legato**: like Overlap, and overlapping notes don't retrigger the envelope either.

Glide is calculated per sample in pitch, not in frequency. That matters for APHX: its per-key offsets blend between neighbouring keys, so a slow glide slides through them smoothly instead of stepping at each semitone.

### Default macros

| Macro | Target |
| :--- | :--- |
| M1 Spread | Spread |
| M2 Fold | Fold (bipolar) |
| M3 Cutoff | Cutoff (bipolar) |
| M4 Resonance | Resonance (bipolar) |
| M5 APHX | APHX |
| M6 Env Decay / Rel | Decay 0.8, Release 0.6 (bipolar) |
| Velocity (system) | Volume, the same convention as CHIP and the factory engines |

M1–M4 are toro's four page-1 knobs on the OP-1, the same ones meikyu's MOD lane modulates there. That keeps the muscle memory if you've played it on either box.

---

## APHX

APHX gives every key on the keyboard its own small, fixed differences: the tuning of each oscillator, the cutoff, the fold amount, the saw/triangle shape and the resonance. The differences come from a hash of the note's position on the keyboard, so the same key always sounds the same way, the way the voices of an old polysynth each drift a little differently.

| APHX | What you hear |
| :--- | :--- |
| 0 % | The plain sound |
| 25 % | Still one sound: each key a few cents off, with a touch of filter and fold colour |
| 50 % | Keys clearly drift apart (around half a semitone), and the filter and fold move noticeably from key to key |
| 100 % | Keys retuned by up to a few semitones, the filter thrown by up to three octaves, more fold and resonance, and above 80 % each key crushed at its own sample-and-hold rate |

How it works and why:
- **It needs no memory.** The key position is worked out from the pitch every block with a cheap integer log2. That's a few multiplies per block, and only the detune and the hold run per sample.
- **Slides stay smooth.** The offsets of neighbouring keys are blended by the fraction between them.
- **It's gentle, then wild.** The amounts follow a curve that's nearly linear at the start and cubic at the end, so the bottom half stays subtle and the top quarter falls apart.

---

## Starting points

These are the presets from the OP-XY build. On the Tracker, dial them in from **Init** (which is *Clean*) and save them as patches.

| Patch | Spread | Fold | Cutoff | Res | Shape | Noise | Drive | APHX |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clean | 6 % | 18 % | 55 % | 24 % | 0 % | 0 % | 0 % | Off |
| Bull | 30 % | 60 % | 70 % | 50 % | 20 % | 10 % | 50 % | Off |
| Drift | 6 % | 25 % | 60 % | 30 % | 0 % | 0 % | 20 % | 30 % |
| Labyrinth | 50 % | 90 % | 80 % | 70 % | 50 % | 30 % | 80 % | 100 % |

---

## Developer notes

| File | What's in it |
| :--- | :--- |
| [`core/synth/engines/ToroCore.h`](../core/synth/engines/ToroCore.h) | The DSP, header-only and dependency-free. Block length is a template parameter: `render<6>` matches the OP-XY, `render<7>` is the Tracker. |
| [`core/synth/engines/ToroSynthVoice.cpp`](../core/synth/engines/ToroSynthVoice.cpp) | The PolySynth voice: parameters, formatters, ADSR, glide, pitch, editor layout and macros |
| [`core/libs/images/gen_toro_badges.py`](../core/libs/images/gen_toro_badges.py) | Generates the two L2 badges from the CHIP pill silhouette |
| [`tests/toro/`](../tests/toro/) | Host tests |

**Parameter ids are serialized in patch files.** New parameters go at the end of `ToroParamId`, and existing ones never move.

**Cost:** one voice keeps 36 bytes of DSP state plus envelope and glide state. The sample loop is integer-only with no division, and per-block work is a handful of multiplies plus two `exp2f` calls for the pitch.

### Testing

```bash
make -C tests/toro
```

This needs plain `g++` with C++20, no ARM toolchain. Both suites build with AddressSanitizer, UBSan and `-Werror`.

- **`core_test`** checks parity against golden hashes from the OP-XY reference. The 64-sample cases are every 0 / half / full combination of all 8 controls at 3 pitches, plus 4,000 random patches with gliding pitch. It also checks the output peak, brightness, DC and APHX drift at the Tracker's 128-sample blocks.
- **`voice_test`** compiles the real `ToroSynthVoice.cpp` against the real SDK headers. It checks:
  - **Metadata:** ranges, formatters, CC collisions, editor layout and macros.
  - **Sound:** the voice renders the core's output sample for sample.
  - **Behaviour:** idle silence, release, all three glide modes, legato without retrigger, pitch bend and volume.
  - **Stress:** a randomized run that must stay finite and bounded.
