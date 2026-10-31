#!/usr/bin/env bash
# Check that the generated XML Schemas still accept every valid behavior file. The language part of adamskai.xsd is
# written by hand in ADSKSchemaXsd.cpp, so a new attribute the compiler takes but the schema lacks would underline
# correct files in the IDE; this catches it. No engine needed. Linux; the Win64 twin is Tools/adsk_schema_check_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_schema_check.sh [--project <uproject|dir>]
#
# Checks docs/adamskai.xsd against Examples, Demo, Fixtures/valid and Fixtures/conformance, and the project's schema
# (Content/<BehaviorRoot>/adamskai.xsd, else the plugin's) against the behavior root and Fixtures/world. Run
# adsk_validate first: it checks that both schemas are current. Needs xmllint (libxml2-utils).
#
# Exit codes: 0 every file passes; 1 a file a schema refuses; 2 usage, project, a missing xmllint or a schema
# that does not load.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

PROJECT_ARG=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--project)
			[ "$#" -ge 2 ] || { adsk_err "--project needs a value."; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		--project=*) PROJECT_ARG="${1#--project=}"; shift ;;
		*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
	esac
done

command -v xmllint >/dev/null 2>&1 || { adsk_err "xmllint not found (install libxml2-utils)."; exit 2; }
adsk_resolve_project "$PROJECT_ARG" || exit $?

ROOT="AI"
INI="$ADSK_PROJECT_ROOT/Config/DefaultGame.ini"
if [ -f "$INI" ]; then
	FOUND="$(sed -n 's/^[[:space:]]*BehaviorRoot[[:space:]]*=[[:space:]]*"\{0,1\}\([^"]*\)"\{0,1\}[[:space:]]*$/\1/p' "$INI" | tail -1)"
	[ -n "$FOUND" ] && ROOT="$FOUND"
fi
BEHAVIOR_ROOT="$ADSK_PROJECT_ROOT/Content/$ROOT"
PLUGIN_SCHEMA="$ADSK_PLUGIN_DIR/docs/adamskai.xsd"
PROJECT_SCHEMA="$BEHAVIOR_ROOT/adamskai.xsd"
[ -f "$PROJECT_SCHEMA" ] || PROJECT_SCHEMA="$PLUGIN_SCHEMA"

PROBLEMS=0
FILES=0

# check_schema <xsd> <dir...>
check_schema() {
	local xsd="$1"; shift
	xmllint --noout "$xsd" >/dev/null 2>&1 || { adsk_err "schema $xsd does not load."; exit 2; }
	local dir file
	for dir in "$@"; do
		[ -d "$dir" ] || continue
		while IFS= read -r -d '' file; do
			FILES=$((FILES + 1))
			if ! xmllint --noout --nonet --schema "$xsd" "$file" >/dev/null 2>"${TMPDIR:-/tmp}/adsk_schema_check.err"; then
				sed 's/^/error schema: /' "${TMPDIR:-/tmp}/adsk_schema_check.err"
				PROBLEMS=$((PROBLEMS + 1))
			fi
		done < <(find "$dir" -name '*.xml' -print0 | sort -z)
	done
}

check_schema "$PLUGIN_SCHEMA" "$ADSK_PLUGIN_DIR/Examples" "$ADSK_PLUGIN_DIR/Demo" "$ADSK_PLUGIN_DIR/Fixtures/valid" "$ADSK_PLUGIN_DIR/Fixtures/conformance"
check_schema "$PROJECT_SCHEMA" "$BEHAVIOR_ROOT" "$ADSK_PLUGIN_DIR/Fixtures/world"

if [ "$PROBLEMS" -eq 0 ]; then
	echo "PASS adsk_schema_check: $FILES files"
	exit 0
fi
echo "FAIL adsk_schema_check: $PROBLEMS file(s) refused of $FILES"
exit 1
