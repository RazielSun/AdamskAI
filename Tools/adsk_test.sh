#!/usr/bin/env bash
# Run the AdamskAI automation tests headlessly. Linux; the Win64 twin is Tools/adsk_test_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_test.sh [--project <uproject|dir>] [--standalone] [filter]
#
#   filter         automation test path prefix, default "AdamskAI."
#   --standalone   skip the project's Tools/run_tests.sh even when it exists
#
# With a project Tools/run_tests.sh, this delegates to it and returns its exit code unchanged.
# Otherwise it runs `Automation RunTests <filter>` then `Automation Quit` in a fresh report directory
# and reads the structured report the way the project tooling does (index.json first, log markers
# only as a fallback: `**** TEST COMPLETE. EXIT CODE: N ****` and `Automation Test Queue Empty N tests performed`).
# The commands are comma-separated, as in the project tooling; a semicolon-separated form was
# observed to leave the editor running after the tests, so it is not used.
# The engine log goes to Saved/Logs/adsk_test.log, the process's stdout to Saved/Logs/adsk_test.stdout.log.
#
# Exit codes: 0 every executed test passed; 1 a test failed or none ran; 2 usage, project or
# engine failure; 3 timeout, crash, or a missing/incomplete completion.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

PROJECT_ARG=""
STANDALONE=0
FILTER="AdamskAI."
FILTER_SET=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--project)
			[ "$#" -ge 2 ] || { adsk_err "--project needs a value."; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		--project=*) PROJECT_ARG="${1#--project=}"; shift ;;
		--standalone) STANDALONE=1; shift ;;
		-*) adsk_err "unknown flag '$1'. Try --help."; exit 2 ;;
		*)
			[ "$FILTER_SET" -eq 0 ] || { adsk_err "at most one filter argument."; exit 2; }
			FILTER="$1"; FILTER_SET=1; shift ;;
	esac
done
case "$FILTER" in
	"") adsk_err "empty filter."; exit 2 ;;
	*,*) adsk_err "filter must not contain a comma: -ExecCmds splits on it."; exit 2 ;;
	*$'\n'*|*$'\r'*) adsk_err "filter must not contain a newline or carriage return."; exit 2 ;;
esac

adsk_resolve_project "$PROJECT_ARG" || exit $?

if [ "$STANDALONE" -eq 0 ] && [ -x "$ADSK_PROJECT_ROOT/Tools/run_tests.sh" ]; then
	exec "$ADSK_PROJECT_ROOT/Tools/run_tests.sh" "$FILTER"
fi

adsk_resolve_engine || exit $?
adsk_preflight || exit $?

REPORT_DIR="$ADSK_PROJECT_ROOT/Saved/AdamskAI/automation"
LOG_DIR="$ADSK_PROJECT_ROOT/Saved/Logs"
LOG="$LOG_DIR/adsk_test.log"
# The engine writes its own log to $LOG (-abslog): stdout is shared with the UnrealTraceServer the editor forks, whose
# output overwrote the tail of a redirected stdout, completion markers included. stdout goes to a file of its own.
STDOUT_LOG="$LOG_DIR/adsk_test.stdout.log"
# The report directory is deleted before every run so a stale one can never read as a pass.
if [ -L "$REPORT_DIR" ] || { [ -e "$REPORT_DIR" ] && [ ! -d "$REPORT_DIR" ]; }; then
	adsk_err "$REPORT_DIR is a symlink or not a directory; refusing to delete it."
	exit 2
fi
rm -rf -- "$REPORT_DIR" && mkdir -p "$REPORT_DIR" "$LOG_DIR" && rm -f -- "$LOG" "$STDOUT_LOG" || {
	adsk_err "cannot prepare report and log directories."; exit 2; }

echo "Running tests [$FILTER]..."
timeout --signal=TERM --kill-after=30s "${ADSK_TEST_TIMEOUT:-900}s" \
	"$ADSK_EDITOR_CMD" "$ADSK_UPROJECT" "-ReportExportPath=$REPORT_DIR" "-abslog=$LOG" \
	-unattended -nullrhi -nosplash -nopause \
	"-ExecCmds=Automation RunTests $FILTER, Automation Quit," >"$STDOUT_LOG" 2>&1
STATUS=$?

TIMED_OUT=0
if [ "$STATUS" -eq 124 ] || [ "$STATUS" -eq 137 ]; then TIMED_OUT=1; fi
command -v python3 >/dev/null 2>&1 || { adsk_err "python3 is required to read the report."; exit 3; }

# The verdict lives in Tools/lib/adsk_test_report.py, shared with adsk_test_win64.ps1: the structured report decides; only a
# missing or malformed one falls back to the log's completion markers; neither means exit 3.
exec python3 -I "$ADSK_PLUGIN_DIR/Tools/lib/adsk_test_report.py" "$REPORT_DIR/index.json" "$LOG" "$FILTER" "$STATUS" "$TIMED_OUT"
