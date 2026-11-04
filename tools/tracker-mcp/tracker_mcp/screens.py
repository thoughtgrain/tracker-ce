"""UI screens (modules): which instance sits in each Interface::modules slot, its class, directory and draw
(g*) / input (f*) files, the hardware button that opens it, and its name in the Mini's module menu."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .source import ParseError, Source, parse_array, unquote

INTERFACE_CPP = "core/interface/core/interface.cpp"
MENU_CPP = "core/interface/core/interfaceMenuPopup.cpp"
INTERFACE_DIR = "core/interface"


def _instance_classes(root: Path) -> dict[str, dict[str, str]]:
    """instance name -> {class, header, directory} from `extern cX instance;` in core/interface headers."""
    found: dict[str, dict[str, str]] = {}
    for path in sorted((root / INTERFACE_DIR).rglob("*.h")):
        rel = str(path.relative_to(root))
        src = Source.load(root, rel)
        for m in re.finditer(r"\bextern\s+(c\w+)\s+(\w+)\s*;", src.code):
            found[m.group(2)] = {
                "class": m.group(1),
                "header": src.at(m.start()),
                "directory": str(path.parent.relative_to(root)),
            }
    return found


def list_screens(root: Path) -> dict[str, Any]:
    iface = Source.load(root, INTERFACE_CPP)
    slots = parse_array(iface, r"Interface::modules\s*\[\s*\w*\s*\]", "Interface::modules[]")
    buttons = parse_array(iface, r"Interface::modulesButtons\s*\[\s*\w*\s*\]\s*\[\s*\d+\s*\]", "modulesButtons[]")

    menu_src = Source.load(root, MENU_CPP)
    names = [(unquote(i), w) for i, w in parse_array(menu_src, r"\bkModuleNamesList\s*\[\s*\w*\s*\]", "kModuleNamesList")]
    menu_rows = parse_array(menu_src, r"InterfaceMenuPopup::modules\s*\[\s*\w*\s*\]", "InterfaceMenuPopup::modules[]")
    count_m = menu_src.find(r"kMenuModulesCount\s*=\s*(\d+)", "kMenuModulesCount")
    menu_count = int(count_m.group(1))

    warnings: list[str] = []
    if not (len(names) == len(menu_rows) == menu_count):
        warnings.append(
            f"kMenuModulesCount={menu_count}, kModuleNamesList has {len(names)}, menu table has {len(menu_rows)}"
        )

    classes = _instance_classes(root)

    # slot index -> buttons that open it (with the mode passed to the module, 0 = default)
    slot_buttons: dict[int, list[dict[str, Any]]] = {}
    for item, where in buttons:
        m = re.fullmatch(r"\{\s*(\w+)\s*,\s*(\d+)\s*,\s*(\w+)\s*\}", item)
        if not m:
            raise ParseError(f"{where}: unexpected modulesButtons row {item!r}")
        slot_buttons.setdefault(int(m.group(2)), []).append(
            {"button": m.group(1), "mode": m.group(3), "where": where}
        )

    # slot index -> Mini module-menu entries
    slot_menu: dict[int, list[dict[str, Any]]] = {}
    for idx, (row, where) in enumerate(menu_rows):
        m = re.search(r"modules\s*\[\s*(\d+)\s*\]\s*,\s*(\w+)", row)
        if not m:
            raise ParseError(f"{where}: unexpected menu row {row!r}")
        slot_menu.setdefault(int(m.group(1)), []).append(
            {
                "menu_index": idx,
                "name": names[idx][0] if idx < len(names) else None,
                "button": m.group(2),
                "where": where,
            }
        )

    screens = []
    for slot, (item, where) in enumerate(slots):
        if item == "nullptr":
            screens.append({"slot": slot, "empty": True, "where": where})
            continue
        m = re.fullmatch(r"&\s*(\w+)", item)
        if not m:
            raise ParseError(f"{where}: unexpected Interface::modules entry {item!r}")
        instance = m.group(1)
        info = classes.get(instance)
        if info is None:
            raise ParseError(f"{where}: no `extern cX {instance};` found under {INTERFACE_DIR}")
        directory = root / info["directory"]
        cpps = sorted(p.name for p in directory.glob("*.cpp"))
        screens.append(
            {
                "slot": slot,
                "instance": instance,
                "class": info["class"],
                "declared": info["header"],
                "directory": info["directory"],
                "draw_files": [f for f in cpps if f.startswith("g")],
                "input_files": [f for f in cpps if f.startswith("f")],
                "other_files": [f for f in cpps if not f.startswith(("g", "f"))],
                "buttons": slot_buttons.get(slot, []),
                "mini_menu": slot_menu.get(slot, []),
                "where": where,
            }
        )
        if not slot_buttons.get(slot) and not slot_menu.get(slot):
            screens[-1]["note"] = "no dedicated button or menu entry: reached from another screen"

    return {
        "screens": screens,
        "warnings": warnings,
        "sources": [INTERFACE_CPP, MENU_CPP, INTERFACE_DIR],
        "notes": [
            "g*.cpp files build and draw a screen; f*.cpp files handle its buttons, encoder and pads.",
            "Buttons are desktop (Original / Tracker+) hardware buttons; the Tracker Mini reaches the same "
            "screens through its module menu and Fn keys (mini_menu).",
        ],
    }


def find_screen(root: Path, query: str) -> dict[str, Any]:
    q = query.strip().lower()
    if not q:
        raise ValueError("query is empty")
    data = list_screens(root)
    matches = []
    for s in data["screens"]:
        if s.get("empty"):
            continue
        haystack = [s["instance"], s["class"], s["directory"]]
        haystack += [b["button"] for b in s["buttons"]]
        haystack += [m["name"] or "" for m in s["mini_menu"]]
        if any(q in h.lower() for h in haystack):
            matches.append(s)
    return {"query": query, "matches": matches, "warnings": data["warnings"]}
