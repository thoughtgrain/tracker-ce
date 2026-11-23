# Sequencer host tests

`make -C tests/sequencer` compiles the **real** sequencer (`core/source/mtSequencer.cpp` and `mtSequencer_patternEdit.cpp`, unmodified) with the host's compiler, runs it on a desktop, and checks what it sends. It builds three times, once per device: Original Tracker, Tracker+ and Tracker Mini.

I built it so a sequencer fix can be proven without flashing a Tracker. A test writes steps into a pattern, runs time forward one audio block at a time, and reads back a log of every note, FX and MIDI message the sequencer handed to the rest of the firmware.

```bash
make -C tests/sequencer                 # build and run, all three device configurations
make -C tests/sequencer plus            # one configuration: og, plus or mini
make -C tests/sequencer print-goldens   # fresh golden hashes: only after an INTENDED change
build/plus/test 16101                   # run only the cases whose name or bug id matches
```

Needs `g++` with C++20 and `make`. Nothing else: no test framework to install.

## How it works

- **The code under test is the firmware's own.** Four files are compiled straight from `core/source`: the two sequencer sources, `scales.cpp` and `util_math.cpp` (the firmware's random generator, so randomness is seedable and repeatable).
- **`shim/host_prelude.h`** is force-included ahead of every file. It does two things, and both are places where the host build differs from the device:
  - It switches off the struct size asserts in `mtStructs.h`. They assume 4-byte pointers and fail on a 64-bit desktop.
  - It replaces the ARM interrupt-mask instructions with a counter, so a test can check every "disable" has its "enable".
- **`support/firmware_stubs.cpp`** stands in for everything the sequencer calls but the suite doesn't build: the audio engine, MIDI out, the file manager and the UI. That's about 70 functions. Each one either records an event or returns a fixed, commented value. The list is exactly what the sequencer leaves undefined, so if the firmware gains a dependency the link fails and names it.
- **`support/harness.h`** is what a test uses: `reset()`, `setNote()`, `setFx()`, `runSteps()`, `events()`.
- **`support/testing.h`** is a small runner with two kinds of case:
  - `TEST` must pass.
  - `KNOWN_BUG("id", ...)` describes the behaviour I want for a bug that isn't fixed yet. It's expected to fail and doesn't fail the run. When the fix lands it starts passing, the run fails with "now passes", and the fix's PR changes it to a `TEST`. The reproduction is in the tree from the day the bug is found.

## Adding a case

Put a `.cpp` in `cases/`. The Makefile picks it up.

```cpp
TEST("a note is sent on the step it was written on")
{
    seqtest::reset();                 // known state: 120 BPM, internal clock, 16 steps
    seqtest::setNote(0, 4, 62, 0);    // track 0, row 4, note 62, instrument 0
    sequencer.playPattern();
    seqtest::runSteps(8);

    const auto notes = seqtest::eventsOfKind(seqtest::Event::kSampleNoteOn);
    CHECK_EQ(notes.size(), 1);
    CHECK_EQ(notes[0].step, 4);
}
```

Each event carries the audio block, step and microstep it was sent on, so timing is checkable too.

## What it proves, and what it doesn't

It proves **logic and tick-level timing**: which events the sequencer sends, in what order, on which step and microstep, for all three device configurations.

It doesn't prove:

- **Sound.** The audio engine is a stand-in. A note-on is logged, not rendered.
- **File layout.** Struct sizes are the host's, not the device's (see the shim). Byte-identical project files need the ARM build.
- **The ARM compiler.** This is `g++` on a desktop at `-O1`, with sanitizers. It isn't the firmware's toolchain or flags.
- **Real interrupts.** Ticks run one after another on one thread. A race between the audio interrupt and the main loop can't show up here.
- **Anything behind a stand-in.** Pattern loads "finish" instantly, no synth patch is loaded, no screen is active. Song mode, performance mode and external clock aren't exercised yet; the stand-ins for them are minimal and will need filling in when the first case needs them.

## Checks on the harness itself

`cases/basics.cpp` tests the harness before anything else relies on it: notes land on their steps, the pattern loops, a step lasts 125 ms at 120 BPM, there are 24 MIDI clocks per quarter note, MIDI instruments go to MIDI out, and two runs from the same seed give the same event log.

One check from the host-test recipe is still to do: changing a constant in the code under test and seeing the golden fail. That needs an edit to a firmware file, so I've left it for when the first fix is reviewed.
