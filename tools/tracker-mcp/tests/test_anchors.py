"""Existence checks over every AGENTS.md and recipe: the paths, file:line, links and names they mention exist.

This is the only check on the guidance. It doesn't judge the prose, so rewording never breaks it; it only
catches a doc that points at something that's gone (a renamed function, a moved file, a typo).

Each backticked token is read by shape:
- a path must exist, and a `file:line` must be within the file;
- a code name (`Class::name`, `obj.method()`, `call()`, or any name with a capital letter) must appear as
  an identifier in the firmware source or a makefile; for `A::b` and `a.b()` both halves must.
Skipped: an explicit list of library names and keywords, the MCP tool names, compiler flags, SD-card paths
(they start with `/`), globs, plain lowercase words and branch names (CI checkouts don't fetch other
branches).
"""

import subprocess
import unittest
from functools import lru_cache

from support import ROOT
from tracker_mcp import rules
from tracker_mcp.tokens import words

SOURCE_DIRS = ("core", "platforms/imx/source", "platforms/mk66/source", "synth/include", "util/include", "editor-fx-sdk")
SOURCE_SUFFIXES = (".h", ".hpp", ".c", ".cpp")
EXTERNAL = {"double", "new", "delete", "malloc", "memset", "memcpy", "floorf", "sqrtf", "sinf", "powf", "delay",
            "std::vector::push_back", "std::string", "fmt::format", "std::unique_ptr", "std::function",
            "clang-format", "g++", "make", "ld", "objdump", "static_assert", "volatile", "constexpr", "update"}
TOOLS = {"project_map", "rules", "recipe"}
PATH_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-")
FILE_SUFFIXES = (".cpp", ".h", ".hpp", ".c", ".md", ".json", ".py", ".yml", ".ld", ".mk")


def _guidance_files():
    return ["AGENTS.md"] + rules.area_files(ROOT) + rules.recipe_files(ROOT)


@lru_cache(maxsize=1)
def _identifiers():
    """Every identifier in the firmware source and makefiles."""
    files = [p for d in SOURCE_DIRS for p in (ROOT / d).rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES]
    files += [p for p in ROOT.rglob("*") if p.name in ("makefile", "Makefile") and ".venv" not in p.parts]
    files += [ROOT / "editor-fx-sdk/ptfx_builder.py"]
    out = set()
    for p in files:
        out |= words(p.read_text(errors="replace"))
    return out


@lru_cache(maxsize=1)
def _tracked_names():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True).stdout
    return {t.rsplit("/", 1)[-1] for t in out.split()}


def _is_name(s):
    s = s.lstrip("~")
    return bool(s) and (s[0].isalpha() or s[0] == "_") and all(c.isalnum() or c == "_" for c in s)


def _is_number(s):
    return s.rstrip("f").replace(".", "", 1).isdigit()


def check_token(rel, tok):
    """Raise AssertionError if `tok`, read by its shape, names something that doesn't exist."""
    tok = tok.strip()
    if (not tok or tok in EXTERNAL or tok in TOOLS or tok[0] in "-./" or " " in tok or "*" in tok
            or "<" in tok):
        return
    path, sep, line = tok.rpartition(":")
    if sep and line.replace("-", "", 1).isdigit() and "/" in path and set(path) <= PATH_CHARS:
        assert (ROOT / path).is_file(), f"{rel}: `{tok}` names a missing file"
        last = int(line.split("-")[-1])
        n = (ROOT / path).read_text(errors="replace").count("\n") + 1
        assert last <= n, f"{rel}: `{tok}` is past the end of the file"
        return
    if "/" in tok and set(tok) <= PATH_CHARS:
        assert (ROOT / tok.rstrip("/")).exists(), f"{rel}: `{tok}` doesn't exist"
        return
    if tok.endswith(FILE_SUFFIXES) and set(tok) <= PATH_CHARS:
        assert tok in _tracked_names(), f"{rel}: no file named `{tok}`"
        return
    call = tok.endswith("()")
    body = tok[:-2] if call else tok
    for sep in ("::", "->", "."):
        if sep in body:
            parts = body.split(sep)
            break
    else:
        parts = [body]
    if len(parts) > 2 or not all(_is_name(p) for p in parts):
        return  # an expression or prose, not a name
    if len(parts) == 1 and not call and not any(c.isupper() for c in parts[0]):
        return  # a plain lowercase word: too generic to check
    if parts[-1] in EXTERNAL or _is_number(parts[-1]):
        return
    for p in parts:
        assert p.lstrip("~") in _identifiers(), f"{rel}: `{tok}`: `{p}` appears nowhere in the source or makefiles"


def _backticked(text):
    """(token, text right after it) for every inline-code span outside fenced code blocks."""
    out, fenced = [], False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        parts = line.split("`")
        for i in range(1, len(parts) - 1, 2):
            out.append((parts[i], parts[i + 1]))
    return out


def _links(text):
    out, start = [], 0
    while (i := text.find("](", start)) >= 0:
        end = text.find(")", i + 2)
        if end < 0:
            break
        out.append(text[i + 2 : end])
        start = end
    return out


class Anchors(unittest.TestCase):
    def test_backticked_names_and_paths_exist(self):
        for rel in _guidance_files():
            for tok, after in _backticked((ROOT / rel).read_text()):
                if after.startswith(" branch"):
                    continue
                with self.subTest(file=rel, token=tok):
                    check_token(rel, tok)

    def test_links_resolve(self):
        for rel in _guidance_files():
            base = (ROOT / rel).parent
            for target in _links((ROOT / rel).read_text()):
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                with self.subTest(file=rel, link=target):
                    self.assertTrue((base / target.split("#")[0]).exists(), f"{rel}: link to missing {target}")

    def test_area_files_are_listed_in_the_root_guidance(self):
        root = (ROOT / "AGENTS.md").read_text()
        for rel in rules.area_files(ROOT):
            with self.subTest(file=rel):
                self.assertIn(f"`{rel}`", root, "every area AGENTS.md must be listed in section 8")

    def test_checker_rejects_planted_errors(self):
        for bad in ("core/source/NoSuchFile.cpp", "core/source/Tracker.cpp:999999", "TrackerImpl::noSuchMethod",
                    "ZZ_NO_SUCH_MACRO", "sequencer.noSuchCall()", "NoSuchClassAnywhere", "noSuchFunction()",
                    "NoSuchFile.cpp"):
            with self.subTest(token=bad), self.assertRaises(AssertionError):
                check_token("planted", bad)
        for good in ("core/source/Tracker.cpp:171", "TrackerImpl::updateAudioBuffers", "FEATURE_SYNTH",
                     "__MK66FX1M0__", "sequencer.tick()", "midiUpdate()", "sinf", "Tracker.cpp", "core/source/"):
            check_token("planted", good)


if __name__ == "__main__":
    unittest.main()
