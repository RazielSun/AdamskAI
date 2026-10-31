#!/usr/bin/env bash
# The usual loop after a change in one run: check, validate, schema, simulate, test. Linux; the Win64 twin is
# Tools/adsk_all_win64.ps1, which also builds (here, build the editor target with the host project's script first).
#
# Usage: Plugins/AdamskAI/Tools/adsk_all.sh [--project <uproject|dir>] [--world] [--filter <prefix>]
#
#   --world    also run the AdamskAIWorld.* tests
#   --filter   the AdamskAI test filter, default "AdamskAI."
#
# A failed step is reported and the next one still runs; a summary lists every step with its exit code and time.
#
# Exit codes: the worst step's, ranked 3 > 2 > 1 > 0 (0 every step clean; see each script for 1, 2 and 3).

set -uo pipefail

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_ARGS=()
WORLD=0
FILTER="AdamskAI."
while [ "$#" -gt 0 ]; do
	case "$1" in
		-h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		--project)
			[ "$#" -ge 2 ] || { echo "ERROR: --project needs a value." >&2; exit 2; }
			PROJECT_ARGS=(--project "$2"); shift 2 ;;
		--world) WORLD=1; shift ;;
		--filter)
			[ "$#" -ge 2 ] || { echo "ERROR: --filter needs a value." >&2; exit 2; }
			FILTER="$2"; shift 2 ;;
		*) echo "ERROR: unknown argument '$1'. Try --help." >&2; exit 2 ;;
	esac
done

WORST=0
SUMMARY=""
run_step() {
	local name="$1" start code
	shift
	echo "==== adsk_all: $name"
	start=$(date +%s)
	"$@"
	code=$?
	# A code outside the contract (a signal) counts as 3.
	[ "$code" -ge 0 ] && [ "$code" -le 3 ] || code=3
	[ "$code" -gt "$WORST" ] && WORST=$code
	SUMMARY+="$(printf '%-24s exit %d  %4ds' "$name" "$code" "$(( $(date +%s) - start ))")"$'\n'
}

run_step check "$TOOLS/adsk_check.sh"
run_step validate "$TOOLS/adsk_validate.sh" "${PROJECT_ARGS[@]}"
run_step schema "$TOOLS/adsk_schema_check.sh" "${PROJECT_ARGS[@]}"
run_step simulate "$TOOLS/adsk_simulate.sh" "${PROJECT_ARGS[@]}"
run_step "test $FILTER" "$TOOLS/adsk_test.sh" "${PROJECT_ARGS[@]}" "$FILTER"
[ "$WORLD" -eq 1 ] && run_step "test AdamskAIWorld." "$TOOLS/adsk_test.sh" "${PROJECT_ARGS[@]}" "AdamskAIWorld."

echo "==== adsk_all: summary"
printf '%s' "$SUMMARY"
if [ "$WORST" -eq 0 ]; then echo "adsk_all: PASS"; else echo "adsk_all: FAIL (exit $WORST)"; fi
exit "$WORST"
