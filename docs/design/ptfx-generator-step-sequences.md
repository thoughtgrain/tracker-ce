# Short step sequences in `.ptfx` generators (16–24 steps)

**Status:** research only, nothing built. Branch `research/ptfx-step-sequences`, off `main` at `43d39ba`.
**Subject:** Aisjam's generator collection, [`Aisjam/polyendtracker_effects`](https://github.com/Aisjam/polyendtracker_effects) at `722cc8a`. **Devices:** all three. `.ptfx` runs on the Original too, which is the collection's main target.

## The question

Could these generators render a short sequence (16 to 24 steps) instead of a single sound? For example, a bassline, an arp phrase or a drum loop baked into one sample. What would it take?

## Short answer

**We can't modify his generators. We can make sequences work for all of them from our side.**

- The repo ships **compiled `.ptfx` files only, with no source code and no licence file.** We have nothing to rebuild, and without a licence we shouldn't patch or redistribute his binaries. Changing the generators themselves is Aisjam's call.
- What we can do is a **"Render sequence" mode in our Sample Editor.** It plays any generator once per step with a parameter changed, places each render on a timeline, and saves the result as one sample. That works for his 46 generators, Polyend's reference ones, and anything written later, with no changes to them.
- Separately, I can describe a small **in-plugin pattern** he could adopt if he wants it. That's his choice to make, and it's easy to suggest on his repo.

## What I found in his collection

- **61 plugins:** 46 generators (21 synth, 10 drum-kit, 15 percussion) and 15 effects and converters.
- **They fit our host.** Every header is valid, and the largest asks for 361 KB of working memory, inside the 384 KB `main` now allows.
- **Up to 12 parameters each,** which is the format's limit. Most already use 8 to 12, so there's no room to add 24 steps of note data as ordinary parameters.
- **32 of the 46 generators have a pitch-like parameter** (Note, Pitch, Tune, Root and similar). Those are the ones a melodic sequence makes sense for.
  - The other 14 are mostly **wavetable generators** (`*_WT`): they render waveform frames that the instrument then plays at any pitch, so sequencing them doesn't apply.
  - Two drum kits (7 and 8) have no pitch parameter but could still be sequenced by kit slot.

## What our host allows today

| Thing | Where | Matters because |
| :--- | :--- | :--- |
| Output length | `mtEffectModular::getExpectedProcessLength` (`core/effector/mtEffectModular.cpp`) | A generator defaults to 88,200 samples (2 s). It can ask for more through `set_extra_samples(0, n)`, up to the edit buffer: 1.875 MB per buffer on IMX and about 0.94 MB on the Original, roughly 10 s of mono there. **24 steps of 16ths at 90 BPM is 4 s, which fits.** `docs/modular_effects_system.md` still says 2 s is the maximum, so the docs are out of date. |
| Step data | `PARAM_TEXT` (64 chars), `PARAM_FILE` with any extension, `read_file` (`editor-fx-sdk/tracker_plugin.h`) | A 24-step pattern fits in one text parameter, one character per step, or in a small `.seq` file. A plugin could read either. |
| Render cost | his "coffee guide" | Heavy generators already take 30 s to minutes per render on paper. One render per step would multiply that. |

## Option A: "Render sequence" in our Sample Editor (recommended)

A mode on the Sample Editor's plugin screen, available for any generator:

1. **Choose the step parameter.** Defaults to the generator's pitch-like parameter if it has one. Any parameter works, so you could sequence Decay, Kit slot or Fold. It works like a parameter lock.
2. **Edit the steps.** 16 to 24 steps on the pads (two rows of 12), each with a value for that parameter, a gate length and on/off. There's a row for BPM, and swing if wanted.
3. **Render.** For each active step:
   - reset the plugin;
   - set its parameters, with the step's value on the chosen parameter;
   - render until it stops or hits a tail limit;
   - mix that render into the output at the step's position.

   Overlapping tails are summed and saturated to 16 bits. The output length is steps × step length plus the last tail, requested with the same `set_extra_samples` route.
4. **Apply** saves it like any other render. Preview plays it first.

**Why it suits the Tracker:** a sequence rendered to a sample is just a sample. It loops, slices and goes into patterns, and later into tape tracks, on all three devices, including the Original, which has no live synths.

**What it touches:**
- `core/effector/mtEffectModular.{h,cpp}` (a render loop over steps);
- `core/interface/sampleEditor/` (the step screen);
- possibly `ptfx_manager` (remembering the step settings per plugin).

**Blast radius:** the Sample Editor's plugin path only. Rendering runs from the Sample Editor's main-loop `update()`, not the audio interrupt, so heap and SD rules are the usual main-loop ones. **To verify:** grep that `mtEffectModular` processing really is main-loop only.

**Costs and risks:**
- **Render time grows with the steps.** A 24-step render of a heavy generator could take a long time on the Original, so it needs a progress bar and a way to cancel.
- **Memory:** the output lives in the existing edit buffer. No new buffers.
- **Display budget:** the step page has to fit in 8,172 bytes per frame. Count it.
- **Assumption:** this relies on generators being deterministic from `plugin_init`. Ones using `rand()` will vary per step, which may be welcome. A "Seed per step" toggle could help.

## Option B: inside each generator (Aisjam's side)

If he wants sequences in the plugins themselves, the cheap pattern is:
- one `PARAM_TEXT` parameter for the pattern, one character per step (for example `0`–`9`/`a`–`z` as scale degrees, `.` for a rest, `-` for a tie);
- a `Steps` parameter (16–24) and a `BPM` parameter;
- a render loop that runs his existing per-note voice once per step into the output, after asking for the length with `set_extra_samples`.

The costs: it uses 2–3 of his 12 parameter slots, which most generators have already used, and every generator needs the change. Option A gives everyone the same thing without either cost, so I'd offer B to him as an idea rather than build it.

## Option C: what already works with no changes

Render one hit or note, then sequence it with a Tracker pattern, which is the Tracker's whole job. Sequences baked into samples are only worth it for loops you want to slice, resample, play from tape, or use on the Original as a "synth line" sample.

## Proposed PR split (Option A)

1. **Engine.** Step render in `mtEffectModular`, driven by a fixed test pattern, plus a host test that renders a reference generator (`chiptune_gen.c`) as a sequence. *Invariant:* single renders stay byte-identical to today.
2. **Step screen.** Choosing the parameter, 16–24 steps on the pads, BPM, preview and apply.
3. **Docs.** Fix the out-of-date 2 s limit in `docs/modular_effects_system.md`, and document sequence rendering.

## Open questions for you

1. Is baking sequences into samples the goal, or did you mean live sequences that play while the Tracker plays? Live would need the synth engines, which is the other research doc on `research/engine-step-sequences`, and isn't possible on the Original.
2. Should the step parameter be pitch only to start, or any parameter from day one?
3. Do you want me to open a friendly issue on Aisjam's repo about Option B, or keep this to our side?
