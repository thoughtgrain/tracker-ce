"""rules: the guidance that applies to a path, and the build recipes, straight from the docs.

The root AGENTS.md always applies. An area AGENTS.md applies to everything under its directory, so for a
path the answer is the root file plus every area file in the directories above it, nearest last. Each
section carries the line its heading is on. Nothing is paraphrased or cached: the text is read from the
checkout on every call.

Recipes (docs/recipes/*.md) are step-by-step guides for building a core part: the files to touch, in
order, and the pitfalls. Every answer lists them; pass recipe=<name> to get one.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .source import ParseError

SKIP_DIRS = {".git", ".venv", "node_modules", "build", "__pycache__"}
RECIPES_DIR = "docs/recipes"


def recipe_files(root: Path) -> list[str]:
    d = root / RECIPES_DIR
    return sorted(str(p.relative_to(root)) for p in d.glob("*.md") if p.name != "README.md") if d.is_dir() else []


def _sections(root: Path, rel: str) -> list[dict[str, Any]]:
    """Split a markdown file at its level-1 and level-2 headings (outside code fences)."""
    lines = (root / rel).read_text(encoding="utf-8").split("\n")
    out: list[dict[str, Any]] = []
    start, heading, fenced = 0, None, False
    for i, line in enumerate(lines):
        if line.lstrip().startswith("```"):
            fenced = not fenced
        if not fenced and re.match(r"#{1,2} \S", line):
            if i > start or heading is not None:
                out.append({"heading": heading or "", "where": f"{rel}:{start + 1}",
                            "text": "\n".join(lines[start:i]).strip("\n")})
            start, heading = i, re.sub(r"^#+\s*", "", line).strip()
    out.append({"heading": heading or "", "where": f"{rel}:{start + 1}", "text": "\n".join(lines[start:]).strip("\n")})
    return [s for s in out if s["text"]]


def area_files(root: Path) -> list[str]:
    out = []
    for p in root.rglob("AGENTS.md"):
        rel = p.relative_to(root)
        if rel == Path("AGENTS.md") or any(part in SKIP_DIRS for part in rel.parts):
            continue
        out.append(str(rel))
    return sorted(out)


def _summary(root: Path, rel: str) -> str:
    for line in (root / rel).read_text(encoding="utf-8").split("\n"):
        if line.strip() and not line.startswith("#"):
            return line.strip()[:240]
    return ""


def _recipe_index(root: Path) -> list[dict[str, str]]:
    return [{"recipe": Path(r).stem, "file": r, "summary": _summary(root, r)} for r in recipe_files(root)]


def rules(root: Path, path: str = "", recipe: str = "") -> dict[str, Any]:
    if not (root / "AGENTS.md").is_file():
        raise ParseError("AGENTS.md: the root guidance file is missing")
    recipe = recipe.strip()
    if recipe:
        index = {r["recipe"]: r["file"] for r in _recipe_index(root)}
        if recipe not in index:
            raise ValueError(f"no recipe named {recipe!r}; available: {', '.join(sorted(index)) or 'none'}")
        return {"recipe": recipe, "file": index[recipe], "sections": _sections(root, index[recipe]),
                "notes": ["Recipes name the files and symbols to touch; open them and read the code before "
                          "editing. Call rules with the path of each file you edit for the area rules."]}
    result: dict[str, Any] = {"path": path, "root_rules": {"file": "AGENTS.md", "sections": _sections(root, "AGENTS.md")}}
    areas = area_files(root)
    path = path.strip().strip("/")
    if path:
        target = (root / path).resolve()
        if not target.exists() or root.resolve() not in (target, *target.parents):
            raise ValueError(f"{path!r} isn't a file or directory in this checkout")
        rel_target = target.relative_to(root.resolve())
        applies = [a for a in areas if Path(a).parent in (rel_target, *rel_target.parents)]
        applies.sort(key=lambda a: len(Path(a).parts))
        result["area_rules"] = [{"file": a, "applies_to": str(Path(a).parent) + "/", "sections": _sections(root, a)}
                                for a in applies]
    result["all_area_files"] = [{"file": a, "applies_to": str(Path(a).parent) + "/", "summary": _summary(root, a)}
                                for a in areas]
    result["recipes"] = _recipe_index(root)
    result["notes"] = [
        "Pass the file you're about to edit as `path` to get the root rules plus every area AGENTS.md above it "
        "(nearest last; a nearer file adds to the root rules, it doesn't replace them).",
        "The text is served verbatim from the checkout; each section's `where` is its heading line.",
    ]
    return result
