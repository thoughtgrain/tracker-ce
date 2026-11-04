# Working in this repo (rules for AI coding agents and the people using them)

This is firmware for three music instruments: the Original Polyend Tracker, Tracker+ and Tracker Mini. A bug here doesn't show up as a red test. It shows up as a click in someone's set, a project that won't load, or a firmware update that bricks a Tracker until they find the recovery pinhole. So I care more about changes being *provably safe* than about how much they do.

These rules apply to any tool: Claude Code, Codex, Cursor, Copilot, Gemini or anything else. They sit on top of [`CONTRIBUTING.md`](CONTRIBUTING.md) and the [developer guide](docs/developer_guide.md); read those for the full background. The parts that trip agents up are repeated here, with the reasons. Directories with their own pitfalls have a short `AGENTS.md` of their own (section 8); most tools read the one nearest the file you're editing.

---

## 0. Start here: look things up, don't guess

[`tools/tracker-mcp`](tools/tracker-mcp/README.md) is an MCP server that answers "what is this and where does it live" by parsing this checkout. Every answer carries `file:line` provenance, and its tests check the answers against the compiler, the linker and make. Set it up once with `make -C tools/tracker-mcp setup`. If your tool can't use MCP, run the same functions from Python, or read the files the answers cite.

**Change protocol.** Each step, and the tool that answers it:

| Step | Question | Use |
| :--- | :--- | :--- |
| 1. Locate | Where does this live? What is it called in the code? | `project_map`, `find_screen`, `where_is`, `list_synth_engines`, `step_fx` |
| 2. Scope | What may this change touch, and what depends on it? | `where_is` for definitions, then grep the callers. `interrupt_context` to see whether anything you touch runs in the audio interrupt. `build_config` for which devices compile it. |
| 3. Budget | Will it fit? | `memory_map` (where code and data land, per device), `display_controls` (display-list budget, free style bits, graphics memory) |
| 4. Rules | What's specific to this area? | `rules`, which serves this file and the area `AGENTS.md` files |
| 5. Verify | Which test proves the invariant? | `test_suites`, then the ladder in section 6 |

If a tool returns an error, the source has changed shape in a way it doesn't understand. Read the files it names; don't guess around it.

---

## 1. Scope every change before writing code

Every change, whether a fix, an optimization or a feature, starts with these four things. Write them in the PR description:

1. **Fence.** The files this change is allowed to touch. If the work starts pulling in a file outside the fence, stop and split it into another change. "While I'm here" edits are how unrelated behaviour breaks.
2. **Blast radius.** Every caller of each function you change, and every reader or writer of each variable or struct you touch, **found by grep, not from memory**. Include shared state, `#ifdef` branches for other devices, and serialized data. Re-run the grep right before you start; code moves.
3. **Invariant.** The one thing that must not change, and how you'll show it didn't. Usually one of:
   - **Bit-exact audio:** same input → same output samples.
   - **Timing:** same ticks per block, MIDI clock offsets within ±1 µs, step 1 on the downbeat with external clock.
   - **Byte-identical files:** projects, `.pti`, `.mtp`, `.pts` and exports read and write exactly as before.
   - **Visually identical:** same pixels, and the same display-list bytes or fewer.

   If a change can't keep its invariant, it's a **behaviour change**, not a refactor or optimization. Say so, and expect a listening or visual test.
4. **Verification.** The cheapest check from the ladder in [section 6](#6-verification-ladder) that actually proves the invariant.

One concern per PR. Several small PRs beat one big one, even when they touch the same area. The optimization plan on the `perf/optimization-plan` branch is a worked example: every chunk there has a fence, a blast radius, an invariant and a verification step.

**Before starting, check which files the open PRs touch.** If your fence overlaps one, either wait for it to merge or say in your PR that it depends on that one. Don't silently rebase someone else's change into yours.

---

## 2. Real-time and interrupt rules (these cause audible bugs)

**More runs in interrupt context than you'd guess.** `TrackerImpl::updateAudioBuffers` (`core/source/Tracker.cpp`) runs in the audio DMA interrupt on both platforms. It calls:
- `midiUpdate()`, which means **every MIDI input handler runs in the interrupt**;
- `sequencer.tick()`, which means **the whole sequencer runs in the interrupt**;
- and then the audio engine.

In anything reachable from those (audio `update()`, `renderVoiceAudio()`, sequencer tick and step code, MIDI handlers):
- **No SD card access.** No `fileSystem->`, no `newFileManager.*Now()` load or save, no FatFs. It pre-empts the main loop's own SD access mid-operation. If you need it, set a flag and do the work in a main-loop `update()`.
- **No heap allocation or freeing.** No `new`, `delete`, `malloc`, `std::vector::push_back`, `std::string` or `fmt::format`.
- **No blocking.** No waits, busy-loops on peripherals or `delay`.
- **No UI calls.** Don't call `display.*`, `mtPopups.*` or screen refresh functions from the interrupt; they race with the main loop.
- **No `double`.** Both CPUs have **single-precision-only FPUs** (`-mfpu=fpv4-sp-d16` on MK66, `-mfpu=fpv5-sp-d16` on IMX). Every `double` operation, including an unsuffixed literal like `0.5` or `/ 50.0`, becomes a slow software library call. Use `0.5f`, `sinf` and `powf`. The one exception is the sequencer's tick accumulator, which is deliberately `double` for drift and must stay that way.
- **No library calls you expect to be cheap.** `core/` builds with `-fno-builtin`, so `memset`, `memcpy`, `floorf` and `sqrtf` are real function calls.

A function that's fine in the main loop can be wrong in the interrupt. When you change a function, check whether *any* caller is interrupt context (that's part of the blast radius). `interrupt_context` traces this from the interrupt entry points and shows the call path. It's a text-based search, so it can miss calls through function pointers: treat "not found" as "check the callers yourself", never as proof.

---

## 3. Compatibility rules (these break people's projects)

- **Never reorder, renumber or remove serialized enum values** such as `InstrumentTypeId` in `synth/include/PolySynth/config.h`, pattern FX IDs or synth parameter IDs. Append only. Projects and patches store the integers.
- **Never move, resize or reorder fields** in project, pattern, instrument, patch or `mtConfig` structs. Several have `static_assert`s on their size (e.g. `strInstrument` is 372 bytes); treat those as contracts, not obstacles.
- **Loading old files must keep working.** If a format changes, the old one still loads, as pattern format v6 does.

---

## 4. Hardware facts that shape code

- **Three devices, one `core/`.** Use the guards in [developer guide §7](docs/developer_guide.md#7-conditional-compilation-targeting-specific-platforms-or-devices): `PLATFORM_IMX` / `!PLATFORM_IMX`, `TRACKER_MINI`, `FEATURE_SYNTH` (IMX only), and `#if GUI_HANDLE_SYNTH` (always defined, so never use `#ifdef`). Never use `__MK66FX1M0__` in `core/`: it's defined on both platforms.
- **Memory is tight on Tracker+ and Mini.** The developer guide (§1) measured about 21 KB of fast data RAM and about 22 KB of fast code RAM (ITCM) free. Those figures come from a build and shrink with every feature, so re-check the ARM build's memory report rather than trusting them. Big or cold buffers go in `EXTERNAL_RAM` (`__NOINIT`), which isn't zeroed at startup.
- **Hot code placement:** `AUDIO_CORE_RAM` puts code in ITCM on IMX. On MK66, both `AUDIO_CORE_RAM` and `AUDIO_CORE_FLASH` land in flash, so that choice only matters on IMX. `memory_map` shows where every placement macro, and plain code and data, lands on each device.
- **The display budget is 8,172 bytes per frame**, shared by every visible control (`core/libs/NewDisplay/display.cpp`). Past it, the remaining controls are **silently not drawn**. Count the display-list bytes a new or changed control emits.
- **SD reads and writes** are much faster when the buffer pointer is 4-byte aligned. The NXP driver drops to one 512-byte sector per command otherwise.
- **`core/makefile` compiles every `.cpp` under `core/source`, `core/libs`, `core/interface`, `core/effector`, `core/FileManager`, `core/Exporter`, `core/AudioEngine` and `core/synth`.** Host tests, tools and experiments must live outside those directories (e.g. `tests/<area>/`, `tools/`), or they get compiled into the firmware.

---

## 5. Editing hygiene

- **Preserve line endings.** About 40 % of `core/` sources use CRLF and a few mix CRLF and LF. Many editors and agent edit tools quietly normalize the whole file, which turns a 2-line fix into a 2,000-line diff. Check `git diff --stat` after every edit: if the line count is far bigger than your change, restore the original endings before committing.
- **Formatting:** run `clang-format` with `core/.clang-format` on **the lines you changed only**. Never reformat whole files.
- **Comments explain *why*.** The constraint, the invariant or the bug it prevents. Don't narrate what the code obviously does.
- **No drive-by fixes.** Note them in the PR, or in a plan doc like the optimization plan, and do them as their own change.

---

## 6. Verification ladder

Use the cheapest rung that proves your invariant, and say in the PR which one you used.

| Rung | What it proves | How |
| :--- | :--- | :--- |
| 1. Host syntax check | It compiles against the real headers | `g++ -std=gnu++20 -fsyntax-only` with the makefile's include paths |
| 2. Host tests | Logic, bit-exact output, golden hashes | `test_suites` lists the suites in this checkout, how to run each, and which firmware files each one compiles. The pattern: render the real firmware code on the host (plain `g++`, AddressSanitizer + UBSan + `-Werror`) and compare with golden hashes from a reference. The audio DSP harness and the TORO suite follow it; both are on their branches until they merge. |
| 3. ARM build | It builds for every device, and the memory report still fits | `make all` with the GNU Arm toolchain `10.3-2021.10`, or the PR's CI run. Read the `Memory region` lines in the summary. |
| 4. Hardware smoke test | It boots and the touched screens and features work | Flash the CI `.ptf`. Note which device(s) in the PR's hardware table. |
| 5. Hardware measurement | Performance and timing claims | `Profiler` audio load (wraps every audio block), the DEBUG-build `lastMemoryUsed` (display-list bytes the last frame used, against the 8,172 budget), wall-clock for file operations, a MIDI monitor or logic analyser for clock jitter |
| 6. Listening or visual test | Behaviour changes | Before/after on the same project, written up step by step in the PR so others can repeat it |

Rules for using the ladder:
- **Never claim a rung you didn't run.** "Not compiled for ARM yet" and "not tested on hardware" are fine to write, and expected. Fill in the PR template's hardware table honestly; most contributors don't own all three devices.
- **Performance claims need a before and after number** from rung 5, or an explicit "estimated, not measured".
- **Audio changes that claim bit-exactness need rung 2.** If no host test covers that code yet, adding one is part of the change.

---

## 7. Commits and PRs

- **No AI `Co-authored-by:` trailers.** `CONTRIBUTING.md` requires that commit authors and co-authors are human, and CI fails any PR whose commits credit Copilot, Claude, Cursor, Anthropic, OpenAI, Gemini, Windsurf, Aider, CodeRabbit, Devin or a `[bot]`. Strip them before pushing. Saying in the PR description that a tool helped is fine and welcome.
- **Use the PR template** (`.github/PULL_REQUEST_TEMPLATE.md`): goal, behaviour changes, target devices, honest hardware testing table, step-by-step test instructions, checklist.
- **Discuss first** for UI redesigns, workflow changes or anything that moves muscle memory: a GitHub Discussion or a Backstage topic, per the README's guiding principles.
- **Licensing:** contributions are under the repository [`LICENSE`](LICENSE) (MIT, with the wholesale-clone restriction for the complete firmware). Polyend may relicense contributed changes. Third-party code needs an entry in `NOTICE.md`.
