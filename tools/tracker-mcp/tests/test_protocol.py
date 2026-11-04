"""End-to-end over MCP: the official SDK client starts the server on stdio, the way an editor does, and
calls every tool."""

import asyncio
import json
import os
import sys
import unittest
from pathlib import Path

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from support import ROOT, check_provenance

PKG_DIR = Path(__file__).resolve().parents[1]
EXPECTED_TOOLS = {"project_map", "list_synth_engines", "list_screens", "find_screen", "memory_map", "build_config", "step_fx", "display_controls",
                  "test_suites"}


async def _session_run(calls):
    params = StdioServerParameters(
        command=sys.executable,
        args=["-m", "tracker_mcp.server"],
        cwd=str(PKG_DIR),
        env={**os.environ, "TRACKER_CE_ROOT": str(ROOT), "PYTHONPATH": str(PKG_DIR)},
    )
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            tools = await session.list_tools()
            results = {}
            for name, args in calls:
                results[(name, json.dumps(args, sort_keys=True))] = await session.call_tool(name, args)
            return init, tools, results


def _payload(result):
    """The tool's structured answer (falls back to parsing the text block)."""
    sc = getattr(result, "structured_content", None) or getattr(result, "structuredContent", None)
    if sc:
        return sc.get("result", sc) if set(sc) == {"result"} else sc
    return json.loads(result.content[0].text)


class Protocol(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        calls = [("project_map", {}), ("list_synth_engines", {}), ("list_screens", {}),
                 ("find_screen", {"query": "master"}), ("memory_map", {}),
                 ("build_config", {}), ("step_fx", {"query": "T"}),
                 ("display_controls", {}), ("test_suites", {})]
        cls.init, cls.tools, cls.results = asyncio.run(_session_run(calls))

    def _get(self, name, args=None):
        return self.results[(name, json.dumps(args or {}, sort_keys=True))]

    def test_server_identifies_itself_with_instructions(self):
        info = getattr(self.init, "server_info", None) or self.init.serverInfo
        self.assertEqual(info.name, "tracker-ce")
        self.assertTrue(self.init.instructions)

    def test_tools_are_listed_with_descriptions(self):
        names = {t.name for t in self.tools.tools}
        self.assertEqual(names, EXPECTED_TOOLS)
        for t in self.tools.tools:
            self.assertTrue(t.description and len(t.description) > 40, t.name)

    def test_every_call_succeeds_with_valid_provenance(self):
        for (name, _), result in self.results.items():
            is_error = getattr(result, "is_error", None)
            if is_error is None:
                is_error = getattr(result, "isError", False)
            self.assertFalse(is_error, f"{name}: {result.content}")
            self.assertEqual(check_provenance(_payload(result)), [], name)

    def test_answers_over_the_wire_match_direct_calls(self):
        from tracker_mcp import screens, synth
        self.assertEqual(_payload(self._get("list_synth_engines")), synth.list_synth_engines(ROOT))
        found = _payload(self._get("find_screen", {"query": "master"}))
        self.assertEqual([m["class"] for m in found["matches"]], ["cMasterParams"])
        self.assertEqual(_payload(self._get("list_screens")), screens.list_screens(ROOT))


if __name__ == "__main__":
    unittest.main()
