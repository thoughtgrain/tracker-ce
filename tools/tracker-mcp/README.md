# tracker-ce project MCP server

A small [MCP](https://modelcontextprotocol.io) server for AI coding tools (Claude Code, Cursor, Codex and others) working on the Polyend Tracker CE firmware. It doesn't try to catalogue the code. It tells an agent **where to look** and **how things are built here**, then gets out of the way so the agent reads the real source.

```bash
make -C tools/tracker-mcp setup   # once: .venv + the official MCP Python SDK (Python 3.10+)
make -C tools/tracker-mcp test    # a few seconds, Python only
```

The firmware build doesn't depend on any of this. It's opt-in tooling, and it lives outside the directories the firmware makefiles compile.

## Tools

| Tool | What it answers | Why it's a tool rather than a doc |
| :--- | :--- | :--- |
| `project_map` | What each area of the repo is for, key files with their current line numbers, which `core/` directories the firmware build compiles | Line numbers are found live, so they never go stale |
| `rules(path, recipe)` | With `path`: the root `AGENTS.md` plus every area `AGENTS.md` above that file. With `recipe`: a build guide from [`docs/recipes/`](../../docs/recipes) (synth engine, UI screen, step FX, `.ptfx` effect, host test) | Agents don't reliably find nested guidance files on their own |

No code here parses C++, and nothing uses regular expressions. Finding a key file's line is a plain token scan. Whether something runs in the audio interrupt is answered by tracing callers by hand, from the entry points listed in section 2 of the root `AGENTS.md`.

## How to update things

Everything here is plain text, and only one check runs on it: **every path and name a doc mentions must exist**. Reword whatever you like; nothing breaks unless you point at something that's gone.

- **Guidance (`AGENTS.md`, area `AGENTS.md`, recipes):** edit the markdown, then run `make -C tools/tracker-mcp test`. If `test_anchors` fails, it names the file and the backticked token: you either typo'd it, or the code was renamed and the doc needs to follow.
- **A new area file:** add `<dir>/AGENTS.md` and list it in section 8 of the root `AGENTS.md` (a test checks the listing). Keep it short; it gets read on every edit in that directory.
- **A new recipe:** add `docs/recipes/<name>.md`. Its first paragraph is the summary `rules` shows, and the file name is the recipe name. Name the files and symbols to touch in backticks so the existence check covers them.
- **The map:** `tracker_mcp/data/project_map.json` is the only hand-written data. Each key file names a symbol, and the tool finds its line live. If a symbol is renamed, the tool errors and names it.
- **Interrupt entry points:** they're prose in section 2 of the root `AGENTS.md`. If one moves, update the list; the existence check catches a renamed function.

## What the tests check

| Test | What it proves |
| :--- | :--- |
| `test_anchors` | Every path, `file:line`, link and code name in every `AGENTS.md` and recipe exists. A self-test feeds it planted bad tokens |
| `test_rules` | Guidance and recipes are served whole (split losslessly), area files apply only to their own subtree |
| `test_project_map` | Every key file's line really contains its symbol; file counts match an independent walk |
| `test_protocol` | The official SDK client starts the server over stdio, lists the two tools and calls them; wire answers equal direct calls |

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
