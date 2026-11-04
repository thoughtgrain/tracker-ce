# tracker-ce project MCP server: setup, tests, run.
#
#   make -C tools/tracker-mcp setup   create .venv and install the MCP SDK (once)
#   make -C tools/tracker-mcp test    accuracy + protocol tests (needs g++ or clang++ for compiler ground truth)
#   make -C tools/tracker-mcp serve   run the server on stdio (what an editor does)

PY      ?= python3
VENV    := .venv
VPY     := $(VENV)/bin/python

.PHONY: setup test serve clean
setup: $(VPY)

$(VPY): requirements.txt
	$(PY) -m venv $(VENV)
	$(VPY) -m pip install -q -r requirements.txt
	@touch $(VPY)

test: $(VPY)
	PYTHONPATH=.:tests $(VPY) -m unittest discover -s tests -v

serve: $(VPY)
	$(VPY) -m tracker_mcp.server

clean:
	rm -rf $(VENV)
	find . -name __pycache__ -prune -exec rm -rf {} +
