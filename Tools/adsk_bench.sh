#!/usr/bin/env bash
# Measure one behavior written three ways, an AdamskAI file, a Behavior Tree and a StateTree, side by side
# (Bench/AdamskAIBench, tests AdamskAIBench.Versus.*). Builds a fresh, empty C++ host with this plugin, the bench
# plugin and the engine's StateTree plugins, runs the bench there, and copies its numbers to
# Plugins/AdamskAI/Saved/AdamskAI/perf/versus_*.json. The calling project is not touched.
#
# Usage: Plugins/AdamskAI/Tools/adsk_bench.sh [--association <engine-id>] [--keep] [--out <dir>]
#
#   --association  EngineAssociation written into the host; default: the association of the
#                  project this plugin currently sits in (UE_ROOT still wins for the engine path)
#   --out <dir>    create the host there (must not exist); default: a new mktemp directory
#   --keep         leave the host on disk afterwards (it is deleted otherwise)
#
# Development editor, -nullrhi, like the other benchmarks; docs/dev/perf.md says how to read the numbers.
#
# Exit codes: adsk_test.sh's (1 a failed bench, 2 setup, 3 crash/timeout); 1 for a failed host build; 2 usage or
# setup failure; 0 when the bench ran.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"
. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_host.sh"

ASSOCIATION=""
OUT=""
KEEP=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--association)
			[ "$#" -ge 2 ] || { adsk_err "--association needs a value."; exit 2; }
			ASSOCIATION="$2"; shift 2 ;;
		--out)
			[ "$#" -ge 2 ] || { adsk_err "--out needs a value."; exit 2; }
			OUT="$2"; shift 2 ;;
		--keep) KEEP=1; shift ;;
		*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
	esac
done

command -v rsync >/dev/null 2>&1 || { adsk_err "rsync is required."; exit 2; }
BENCH="$ADSK_PLUGIN_DIR/Bench/AdamskAIBench"
[ -f "$BENCH/AdamskAIBench.uplugin" ] || { adsk_err "no bench plugin at $BENCH."; exit 2; }
if [ -z "$ASSOCIATION" ]; then
	adsk_resolve_project "" || { adsk_err "pass --association when the plugin is not inside a project."; exit 2; }
	ASSOCIATION="$(sed -n 's/.*"EngineAssociation"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ADSK_UPROJECT")"
	[ -n "$ASSOCIATION" ] || { adsk_err "no EngineAssociation in $ADSK_UPROJECT; pass --association."; exit 2; }
fi

if [ -n "$OUT" ]; then
	[ ! -e "$OUT" ] || { adsk_err "--out '$OUT' already exists."; exit 2; }
	mkdir -p "$OUT" || { adsk_err "cannot create $OUT."; exit 2; }
	HOST="$(cd "$OUT" && pwd -P)"
else
	HOST="$(mktemp -d "${TMPDIR:-/tmp}/adsk-bench.XXXXXX")" || { adsk_err "mktemp failed."; exit 2; }
fi
cleanup() { [ "$KEEP" -eq 1 ] && echo "host kept: $HOST" || rm -rf -- "$HOST"; }
trap cleanup EXIT

adsk_make_host "$HOST" "$ASSOCIATION" \
	'{ "Name": "AdamskAI", "Enabled": true }, { "Name": "AdamskAIBench", "Enabled": true }, { "Name": "StateTree", "Enabled": true }, { "Name": "GameplayStateTree", "Enabled": true }' \
	|| exit $?
rsync -a --exclude=/Binaries --exclude=/Intermediate "$BENCH/" "$HOST/Plugins/AdamskAIBench/" || { adsk_err "copying the bench failed."; exit 2; }
adsk_resolve_engine || exit $?
echo "host: $HOST (engine $ASSOCIATION)"

export ADSK_PREFLIGHT=none
echo "== build ${ADSK_HOST_NAME}Editor"
adsk_build_host || { KEEP=1; exit 1; }
echo "== bench"
"$HOST/Plugins/AdamskAI/Tools/adsk_test.sh" --project "$ADSK_UPROJECT" --standalone "AdamskAIBench."
CODE=$?

RESULTS="$ADSK_PLUGIN_DIR/Saved/AdamskAI/perf"
mkdir -p "$RESULTS"
for FILE in "$HOST"/Saved/AdamskAI/perf/versus_*.json; do
	[ -f "$FILE" ] || continue
	cp "$FILE" "$RESULTS/"
	echo "$(basename "$FILE"): $(cat "$FILE")"
done
[ "$CODE" -eq 0 ] || { KEEP=1; echo "FAIL adsk_bench exit=$CODE"; exit "$CODE"; }
echo "PASS adsk_bench (numbers in $RESULTS)"
exit 0
