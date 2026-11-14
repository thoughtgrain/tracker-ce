# Recipe: add a .ptfx Sample Editor effect

How I add a Sample Editor effect as a `.ptfx` plugin: a separate binary on the SD card, so no firmware change and no firmware build.

## Before you start

- Read [`docs/modular_effects_system.md`](../modular_effects_system.md): the metadata, the parameter macros, the host API and the lifecycle.
- Start from the closest reference in `editor-fx-sdk/effects/`: `multimode_filter.c` for an in-place effect, `stereo_delay.c` for one that adds a tail, `analog_kick_gen.c` for a generator.

## Steps

1. **Source.** Copy the reference into `editor-fx-sdk/effects/<name>.c`, include `tracker_plugin.h`, and set the `PTFX_NAME`, `PTFX_FLAGS` and `PTFX_SCRATCH` header comments.
2. **Parameters.** Declare up to 12 with the parameter macros: float, int, list, or a file (`PTFX_PARAM_FILE` to pick an input such as a second `.wav`, `PTFX_PARAM_SAVE_FILE` for an output path). A file parameter opens the global file browser and then calls `plugin_set_param()`; read the result with the host's `get_selected_file_path`, `get_secondary_sample` or `read_file`.
3. **State.** Reset DSP state in `plugin_init()`, and compute coefficients in `plugin_set_param()`. The firmware calls both again before every Preview and Apply.
4. **Processing.** Write `plugin_process_block()`: `int16_t` in, `int16_t` out, return the number of samples written.
5. **Build.** Run `make ptfx` from the repo root, or `editor-fx-sdk/ptfx_builder.py` for one file. Copy the `.ptfx` into `/Effects/` on the SD card.

## Pitfalls

- **Code and BSS must fit in 32 KB on the Original Tracker** (128 KB on IMX). If yours doesn't, or needs Cortex-M7 speed, add `IMX_ONLY` to `PTFX_FLAGS`.
- **Saturate, don't wrap.** Clamp to 16 bits; there's a helper macro for it in `tracker_plugin.h`.
- **Plugin code runs from the shared scratch buffer**, not your own memory. Use `PTFX_SCRATCH` for working RAM rather than large statics.
- **Working scratch is 384 KB now**, down from 640 KB: the space above `kPtfxScratchTopOffset` holds the secondary sample a file parameter loads (`core/effector/ptfx_loader.h`). An effect written for the old limit may ask for more than exists.

## Verify

- There's no host harness for plugins yet. If yours is DSP-heavy, a small host driver that calls the three entry points on a known buffer is worth adding (see the `host-test` recipe).
- Rung 4: on a device, preview and apply on mono and stereo samples, and on an empty slot if it's a generator. If it isn't `IMX_ONLY`, check the Original Tracker too.
