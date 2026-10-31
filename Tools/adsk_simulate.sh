#!/usr/bin/env bash
# Run behavior scenarios headlessly: each JSON scenario drives one behavior file with fixed steps, its inputs and
# events at their times, and host or project leaves finishing as it says. Linux; the Win64 twin is
# Tools/adsk_simulate_win64.ps1. Format and findings: docs/dev/testing.md, "Simulation".
#
# Usage: Plugins/AdamskAI/Tools/adsk_simulate.sh [--project <uproject|dir>] [scenarios]
#
#   scenarios   files or directories, joined with +; by default the plugin's Fixtures/scenarios and, when it
#               exists, the project's Tests/AdamskAI
#
# A thin wrapper over `adsk_validate.sh --simulate`: same log (Saved/Logs/adsk_validate_simulate.log), output and
# exit codes (0 clean; 1 a livelock, hold, failed expectation or compile diagnostic; 2 usage or a scenario that
# cannot run; 3 crash, signal or timeout). Each run's trace is Saved/AdamskAI/simulate/<scenario>.trace.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

PROJECT_ARG=""
SCENARIOS=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--project)
			[ "$#" -ge 2 ] || { adsk_err "--project needs a value."; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		--project=*) PROJECT_ARG="${1#--project=}"; shift ;;
		-*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
		*) [ -z "$SCENARIOS" ] || { adsk_err "give one + joined scenario list."; exit 2; }; SCENARIOS="$1"; shift ;;
	esac
done

if [ -z "$SCENARIOS" ]; then
	adsk_resolve_project "$PROJECT_ARG" || exit $?
	for DIR in "$ADSK_PLUGIN_DIR/Fixtures/scenarios" "$ADSK_PROJECT_ROOT/Tests/AdamskAI"; do
		[ -d "$DIR" ] && SCENARIOS="${SCENARIOS:+$SCENARIOS+}$DIR"
	done
fi

exec "$(dirname "${BASH_SOURCE[0]}")/adsk_validate.sh" ${PROJECT_ARG:+--project "$PROJECT_ARG"} --simulate "$SCENARIOS"
