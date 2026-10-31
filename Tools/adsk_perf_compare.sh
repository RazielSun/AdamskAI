#!/usr/bin/env bash
# Compares the last benchmark numbers with this machine's committed baseline and the agreed ceilings (docs/dev/perf.md). Needs
# bash and python3; no engine. Linux; the Win64 twin is Tools/adsk_perf_compare_win64.ps1; both run
# Tools/lib/adsk_perf_compare.py.
#
# Usage: Plugins/AdamskAI/Tools/adsk_perf_compare.sh [--project <uproject|dir>] [--baseline FILE] [--update] [--strict]
#
# Reads <project>/Saved/AdamskAI/perf/*.json (written by adsk_test.sh AdamskAI.Perf. and AdamskAIWorld.Perf.),
# Fixtures/perf/baseline-<hostname>.json and Fixtures/perf/ceilings.json in the plugin. Prints every number with its change;
# a time, byte count or per-agent rate more than 20% higher is marked WORSE, a number above its ceiling OVER CEILING.
# --update writes the current numbers as the baseline: do it deliberately, in the change that explains why.
#
# Exit codes: 0 compared or baseline written (WORSE and OVER CEILING lines are warnings); 1 one of them and
# --strict; 2 usage, no project, python3 missing, no results or no baseline.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

PROJECT_ARG=""
EXTRA=()
while [ "$#" -gt 0 ]; do
	case "$1" in
		--project)
			[ "$#" -ge 2 ] || { adsk_err "--project needs a value."; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		--project=*) PROJECT_ARG="${1#--project=}"; shift ;;
		--baseline)
			[ "$#" -ge 2 ] || { adsk_err "--baseline needs a value."; exit 2; }
			EXTRA+=(--baseline "$2"); shift 2 ;;
		--update|--strict) EXTRA+=("$1"); shift ;;
		*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
	esac
done

command -v python3 >/dev/null 2>&1 || { adsk_err "python3 is required."; exit 2; }
adsk_resolve_project "$PROJECT_ARG" || exit $?

exec python3 -I "$ADSK_PLUGIN_DIR/Tools/lib/adsk_perf_compare.py" "$ADSK_PROJECT_ROOT" "$ADSK_PLUGIN_DIR" "${EXTRA[@]}"
