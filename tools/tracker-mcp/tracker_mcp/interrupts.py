"""interrupt_context: does a function run in interrupt context, and by which call path?

A heuristic over source text, not a compiler call graph, and labelled as one in every answer.

Roots are found in the source on every call, each with the line that proves it:
- the platform audio callbacks (any platform function that calls `->updateAudioBuffers(` or
  `->updateInputAudioBuffers(`), which the I2S DMA interrupt invokes;
- callbacks handed to `scheduleMidiClockUs(...)` (the MIDI clock timer interrupt);
- `*_IRQHandler` / `*_isr` definitions and functions passed to `attachInterrupt(...)`.

The PolySynth library is precompiled, so `Synth::renderAudioBuffer` has no body here. A call to it links to
every override, in a class derived from `InstrumentVoice`, of a virtual method the SDK headers declare:
the library calls those from inside the render.

MIDI receive handlers are function pointers registered in `midiInit` and dispatched by the port drivers
when `midiUpdate()` reads the ports, so the graph adds an edge from `midiUpdate` to every registered
handler, citing the registration line.

Edges come from call sites in function bodies. A call through `obj.f()` / `obj->f()` is resolved from
the declared type of `obj` (local, parameter, member, file-level or extern global), including overrides
in derived classes, since any of them may be the dynamic target. A call whose receiver type can't be
worked out links only if exactly one method in the tree has that name; otherwise it's reported as
unresolved. Calls through function pointers, other than the roots and the MIDI handlers, are missed (and a
callback stored for later isn't a call), so "not reachable" is never proof.
"""

from __future__ import annotations

import bisect
import re
from collections import deque
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Any

from .source import ParseError
from .scopes import _active_lines, _files, _index
from .targets import TARGET_DEFINES

KEYWORDS = frozenset("""if for while switch return sizeof alignof decltype catch static_cast reinterpret_cast
const_cast dynamic_cast defined new delete throw case goto else do typedef using public private protected class
struct enum union namespace template typename operator default static const volatile extern constexpr inline
mutable register auto void unsigned signed noexcept static_assert __attribute__ asm __asm__ alignas""".split())
PLATFORM_DIRS = ("platforms/imx/source", "platforms/mk66/source")
SDK_DIR = "synth/include/PolySynth/sdk"
LIBRARY_ENTRY = ("synth/include/PolySynth/Synth.h", "Synth", "renderAudioBuffer")
VOICE_BASE = "InstrumentVoice"


@dataclass
class Func:
    name: str  # unqualified
    owner: str | None  # class or namespace qualifier, None for a plain free function
    rel: str
    pos: int  # offset of the name in the file
    params: tuple[int, int]  # header span holding the parameter list
    body: tuple[int, int]  # brace span

    @property
    def qualified(self) -> str:
        return f"{self.owner}::{self.name}" if self.owner else self.name


@dataclass
class Model:
    root: Path
    funcs: list[Func] = field(default_factory=list)
    by_name: dict[str, list[int]] = field(default_factory=dict)
    by_key: dict[tuple[str | None, str], list[int]] = field(default_factory=dict)
    classes: set[str] = field(default_factory=set)
    bases: dict[str, set[str]] = field(default_factory=dict)
    derived: dict[str, set[str]] = field(default_factory=dict)
    members: dict[str, dict[str, set[str]]] = field(default_factory=dict)  # class -> var -> types
    file_vars: dict[str, dict[str, set[str]]] = field(default_factory=dict)  # rel -> var -> types
    global_vars: dict[str, set[str]] = field(default_factory=dict)
    roots: list[dict[str, Any]] = field(default_factory=list)
    dispatch: dict[int, list[tuple[int, str, str]]] = field(default_factory=dict)  # fid -> (callee, rel, pos)
    library_callbacks: list[int] = field(default_factory=list)
    library_entry: str = ""
    lines: dict[str, list[int]] = field(default_factory=dict)
    _edges: dict[int, tuple[list, list]] = field(default_factory=dict)

    def line(self, rel: str, pos: int) -> int:
        return bisect.bisect_right(self.lines[rel], pos)

    def where(self, rel: str, pos: int) -> str:
        return f"{rel}:{self.line(rel, pos)}"

    def code(self, rel: str) -> str:
        return _index(str(self.root), rel, (self.root / rel).stat().st_mtime).code


def _match_paren(code: str, i: int, open_c: str = "(", close_c: str = ")") -> int:
    depth = 0
    for j in range(i, len(code)):
        if code[j] == open_c:
            depth += 1
        elif code[j] == close_c:
            depth -= 1
            if depth == 0:
                return j
    return -1


def _blank_attributes(text: str) -> str:
    out = text
    for m in reversed(list(re.finditer(r"__attribute__\s*\(", text))):
        end = _match_paren(text, m.end() - 1)
        if end > 0:
            out = out[: m.start()] + " " * (end + 1 - m.start()) + out[end + 1 :]
    return out


_NAME_IN_HEADER = re.compile(
    r"((?:[A-Za-z_]\w*\s*::\s*)*(?:~\s*)?(?:operator\s*(?:\(\)|[^\s(]+)|[A-Za-z_]\w*))\s*\(")
_CLASS_NAME = re.compile(r"\b(?:class|struct|union)\s+(?:[A-Za-z_]\w*\s+)*?([A-Za-z_]\w*)\s*(?:final\s*)?(?::(?!:)(.*))?$",
                         re.S)
_DECL = re.compile(r"(?<![\w.>:])([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)(?:\s*<[^;{}()]*?>)?"
                   r"(\s*[*&]+\s*(?:const\s+)?|\s+(?:const\s+)?)([A-Za-z_]\w*)\s*(?=[;=\[,)({:])")


def _decls(text: str) -> dict[str, set[str]]:
    """var name -> declared type names (last component), from declarations found in text."""
    out: dict[str, set[str]] = {}
    for m in _DECL.finditer(text):
        typ = re.sub(r"\s+", "", m.group(1)).split("::")[-1]
        name = m.group(3)
        if typ in KEYWORDS or name in KEYWORDS or typ == name:
            continue
        out.setdefault(name, set()).add(typ)
    return out


def _blank_children(code: str, start: int, end: int, children: list[tuple[int, int]]) -> str:
    parts, cur = [], start
    for s, e in children:
        if s < cur:
            continue
        parts.append(code[cur:s])
        parts.append(";" + " " * (e - s))
        cur = e + 1
    parts.append(code[cur:end])
    return "".join(parts)


def _signature(root: Path) -> tuple:
    return tuple((str(p.relative_to(root)), p.stat().st_mtime) for p in _files(root))


def build_model(root: Path) -> Model:
    return _cached_model(str(root), _signature(root))


@lru_cache(maxsize=4)
def _cached_model(root_s: str, sig: tuple) -> Model:
    root = Path(root_s)
    model = Model(root)
    raw_members: dict[str, list[str]] = {}
    for rel, mtime in sig:
        ix = _index(root_s, rel, mtime)
        code = ix.code
        model.lines[rel] = [0] + [m.end() for m in re.finditer("\n", code)]
        scopes = ix.scopes  # sorted by start
        parent: list[int] = []
        stack: list[int] = []
        children: dict[int, list[tuple[int, int]]] = {}
        for i, (s, e, kind, header) in enumerate(scopes):
            while stack and scopes[stack[-1]][1] < s:
                stack.pop()
            p = stack[-1] if stack else -1
            parent.append(p)
            if p >= 0:
                children.setdefault(p, []).append((s, e))
            stack.append(i)
        for i, (s, e, kind, header) in enumerate(scopes):
            p = parent[i]
            pkind = scopes[p][2] if p >= 0 else "global"
            if kind == "class":
                h = re.sub(r"\s+", " ", _blank_attributes("\n".join(
                    ln for ln in header.split("\n") if not ln.lstrip().startswith("#"))))
                h = re.sub(r"^.*\b(?:public|private|protected)\s*:\s*", "", h) if "(" not in h else h
                cm = _CLASS_NAME.search(h)
                if not cm:
                    continue
                cls = cm.group(1)
                model.classes.add(cls)
                for b in (cm.group(2) or "").split(","):
                    b = re.sub(r"<.*", "", b).strip()
                    b = re.sub(r"\b(?:public|private|protected|virtual)\b", "", b).strip().split("::")[-1]
                    if re.fullmatch(r"[A-Za-z_]\w*", b):
                        model.bases.setdefault(cls, set()).add(b)
                        model.derived.setdefault(b, set()).add(cls)
                raw_members.setdefault(cls, []).append(
                    _blank_children(code, s + 1, e, sorted(children.get(i, []))))
            elif kind == "function" and pkind in ("global", "namespace", "class"):
                f = _function(code, s, e, header, rel)
                if f is None:
                    continue
                if f.owner is None and pkind == "class":
                    cm = _CLASS_NAME.search(re.sub(r"\s+", " ", _blank_attributes(scopes[p][3])))
                    f.owner = cm.group(1) if cm else None
                model.funcs.append(f)
        # File-level declarations: blank every top-level non-namespace block.
        flat_children = []
        for i, (s, e, kind, header) in enumerate(scopes):
            p = parent[i]
            if kind != "namespace" and (p < 0 or scopes[p][2] == "namespace"):
                flat_children.append((s, e))
        file_text = _blank_children(code, 0, len(code), sorted(flat_children))
        file_text = re.sub(r"(?m)^\s*#.*$", "", file_text)
        decl = _decls(file_text)
        model.file_vars[rel] = decl
        for k, v in decl.items():
            model.global_vars.setdefault(k, set()).update(v)
    for cls, texts in raw_members.items():
        merged: dict[str, set[str]] = {}
        for t in texts:
            for k, v in _decls(t).items():
                merged.setdefault(k, set()).update(v)
        model.members[cls] = merged
    for fid, f in enumerate(model.funcs):
        model.by_name.setdefault(f.name, []).append(fid)
        model.by_key.setdefault((f.owner, f.name), []).append(fid)
    _find_roots(model, sig)
    _find_library_callbacks(model)
    return model


def _find_library_callbacks(model: Model) -> None:
    """The PolySynth library is precompiled: Synth::renderAudioBuffer has no source here, and it calls the
    open-source engines' voices through the SDK's virtual methods. Every override of those, in a class
    derived from InstrumentVoice, is a callback the library can make from inside renderAudioBuffer."""
    rel, cls, method = LIBRARY_ENTRY
    code = model.code(rel)
    m = re.search(rf"\bvirtual\b[^;{{}}]*\b{method}\s*\(", code)
    if not m or cls not in model.classes:
        raise ParseError(f"{rel}: no virtual {cls}::{method} declaration; the precompiled synth entry has moved")
    if (cls, method) in model.by_key:
        return  # the library has source now; ordinary edges cover it
    model.library_entry = model.where(rel, m.start())
    sdk = model.root / SDK_DIR
    virtuals = set()
    for path in sorted(sdk.glob("*.h")):
        virtuals |= set(re.findall(r"\bvirtual\b[^;{}()]*?\b(\w+)\s*\(", model.code(str(path.relative_to(model.root)))))
    if VOICE_BASE not in model.classes or not virtuals:
        raise ParseError(f"{SDK_DIR}: no {VOICE_BASE} class or virtual methods; the synth SDK has changed shape")
    for d in _all_derived(model, VOICE_BASE):
        for name in sorted(virtuals):
            model.library_callbacks += model.by_key.get((d, name), [])


def _function(code: str, s: int, e: int, header: str, rel: str) -> Func | None:
    # Locate the header text in the file: it ends right before the brace.
    region_start = s - len(header) - 64 if s - len(header) - 64 > 0 else 0
    region = code[region_start:s]
    hoff = region.rfind(header) if header else -1
    if hoff < 0:
        return None
    base = region_start + hoff
    text = _blank_attributes(header)
    text = re.sub(r"(?m)^\s*#.*$", lambda m: " " * len(m.group(0)), text)
    text = re.sub(r"^(\s*(?:public|private|protected)\s*:)", lambda m: " " * len(m.group(0)), text)
    for m in _NAME_IN_HEADER.finditer(text):
        name = re.sub(r"\s+", "", m.group(1))
        last = name.split("::")[-1]
        if last in KEYWORDS or last.startswith("__"):
            continue
        open_p = m.end() - 1
        close_p = _match_paren(text, open_p)
        if close_p < 0:
            return None
        parts = name.split("::")
        owner = parts[-2] if len(parts) > 1 else None
        fname = parts[-1]
        pos = base + m.start(1) + m.group(1).rfind(fname)
        return Func(fname, owner, rel, pos, (base + open_p, base + close_p), (s, e))
    return None


def _find_roots(model: Model, sig: tuple) -> None:
    rels = [rel for rel, _ in sig]

    def enclosing(rel: str, pos: int) -> int | None:
        best = None
        for fid, f in enumerate(model.funcs):
            if f.rel == rel and f.body[0] < pos < f.body[1]:
                if best is None or f.body[0] > model.funcs[best].body[0]:
                    best = fid
        return best

    audio = {"updateAudioBuffers": "audio output DMA interrupt", "updateInputAudioBuffers": "audio input DMA interrupt"}
    found_audio = set()
    for rel in rels:
        if not rel.startswith(PLATFORM_DIRS):
            continue
        code = model.code(rel)
        for m in re.finditer(r"->\s*(updateAudioBuffers|updateInputAudioBuffers)\s*\(", code):
            fid = enclosing(rel, m.start())
            if fid is not None:
                found_audio.add(m.group(1))
                model.roots.append({"kind": audio[m.group(1)], "fid": fid, "evidence": model.where(rel, m.start())})
    if found_audio != set(audio):
        raise ParseError("platforms/*/source: no platform audio callback calls ->updateAudioBuffers( and "
                         "->updateInputAudioBuffers(; the audio interrupt entry has moved")

    clock = 0
    registrations = []
    for rel in rels:
        code = model.code(rel)
        for m in re.finditer(r"\bscheduleMidiClockUs\s*\(", code):
            close = _match_paren(code, m.end() - 1)
            arg = code[m.end():close].split(",")[-1].strip().lstrip("&")
            for fid in _free(model, arg, rel):
                clock += 1
                model.roots.append({"kind": "MIDI clock timer interrupt", "fid": fid,
                                    "evidence": model.where(rel, m.start())})
        for m in re.finditer(r"\battachInterrupt\s*\(\s*&?(\w+)\s*\)", code):
            cand = _free(model, m.group(1), rel) or _visible(model, model.by_name.get(m.group(1), []), rel)
            for fid in cand:
                model.roots.append({"kind": "hardware interrupt (attachInterrupt)", "fid": fid,
                                    "evidence": model.where(rel, m.start())})
        for m in re.finditer(r"\b\w*[Aa]ctions\s*\.\s*\w+\s*=\s*&?(\w+)\s*;|\bset\w*Action\s*\(\s*&?(\w+)\s*\)",
                             code):
            name = m.group(1) or m.group(2)
            for fid in _free(model, name, rel):
                registrations.append((fid, rel, m.start()))
    for fid, f in enumerate(model.funcs):
        if re.fullmatch(r"\w+_IRQHandler|\w+_isr", f.name) and f.owner is None:
            model.roots.append({"kind": "hardware interrupt handler", "fid": fid, "evidence": model.where(f.rel, f.pos)})
    if not clock:
        raise ParseError("core/source/mtMidi.cpp: no function is passed to scheduleMidiClockUs(); the MIDI clock "
                         "timer callback has moved")
    midi = _free(model, "midiUpdate")
    if not midi or not registrations:
        raise ParseError("core/source/mtMidi.cpp: midiUpdate() or the MIDI receive-action registrations "
                         "(xxxReceiveActions.field = handler; / setXxxAction(handler)) weren't found")
    for m in midi:
        model.dispatch[m] = registrations


def _platform(rel: str) -> str | None:
    return next((d for d in PLATFORM_DIRS if rel.startswith(d + "/")), None)


def _free(model: Model, name: str, caller_rel: str | None = None) -> list[int]:
    """Free functions named `name` that a file can link against: the two platform trees are never linked
    together, so a platform file only sees its own platform's definitions (and core's)."""
    out = [fid for fid in model.by_name.get(name, []) if model.funcs[fid].owner not in model.classes]
    return _visible(model, out, caller_rel)


def _visible(model: Model, fids: list[int], caller_rel: str | None) -> list[int]:
    plat = _platform(caller_rel) if caller_rel else None
    if plat is None:
        return fids
    return [f for f in fids if _platform(model.funcs[f].rel) in (None, plat)]


def _hierarchy_up(model: Model, cls: str) -> list[str]:
    out, queue, seen = [], deque([cls]), {cls}
    while queue:
        c = queue.popleft()
        out.append(c)
        for b in sorted(model.bases.get(c, ())):
            if b not in seen:
                seen.add(b)
                queue.append(b)
    return out


def _all_derived(model: Model, cls: str) -> list[str]:
    out, queue, seen = [], deque([cls]), {cls}
    while queue:
        c = queue.popleft()
        for d in sorted(model.derived.get(c, ())):
            if d not in seen:
                seen.add(d)
                out.append(d)
                queue.append(d)
    return out


def _methods(model: Model, cls: str, name: str, virtual: bool = True) -> list[tuple[int, str]]:
    """Definitions a call to cls::name (or obj.name with obj of type cls) can reach."""
    out = []
    for c in _hierarchy_up(model, cls):
        if (c, name) in model.by_key:
            out += [(fid, "call") for fid in model.by_key[(c, name)]]
            break
    if virtual:
        for d in _all_derived(model, cls):
            out += [(fid, f"override in derived class {d}") for fid in model.by_key.get((d, name), [])]
    return out


def _member_type(model: Model, cls: str, var: str) -> set[str]:
    for c in _hierarchy_up(model, cls):
        t = model.members.get(c, {}).get(var)
        if t:
            return t
    return set()


def _edges(model: Model, fid: int) -> tuple[list[tuple[int, str, str, int]], list[dict[str, str]]]:
    """(callee fid, how, rel, pos) for every resolved call site, plus unresolved member calls."""
    if fid in model._edges:
        return model._edges[fid]
    f = model.funcs[fid]
    code = model.code(f.rel)
    body_s, body_e = f.body
    body = code[body_s:body_e]
    locals_ = _decls(code[f.params[0] + 1 : f.params[1]] + ",")
    for k, v in _decls(re.sub(r"(?m)^\s*#.*$", "", body)).items():
        locals_.setdefault(k, set()).update(v)
    cls = f.owner if f.owner in model.classes else None

    def var_types(var: str) -> set[str]:
        if var == "this" and cls:
            return {cls}
        if var in locals_:
            return locals_[var]
        if cls:
            t = _member_type(model, cls, var)
            if t:
                return t
        return model.file_vars.get(f.rel, {}).get(var) or model.global_vars.get(var, set())

    out: list[tuple[int, str, str, int]] = []
    unresolved: list[dict[str, str]] = []
    for m in re.finditer(r"(?<![\w~])(~?[A-Za-z_]\w*)\s*(?:<[^;{}()<>]*>\s*)?\(", body):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        pos = body_s + m.start(1)
        if name == LIBRARY_ENTRY[2] and model.library_entry:
            for t in model.library_callbacks:
                out.append((t, f"called back by the precompiled PolySynth library inside "
                               f"{LIBRARY_ENTRY[1]}::{name} (declared at {model.library_entry})", f.rel, pos))
            continue
        if name not in model.by_name:
            continue
        before = code[max(0, pos - 240) : pos].rstrip()
        targets: list[tuple[int, str]] = []
        q = re.search(r"([A-Za-z_]\w*)\s*::\s*$", before)
        if q:
            owner = q.group(1)
            targets = _methods(model, owner, name, virtual=False) if owner in model.classes else \
                [(t, "call") for t in model.by_key.get((owner, name), [])]
        elif before.endswith(".") or before.endswith("->"):
            chain = re.search(r"((?:[A-Za-z_]\w*\s*(?:\[[^\[\]]*\]\s*)?(?:\.|->)\s*)+)$", before)
            types: set[str] = set()
            if chain and not re.search(r"[)\]]\s*$", before[: chain.start()].rstrip()[-1:] or ""):
                segs = re.findall(r"([A-Za-z_]\w*)\s*(?:\[[^\[\]]*\]\s*)?(?:\.|->)", chain.group(1))
                types = var_types(segs[0])
                for seg in segs[1:]:
                    types = set().union(*(_member_type(model, t, seg) for t in types)) if types else set()
            for t in sorted(types):
                if t in model.classes:
                    targets += _methods(model, t, name)
            if not targets:
                cands = _visible(model, [c for c in model.by_name[name] if model.funcs[c].owner in model.classes],
                                 f.rel)
                if len(cands) == 1:
                    targets = [(cands[0], "unique method name (receiver type not resolved)")]
                elif cands:
                    unresolved.append({"call": re.sub(r"\s+", "", (chain.group(1) if chain else "?.")) + name + "()",
                                       "where": model.where(f.rel, pos)})
        else:
            if cls:
                targets = _methods(model, cls, name)
            if not targets:
                targets = [(t, "call") for t in _free(model, name, f.rel)]
            if not targets and name in model.classes:
                targets = [(t, "constructor") for t in model.by_key.get((name, name), [])]
        for t, how in targets:
            out.append((t, how, f.rel, pos))
    for t, rel, pos in model.dispatch.get(fid, []):
        out.append((t, f"dispatched by the MIDI port driver (handler registered at {model.where(rel, pos)})",
                    f.rel, f.pos))
    model._edges[fid] = (out, unresolved)
    return model._edges[fid]


def _targets(model: Model, query: str) -> list[int]:
    m = re.fullmatch(r"(?:(\w+)::)?(~?\w+)", query.strip())
    if not m:
        raise ValueError("function must be a name or Class::name")
    owner, name = m.group(1), m.group(2)
    return [fid for fid in model.by_name.get(name, []) if owner is None or model.funcs[fid].owner == owner]


def _active(model: Model, rel: str, line: int) -> set[str] | None:
    mtime = (model.root / rel).stat().st_mtime
    out = set()
    for t in TARGET_DEFINES:
        lines = _active_lines(str(model.root), rel, mtime, t)
        if lines is None:
            return None
        if line in lines:
            out.add(t)
    return out


def _describe(model: Model, fid: int) -> dict[str, str]:
    f = model.funcs[fid]
    return {"function": f.qualified, "where": model.where(f.rel, f.pos)}


def _path(model: Model, root_fid: int, goal: set[int]) -> list[tuple[int, str, str, int]] | None:
    prev: dict[int, tuple[int, str, str, int] | None] = {root_fid: None}
    queue = deque([root_fid])
    while queue:
        cur = queue.popleft()
        if cur in goal:
            steps = []
            while prev[cur] is not None:
                p, how, rel, pos = prev[cur]
                steps.append((cur, how, rel, pos))
                cur = p
            return list(reversed(steps))
        for callee, how, rel, pos in _edges(model, cur)[0]:
            if callee not in prev:
                prev[callee] = (cur, how, rel, pos)
                queue.append(callee)
    return None


def _reachable(model: Model) -> set[int]:
    seen = {r["fid"] for r in model.roots}
    queue = deque(seen)
    while queue:
        cur = queue.popleft()
        for callee, *_ in _edges(model, cur)[0]:
            if callee not in seen:
                seen.add(callee)
                queue.append(callee)
    return seen


NOTES = [
    "Heuristic: a text-based call graph, not a compiler's. Calls through function pointers (other than the "
    "roots and the registered MIDI handlers) are missed, so reachable=false is never proof. Grep the callers "
    "yourself.",
    "Virtual calls link every override in derived classes, so a path can go through an override that isn't "
    "the dynamic type at runtime. Read the path before relying on it.",
    "compiled_for on a path is the set of targets whose preprocessor keeps every call site on it.",
]


def interrupt_context(root: Path, function: str = "") -> dict[str, Any]:
    model = build_model(root)
    roots = [{"kind": r["kind"], **_describe(model, r["fid"]), "evidence": r["evidence"]} for r in model.roots]
    if not function.strip():
        reach = _reachable(model)
        return {
            "roots": roots,
            "reachable_function_count": len(reach),
            "reachable_functions": sorted({model.funcs[f].qualified for f in reach}),
            "notes": NOTES,
        }
    goal = set(_targets(model, function))
    if not goal:
        raise ValueError(f"no function definition named {function!r} in the scanned source; grep for it")
    paths, seen_roots = [], set()
    unresolved: dict[str, dict[str, str]] = {}
    for r in model.roots:
        key = (r["kind"], r["fid"])
        if key in seen_roots:
            continue
        seen_roots.add(key)
        steps = _path(model, r["fid"], goal)
        if steps is None:
            continue
        out_steps, active = [], set(TARGET_DEFINES)
        unknown = False
        for callee, how, rel, pos in steps:
            line = model.line(rel, pos)
            a = _active(model, rel, line)
            if a is None:
                unknown = True
            else:
                active &= a
            out_steps.append({**_describe(model, callee), "called_at": f"{rel}:{line}", "how": how})
        path = {"root": {"kind": r["kind"], **_describe(model, r["fid"]), "evidence": r["evidence"]},
                "steps": out_steps, "compiled_for": sorted(active)}
        if unknown:
            path["preprocessing_unknown"] = True
        paths.append(path)
    if not paths:
        # Show where the trace may have stopped short: unresolved member calls in interrupt-reachable code.
        for fid in _reachable(model):
            for u in _edges(model, fid)[1]:
                unresolved.setdefault(u["where"], u)
    result: dict[str, Any] = {
        "function": function,
        "definitions": [_describe(model, f) for f in sorted(goal)],
        "reachable": bool(paths),
        "paths": paths,
        "roots": roots,
        "notes": NOTES,
    }
    if not paths:
        result["unresolved_calls_in_interrupt_code"] = {
            "count": len(unresolved),
            "sample": [unresolved[k] for k in sorted(unresolved)][:25],
        }
    return result
