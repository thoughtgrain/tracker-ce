# Audio DSP host tests

```bash
make -C tests/audio                 # build and run (plain g++, AddressSanitizer + UBSan, -Werror)
make -C tests/audio print-goldens   # print current hashes: only after an INTENDED behaviour change
```

These tests compile the **real** firmware sources (`core/libs/Audio/effect_limiter.cpp` and `filter_variable.cpp` today) on a desktop and pin their exact output with golden hashes. They exist so that "this optimization is bit-exact" is something a test checks rather than something a PR claims. The limiter and filter are the ones chunks A1 and A2 in [`docs/design/optimization-plan.md`](../../docs/design/optimization-plan.md) change.

## How it works

- **`shim/host_prelude.h`** is force-included before every source file. It pre-defines the include guards of the two headers that can't build on a desktop and supplies what the code under test needs from them:
  - `dspinst.h`: ARM DSP intrinsics as inline asm. The shim has exact C++ versions of the ones used (`SSAT` with shift, `SMMULR`), written from the instruction definitions.
  - `mtStructs.h`: the project struct, whose size checks fail on a 64-bit host. The limiter reads five settings, so the shim has a minimal stand-in.
- **`stream_stub.cpp`** defines `AudioStream`'s block plumbing (`allocate`, `release`, `transmit`, `receive*`). The real class declaration is used unchanged. Tests `feed()` blocks in, call `update()` and `take()` what came out. Blocks are reference counted like the firmware's, and every test checks that no blocks leak.
- **`Fresh<T>`** constructs each audio object in zero-filled storage. On the device these objects are globals in `.bss`, and some of their members (the limiter's `xpeak`, `sumGain`, `isBlocked` and output delay line) are never set by a constructor or `begin()`. They rely on starting at zero. The tests do too, on purpose.

## What the goldens cover

- **Limiter: 55 hashes.** Both modes; thresholds from 0 to full scale; fast and slow attack and release; main-stream and sidechain, with and without a key signal (the latter exercises the shared `zeroblock`); ramped sine, full-scale square, noise, quiet sine and digital silence.
  - Silence matters: `xpeak` stays 0 and the gain calculation divides by zero on purpose, relying on `inf`/`NaN` resolving to unity gain. Optimization A1 changes exactly that line.
  - Each hash covers output samples *and* the per-block average gain.
- **Filter: 25 hashes.** All three types; cutoffs across the lookup table including both ends; resonance from minimum to maximum; overdrive on and off; bypass; and type and enable changes mid-stream (the `stabilize()` and crossfade paths).

## Checks done on the harness itself

- **Stable across compilers:** all 80 hashes are identical with GCC `-O0`, `-O2` and `-O3` and Clang `-O2`. They don't depend on optimizer choices.
- **Catches changes:** five deliberate one-line mutations of the real sources were each caught. Each one made between 5 and 28 checks fail:
  - release coefficient swapped for attack;
  - delay line one tap short;
  - gain clamp 1.0 changed to 0.999;
  - bandpass returning lowpass;
  - output shift off by one.

## What a passing run does and doesn't prove

- **It proves:** the C++ before and after a change computes the same samples under plain IEEE single-precision float and exact ARM integer intrinsics. For a refactor (locals instead of members, hoisting a branch, skipping a divide whose result was clamped away), that's the property that matters.
- **It doesn't prove that the device output is unchanged, in two cases:**
  - **Fused multiply-add.** ARM GCC at `-O2` may fuse `a*b + c` into one VFMA instruction (both FPUs have it), which rounds once instead of twice. The host build uses `-ffp-contract=off`, so it never fuses. If a change rearranges float expressions, the device may fuse differently than before even though the host hashes match. Keep float expression *shapes* identical in a bit-exact change, or confirm on hardware.
  - **Math library differences.** Values from `powf`/`sinf`/`expf` (filter cutoff table, limiter attack/release coefficients) come from glibc here and newlib on the device. The goldens are host-only reference values, not device recordings. That's fine for before/after comparisons, which is all they're for.

## Adding a DSP class

1. Add `<name>_test.cpp` here, compile the real source file in the `Makefile`, and add an empty `goldens/<name>.inc`.
2. If the source needs another `dspinst.h` intrinsic, add it to `shim/host_prelude.h` **from the ARM instruction definition**, and add a mutation check that would catch it being wrong.
3. Run `make print-goldens`, save the output into the `.inc` file, then check the goldens are stable across `-O0`/`-O2` and catch at least one deliberate mutation before committing.
