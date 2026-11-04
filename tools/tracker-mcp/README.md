# tracker-ce project MCP server

An [MCP](https://modelcontextprotocol.io) server that answers "what is this and where does it live" about the Polyend Tracker CE firmware, for AI coding tools (Claude Code, Cursor, Codex and others). The answers come from parsing **this checkout** every time a tool is called, so they follow the code instead of drifting from it.

```bash
make -C tools/tracker-mcp setup   # once: .venv + the official MCP Python SDK (Python 3.10+)
make -C tools/tracker-mcp test    # accuracy + protocol tests
```

The firmware build doesn't depend on any of this. It's opt-in developer tooling, and it lives outside the directories the firmware makefiles compile.

## Tools

| Tool | What it answers | Checked against |
| :--- | :--- | :--- |
| `project_map` | What each area of the repo is for, file counts, key files with their **current** line numbers, which `core/` directories the firmware build compiles | Anchors re-read, independent file walk |
| `list_synth_engines` | Every `InstrumentTypeId`: value, registered or not, open-source or precompiled, Model-menu position, UI name, badges, consistency warnings | Compiler (enum completeness, `kSynthTypes`) |
| `list_screens`, `find_screen(query)` | Every UI screen: slot, class, draw (`g*`) / input (`f*`) files, desktop button, Tracker Mini menu name | Independent class scan, menu vs button tables |
| `memory_map` | Per target: linker regions, output sections, and where each placement macro and plain code/data lands | GNU `ld` linking the real macros against the real scripts |
| `build_config` | Per target: effective compile flags per unit with each `-D` traced to its makefile line, overridden flags, link inputs, output `.ptf`, guard matrix | `make -n` expansion, host GCC `-dM` |
| `step_fx(query)` | Every step FX: stored id, menu letter/name, min / max / default (and randomized bounds), per target where they differ | The real `getFxMin`/`getFxMax`/`getFxDefault` compiled and run |
| `display_controls` | Control classes, ids, stages, create sites, budgets, free style bits, RAM_G layout with computed overlaps | Compiler (ids, constants, style bits, image `sizeof`) |
| `where_is(symbol)` | Definition and declaration sites of a name or `Class::name`, each with the targets that compile it | Locations the other tools derive, per-target preprocessor, mutation (comments and strings) |
| `test_suites` | Host test suites, how to run them, which firmware sources each compiles | The real suites from their branches |

The remaining planned tools (interrupt context, rules) are in [`docs/design/ai-dev-guidance-plan.md`](../../docs/design/ai-dev-guidance-plan.md).

Some tools need build tools on the machine running them: `build_config` and `test_suites` run `make -n` (nothing is built; the tests check the tree is untouched), and the accuracy tests need `g++` and GNU binutils (`ld`, `objdump`).

## Why you can trust an answer (and when you can't)

- **Everything carries provenance.** Every fact has a `file:line` (`where`, `declared` or `position`). Open it before relying on a detail.
- **Parsers fail loudly.** If the source doesn't look the way a parser expects (an enum renamed, a table restructured), the tool returns an error naming the file. It doesn't return a partial or guessed answer. An error means "go read the source", not "nothing there".
- **One hand-written file.** The only hand-written facts are the area descriptions in `tracker_mcp/data/project_map.json`. Each key file there names a symbol, the tool finds its line live, and a missing symbol is an error. Line numbers are never stored.

The tests check accuracy five ways (`tests/`):

| Layer | Example |
| :--- | :--- |
| **Compiler ground truth** | A probe compiled against the real `config.h` switches over every enumerator the parser found, with `-Werror=switch`. If the parser missed one, the compiler rejects the probe. Its printed values must match. `Synth::kSynthTypes` is checked the same way. |
| **Independent re-derivation** | The screen list must equal the set of `class cX : public ModuleBase` found by a separate scan. The Mini menu table and the button table must agree on which slot each button opens. |
| **Mutation** | Add an engine, rename a menu entry, or drop a UI name in a temporary copy of the source, and the answer must change. A restructured table must raise, not mislead. |
| **Anchor checks** | Every `file:line` in every answer exists, and every curated anchor's line really contains its symbol. |
| **Protocol** | The official SDK client starts the server over stdio, lists the tools and calls each one. The wire answers must equal the direct calls. |

The tests were themselves checked by planting bugs in copies of the parsers (dropping an enumerator, an off-by-one implicit value, a dropped screen). Each planted bug made the suite fail.

## Using it from an editor or agent

The server speaks MCP over stdio. Run it with the venv's Python, from this directory:

```bash
tools/tracker-mcp/.venv/bin/python -m tracker_mcp.server   # cwd: tools/tracker-mcp
```

**Claude Code** (from the repo root):

```bash
claude mcp add tracker-ce --scope project \
  --env PYTHONPATH=tools/tracker-mcp --env TRACKER_CE_ROOT=. \
  -- tools/tracker-mcp/.venv/bin/python -m tracker_mcp.server
```

Other MCP clients take the same command, `PYTHONPATH` and `TRACKER_CE_ROOT`. A checked-in `.mcp.json` comes in phase 4.

`TRACKER_CE_ROOT` is optional: without it the server finds the checkout by walking up from its own location.
