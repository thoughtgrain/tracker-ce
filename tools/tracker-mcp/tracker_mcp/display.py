"""Display controls and FT812 graphics-memory layout.

- Control classes (subclasses of cDisplayControl), the control id and render-stage count each passes to
  cDisplayControl, where each draws (::update) and every createControl<...> site that instantiates it.
- Budgets: control slots, slot size, the per-frame display-list budget, style bits still free.
- RAM_G layout per target: the static bitmap table (addresses, byte counts, checked against each image
  array and stride x height), control display-list slots and the image buffer, with overlaps computed.

Everything is parsed from core/libs/NewDisplay and core/libs/images; overlaps and mismatches are computed
from those numbers, not written down anywhere.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .source import ParseError, Source, parse_array, parse_enum, parse_enum_at, _int_expr
from .targets import RELEASE_UNDEFINED, TARGET_DEFINES

ND = "core/libs/NewDisplay"
STRUCTS_H = f"{ND}/displayStructs.h"
DISPLAY_CPP = f"{ND}/display.cpp"
FT81X_H = "core/libs/FT812/FT81x.h"
IMAGES = "core/libs/images"


def _const(src: Source, name: str, known: dict[str, int]) -> tuple[int, str]:
    m = src.find(rf"\bconst\s+\w+\s+{name}\s*=\s*([^;]+);", f"const {name}")
    value = _int_expr(m.group(1), known)
    if value is None:
        raise ParseError(f"{src.at(m.start())}: can't evaluate {name} = {m.group(1)}")
    return value, src.at(m.start())


def _classes(root: Path) -> dict[str, dict[str, Any]]:
    """cDisplayControl subclasses (direct and indirect), their control id / stage count and where each is
    implemented, reading every file as each target compiles it (e.g. arrangement.cpp is entirely inside
    #ifdef TRACKER_ARRANGEMENT, which no build defines)."""
    paths = sorted((root / ND).glob("*.h")) + sorted((root / ND).glob("*.cpp"))
    plain = [Source.load(root, str(p.relative_to(root))) for p in paths]
    bases: dict[str, tuple[str, str]] = {}
    for src in plain:
        for m in re.finditer(r"\bclass\s+(\w+)\s*:\s*public\s+(\w+)", src.code):
            bases[m.group(1)] = (m.group(2), src.at(m.start()))
    classes: dict[str, dict[str, Any]] = {}
    changed = True
    while changed:
        changed = False
        for name, (base, where) in bases.items():
            if name not in classes and (base == "cDisplayControl" or base in classes):
                classes[name] = {"class": name, "base": base, "declared": where, "implemented_for": []}
                changed = True

    for target, defines in TARGET_DEFINES.items():
        for p in paths:
            src = Source.load(root, str(p.relative_to(root)), defines, RELEASE_UNDEFINED)
            for name, c in classes.items():
                ctor = re.search(rf"(?:\b{name}::)?\b{name}\s*\([^)]*\)\s*:\s*(\w+)\s*\(\s*([^,)]*)\s*(?:,\s*([^)]+?))?\s*\)",
                                 src.code)
                if ctor and ctor.group(1) == c["base"]:
                    if target not in c["implemented_for"]:
                        c["implemented_for"].append(target)
                    if c["base"] == "cDisplayControl" and "control_id" not in c:
                        c.update({"control_id": ctor.group(2).strip(),
                                  "stages": (ctor.group(3) or "").strip() or None,
                                  "constructor": src.at(ctor.start())})
                upd = re.search(rf"\b{name}::update\s*\(\s*(?:void)?\s*\)\s*\{{", src.code)
                if upd and "draws_in" not in c:
                    c["draws_in"] = src.at(upd.start())
    for c in classes.values():
        if c["base"] != "cDisplayControl" and c["base"] in classes:
            c["control_id_from"] = c["base"]
            c["control_id"] = classes[c["base"]].get("control_id")
        if not c["implemented_for"]:
            c["note"] = "declared, but no build compiles a constructor for it (implementation disabled by #if)"
    return classes


def _create_sites(root: Path) -> dict[str, list[str]]:
    sites: dict[str, list[str]] = {}
    for path in sorted((root / "core").rglob("*.cpp")):
        rel = str(path.relative_to(root))
        if rel.startswith(ND):
            continue
        src = Source.load(root, rel)
        for m in re.finditer(r"createControl\s*<\s*(\w+)\s*>", src.code):
            sites.setdefault(m.group(1), []).append(src.at(m.start()))
    return sites


def _image_array_bytes(root: Path, name: str) -> tuple[int | None, str | None, str | None]:
    """(bytes, file:line, kind) for the array a bitmap entry points at: static image data, or a RAM buffer
    filled at runtime (e.g. the boot logo, decompressed at startup)."""
    pattern = re.compile(rf"\b{re.escape(name)}\s*\[\s*(\d*)\s*\]\s*(=\s*\{{|\{{|;)")
    for path in sorted((root / "core").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        m = pattern.search(text)
        if not m:
            continue
        where = f"{path.relative_to(root)}:{text.count(chr(10), 0, m.start()) + 1}"
        if m.group(2) == ";":
            return (int(m.group(1)) if m.group(1) else None), where, "runtime buffer"
        if m.group(1):
            return int(m.group(1)), where, "static data"
        body = text[m.end() : text.index("}", m.end())]
        body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
        return len(re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", body)), where, "static data"
    return None, None, None


def _image_loads(root: Path, known: dict[str, int]) -> list[dict[str, Any]]:
    """Every readImgFromSd(path, address, max_w, max_h) call: where the JPEG is decoded in RAM_G and the
    largest region it can occupy. CMD_LOADIMAGE without OPT_MONO decodes to RGB565 (2 bytes per pixel)."""
    loads = []
    for path in sorted((root / "core").rglob("*.cpp")):
        rel = str(path.relative_to(root))
        src = Source.load(root, rel)
        for m in re.finditer(r"\breadImgFromSd\s*\(\s*[^,()]+,\s*([^,()]+?)\s*,\s*([^,()]+?)\s*,\s*([^,()]+?)\s*\)",
                             src.code):
            if re.search(r"\buint8_t\s+(?:cDisplay::)?readImgFromSd", src.code[max(0, m.start() - 40) : m.start() + 15]):
                continue  # the declaration/definition, not a call
            args = [m.group(1), m.group(2), m.group(3)]
            values = []
            for a in args:
                v = _int_expr(a, known)
                if v is None:
                    v = _global_int(root, a)
                if v is None:
                    raise ParseError(f"{src.at(m.start())}: can't resolve readImgFromSd argument {a!r}")
                values.append(v)
            address, w, h = values
            loads.append({"where": src.at(m.start()), "address": address, "address_expr": args[0],
                          "max_width": w, "max_height": h, "decoded_max_bytes": w * h * 2,
                          "start": address, "end": address + w * h * 2})
    return loads


def _global_int(root: Path, name: str) -> int | None:
    """Value of a global integer initialized with a literal, e.g. `uint32_t ImageViewerJpegAddress = 350000;`."""
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        return None
    for path in sorted((root / "core").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        m = re.search(rf"^\s*(?:const\s+)?(?:uint\d+_t|int\d+_t|int|unsigned)\s+{name}\s*=\s*(\d+)\s*;",
                      path.read_text(encoding="utf-8", errors="replace"), re.M)
        if m:
            return int(m.group(1))
    return None


_BPP = {"L1": 1, "L2": 2, "L4": 4, "L8": 8, "RGB565": 16, "ARGB4": 16, "ARGB1555": 16, "RGB332": 8,
        "ARGB2": 8, "PALETTED": 8}


def _bitmaps(root: Path, target: str) -> tuple[list[dict[str, Any]], list[str]]:
    src = Source.load(root, DISPLAY_CPP, TARGET_DEFINES[target], RELEASE_UNDEFINED)
    rows = parse_array(src, r"\bstrBitmap\s+bitmaps\s*\[\s*\w+\s*\]", "bitmaps[]")
    out, warnings = [], []
    for item, where in rows:
        m = re.fullmatch(r"\{\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,?\s*\}", item)
        if not m:
            raise ParseError(f"{where}: unexpected bitmaps[] row {item!r}")
        name, address, nbytes, w, h, fmt, stride = m.groups()
        address, nbytes, w, h, stride = int(address), int(nbytes), int(w), int(h), int(stride)
        array_bytes, array_where, kind = _image_array_bytes(root, name)
        row = {"name": name, "address": address, "bytes": nbytes, "width": w, "height": h, "format": fmt,
               "stride": stride, "image_array_bytes": array_bytes, "image": array_where, "image_kind": kind,
               "where": where}
        if array_bytes is None:
            warnings.append(f"{where}: image array {name} not found under core/")
        elif nbytes > array_bytes:
            warnings.append(f"{where}: {name} uploads {nbytes} bytes but its array has {array_bytes} "
                            "(reads past the end of the array)")
        elif nbytes < array_bytes:
            warnings.append(f"{where}: {name} uploads {nbytes} of its {array_bytes} bytes")
        if stride * h != nbytes:
            warnings.append(f"{where}: {name} stride {stride} x height {h} = {stride * h}, but bytes = {nbytes}")
        if fmt in _BPP and (w * _BPP[fmt] + 7) // 8 > stride:
            warnings.append(f"{where}: {name} is {w} px of {fmt} ({_BPP[fmt]} bpp) but stride is only {stride}")
        out.append(row)
    return out, warnings


def display_controls(root: Path) -> dict[str, Any]:
    structs = Source.load(root, STRUCTS_H)
    known: dict[str, int] = {}
    consts = {}
    for name in ("controlsCount", "controlsRamStartAddress", "controlsRamAddressStep", "imgRamAddress",
                 "imgSizeMax"):
        value, where = _const(structs, name, known)
        known[name] = value
        consts[name] = {"value": value, "where": where}

    disp = Source.load(root, DISPLAY_CPP)
    budget = disp.find(r"controlMemoryUsed\s*\+\s*memoryUsed\s*<\s*(\d+)", "the frame display-list budget check")
    ft = Source.load(root, FT81X_H)
    dl = ft.find(r"#define\s+FT_DL_SIZE\s+\(?\s*(\d+)\s*\*\s*(\d+)L?\s*\)?", "FT_DL_SIZE")
    ramg = ft.find(r"#define\s+RAM_G_SIZE\s+(\d+)\s*\*\s*(\d+)L?", "RAM_G_SIZE")
    ram_g_size = int(ramg.group(1)) * int(ramg.group(2))

    ids = [e for e in parse_enum(structs, "enControlId")]
    style_m = structs.find(r"\benum\s+controlsStyle\s*\{", "enum controlsStyle")
    styles = parse_enum_at(structs, style_m.end() - 1, "controlsStyle")
    used_bits = 0
    for e in styles:
        used_bits |= e.value
    free_bits = [b for b in range(32) if not used_bits >> b & 1]

    classes = _classes(root)
    sites = _create_sites(root)
    id_names = {e.name for e in ids}
    warnings = []
    controls = []
    for name, c in sorted(classes.items()):
        c = dict(c)
        c["created_at"] = sites.get(name, [])
        c["instances_created_in_code"] = len(c["created_at"])
        if c.get("control_id") and c["control_id"] not in id_names and c["implemented_for"]:
            warnings.append(f"{c['constructor']}: {name} uses {c['control_id']}, which isn't in enControlId")
        controls.append(c)
    unused_ids = sorted(id_names - {c.get("control_id") for c in controls})

    slots_end = known["controlsRamStartAddress"] + known["controlsCount"] * known["controlsRamAddressStep"]
    loads = _image_loads(root, known)
    layout = {}
    for target in TARGET_DEFINES:
        bitmaps, bmp_warnings = _bitmaps(root, target)
        regions = [
            {"name": "bitmaps (static table)", "start": min(b["address"] for b in bitmaps),
             "end": max(b["address"] + b["bytes"] for b in bitmaps), "where": bitmaps[0]["where"]},
            {"name": "control display-list slots", "start": known["controlsRamStartAddress"], "end": slots_end,
             "where": consts["controlsRamStartAddress"]["where"]},
        ]
        for ld in loads:
            regions.append({"name": f"decoded image ({ld['where']})", "start": ld["start"], "end": ld["end"],
                            "where": ld["where"]})
        overlaps = []
        for i, a in enumerate(regions):
            for b in regions[i + 1 :]:
                lo, hi = max(a["start"], b["start"]), min(a["end"], b["end"])
                if lo < hi:
                    overlaps.append({"regions": [a["name"], b["name"]], "start": lo, "end": hi, "bytes": hi - lo})
        for r in regions:
            if r["end"] > ram_g_size:
                overlaps.append({"regions": [r["name"], "end of RAM_G"], "start": ram_g_size, "end": r["end"],
                                 "bytes": r["end"] - ram_g_size})
        slot = lambda i: (known["controlsRamStartAddress"] + i * known["controlsRamAddressStep"],  # noqa: E731
                          known["controlsRamStartAddress"] + (i + 1) * known["controlsRamAddressStep"])
        slots_hit = {ld["where"]: [i for i in range(known["controlsCount"])
                                   if slot(i)[0] < ld["end"] and ld["start"] < slot(i)[1]] for ld in loads}
        for b in bitmaps:
            for other in bitmaps:
                if other is not b and b["address"] < other["address"] < b["address"] + b["bytes"]:
                    bmp_warnings.append(f"{b['where']}: {b['name']} overlaps {other['name']} at {other['address']}")
        layout[target] = {
            "ram_g_bytes": ram_g_size,
            "fonts": "loaded from address 0 upward; their size depends on the fonts installed (runtime), so "
                     "the gap below the bitmap table can't be checked statically",
            "regions": regions,
            "overlaps": overlaps,
            "image_loads": loads,
            "control_slots_overlapped_by_image_loads": slots_hit,
            "bitmaps": bitmaps,
            "warnings": bmp_warnings,
        }
        for where, hit in slots_hit.items():
            if hit and target == "plus":  # identical on every target; report once
                warnings.append(f"{where}: a decoded image here can occupy control display-list slots "
                                f"{hit[0]}-{hit[-1]}; if that many controls exist while the image is shown, "
                                "they overwrite each other (slots are assigned first-free)")
        warnings += [f"{target}: {w}" for w in bmp_warnings]

    return {
        "controls": controls,
        "control_ids": [{"id": e.name, "value": e.value, "where": e.where} for e in ids],
        "control_ids_without_a_class": unused_ids,
        "declared_but_unused_constants": {
            k: consts[k] for k in ("imgRamAddress", "imgSizeMax")
            if not any(re.search(rf"\b{k}\b", p.read_text(encoding="utf-8", errors="replace"))
                       for p in (root / "core").rglob("*.cpp"))},
        "budgets": {
            "control_slots": consts["controlsCount"],
            "slot_bytes": consts["controlsRamAddressStep"],
            "frame_display_list_budget_bytes": {"value": int(budget.group(1)), "where": disp.at(budget.start())},
            "ft812_display_list_bytes": {"value": int(dl.group(1)) * int(dl.group(2)), "where": ft.at(dl.start())},
            "style_bits_free": free_bits,
        },
        "ram_g_layout": layout,
        "warnings": warnings,
        "notes": [
            "Each control renders its display-list fragment into its own RAM_G slot; every frame appends the "
            "visible fragments, and once frame_display_list_budget_bytes is reached the remaining controls "
            "are skipped for that frame (display.cpp).",
            "stages is how many render passes the control needs (update(), update2(), ...).",
            "Parameter semantics can't go in style bits (style_bits_free); pass a struct through setData().",
            "readImgFromSd guards with the compressed file size, not the decoded size; image_loads gives the "
            "decoded worst case (max_width x max_height x 2 bytes, RGB565).",
        ],
    }
