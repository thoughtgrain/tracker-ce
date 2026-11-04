"""tracker-ce project MCP server (stdio).

Answers "what is this and where does it live" about the Polyend Tracker CE firmware from the source itself.
Every answer is parsed live from the checkout, carries file:line provenance, and errors out rather than
guessing when the source doesn't look the way a parser expects. Read-only: no writes, builds or network.

Run:  python -m tracker_mcp.server     (from tools/tracker-mcp, with the venv active)
"""

from __future__ import annotations

from typing import Any

from mcp.server.mcpserver import MCPServer

from . import build as _build
from . import display as _display
from . import fx as _fx
from . import interrupts as _interrupts
from . import memory as _memory
from . import project_map as _project_map
from . import rules as _rules
from . import screens as _screens
from . import suites as _suites
from . import symbols as _symbols
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


@server.tool()
def memory_map() -> dict[str, Any]:
    """Memory per target (original, plus, mini): linker regions with sizes, output sections, and where each
    placement macro (AUDIO_CORE_RAM, AUDIO_CORE_FLASH, DMAMEM, __NOINIT(EXTERNAL_RAM), ...) and plain
    code/data actually land. Use before adding static buffers or moving code into fast RAM."""
    return _memory.memory_map(_root())


@server.tool()
def build_config() -> dict[str, Any]:
    """Build configuration per target (original, plus, mini) exactly as make expands it: effective compile
    flags per compile unit (core vs platform; C/C++/asm) with each -D traced to its makefile line, flags that
    are overridden, link inputs, the output .ptf, and which #if guards used in core/ are defined per target.
    Runs `make -n` with a dummy toolchain: nothing is built or written."""
    return _build.build_config(_root())


@server.tool()
def step_fx(query: str = "") -> dict[str, Any]:
    """Pattern step FX: id (as stored in pattern files), aliases, FX-menu position, letter and name, and
    the min / max / default the sequencer enforces (plus the narrower bounds used when randomizing), per
    target where they differ. query: empty for all; one character = exact, case-sensitive FX letter
    (C = Chance, c = CC C); digits = id; otherwise a substring of the id or name."""
    return _fx.step_fx(_root(), query)


@server.tool()
def display_controls() -> dict[str, Any]:
    """Display widgets and FT812 graphics memory: every cDisplayControl subclass (control id, render stages,
    where it draws, every createControl site, which builds implement it), slot and display-list budgets,
    free style bits, and the RAM_G layout per target - bitmap table (checked against the image arrays),
    control slots, decoded-image regions - with overlaps computed."""
    return _display.display_controls(_root())


@server.tool()
def test_suites() -> dict[str, Any]:
    """Host test suites in this checkout: how to run each one and which firmware source files each compiles
    (from make's own dry run of the suite's test target). Use it to find the test that covers code you're
    changing, or to see that none does yet."""
    return _suites.test_suites(_root())


@server.tool()
def where_is(symbol: str) -> dict[str, Any]:
    """Definition and declaration sites of a symbol (a name or Class::name): types, enums, enumerators,
    functions, macros, typedefs, variables, members and externs, each with file:line and which targets
    compile it (compiled_for, from the per-target preprocessor). A comment-stripped text search, not a
    compiler: it skips uses inside function bodies, so grep for callers afterwards."""
    return _symbols.where_is(_root(), symbol)


@server.tool()
def interrupt_context(function: str = "") -> dict[str, Any]:
    """Does a function (name or Class::name) run in interrupt context, and by which call path? Returns the
    shortest path from each interrupt root that reaches it (audio DMA callbacks, MIDI handlers dispatched by
    midiUpdate, the MIDI clock timer, IRQ handlers), every hop with the line it's called at and which targets
    compile the whole path. With no function, lists the roots and every reachable function. A text-based
    heuristic: reachable=false is never proof, so check the callers yourself."""
    return _interrupts.interrupt_context(_root(), function)


@server.tool()
def rules(path: str = "") -> dict[str, Any]:
    """The contribution rules that apply to a path, verbatim from the AGENTS.md files: the root rules plus
    every area AGENTS.md in the directories above the path (nearest last), split into sections with the
    line each heading is on. Pass the file you're about to edit. With no path, the root rules and a list
    of area files."""
    return _rules.rules(_root(), path)


def main() -> None:
    server.run("stdio")


if __name__ == "__main__":
    main()
