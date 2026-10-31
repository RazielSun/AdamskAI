#!/usr/bin/env bash
# Compile every AdamskAI module with unity and precompiled headers off, the configuration a user's
# project may run and the normal dev loop never does. A unity build lets a file borrow an include
# from whichever translation unit was concatenated before it; this proves each file includes what
# it uses. Win64 twin: Tools/adsk_strict_includes_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_strict_includes.sh [--project <uproject|dir>]
#
# One UBT run per target:module build, `-DisableUnity -NoPCH -NoSharedPCH`, Linux Development. No
# tracked file is touched. The plugin's own modules are rebuilt, the engine's actions stay cached,
# and the next normal build recompiles those modules once.
#
# A build whose every compiler error is outside this plugin's Source/ (an engine module or another
# plugin of the host that is not strict-clean itself) is BLOCKED, not FAILED: it is listed, does not fail
# the run, and proves nothing about this plugin's files in that module. adsk_host_check.sh runs this
# script in a host without third-party plugins, where no build can be blocked.
#
# Exit codes: 0 every build compiled or was blocked; 1 a build failed in this plugin's code (its errors
# are printed); 2 usage, project or engine failure.

set -uo pipefail

case "${1:-}" in
	-h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

adsk_resolve_project "$PROJECT_ARG" || exit $?
adsk_resolve_engine || exit $?
adsk_preflight || exit $?

BUILD="$ADSK_UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
[ -x "$BUILD" ] || { adsk_err "no Linux Build.sh at $BUILD."; exit 2; }
NAME="$(basename "$ADSK_UPROJECT" .uproject)"
LOG_DIR="$ADSK_PROJECT_ROOT/Saved/Logs"
mkdir -p "$LOG_DIR" || { adsk_err "cannot create $LOG_DIR."; exit 2; }

# The game target is monolithic and carries only Runtime modules (AdamskAIDebug and AdamskAIDemo
# included outside Shipping); Editor and DeveloperTool modules build in the editor target.
BUILDS=(
	"${NAME}Editor:AdamskAI"
	"${NAME}Editor:AdamskAIHost"
	"${NAME}Editor:AdamskAIDebug"
	"${NAME}Editor:AdamskAIDemo"
	"${NAME}Editor:AdamskAIEditor"
	"${NAME}Editor:AdamskAITests"
	"${NAME}:AdamskAI"
	"${NAME}:AdamskAIHost"
	"${NAME}:AdamskAIDebug"
	"${NAME}:AdamskAIDemo"
)

echo "strict-includes: unity OFF, PCH OFF, ${#BUILDS[@]} builds, project $ADSK_UPROJECT"
FAILED=0
BLOCKED=0
for ENTRY in "${BUILDS[@]}"; do
	TARGET="${ENTRY%%:*}"
	MODULE="${ENTRY##*:}"
	LOG="$LOG_DIR/adsk_strict_${TARGET}_${MODULE}.log"
	printf '  %-28s %-16s ' "$TARGET" "$MODULE"
	if "$BUILD" "$TARGET" Linux Development "-project=$ADSK_UPROJECT" "-Module=$MODULE" \
		-DisableUnity -NoPCH -NoSharedPCH >"$LOG" 2>&1; then
		# UBT can exit 0 without building; the Result line is what it actually says.
		if grep -q "^Result: Succeeded" "$LOG"; then
			echo "ok"
		else
			echo "UNCLEAR (no 'Result: Succeeded'). Log: $LOG"
			FAILED=1
		fi
	elif grep -E "error:" "$LOG" | grep -q -F "$ADSK_PLUGIN_DIR/Source/" || ! grep -q "error:" "$LOG"; then
		echo "FAILED. Log: $LOG"
		grep -E "error:|^Result:" "$LOG" | head -20 | sed 's/^/      /'
		FAILED=1
	else
		echo "BLOCKED by code outside this plugin. Log: $LOG"
		grep -E "error:" "$LOG" | head -5 | sed 's/^/      /'
		BLOCKED=$((BLOCKED + 1))
	fi
done

if [ "$FAILED" -ne 0 ]; then
	echo "FAIL adsk_strict_includes"
	exit 1
fi
if [ "$BLOCKED" -ne 0 ]; then
	echo "PASS adsk_strict_includes ($BLOCKED build(s) blocked by code outside this plugin; see above)"
	exit 0
fi
echo "PASS adsk_strict_includes"
exit 0
