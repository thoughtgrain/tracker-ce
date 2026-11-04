# tracker-ce project MCP server

An [MCP](https://modelcontextprotocol.io) server that answers "what is this and where does it live" about the Polyend Tracker CE firmware, for AI coding tools (Claude Code, Cursor, Codex and others). The answers come from parsing **this checkout** every time a tool is called, so they follow the code instead of drifting from it.

```bash
make -C tools/tracker-mcp setup   # once: .venv + the official MCP Python SDK (Python 3.10+)
make -C tools/tracker-mcp test    # accuracy + protocol tests
```

The firmware build doesn't depend on any of this. It's opt-in developer tooling, and it lives outside the directories the firmware makefiles compile.

## Tools (phase 1)

| Tool | What it answers |
| :--- | :--- |
| `project_map` | What each area of the repo is for, file counts, key files with their **current** line numbers, and which `core/` directories the firmware build compiles |
| `list_synth_engines` | Every `InstrumentTypeId`: value, registered or not, open-source or precompiled, position in the Model menu, UI name, badges, plus consistency warnings |
| `list_screens` | Every UI screen: `Interface::modules` slot, class, directory, draw (`g*`) and input (`f*`) files, the desktop button that opens it, its Tracker Mini menu name |
| `find_screen(query)` | Screens matching a menu name, class, instance, directory or button |

The rest of the planned tools (step FX, build config, memory map, display controls, test suites, interrupt context, rules) are in [`docs/design/ai-dev-guidance-plan.md`](../../docs/design/ai-dev-guidance-plan.md).

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
