"""tracker-ce project MCP server (stdio).

Answers "what is this and where does it live" about the Polyend Tracker CE firmware from the source itself.
Every answer is parsed live from the checkout, carries file:line provenance, and errors out rather than
guessing when the source doesn't look the way a parser expects. Read-only: no writes, builds or network.

Run:  python -m tracker_mcp.server     (from tools/tracker-mcp, with the venv active)
"""

from __future__ import annotations

from typing import Any

from mcp.server.mcpserver import MCPServer

from . import project_map as _project_map
from . import screens as _screens
from . import synth as _synth
from .source import find_repo_root

server = MCPServer(
    "tracker-ce",
    instructions=(
        "Facts about the Polyend Tracker CE firmware source, parsed live from this checkout. "
        "Start with project_map for orientation. Every answer includes file:line provenance: open those "
        "lines before relying on a detail. If a tool returns an error, the source has changed shape; "
        "read the files it names instead of guessing."
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
def list_synth_engines() -> dict[str, Any]:
    """Every InstrumentTypeId with its numeric value, whether it's registered, open-source or precompiled,
    its position in the synth Model menu, and its UI name and badges. Includes consistency warnings
    (e.g. a registered engine missing from the menu)."""
    return _synth.list_synth_engines(_root())


@server.tool()
def list_screens() -> dict[str, Any]:
    """All UI screens (modules): Interface::modules slot, class, directory, draw (g*) and input (f*) files,
    the desktop button that opens each one, and its name in the Tracker Mini module menu."""
    return _screens.list_screens(_root())


@server.tool()
def find_screen(query: str) -> dict[str, Any]:
    """Find screens by any name: menu name ("Sample Editor"), class (cMasterParams), instance, directory
    or button (interfaceButtonMaster). Case-insensitive substring match."""
    return _screens.find_screen(_root(), query)


def main() -> None:
    server.run("stdio")


if __name__ == "__main__":
    main()
