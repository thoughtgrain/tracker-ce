# Host tests

Everything in here runs on a desktop, not on a Tracker. Each folder is one suite that compiles real firmware code with the host's compiler and checks its behaviour.

```bash
make -C tests            # run every suite
make -C tests/sequencer  # run one
```

| Suite | What it covers |
| :--- | :--- |
| [`sequencer/`](sequencer/README.md) | The sequencer: notes, step FX and MIDI output, per device configuration |
| [`ptfx-drum-loop/`](ptfx-drum-loop/README.md) | The Drum Loop Gen `.ptfx` effect |
| [`ptfx-synth-loop/`](ptfx-synth-loop/README.md) | The Synth Loop Gen `.ptfx` effect |

## This branch is meant to become its own repository

I keep the harness on the `test-harness` branch for now, with the plan of moving it into a repository of its own later. A few rules keep that move cheap. Please hold to them when adding a suite.

1. **Nothing outside `tests/` changes on this branch.** No firmware file is edited to make it testable. If a file can't build on a desktop, a force-included shim in the suite deals with it.
2. **A suite reaches the firmware through one variable.** The sequencer suite uses `FW_ROOT` (default `../..`). After the move that becomes a path to a firmware checkout or a submodule:
   ```bash
   make -C sequencer FW_ROOT=/path/to/tracker-ce
   ```
   The two `ptfx` suites predate this and still use a hard-coded `ROOT := ../..`. They'll need the same treatment before the move.
3. **No dependencies beyond `g++` and `make`.** No test framework is vendored or downloaded.
4. **A suite is self-contained.** Its shims, stand-ins and runner live in its own folder. When a second suite needs the same stand-ins, that's the moment to lift them into a shared folder, not before.

What the move will need that isn't here yet: the firmware revision each golden hash was taken against (today that's implied by the branch), and a CI job that checks out the firmware and runs `make -C tests`.

## Bug reproductions live here before their fix does

A suite can hold a `KNOWN_BUG` case: a test for the behaviour I want, for a bug that isn't fixed yet. It's expected to fail and doesn't fail the run. The fix's PR flips it to an ordinary test. See the sequencer suite's README.
