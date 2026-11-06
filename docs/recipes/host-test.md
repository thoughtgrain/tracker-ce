# Recipe: add a host test for firmware code

How I test firmware logic on a desktop with plain `g++`: compile the real firmware source, run it, and compare the output with golden hashes. This is rung 2 of the verification ladder in the root `AGENTS.md`.

## Before you start

- Two suites follow this pattern, each still on its own branch: the audio DSP harness (tests/audio on the `perf/optimization-plan` branch) and the TORO suite (tests/toro on the `feat/toro-synth` branch). Copy the one closest to your code.
- Put the suite in `tests/<area>/`, never under `core/`. The firmware makefile compiles every `.cpp` in the core directories, so a test placed there ends up in the firmware.

## Steps

1. **Makefile** in `tests/<area>/`, with a `test` target, so `make -C tests/<area>` builds and runs it. Compile the **real** firmware `.cpp` files with `-std=gnu++20 -Wall -Wextra -Werror -fsanitize=address,undefined`. Add `-ffp-contract=off`, so the host doesn't fuse float operations the way ARM might not.
2. **Shims, only where a header can't build on a desktop.** For example ARM intrinsics or 32-bit-only structs. Force-include them (`-include shim/...`) rather than editing firmware files. The audio harness's prelude shows how.
3. **Inputs.** Make them deterministic: a fixed seed, fixed buffers, and every parameter the code reads.
4. **Goldens.** Hash the output, store the hashes in the suite, and add a `print-goldens` target. Regenerate them only for an intended behaviour change, and say so in the PR.
5. **README.** One paragraph: what the suite proves, and what it doesn't (it's not the ARM compiler, and not the hardware).

## Pitfalls

- **Bit-exact means the same code path.** If the firmware has an ARM-only branch (`#ifdef`s or intrinsics), the host test covers the other branch. Say which one you tested.
- **Don't change firmware files to make them testable** in the same PR as a behaviour change. Split the refactor out and prove it's bit-exact first.

## Verify

- `make -C tests/<area>` passes on a clean checkout. Each golden fails if you change one constant in the code under test; try it once.
