"""Synth engines: every InstrumentTypeId, whether and how it's registered, its place in the synth model
menu, and its UI name and badges. All derived from the source on each call."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .source import ParseError, Source, parse_array, parse_enum, unquote

CONFIG_H = "synth/include/PolySynth/config.h"
REGISTRY = "core/synth/registry.cpp"
SYNTH_H = "core/synth/Synth.h"
SYNTH_EDITOR = "core/interface/instrumentEditor/SynthEditor.cpp"
PATCH_EDITOR = "core/interface/instrumentEditor/SynthPatchEditor.cpp"
ENGINES_DIR = "core/synth/engines"

FX_ID_BASE = 1000  # config.h: "Keep FX IDs at 1000+"


def _registrations(root: Path) -> tuple[dict[str, dict[str, str]], set[str]]:
    src = Source.load(root, REGISTRY)
    regs: dict[str, dict[str, str]] = {}
    for m in re.finditer(
        r"addInstrumentType\s*\(\s*static_cast<int>\s*\(\s*InstrumentTypeId::(\w+)\s*\)\s*,\s*(\w+)\s*\(\s*\)",
        src.code,
    ):
        regs[m.group(1)] = {"factory": m.group(2), "where": src.at(m.start())}
    if not regs:
        raise ParseError(f"{REGISTRY}: no addInstrumentType(...) registrations found")
    precompiled = set(re.findall(r"extern\s+std::unique_ptr<InstrumentType>\s+(\w+)\s*\(\s*\)", src.code))
    return regs, precompiled


def _open_source_engines(root: Path) -> dict[str, dict[str, str]]:
    """factory function -> {file, name, where} for engines implemented in core/synth/engines."""
    found: dict[str, dict[str, str]] = {}
    for path in sorted((root / ENGINES_DIR).glob("*.cpp")):
        src = Source.load(root, str(path.relative_to(root)))
        for m in re.finditer(r"std::unique_ptr<InstrumentType>\s+(\w+)\s*\(\s*\)\s*\{", src.code):
            name_m = re.search(r'\.name\s*=\s*"([^"]*)"', src.code[m.end() :])
            found[m.group(1)] = {
                "file": src.rel,
                "where": src.at(m.start()),
                "engine_name": name_m.group(1) if name_m else None,
            }
    return found


def list_synth_engines(root: Path) -> dict[str, Any]:
    cfg = Source.load(root, CONFIG_H)
    enum = parse_enum(cfg, "InstrumentTypeId")
    regs, precompiled = _registrations(root)
    open_src = _open_source_engines(root)

    synth_h = Source.load(root, SYNTH_H)
    menu = [
        (re.sub(r"^.*InstrumentTypeId::", "", item), where)
        for item, where in parse_array(synth_h, r"\bkSynthTypes\b", "Synth::kSynthTypes")
    ]
    editor = Source.load(root, SYNTH_EDITOR)
    names = [(unquote(i), w) for i, w in parse_array(editor, r"\bsynthModelNames\b", "synthModelNames")]
    mini = [(i.split("::")[-1], w) for i, w in parse_array(editor, r"\bsynthModelListImages\b", "mini badges")]
    patch = Source.load(root, PATCH_EDITOR)
    medium = [(i.split("::")[-1], w) for i, w in parse_array(patch, r"\bsynthModelListImages\b", "medium badges")]

    warnings: list[str] = []
    for label, arr in (("synthModelNames", names), ("mini badges", mini), ("medium badges", medium)):
        if len(arr) != len(menu):
            warnings.append(f"{label} has {len(arr)} entries but Synth::kSynthTypes has {len(menu)}")

    by_name = {e.name: e for e in enum}
    for m, where in menu:
        if m not in by_name:
            warnings.append(f"{where}: kSynthTypes lists {m}, which isn't an InstrumentTypeId")

    engines = []
    aliases = []
    for e in enum:
        if e.alias_of:
            aliases.append({"id": e.name, "value": e.value, "alias_of": e.alias_of, "where": e.where})
            continue
        reg = regs.get(e.name)
        factory = reg["factory"] if reg else None
        if factory in open_src:
            implementation = {"kind": "open-source", **open_src[factory]}
        elif factory in precompiled:
            implementation = {"kind": "precompiled", "library": "synth/lib/libPolySynth.a"}
        elif factory:
            implementation = {"kind": "unknown", "note": f"{factory}() is neither declared extern nor defined"}
            warnings.append(f"{reg['where']}: factory {factory}() not found")
        else:
            implementation = {"kind": "not registered"}

        menu_index = next((i for i, (m, _) in enumerate(menu) if m == e.name), None)
        entry: dict[str, Any] = {
            "id": e.name,
            "value": e.value,
            "category": "fx" if e.value >= FX_ID_BASE else "instrument",
            "declared": e.where,
            "registered": reg is not None,
            "registration": reg,
            "implementation": implementation,
            "menu_index": menu_index,
        }
        if menu_index is not None:
            entry["menu"] = {
                "position": menu[menu_index][1],
                "ui_name": names[menu_index][0] if menu_index < len(names) else None,
                "mini_badge": mini[menu_index][0] if menu_index < len(mini) else None,
                "medium_badge": medium[menu_index][0] if menu_index < len(medium) else None,
            }
        if reg and menu_index is None and e.value < FX_ID_BASE and e.name != "kMidiOut":
            warnings.append(f"{e.name} is registered but not in Synth::kSynthTypes, so the UI can't select it")
        engines.append(entry)

    return {
        "engines": engines,
        "aliases": aliases,
        "warnings": warnings,
        "sources": [CONFIG_H, REGISTRY, SYNTH_H, SYNTH_EDITOR, PATCH_EDITOR, ENGINES_DIR],
        "notes": [
            "InstrumentTypeId values are stored in projects and patches: append new ids, never reorder.",
            "menu_index is the engine's position in Synth::kSynthTypes (the Model list); "
            "synthModelNames and both badge arrays are indexed the same way.",
        ],
    }
