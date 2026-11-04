"""tracker-ce project MCP server (stdio).

Three tools: a map of where things live, the rules and build recipes for the code you're about to touch,
and whether a function runs in the audio interrupt. Everything else, read from the code: the map and the
recipes say which files to open. Read-only: no writes, builds or network.

Run:  python -m tracker_mcp.server     (from tools/tracker-mcp, with the venv active)
"""

from __future__ import annotations

from typing import Any

from mcp.server.mcpserver import MCPServer

from . import interrupts as _interrupts
from . import project_map as _project_map
from . import rules as _rules
from .source import find_repo_root

server = MCPServer(
    "tracker-ce",
    instructions=(
        "Orientation for the Polyend Tracker CE firmware. Start with project_map to find the area and its key "
        "files, then read the code. Before editing, call rules with the file's path; to build something new "
        "(a synth engine, a UI screen, a step FX, a .ptfx effect, a host test), call rules with recipe=<name>. "
        "Use interrupt_context before changing any function that might run in the audio interrupt."
    ),
)


def _root():
    return find_repo_root()


@server.tool()
def project_map() -> dict[str, Any]:
    """Top-level layout of the firmware: what each area is for, file counts, and key files with their
    current line numbers. Also which core directories the firmware build compiles (anything placed there
    gets built into the firmware)."""
    return _project_map.project_map(_root())


@server.tool()
def rules(path: str = "", recipe: str = "") -> dict[str, Any]:
    """The contribution rules that apply to a path, verbatim from the AGENTS.md files: the root rules plus
    every area AGENTS.md in the directories above the path (nearest last), split into sections with the
    line each heading is on. Pass the file you're about to edit. Every answer also lists the build recipes;
    pass recipe=<name> (e.g. "synth-engine") to get one: the files to touch, in order, and the pitfalls."""
    return _rules.rules(_root(), path, recipe)


@server.tool()
def interrupt_context(function: str = "") -> dict[str, Any]:
    """Does a function (name or Class::name) run in interrupt context, and by which call path? Returns the
    shortest path from each interrupt root that reaches it (audio DMA callbacks, MIDI handlers dispatched by
    midiUpdate, the MIDI clock timer, IRQ handlers), every hop with the line it's called at and which devices
    compile the whole path. With no function, lists the roots and every reachable function. A text-based
    heuristic: reachable=false is never proof, so check the callers yourself."""
    return _interrupts.interrupt_context(_root(), function)


def main() -> None:
    server.run("stdio")


if __name__ == "__main__":
    main()
