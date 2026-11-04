"""Anchor checks over every AGENTS.md: each path, file:line, link and code symbol the guidance names exists.

Backticked tokens are classified by shape. Paths and links must exist; `file:line` must be in range;
`Class::name`, CamelCase and camelCase names must be found by where_is; ALL_CAPS macros must appear in the
scanned source or a makefile; `obj.method()` needs both halves. The only skips are an explicit list of C/C++
library names and keywords, tool names (test_guidance checks those), compiler flags and branch names (CI
checkouts don't fetch other branches). A rename in the
code, or a typo in the guidance, fails here and names the token.
"""

import re
import subprocess
import unittest

from support import ROOT
from tracker_mcp import rules
from tracker_mcp.symbols import SCAN, where_is

# Names the guidance mentions that live in the C/C++ runtime, the compiler or a library, not in this tree.
EXTERNAL = {"double", "new", "delete", "malloc", "memset", "memcpy", "floorf", "sqrtf", "sinf", "powf", "delay",
            "std::vector::push_back", "std::string", "fmt::format", "clang-format", "update", "g++", "make", "ld",
            "objdump", "static_assert", "volatile"}
TOOLS = {"project_map", "find_screen", "where_is", "list_synth_engines", "step_fx", "interrupt_context",
         "build_config", "memory_map", "display_controls", "rules", "test_suites", "list_screens"}


def _guidance_files():
    return ["AGENTS.md"] + rules.area_files(ROOT)


def _tracked():
    return set(subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True,
                              check=True).stdout.split())


def _word_in_tree(word):
    pattern = re.compile(rf"(?:(?<!\w)|(?<=-D)){re.escape(word)}(?!\w)")  # -DNAME on a makefile line
    files = [p for d in SCAN for p in (ROOT / d).rglob("*") if p.is_file() and p.suffix in (".h", ".hpp", ".c", ".cpp")]
    files += list(ROOT.glob("**/makefile")) + list(ROOT.glob("**/Makefile")) + list(ROOT.glob("**/*.mk"))
    return any(pattern.search(p.read_text(errors="replace")) for p in files if ".venv" not in p.parts)


def _symbol_exists(sym):
    return bool(where_is(ROOT, sym, limit=1)["hits"])


class Anchors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tracked = _tracked()

    def _check_token(self, rel, tok):
        tok = tok.strip()
        if not tok or tok in EXTERNAL or tok in TOOLS or tok.startswith(("-", ".")) or " " in tok:
            return
        m = re.fullmatch(r"([\w./-]+\.\w+):(\d+)(?:-(\d+))?", tok)
        if m:
            path = ROOT / m.group(1)
            self.assertTrue(path.is_file(), f"{rel}: `{tok}` names a missing file")
            n = path.read_text(errors="replace").count("\n") + 1
            self.assertLessEqual(int(m.group(3) or m.group(2)), n, f"{rel}: `{tok}` is past the end of the file")
            return
        if "/" in tok and re.fullmatch(r"[\w./-]+", tok):
            path = tok.rstrip("/")
            self.assertTrue((ROOT / path).exists(), f"{rel}: `{tok}` doesn't exist")
            return
        if re.fullmatch(r"[\w-]+\.(?:cpp|h|hpp|c|md|json|py|yml|ld|mk)", tok):
            self.assertTrue(any(t.rsplit("/", 1)[-1] == tok for t in self.tracked), f"{rel}: no file named `{tok}`")
            return
        call = re.fullmatch(r"(\w+)(?:\.|->)(\w+)\(\)", tok)
        if call:
            self.assertTrue(_symbol_exists(call.group(1)), f"{rel}: `{tok}`: no `{call.group(1)}` in the tree")
            self.assertTrue(_symbol_exists(call.group(2)), f"{rel}: `{tok}`: no `{call.group(2)}` in the tree")
            return
        obj = re.fullmatch(r"(\w+)(?:\.\*|->)", tok)
        if obj:
            self.assertTrue(_symbol_exists(obj.group(1)), f"{rel}: `{tok}`: no `{obj.group(1)}` in the tree")
            return
        sym = re.fullmatch(r"((?:\w+::)?~?\w+)(?:\(\))?", tok)
        if not sym:
            return  # prose, an expression or a glob: not an anchor
        name = sym.group(1)
        if name in EXTERNAL or name.isdigit() or re.fullmatch(r"[\d.]+f?", name):
            return
        if re.fullmatch(r"_*[A-Z][A-Z0-9_]*_*", name):
            self.assertTrue(_word_in_tree(name), f"{rel}: macro `{name}` appears nowhere in the source or makefiles")
            return
        if "::" in name or re.search(r"[A-Z]", name) or tok.endswith("()"):
            self.assertTrue(_symbol_exists(name), f"{rel}: `{tok}` isn't defined or declared anywhere in the tree")

    def test_backticked_anchors(self):
        for rel in _guidance_files():
            text = (ROOT / rel).read_text()
            text = re.sub(r"```.*?```", "", text, flags=re.S)
            # Branch names aren't checked: CI checkouts don't fetch other branches.
            text = re.sub(r"`[^`\n]+` branch\b", "", text)
            for tok in re.findall(r"`([^`\n]+)`", text):
                with self.subTest(file=rel, token=tok):
                    self._check_token(rel, tok)

    def test_checker_rejects_planted_errors(self):
        for bad in ("core/source/NoSuchFile.cpp", "core/source/Tracker.cpp:999999", "TrackerImpl::noSuchMethod",
                    "ZZ_NO_SUCH_MACRO", "sequencer.noSuchCall()", "NoSuchClassAnywhere", "noSuchFunction()"):
            with self.subTest(token=bad), self.assertRaises(AssertionError):
                self._check_token("planted", bad)
        for good in ("core/source/Tracker.cpp:171", "TrackerImpl::updateAudioBuffers", "FEATURE_SYNTH",
                     "sequencer.tick()", "midiUpdate()", "sinf"):
            self._check_token("planted", good)

    def test_links_resolve(self):
        for rel in _guidance_files():
            base = (ROOT / rel).parent
            for target in re.findall(r"\]\(([^)]+)\)", (ROOT / rel).read_text()):
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                path = target.split("#")[0]
                with self.subTest(file=rel, link=target):
                    self.assertTrue((base / path).exists(), f"{rel}: link to missing {target}")

    def test_area_files_are_listed_in_the_root_guidance(self):
        root = (ROOT / "AGENTS.md").read_text()
        for rel in rules.area_files(ROOT):
            with self.subTest(file=rel):
                self.assertIn(f"`{rel}`", root, "every area AGENTS.md must be listed in section 8")


if __name__ == "__main__":
    unittest.main()
