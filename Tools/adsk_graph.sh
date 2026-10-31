#!/usr/bin/env bash
# The state machine of a behavior file as a diagram: Mermaid (default; GitHub renders it in a ```mermaid block) or
# Graphviz DOT (`dot -Tsvg`). Needs python3; no engine, no project. Linux; the Win64 twin is Tools/adsk_graph_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_graph.sh <file.xml> [mermaid|dot] > diagram.txt
#
# States are boxes, parents hold their children (with initial, select and history), and every `On` with a `goto` is an
# arrow labelled with its event and condition. It draws the file as written and checks nothing: validate it first.
#
# Exit codes: 0 written, 2 usage, missing python3, or a file that is not an Agent.

set -uo pipefail

case "${1:-}" in
	-h|--help|"") sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; [ -n "${1:-}" ] && exit 0 || exit 2 ;;
esac

PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 is required." >&2; exit 2; }

exec python3 -I "$PLUGIN_DIR/Tools/lib/adsk_graph.py" "$@"
