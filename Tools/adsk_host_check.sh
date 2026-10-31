#!/usr/bin/env bash
# Prove the plugin stands alone: copy it into a fresh, empty C++ host project that has no other
# project plugin and no project tooling, build the host editor, then run the copy's own
# adsk_check.sh, adsk_validate.sh, adsk_test.sh --standalone and adsk_strict_includes.sh against that
# host. Strict includes runs last because it rebuilds the plugin modules without unity or PCH.
#
# Usage: Plugins/AdamskAI/Tools/adsk_host_check.sh [--association <engine-id>] [--keep] [--out <dir>]
#
#   --association  EngineAssociation written into the host; default: the association of the
#                  project this plugin currently sits in (UE_ROOT still wins for the engine path)
#   --out <dir>    create the host there (must not exist); default: a new mktemp directory
#   --keep         leave the host on disk afterwards (it is deleted otherwise)
#
# The copy excludes Binaries/, Intermediate/ and Saved/, so nothing built for the current project
# leaks in.
#
# Exit codes: the first failing stage's code (adsk_* scripts keep theirs: 1 findings, 2 setup,
# 3 crash/timeout); 1 for a failed host build; 2 usage or setup failure; 0 when every stage passed.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
	HOST="$(mktemp -d "${TMPDIR:-/tmp}/adsk-host.XXXXXX")" || { adsk_err "mktemp failed."; exit 2; }
fi
cleanup() { [ "$KEEP" -eq 1 ] && echo "host kept: $HOST" || rm -rf -- "$HOST"; }
trap cleanup EXIT

adsk_make_host "$HOST" "$ASSOCIATION" '{ "Name": "AdamskAI", "Enabled": true }' || exit $?
NAME="$ADSK_HOST_NAME"
COPY="$HOST/Plugins/AdamskAI/Tools"
adsk_resolve_engine || exit $?
echo "host: $HOST (engine $ASSOCIATION)"

stage() {
	local label="$1"; shift
	echo "== $label"
	"$@"
	local code=$?
	if [ "$code" -ne 0 ]; then
		echo "FAIL adsk_host_check at '$label' exit=$code"
		KEEP=1
		exit "$code"
	fi
}

# The host has no Tools/run_tests.sh and no Tools/lib/preflight.sh; pin both explicitly anyway so
# an environment variable from the calling project cannot change what this run proves.
export ADSK_PREFLIGHT=none
stage "static checks" "$COPY/adsk_check.sh"
stage "build ${NAME}Editor" adsk_build_host
stage "validate" "$COPY/adsk_validate.sh" --project "$ADSK_UPROJECT"
stage "tests" "$COPY/adsk_test.sh" --project "$ADSK_UPROJECT" --standalone "AdamskAI."
stage "strict includes" "$COPY/adsk_strict_includes.sh" --project "$ADSK_UPROJECT"
echo "PASS adsk_host_check"
exit 0
