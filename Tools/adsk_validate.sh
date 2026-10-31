#!/usr/bin/env bash
# Validate AdamskAI behavior files headlessly: no renderer, no client, no PIE. Linux; the Win64 twin is Tools/adsk_validate_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_validate.sh [--project <uproject|dir>] [mode]
#
#   --all (default)   --default, --fixtures, --catalog and --xsd, plus the checks of the project's
#                     full catalogue <BehaviorRoot>/node-catalog.md and schema adamskai.xsd (required
#                     once the project registers node providers; skipped with a note when it has none)
#   --default         the plugin Examples against the plugin catalogue, then the project's
#                     behavior root (Content/<BehaviorRoot>, default Content/AI)
#   --fixtures        the plugin's valid/invalid fixtures
#   --catalog         check docs/node-catalog.md against the plugin catalogue (never writes)
#   --xsd             check docs/adamskai.xsd against the plugin catalogue (never writes)
#   --paths <p[+p]>   validate files or directories against the full catalogue
#   --simulate <p[+p]> run scenario files or directories of them (Tools/adsk_simulate.sh supplies defaults)
#
# Each check is one `UnrealEditor-Cmd -run=AdamskAIValidate` run; its full log is
# Saved/Logs/adsk_validate_<check>.log. stdout carries only structured diagnostics
# (`path:line: severity code [node]: message`), stale-catalogue lines, each run's summary and a
# final PASS/FAIL line.
#
# Exit codes: 0 clean; 1 diagnostics or a stale catalogue; 2 usage, project, engine or write
# failure; 3 timeout, signal, crash or a missing `ADSK_VALIDATE_COMPLETE` marker, which is
# never a pass. Several checks report the worst code, ranked 3 > 2 > 1 > 0.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

PROJECT_ARG=""
MODE="all"
PATHS_ARG=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--project)
			[ "$#" -ge 2 ] || { adsk_err "--project needs a value."; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		--project=*) PROJECT_ARG="${1#--project=}"; shift ;;
		--all|--default|--fixtures|--catalog|--xsd) MODE="${1#--}"; shift ;;
		--paths|--simulate)
			[ "$#" -ge 2 ] || { adsk_err "$1 needs a value."; exit 2; }
			MODE="${1#--}"; PATHS_ARG="$2"; shift 2 ;;
		--paths=*) MODE="paths"; PATHS_ARG="${1#--paths=}"; shift ;;
		--simulate=*) MODE="simulate"; PATHS_ARG="${1#--simulate=}"; shift ;;
		*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
	esac
done
if { [ "$MODE" = "paths" ] || [ "$MODE" = "simulate" ]; } && [ -z "$PATHS_ARG" ]; then
	adsk_err "--$MODE needs a non-empty value."
	exit 2
fi
# The editor resolves a relative path against its own binaries folder, so each part is made absolute here, against
# the caller's directory.
if [ -n "$PATHS_ARG" ]; then
	ABSOLUTE_PATHS=""
	IFS='+' read -r -a PATH_PARTS <<< "$PATHS_ARG"
	for PART in "${PATH_PARTS[@]}"; do
		case "$PART" in /*) ;; *) PART="$PWD/$PART" ;; esac
		ABSOLUTE_PATHS="${ABSOLUTE_PATHS:+$ABSOLUTE_PATHS+}$PART"
	done
	PATHS_ARG="$ABSOLUTE_PATHS"
fi

adsk_resolve_project "$PROJECT_ARG" || exit $?
adsk_resolve_engine || exit $?
adsk_preflight || exit $?

LOG_DIR="$ADSK_PROJECT_ROOT/Saved/Logs"
mkdir -p "$LOG_DIR" || { adsk_err "cannot create $LOG_DIR."; exit 2; }
TIMEOUT_SECONDS="${ADSK_VALIDATE_TIMEOUT:-600}"
WORST=0

worse() {
	# 3 > 2 > 1 > 0
	local rank_a rank_b
	case "$1" in 3) rank_a=4 ;; 2) rank_a=3 ;; 1) rank_a=2 ;; *) rank_a=1 ;; esac
	case "$2" in 3) rank_b=4 ;; 2) rank_b=3 ;; 1) rank_b=2 ;; *) rank_b=1 ;; esac
	[ "$rank_a" -ge "$rank_b" ] && echo "$1" || echo "$2"
}

# run_check <label> <commandlet args...>: sets CHECK_EXIT from the log and prints its structured lines.
run_check() {
	local label="$1"; shift
	local log="$LOG_DIR/adsk_validate_$label.log" status marker
	timeout --signal=TERM --kill-after=30s "${TIMEOUT_SECONDS}s" \
		"$ADSK_EDITOR_CMD" "$ADSK_UPROJECT" -run=AdamskAIValidate "$@" \
		-unattended -nullrhi -nosplash -nopause -stdout >"$log" 2>&1
	status=$?
	sed -n 's/^\(\[[^]]*\]\[[^]]*\]\)\{0,1\}LogAdamskAIValidate: Display: //p' "$log" | grep -a -v '^ADSK_VALIDATE_COMPLETE'
	marker="$(sed -n 's/^.*LogAdamskAIValidate: Display: ADSK_VALIDATE_COMPLETE exit=\([0-9][0-9]*\).*$/\1/p' "$log" | tail -1)"
	if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
		echo "$label: timed out after ${TIMEOUT_SECONDS}s. Full log: $log"; CHECK_EXIT=3
	elif [ "$status" -ge 128 ]; then
		echo "$label: editor died on signal $((status - 128)). Full log: $log"; CHECK_EXIT=3
	elif [ -z "$marker" ]; then
		echo "$label: no ADSK_VALIDATE_COMPLETE marker (crash or the commandlet never ran). Full log: $log"; CHECK_EXIT=3
	elif [ "$marker" -gt 2 ] || [ "$marker" -ne "$status" ]; then
		echo "$label: marker exit=$marker disagrees with editor status $status. Full log: $log"; CHECK_EXIT=3
	else
		CHECK_EXIT="$marker"
	fi
}

check() {
	local label="$1"; shift
	run_check "$label" "$@"
	WORST="$(worse "$WORST" "$CHECK_EXIT")"
}

case "$MODE" in
	default) check default ;;
	fixtures) check fixtures -fixtures ;;
	catalog) check catalog -catalog="$ADSK_PLUGIN_DIR/docs/node-catalog.md" -registry=plugin -check ;;
	xsd) check xsd -xsd="$ADSK_PLUGIN_DIR/docs/adamskai.xsd" -registry=plugin -check ;;
	paths) check paths "-paths=$PATHS_ARG" ;;
	simulate) check simulate "-simulate=$PATHS_ARG" ;;
	# default, fixtures, and the plugin's and the project's catalogue and schema checks in one editor launch
	# (-all). The commandlet resolves the behavior root and whether the project has provider nodes, so a
	# project without any passes without its own catalogue and schema.
	all) check all -all ;;
esac

if [ "$WORST" -eq 0 ]; then
	echo "PASS adsk_validate [$MODE]"
else
	echo "FAIL adsk_validate [$MODE] exit=$WORST"
fi
exit "$WORST"
