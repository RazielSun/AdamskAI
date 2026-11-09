#!/usr/bin/env bash
# Static checks for the AdamskAI plugin itself. Needs bash and python3; no engine, no project. Linux; the Win64 twin is Tools/adsk_check_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_check.sh
#
# Rules (each finding prints as `path:line: rule: message`):
#   license-header       every .h/.cpp/.cs starts with the MIT header line
#   generated-last       a header's `.generated.h` include is its last #include
#   check-side-effect    check()/checkSlow() only calls questions (Is*/Has*/Can*/Find*/Contains*/
#                        Num/..., or Get*() with no arguments): the argument compiles out of
#                        Shipping and Test, so anything else must be stored first or use verify()
#   module-dependency    Build.cs dependencies come from the allowlist below, so a game module
#                        can never become a plugin dependency
#   core-engine-free     the AdamskAI core module neither depends on nor includes Engine
#   no-imgui             no plugin file includes ImGui (projects draw their own panels)
#   semantics-citation   every rule in docs/semantics.md names a conformance file or a registered
#                        AdamskAI.* test that checks it, and every name resolves
#   decision-evidence    every docs/dev/decisions/NNNN-slug.md has a matching title, a Status of Proposed,
#                        Accepted (dated) or Superseded by an existing decision, and Evidence whose every test,
#                        fixture or path resolves
#
# Exit codes: 0 clean, 1 findings, 2 setup failure (python3 missing, plugin layout not found).

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	"") ;;
	*) echo "ERROR: unknown argument '$1'. Try --help." >&2; exit 2 ;;
esac

PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 is required." >&2; exit 2; }
[ -d "$PLUGIN_DIR/Source" ] || { echo "ERROR: no Source/ under $PLUGIN_DIR." >&2; exit 2; }

exec python3 -I "$PLUGIN_DIR/Tools/lib/adsk_check.py" "$PLUGIN_DIR"
