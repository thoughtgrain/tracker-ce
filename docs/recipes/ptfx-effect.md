# Recipe: add a .ptfx Sample Editor effect

How I add a Sample Editor effect as a `.ptfx` plugin: a separate binary on the SD card, so no firmware change and no firmware build.

## Before you start

- Read [`docs/modular_effects_system.md`](../modular_effects_system.md): the metadata, the parameter macros, the host API and the lifecycle.
- Start from the closest reference in `editor-fx-sdk/effects/`: `multimode_filter.c` for an in-place effect, `stereo_delay.c` for one that adds a tail, `analog_kick_gen.c` for a generator.

## Steps

1. **Source.** Copy the reference into `editor-fx-sdk/effects/<name>.c`, include `tracker_plugin.h`, and set the `PTFX_NAME`, `PTFX_FLAGS` and `PTFX_SCRATCH` header comments.
2. **Parameters.** Declare up to 12 with the parameter macros (float, int or list).
3. **State.** Reset DSP state in `plugin_init()`, and compute coefficients in `plugin_set_param()`. The firmware calls both again before every Preview and Apply.
4. **Processing.** Write `plugin_process_block()`: `int16_t` in, `int16_t` out, return the number of samples written.
5. **Build.** Run `make ptfx` from the repo root, or `editor-fx-sdk/ptfx_builder.py` for one file. Copy the `.ptfx` into `/Effects/` on the SD card.

## Pitfalls

- **Code and BSS must fit in 32 KB on the Original Tracker** (128 KB on IMX). If yours doesn't, or needs Cortex-M7 speed, add `IMX_ONLY` to `PTFX_FLAGS`.
- **Saturate, don't wrap.** Clamp to 16 bits; there's a helper macro for it in `tracker_plugin.h`.
- **Plugin code runs from the shared scratch buffer**, not your own memory. Use `PTFX_SCRATCH` for working RAM rather than large statics.

## Verify

- There's no host harness for plugins yet. If yours is DSP-heavy, a small host driver that calls the three entry points on a known buffer is worth adding (see the `host-test` recipe).
- Rung 4: on a device, preview and apply on mono and stereo samples, and on an empty slot if it's a generator. If it isn't `IMX_ONLY`, check the Original Tracker too.
