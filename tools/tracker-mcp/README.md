# tracker-ce project MCP server

A small [MCP](https://modelcontextprotocol.io) server for AI coding tools (Claude Code, Cursor, Codex and others) working on the Polyend Tracker CE firmware. It doesn't try to catalogue the code. It tells an agent **where to look** and **how things are built here**, then gets out of the way so the agent reads the real source.

```bash
make -C tools/tracker-mcp setup   # once: .venv + the official MCP Python SDK (Python 3.10+)
make -C tools/tracker-mcp test    # about 15 s, Python only
```

The firmware build doesn't depend on any of this. It's opt-in tooling, and it lives outside the directories the firmware makefiles compile.

## Tools

| Tool | What it answers | Why it's a tool rather than a doc |
| :--- | :--- | :--- |
| `project_map` | What each area of the repo is for, key files with their current line numbers, which `core/` directories the firmware build compiles | Line numbers are found live, so they never go stale |
| `rules(path, recipe)` | With `path`: the root `AGENTS.md` plus every area `AGENTS.md` above that file. With `recipe`: a build guide from [`docs/recipes/`](../../docs/recipes) (synth engine, UI screen, step FX, `.ptfx` effect, host test) | Agents don't reliably find nested guidance files on their own |
| `interrupt_context(function)` | Whether a function runs in the audio interrupt, by which call path, and on which devices | The trace crosses about 600 files, MIDI handlers called through function pointers, and a precompiled synth library. Grep can't do that |

`interrupt_context` is a heuristic over source text, and every answer says so. "Reachable" comes with a path you can check line by line; "not reachable" isn't proof, because calls through function pointers can hide a path. It found two interrupt-rule violations nobody had listed, both recorded in `core/source/AGENTS.md`.

## How to update things

Everything here is plain text, and only one check runs on it: **every path and name a doc mentions must exist**. Reword whatever you like; nothing breaks unless you point at something that's gone.

- **Guidance (`AGENTS.md`, area `AGENTS.md`, recipes):** edit the markdown, then run `make -C tools/tracker-mcp test`. If `test_anchors` fails, it names the file and the backticked token: you either typo'd it, or the code was renamed and the doc needs to follow.
- **A new area file:** add `<dir>/AGENTS.md` and list it in section 8 of the root `AGENTS.md` (a test checks the listing). Keep it short; it gets read on every edit in that directory.
- **A new recipe:** add `docs/recipes/<name>.md`. Its first paragraph is the summary `rules` shows, and the file name is the recipe name. Name the files and symbols to touch in backticks so the existence check covers them.
- **The map:** `tracker_mcp/data/project_map.json` is the only hand-written data. Each key file names a symbol, and the tool finds its line live. If a symbol is renamed, the tool errors and names it.
- **`interrupt_context` roots:** found in the source (the platform audio callbacks, `scheduleMidiClockUs` callbacks, IRQ handlers, the MIDI handler registrations in `midiInit`). If one moves, the tool raises an error naming the file instead of quietly returning less.

## What the tests check

| Test | What it proves |
| :--- | :--- |
| `test_anchors` | Every path, `file:line`, link and code name in every `AGENTS.md` and recipe exists. A self-test feeds it planted bad tokens |
| `test_rules` | Guidance and recipes are served whole (split losslessly), area files apply only to their own subtree |
| `test_project_map` | Every key file's line really contains its symbol; file counts match an independent walk |
| `test_interrupts` | Every hop of every returned path is re-read from its cited line. The known interrupt SD-card paths are found, main-loop code isn't, and a planted call changes the answer |
| `test_protocol` | The official SDK client starts the server over stdio, lists the three tools and calls them; wire answers equal direct calls |

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

Other MCP clients take the same command, `PYTHONPATH` and `TRACKER_CE_ROOT`. Without MCP, read [`docs/recipes/`](../../docs/recipes) and the `AGENTS.md` files directly; the server adds nothing to them except finding them.

`TRACKER_CE_ROOT` is optional: without it the server finds the checkout by walking up from its own location.
