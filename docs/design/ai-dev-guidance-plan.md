# AI Development Guidance & Project MCP: Plan

> Status: **plan + phase 1 in progress** on branch `ai/dev-guidance` (from `main` at `9cc7aae`, one commit behind upstream).

Upstream has exactly one AI rule today: no AI `Co-authored-by:` trailers, enforced by CI. Nothing tells an agent *how to work safely* in this codebase or *where things are*, and agents guess both. In this codebase, wrong guesses turn into clicks, broken projects and blown memory budgets. This plan fixes both halves:

1. **Guidance and protocols:** written rules for how changes get scoped, verified and submitted.
2. **A project MCP server:** answers "what is this and where does it live" from the source itself, so the answers can't quietly drift out of date.

The thread through both: **nothing an agent is told should be unverified.** Rules point to tests. Facts come from parsing the code, and those parsers are checked against the compiler.

---

## Part A: Guidance and protocols

### A1. Root `AGENTS.md` (exists on `perf/optimization-plan`)
It already covers:
- change scoping (fence, blast radius, invariant, verification);
- interrupt-context rules (MIDI handlers and the whole sequencer run in the audio interrupt);
- compatibility, hardware facts, line-ending preservation, the verification ladder, and the commit/PR rules.

**Plan:** this branch becomes its home. Cherry-pick it here, and drop it from `perf/optimization-plan` before that branch merges, so it only lands once. `CLAUDE.md` is one line, `@AGENTS.md`, so every tool reads the same file.

### A2. Area guidance (nested `AGENTS.md`)
Most agent tools read the closest `AGENTS.md` to the file being edited. Short area files keep the important rule next to the code it protects:

| Directory | What it adds |
| :--- | :--- |
| `core/AudioEngine/`, `core/libs/Audio/` | Per-sample cost rules, `AUDIO_CORE_RAM` placement, bit-exactness and `tests/audio` |
| `core/source/` (sequencer, MIDI) | What runs in the interrupt; the double-precision tick accumulator is deliberate; timing invariants |
| `core/FileManager/` | File-format compatibility, SD alignment, `externalRamScratchBuff` sharing rules |
| `core/libs/NewDisplay/` | The 8,172-byte frame budget, refresh queue behaviour, style bits nearly gone |
| `core/synth/engines/` | SDK rules, appending parameter IDs, host-testing engines the way TORO is tested |
| `platforms/` | MK66 and IMX differences, the HAL boundary |

Each one must be **short** (it gets read on every edit in that area), and every claim in it must pass the anchor check in Part C.

### A3. Protocols (repeatable checklists)
- **Change protocol:** locate (via the MCP), scope (fence, blast radius, invariant), change, verify (ladder rung), PR. Kept as a short section in the root `AGENTS.md` that names the MCP tools to use at each step.
- **Hardware test checklists** (`docs/testing/`): one page per area (audio, sequencer/MIDI, display, file I/O) listing what a hardware tester actually does. The PR template's "How to Test" then links to the page instead of improvising.
- **Upstream PR template:** I won't change it in the fork. If the guidance proves useful, I'd propose an optional "Scope" section upstream.

---

## Part B: Project MCP server (`tools/tracker-mcp/`)

### Design rules
1. **Facts come from parsing the source when a tool is called.** No hand-maintained tables of enum values, file lists or screen names. If the code changes, the answer changes.
2. **Every answer carries where it came from** (`file:line`), so an agent or a person can check it.
3. **Parsers fail loudly.** If a parser can't find what it expects (an enum moved, a file was renamed), the tool returns an error saying so. It never returns a partial or guessed answer.
4. **Curated prose is the exception.** Where a fact can't be derived (e.g. "this directory holds the display widgets"), it lives in one small data file, and every path or symbol it names is checked by tests.
5. **Read-only.** The server never writes files, runs builds or reaches the network.
6. **Location:** `tools/tracker-mcp/`, outside every directory the firmware makefiles glob.
   - Python, using the official `mcp` SDK (`mcp>=2.3,<3`) in its own virtual environment.
   - The firmware build itself still needs only the Python standard library.
   - I'm using the SDK rather than hand-rolling the protocol because protocol versions move (the SDK currently negotiates `2025-11-25`), and keeping up with that isn't this project's job.

### Tools (by phase)

| Phase | Tool | Answers | Derived from |
| :--- | :--- | :--- | :--- |
| 1 | `project_map` | Top-level layout: what each directory is for, with live file counts | Curated purposes + filesystem |
| 1 | `list_synth_engines` | Every `InstrumentTypeId`: numeric value, registered or not, menu position, UI name, badge, open-source file | `config.h`, `registry.cpp`, `Synth.h`, `SynthEditor.cpp`, `core/synth/engines/` |
| 1 | `find_screen` / `list_screens` | UI screens (modules): menu name, class, the `g*` (draw) and `f*` (input) files | `kModuleNamesList`, `core/interface/` |
| 2 | `step_fx` | Pattern step FX: ID, letter, name, min/max, display format | `interfaceDefs.h`, `mtSequencer_patternEdit.cpp` |
| 2 | `build_config` | Targets, defines per target, which guards are active where | `Makefile`, `core/makefile`, platform makefiles |
| 2 | `memory_map` | Linker regions per platform, section macros and where they land | Linker scripts, `config.h` |
| 2 | `display_controls` | NewDisplay control classes and where screens create them | `core/libs/NewDisplay/`, `createControl<…>` sites |
| 2 | `test_suites` | Host test suites, their commands and what they cover | `tests/*/Makefile` and READMEs |
| 3 | `interrupt_context` | Whether a function is reachable from the audio interrupt | Curated interrupt roots (anchor-checked) plus a text-based call search, **labelled heuristic** |
| 3 | `where_is` | A symbol's definition and declaration sites | Source scan |
| 3 | `rules` | `AGENTS.md` sections by topic | `AGENTS.md` |

**Resources:** `tracker://agents` (the rules) and `tracker://docs/<page>` (each `docs/*.md`).

---

## Part C: Proving the information is accurate

"Test it as we go" is the core of this. Each tool gets at least one check from each relevant layer:

1. **Compiler ground truth.** Where a fact is something the compiler knows (enum values, array sizes, the number of synth types), the test compiles a tiny host program against **the real headers** that prints it, then compares that with the tool's answer. The parser and the compiler must agree. This catches parser mistakes that a test written by the same hand wouldn't.
2. **Independent re-derivation.** Where the compiler can't help, the test derives the same fact a different way. For example, the screen list from `kModuleNamesList` is cross-checked against the module classes and `g*`/`f*` files that actually exist.
3. **Change detection (mutation).** The test runs a parser on a temporary copy of the source with a deliberate change (an added enum value, a renamed screen) and checks the answer changes. This proves nothing is hard-coded.
4. **Anchor checks** for every curated statement, in the MCP data file and in every `AGENTS.md`: each file path exists, each named symbol is defined in the named file, and each `file:line` still points at that symbol (within a few lines).
5. **Protocol tests.** The official SDK client starts the server over stdio, lists tools, calls each one, and validates the result shapes. That's the same path a real editor takes.

The suite runs with one command (`make -C tools/tracker-mcp test`) and, in phase 4, in the fork's CI, so a code change that makes the MCP or the guidance wrong fails a build instead of misleading the next agent.

---

## Phases and acceptance criteria

| Phase | Delivers | Done when |
| :--- | :--- | :--- |
| **1** | Server skeleton; `project_map`, `list_synth_engines`, `list_screens`/`find_screen`; all five test layers for those tools | All tests pass; compiler ground truth agrees with the parsers; a mutation changes each tool's answer; a real SDK client round-trips every tool |
| **2** | `step_fx`, `build_config`, `memory_map`, `display_controls`, `test_suites` | Same bar as phase 1 |
| **3** | Root `AGENTS.md` moved here, plus the change protocol naming the MCP tools; area `AGENTS.md` files; `interrupt_context`, `where_is`, `rules`; anchor checks over every `AGENTS.md` | Every path, symbol and `file:line` in guidance is checked by a test |
| **4** | `.mcp.json` registration for Claude Code, setup notes for other tools, fork CI job, hardware checklists in `docs/testing/` | A fresh clone sets up the server in two commands; CI fails on a stale fact |

## Open decisions

- **Upstream or fork:** I'd build it here, use it for the optimization chunks (real use is the best test), then offer the rules and the MCP to Polyend as separate PRs.
- **Where CI runs:** the accuracy tests need only Python and `g++`, so they can run on the existing `ubuntu-22.04` runner without the ARM toolchain.
