# Shared project and engine resolution for the AdamskAI plugin scripts. Sourced by the Tools/adsk_*.sh scripts
# that need a project or an engine; never executed.
#
#   . "$(dirname "$0")/lib/adsk_env.sh"
#   adsk_resolve_project "$EXPLICIT_PROJECT" || exit $?
#   adsk_resolve_engine || exit $?
#
# Defines ADSK_PLUGIN_DIR, then ADSK_PROJECT_ROOT, ADSK_UPROJECT, ADSK_UE_ROOT and
# ADSK_EDITOR_CMD. Every failure returns 2 and says what to do. Nothing here assumes how many
# directories sit between the plugin and the project: the project is the explicit --project,
# else the Git worktree root, else the nearest ancestor, each holding exactly one .uproject.

ADSK_PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"

adsk_err() { echo "ERROR: $*" >&2; }

# Echo the only .uproject directly inside a directory; nonzero when there are none or several.
_adsk_single_uproject() {
	local dir="$1" found=() candidate
	for candidate in "$dir"/*.uproject; do
		[ -f "$candidate" ] && found+=("$candidate")
	done
	[ "${#found[@]}" -eq 1 ] || return 1
	printf '%s\n' "${found[0]}"
}

adsk_resolve_project() {
	local explicit="${1:-}" uproject="" root
	if [ "$(uname)" != "Linux" ]; then
		adsk_err "unsupported platform '$(uname)'. These scripts are Linux-only."
		return 2
	fi
	if [ -n "$explicit" ]; then
		if [ -f "$explicit" ] && [ "${explicit##*.}" = "uproject" ]; then
			uproject="$explicit"
		elif [ -d "$explicit" ]; then
			uproject="$(_adsk_single_uproject "$explicit")" || {
				adsk_err "--project '$explicit' is a directory without exactly one .uproject."
				return 2
			}
		else
			adsk_err "--project '$explicit' is neither a .uproject file nor a directory."
			return 2
		fi
	else
		root="$(git -C "$ADSK_PLUGIN_DIR" rev-parse --show-toplevel 2>/dev/null)" || root=""
		if [ -n "$root" ]; then
			uproject="$(_adsk_single_uproject "$root")" || {
				adsk_err "the Git root '$root' does not hold exactly one .uproject; pass --project."
				return 2
			}
		else
			root="$ADSK_PLUGIN_DIR"
			while [ "$root" != "/" ]; do
				root="$(dirname "$root")"
				uproject="$(_adsk_single_uproject "$root")" && break
				uproject=""
			done
			[ -n "$uproject" ] || { adsk_err "no project found above the plugin; pass --project."; return 2; }
		fi
	fi
	ADSK_UPROJECT="$(cd "$(dirname "$uproject")" && pwd -P)/$(basename "$uproject")"
	ADSK_PROJECT_ROOT="$(dirname "$ADSK_UPROJECT")"
}

# UE_ROOT wins, else the .uproject's EngineAssociation looked up in
# Install.ini. Never a path baked into a tracked file.
adsk_resolve_engine() {
	local assoc
	assoc="$(sed -n 's/.*"EngineAssociation"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ADSK_UPROJECT")"
	ADSK_UE_ROOT="${UE_ROOT:-}"
	if [ -z "$ADSK_UE_ROOT" ]; then
		ADSK_UE_ROOT="$(sed -n "s|^UE_${assoc}=||p" ~/.config/Epic/UnrealEngine/Install.ini 2>/dev/null | head -1)"
	fi
	ADSK_EDITOR_CMD="$ADSK_UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
	if [ -z "$ADSK_UE_ROOT" ] || [ ! -x "$ADSK_EDITOR_CMD" ]; then
		adsk_err "no engine for association '$assoc'. Set UE_ROOT to an engine root containing"
		echo "       Engine/Binaries/Linux/UnrealEditor-Cmd, or register it in ~/.config/Epic/UnrealEngine/Install.ini." >&2
		return 2
	fi
	command -v timeout >/dev/null 2>&1 && timeout --help 2>&1 | grep -q -- "--kill-after" || {
		adsk_err "GNU timeout (coreutils) is required."
		return 2
	}
}

# Host preflight before any editor launch: ADSK_PREFLIGHT=<executable>|none, else the project's
# Tools/lib/preflight.sh (in a subshell, so its `exit` cannot end this script).
adsk_preflight() {
	if [ -n "${ADSK_PREFLIGHT:-}" ]; then
		[ "$ADSK_PREFLIGHT" = "none" ] && return 0
		[ -x "$ADSK_PREFLIGHT" ] || { adsk_err "ADSK_PREFLIGHT '$ADSK_PREFLIGHT' is not executable."; return 2; }
		"$ADSK_PREFLIGHT" || return $?
		return 0
	fi
	local lib="$ADSK_PROJECT_ROOT/Tools/lib/preflight.sh"
	[ -f "$lib" ] || return 0
	( . "$lib" && preflight_require_clean_modules )
}
