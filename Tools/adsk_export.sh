#!/usr/bin/env bash
# A clean copy of the plugin for a game project's Plugins/ folder: the files a package carries, from a commit, without
# the repository's own parts (CI, the bench plugin, docs/dev, the README's pictures). Needs bash, python3 and git; no
# engine. The same set RunUAT BuildPlugin packages (its default folders, then Config/FilterPlugin.ini), as source.
# Linux; on Windows, Tools/adsk_buildplugin_win64.ps1 builds the package through BuildPlugin itself.
#
# Usage: Plugins/AdamskAI/Tools/adsk_export.sh --out <dir> [--rev <commit>]
#
#   --out   a new or empty directory, e.g. <Project>/Plugins/AdamskAI
#   --rev   the commit to export, default HEAD; uncommitted changes are never exported
#
# Exit codes: 0 written, 2 usage, git or write failure (nothing is left at --out).

set -uo pipefail

case "${1:-}" in
	-h|--help|"") sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; [ -n "${1:-}" ] && exit 0 || exit 2 ;;
esac

. "$(dirname "${BASH_SOURCE[0]}")/lib/adsk_env.sh"

OUT=""
REV="HEAD"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--out) [ "$#" -ge 2 ] || { adsk_err "--out needs a value."; exit 2; }; OUT="$2"; shift 2 ;;
		--out=*) OUT="${1#--out=}"; shift ;;
		--rev) [ "$#" -ge 2 ] || { adsk_err "--rev needs a value."; exit 2; }; REV="$2"; shift 2 ;;
		--rev=*) REV="${1#--rev=}"; shift ;;
		*) adsk_err "unknown argument '$1'. Try --help."; exit 2 ;;
	esac
done
[ -n "$OUT" ] || { adsk_err "--out is required."; exit 2; }
command -v python3 >/dev/null 2>&1 || { adsk_err "python3 is required."; exit 2; }
command -v git >/dev/null 2>&1 || { adsk_err "git is required."; exit 2; }

python3 -I "$ADSK_PLUGIN_DIR/Tools/lib/adsk_export.py" "$ADSK_PLUGIN_DIR" "$REV" "$OUT"
