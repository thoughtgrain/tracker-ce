"""Existence checks over every AGENTS.md and recipe: the paths, file:line, links and names they mention exist.

This is the only check on the guidance. It doesn't judge the prose, so rewording never breaks it; it only
catches a guide that points at something that's gone (a renamed function, a moved file, a typo). Backticked
tokens are read by shape:
- a path must exist, and a `file:line` must be within the file;
- a code name (`Class::name`, `obj.method()`, CamelCase, camelCase, `call()`, ALL_CAPS) must appear as an
  identifier somewhere in the firmware source or makefiles; for `A::b` and `a.b()` both halves must.
Skipped: an explicit list of C/C++ library names and keywords, the MCP tool names, compiler flags, globs,
SD-card paths (they start with `/`) and branch names (CI checkouts don't fetch other branches).
"""

import re
import subprocess
import unittest
from functools import lru_cache

from support import ROOT
from tracker_mcp import rules
from tracker_mcp.scopes import SCAN

EXTERNAL = {"double", "new", "delete", "malloc", "memset", "memcpy", "floorf", "sqrtf", "sinf", "powf", "delay",
            "std::vector::push_back", "std::string", "fmt::format", "std::unique_ptr", "std::function",
            "clang-format", "g++", "make", "ld", "objdump", "static_assert", "volatile", "constexpr", "update"}
TOOLS = {"project_map", "rules", "interrupt_context", "recipe"}


def _guidance_files():
    return ["AGENTS.md"] + rules.area_files(ROOT) + rules.recipe_files(ROOT)


@lru_cache(maxsize=1)
def _identifiers():
    """Every identifier in the firmware source and makefiles (`-DNAME` counts as NAME)."""
    files = [p for d in SCAN for p in (ROOT / d).rglob("*") if p.is_file() and p.suffix in (".h", ".hpp", ".c", ".cpp")]
    files += [p for p in ROOT.rglob("*") if p.name in ("makefile", "Makefile") and ".venv" not in p.parts]
    files += [ROOT / "editor-fx-sdk/ptfx_builder.py"]
    words = set()
    for p in files:
        text = re.sub(r"-D(?=[A-Za-z_])", "-D ", p.read_text(errors="replace"))
        words.update(re.findall(r"[A-Za-z_]\w*", text))
    return words


@lru_cache(maxsize=1)
def _tracked():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True, check=True).stdout
    return {t.rsplit("/", 1)[-1] for t in out.split()}


def check_token(rel, tok):
    """Raise AssertionError if `tok`, read by its shape, names something that doesn't exist."""
    tok = tok.strip()
    if not tok or tok in EXTERNAL or tok in TOOLS or tok.startswith(("-", ".", "/")) or " " in tok or "*" in tok:
        return
    m = re.fullmatch(r"([\w./-]+\.\w+):(\d+)(?:-(\d+))?", tok)
    if m:
        path = ROOT / m.group(1)
        assert path.is_file(), f"{rel}: `{tok}` names a missing file"
        n = path.read_text(errors="replace").count("\n") + 1
        assert int(m.group(3) or m.group(2)) <= n, f"{rel}: `{tok}` is past the end of the file"
        return
    if "/" in tok and re.fullmatch(r"[\w./-]+", tok):
        assert (ROOT / tok.rstrip("/")).exists(), f"{rel}: `{tok}` doesn't exist"
        return
    if re.fullmatch(r"[\w-]+\.(?:cpp|h|hpp|c|md|json|py|yml|ld|mk)", tok):
        assert tok in _tracked(), f"{rel}: no file named `{tok}`"
        return
    m = re.fullmatch(r"(?:(\w+)(?:::|\.|->))?(~?\w+)(?:\(\))?", tok)
    if not m:
        return  # an expression or prose, not a name
    names = [n.lstrip("~") for n in m.groups() if n]
    if len(names) == 1 and not (re.search(r"[A-Z]", names[0]) or tok.endswith("()")):
        return  # a plain lowercase word: too generic to check
    if names[-1] in EXTERNAL or re.fullmatch(r"[\d.]+f?", names[-1]):
        return
    for n in names:
        assert n in _identifiers(), f"{rel}: `{tok}`: `{n}` appears nowhere in the firmware source or makefiles"


class Anchors(unittest.TestCase):
    def test_backticked_names_and_paths_exist(self):
        for rel in _guidance_files():
            text = re.sub(r"```.*?```", "", (ROOT / rel).read_text(), flags=re.S)
            text = re.sub(r"`[^`\n]+` branch\b", "", text)
            for tok in re.findall(r"`([^`\n]+)`", text):
                with self.subTest(file=rel, token=tok):
                    check_token(rel, tok)

    def test_links_resolve(self):
        for rel in _guidance_files():
            base = (ROOT / rel).parent
            for target in re.findall(r"\]\(([^)]+)\)", (ROOT / rel).read_text()):
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
                    "ZZ_NO_SUCH_MACRO", "sequencer.noSuchCall()", "NoSuchClassAnywhere", "noSuchFunction()"):
            with self.subTest(token=bad), self.assertRaises(AssertionError):
                check_token("planted", bad)
        for good in ("core/source/Tracker.cpp:171", "TrackerImpl::updateAudioBuffers", "FEATURE_SYNTH",
                     "__MK66FX1M0__", "sequencer.tick()", "midiUpdate()", "sinf"):
            check_token("planted", good)


if __name__ == "__main__":
    unittest.main()
