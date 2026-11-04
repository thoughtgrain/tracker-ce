# ACID Synth Engine (Tracker+ & Tracker Mini)

ACID is an acid bass and lead voice: a saw and pulse oscillator with a sub, three resonant filters, a modulation envelope, an LFO and glide. It's built for squelchy 303-style lines and for plain fat basses.

It's an open engine, ported from OpenTracker's ACD ([`sandlbn/polyend-tracker-re`](https://github.com/sandlbn/polyend-tracker-re), Apache-2.0). I renamed it ACID because Polyend's own ACD is a different engine; the two sound different, and the name shouldn't suggest otherwise. The sound code is OpenTracker's unchanged, and it's written for the Original Tracker's chip too. Today it plays on Tracker+ and Tracker Mini; the Original Tracker needs its own synth host first (see [`docs/design/open-synth-host.md`](design/open-synth-host.md)).

---

## Signal path

saw + pulse + sub + noise → filter (12 dB, 24 dB or ladder) → amp envelope → out

- The **mod envelope** drives the filter cutoff and the pulse width.
- The **LFO** can move pitch, pulse width and cutoff.
- **Glide** slides between notes in four modes.

## Parameters

| Section | Parameters | Notes |
| :--- | :--- | :--- |
| Oscillator | Saw Mix, Square Mix, Sub Mix, Noise Mix, PW, Sub Mode, PW LFO, PW Env, Pitch LFO | Sub Mode picks one or two octaves down, as a square, a pulse or a triangle. Noise Mix above 0 goes through the filter ("Pre"); below 0 it's added after it ("Post"). |
| Filter | Cutoff, Resonance, Filter Type, Filter Env, Note Track, Filter LFO | The ladder self-oscillates at high resonance. Filter Env and Filter LFO are signed: negative values sweep down. |
| Amp Envelope | Attack, Decay, Sustain, Release | Sustain at 0 gives the classic plucked acid note. |
| Mod Envelope | Attack, Decay, Sustain, Release | Shapes the filter sweep and the pulse-width movement. |
| LFO | Rate, Wave, Retrigger | Six waves. Free runs continuously; One Shot runs once per note; Note restarts with every note. |
| Voice | Glide Mode, Glide Time, Volume | Always, Overlap, Legato and Legato Overlap. The Legato modes don't retrigger the envelopes when notes overlap, which is how acid slides are played. |

## Default macros

| Macro | What it moves |
| :--- | :--- |
| 1 Cutoff | Filter cutoff |
| 2 Resonance | Filter resonance |
| 3 Env Amount | How far the mod envelope sweeps the filter |
| 4 Decay | Mod and amp decay together: short for plucks, long for swells |
| 5 Wave | Fades from saw to square |
| 6 Glide | Glide time |

Velocity scales Volume, like the factory engines.

---

## Developer notes

| File | What's in it |
| :--- | :--- |
| [`core/synth/open/engines/Acid.cpp`](../core/synth/open/engines/Acid.cpp) | The engine, ported from OpenTracker with only names and include paths changed. It runs on all three Trackers' chips. |
| [`core/synth/engines/AcidSynthVoice.cpp`](../core/synth/engines/AcidSynthVoice.cpp) | What Tracker+ and Mini add on top: value formatters, enum names, MIDI CCs and macros. Names, defaults, ranges and the editor layout come from the open engine's tables, so they can't drift. |
| [`core/libs/images/gen_acid_badges.py`](../core/libs/images/gen_acid_badges.py) | Generates the two badges from the CHIP pill silhouette, in TORO's lettering style. |
| [`tests/acid/`](../tests/acid/), [`tests/opensynth/`](../tests/opensynth/) | Host tests: the metadata, the SDK-hosted voice against the open engine (122 cases, sample for sample), and the engine's own goldens and behaviour checks. |

**Parameter IDs are serialized in patch files.** New parameters go at the end, and existing ones never move.
