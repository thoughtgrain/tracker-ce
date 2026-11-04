"""Pattern step FX: every FX id (Sequencer::strFxConsts::enFxType), its place, letter and name in the FX
menu, and the min / max / default values the sequencer enforces, including the narrower ranges used when a
value is randomized.

Ranges come from parsing the switch statements in Sequencer::getFxMin / getFxMax / getFxDefault and
resolving the named constants they return from their definitions. The tests compile those three functions
verbatim from the source, run them for every id, and require the same numbers.
"""

from __future__ import annotations

import ast
import re
from pathlib import Path
from typing import Any

from .source import Enumerator, ParseError, Source, parse_array, parse_enum_at, unquote
from .targets import TARGET_DEFINES

SEQ_H = "core/source/mtSequencer.h"
RANGES_CPP = "core/source/mtSequencer_patternEdit.cpp"
MENU_H = "core/interface/core/interfaceDefs.h"
STRUCTS_H = "core/source/mtStructs.h"
CONST_DIRS = ("core/source", "core/config", "core/AudioEngine/ConstantValues")


class _Constants:
    """Resolves the named constants used in FX ranges, recursively, with provenance."""

    def __init__(self, root: Path, fx_enumerators: dict[str, Enumerator], defines: dict[str, str]):
        self.root = root
        self.defines = defines
        self.fx = fx_enumerators
        self.cache: dict[str, tuple[float, str, str]] = {}
        self.files = [p for d in CONST_DIRS for p in sorted((root / d).glob("*")) if p.suffix in (".h", ".cpp")]
        self._enum_values: dict[str, tuple[int, str]] | None = None

    def _all_enumerators(self) -> dict[str, tuple[int, str]]:
        if self._enum_values is None:
            self._enum_values = {}
            for path in self.files:
                src = Source.load(self.root, str(path.relative_to(self.root)), self.defines)
                for m in re.finditer(r"\benum\s+(?:class\s+)?(\w*)\s*(?::\s*\w+\s*)?\{", src.code):
                    try:
                        for e in parse_enum_at(src, m.end() - 1, m.group(1) or "<anonymous>",
                                               {k: v for k, (v, _) in self._enum_values.items()}):
                            self._enum_values.setdefault(e.name, (e.value, e.where))
                    except ParseError:
                        continue  # enums with initializers we can't evaluate aren't needed here
        return self._enum_values

    def resolve(self, name: str, depth: int = 0) -> tuple[float, str, str]:
        """(value, definition text, file:line) for a bare constant name."""
        if name in self.cache:
            return self.cache[name]
        if depth > 8:
            raise ParseError(f"constant {name}: definition chain too deep")
        for path in self.files:
            src = Source.load(self.root, str(path.relative_to(self.root)), self.defines)
            m = re.search(rf"(?:\bconst(?:expr)?\b[^;=(){{}}]*?)\b(?:\w+::)*{re.escape(name)}\s*=\s*([^;]+);", src.code)
            if not m:
                m = re.search(rf"#define\s+{re.escape(name)}\s+([^\n]+)", src.code)
            if m:
                expr = m.group(1).strip()
                value = self.evaluate(expr, depth + 1)
                self.cache[name] = (value, expr, src.at(m.start()))
                return self.cache[name]
        enums = self._all_enumerators()
        if name in enums:
            value, where = enums[name]
            self.cache[name] = (value, name, where)
            return self.cache[name]
        raise ParseError(f"can't find a definition for constant {name!r} in {CONST_DIRS}")

    def evaluate(self, expr: str, depth: int = 0) -> float:
        text = re.sub(r"\b(\d+(?:\.\d+)?)[fFuUlL]+\b", r"\1", expr.strip())
        text = re.sub(r"\bfx\.(\w+)", r"__fx__\1", text)
        text = text.replace("::", "__ns__")
        try:
            tree = ast.parse(text, mode="eval")
        except SyntaxError as exc:
            raise ParseError(f"can't evaluate expression {expr!r}") from exc

        def ev(node: ast.AST) -> float:
            if isinstance(node, ast.Expression):
                return ev(node.body)
            if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
                return node.value
            if isinstance(node, ast.Name):
                ident = node.id
                if ident.startswith("__fx__"):
                    member = ident[len("__fx__"):]
                    if member in self.fx:
                        return self.fx[member].value
                    return self.resolve(member, depth)[0]
                return self.resolve(ident.split("__ns__")[-1], depth)[0]
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
                return -ev(node.operand)
            if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
                a, b = ev(node.left), ev(node.right)
                return a + b if isinstance(node.op, ast.Add) else a - b if isinstance(node.op, ast.Sub) else a * b
            raise ParseError(f"unsupported expression {expr!r}")

        return ev(tree)


def _switch(src: Source, function: str) -> tuple[dict[str, tuple[str, str]], tuple[str, str] | None]:
    """case label -> (return expression, file:line) and the default, for Sequencer::<function>'s switch."""
    fm = src.find(rf"Sequencer::{function}\s*\([^)]*\)\s*\{{", f"Sequencer::{function}")
    fstart, fend = src.braces(fm.end() - 1)
    sm = re.compile(r"\bswitch\s*\([^)]*\)\s*\{").search(src.code, fstart, fend)
    if not sm:
        raise ParseError(f"{src.at(fm.start())}: no switch in {function}")
    start, end = src.braces(sm.end() - 1)
    token = re.compile(r"\bcase\s+fx\.(\w+)\s*:|\bdefault\s*:|\breturn\s+([^;]+);|[{}]|(\S+)")
    cases: dict[str, tuple[str, str]] = {}
    default = None
    pending: list[str] = []
    pending_default = False
    for m in token.finditer(src.code, start, end):
        if m.group(1):
            pending.append(m.group(1))
        elif m.group(0).startswith("default"):
            pending_default = True
        elif m.group(2) is not None:
            value = (m.group(2).strip(), src.at(m.start()))
            for label in pending:
                cases[label] = value
            if pending_default:
                default = value
            pending, pending_default = [], False
        elif m.group(0) in "{}":
            continue
        else:
            raise ParseError(f"{src.at(m.start())}: unexpected statement {m.group(0)!r} in {function}'s switch")
    return cases, default


def _variants(expr: str) -> tuple[str, str]:
    """(normal, randomized) expressions: `forRandom ? A : B` -> (B, A); anything else is the same for both."""
    m = re.fullmatch(r"\s*forRandom\s*\?\s*(.+?)\s*:\s*(.+?)\s*", expr)
    return (m.group(2), m.group(1)) if m else (expr, expr)


def _compute(root: Path, defines: dict[str, str]) -> dict[str, Any]:
    seq = Source.load(root, SEQ_H, defines)
    block = seq.find(r"\bstruct\s+strFxConsts\s*\{", "struct strFxConsts")
    bstart, bend = seq.braces(block.end() - 1)
    fx_members: dict[str, Enumerator] = {}
    type_enum: list[Enumerator] = []
    for m in re.finditer(r"\benum\s+(\w+)\s*\{", seq.code[: bend]):
        if m.start() < bstart:
            continue
        values = parse_enum_at(seq, m.end() - 1, m.group(1), {k: e.value for k, e in fx_members.items()})
        for e in values:
            fx_members[e.name] = e
        if m.group(1) == "enFxType":
            type_enum = values
    if not type_enum:
        raise ParseError(f"{SEQ_H}: enum enFxType not found inside strFxConsts")
    consts = _Constants(root, fx_members, defines)

    ranges_src = Source.load(root, RANGES_CPP, defines)
    switches = {fn: _switch(ranges_src, fn) for fn in ("getFxMin", "getFxMax", "getFxDefault")}

    menu = Source.load(root, MENU_H, defines)
    ids = [i.split("::")[-1] for i, _ in parse_array(menu, r"\bfxIDs\s*\[\s*\w+\s*\]", "fxIDs[]")]
    names = [(unquote(i), w) for i, w in parse_array(menu, r"\bfxNames\s*\[\s*\w+\s*\]\s*\[[^\]]*\]", "fxNames[]")]
    count = int(consts.resolve("FX_COUNT")[0])
    hidden = int(consts.resolve("FX_COUNT_HIDDEN_FXes")[0])

    warnings = []
    if not (len(ids) == len(names) == count):
        warnings.append(f"FX_COUNT={count}, fxIDs has {len(ids)} entries, fxNames has {len(names)}")

    def value_of(function: str, names_for_id: list[str], randomized: bool) -> dict[str, Any]:
        cases, default = switches[function]
        hit = next(((n, cases[n]) for n in names_for_id if n in cases), None)
        expr, where = hit[1] if hit else (default or (None, None))
        if expr is None:
            raise ParseError(f"{RANGES_CPP}: {function} has no case or default for {names_for_id[0]}")
        chosen = _variants(expr)[1 if randomized else 0]
        value = consts.evaluate(chosen)
        return {"value": int(value), "expr": expr, "where": where, "from_default": hit is None}

    by_value: dict[int, list[Enumerator]] = {}
    for e in type_enum:
        by_value.setdefault(e.value, []).append(e)

    effects = []
    for value, members in sorted(by_value.items()):
        primary = next((e for e in members if not e.alias_of), members[0])
        all_names = [e.name for e in members]
        menu_index = next((i for i, n in enumerate(ids) if n in all_names), None)
        entry: dict[str, Any] = {
            "id": primary.name,
            "value": value,
            "aliases": [e.name for e in members if e is not primary],
            "declared": primary.where,
            "menu_index": menu_index,
        }
        if menu_index is not None and menu_index < len(names):
            label, where = names[menu_index]
            letter, _, title = label.partition(" - ")
            entry.update({"letter": letter.strip(), "name": title.strip() or label, "name_where": where,
                          "hidden": menu_index >= count - hidden})
        for function, key in (("getFxMin", "min"), ("getFxMax", "max")):
            normal = value_of(function, all_names, False)
            randomized = value_of(function, all_names, True)
            entry[key] = normal
            if randomized["value"] != normal["value"]:
                entry[key] = {**normal, "randomized_value": randomized["value"]}
        entry["default"] = value_of("getFxDefault", all_names, False)
        effects.append(entry)

    return {"effects": effects, "menu_count": count, "hidden_at_end_of_menu": hidden, "warnings": warnings}


_PER_TARGET_FIELDS = ("menu_index", "letter", "name", "name_where", "hidden", "min", "max", "default")


def list_step_fx(root: Path) -> dict[str, Any]:
    """All step FX, as each target compiles them. Fields that differ between targets (e.g. FX names that
    mention synth macros only on Tracker+ / Mini) are listed under differs_by_target."""
    per = {t: _compute(root, d) for t, d in TARGET_DEFINES.items()}
    base_target = "plus"
    by_id = {t: {e["id"]: e for e in r["effects"]} for t, r in per.items()}
    effects = []
    for e in per[base_target]["effects"]:
        merged = dict(e)
        differs = {}
        for field in _PER_TARGET_FIELDS:
            values = {t: by_id[t].get(e["id"], {}).get(field) for t in per}
            if len({repr(v) for v in values.values()}) > 1:
                differs[field] = values
        if differs:
            merged["differs_by_target"] = differs
        effects.append(merged)
    only_elsewhere = sorted({i for t in per for i in by_id[t]} - set(by_id[base_target]))
    warnings = sorted({w for r in per.values() for w in r["warnings"]})
    if only_elsewhere:
        warnings.append(f"FX ids not present on {base_target}: {only_elsewhere}")
    counts = {t: r["menu_count"] for t, r in per.items()}
    return {
        "effects": effects,
        "menu_count": counts[base_target] if len(set(counts.values())) == 1 else counts,
        "hidden_at_end_of_menu": per[base_target]["hidden_at_end_of_menu"],
        "warnings": warnings,
        "sources": [SEQ_H, RANGES_CPP, MENU_H, STRUCTS_H],
        "notes": [
            "value is the id stored in pattern files: never renumber (the FX_TYPE_Rx placeholders exist so old "
            "patterns keep their meaning).",
            "menu_index is the position in the FX menu (fxIDs/fxNames); ids without one aren't selectable.",
            "randomized_value is the bound used when a value is randomized (getFxMin/Max with forRandom).",
            "Values are the raw stored values; some FX display them shifted (e.g. Panning shows value - 50).",
            "Unless differs_by_target says otherwise, a field is the same on original, plus and mini.",
        ],
    }


def step_fx(root: Path, query: str = "") -> dict[str, Any]:
    data = list_step_fx(root)
    q = query.strip()
    if not q:
        return data
    if len(q) == 1:
        # FX letters are case-sensitive on the Tracker: C = Chance, c = CC C, M = Micro-tune, m = Micro-move.
        matches = [e for e in data["effects"] if e.get("letter") == q]
    elif q.isdigit():
        matches = [e for e in data["effects"] if e["value"] == int(q)]
    else:
        ql = q.lower()
        matches = [e for e in data["effects"]
                   if ql in e["id"].lower() or ql in (e.get("name") or "").lower()
                   or any(ql in a.lower() for a in e["aliases"])]
    return {"query": query, "effects": matches, "notes": data["notes"], "warnings": data["warnings"]}
