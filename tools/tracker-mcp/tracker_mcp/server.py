"""tracker-ce project MCP server (stdio).

Two tools: a map of where things live, and the rules and build recipes for the code you're about to
touch. Everything else, read from the code: the map and the recipes say which files to open. Read-only: no writes, builds or network.

Run:  python -m tracker_mcp.server     (from tools/tracker-mcp, with the venv active)
"""

from __future__ import annotations

from typing import Any

from mcp.server.mcpserver import MCPServer

from . import project_map as _project_map
from . import rules as _rules
from .source import find_repo_root

server = MCPServer(
    "tracker-ce",
    instructions=(
        "Orientation for the Polyend Tracker CE firmware. Start with project_map to find the area and its key "
        "files, then read the code. Before editing, call rules with the file's path; to build something new "
        "(a synth engine, an open synth engine for all three devices, a UI screen, a step FX, a .ptfx effect, "
        "a host test), call rules with recipe=<name>."
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


def main() -> None:
    server.run("stdio")


if __name__ == "__main__":
    main()
