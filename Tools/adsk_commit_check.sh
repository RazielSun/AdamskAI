#!/usr/bin/env bash
# Check that commit messages and pull request descriptions say what was verified. Needs bash, python3 and git; no
# engine, no project. Linux; the Win64 twin is Tools/adsk_commit_check_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_commit_check.sh [--range <a..b> | --message <file> | --pr-body <file>]
#
#   --range <a..b>     every non-merge commit in the range that touches this plugin (default origin/main..HEAD)
#   --message <file>   one commit message, as a commit-msg hook or an orchestrator has it
#   --pr-body <file>   a pull request description
#
# Rules (each finding prints as `<commit|file>: rule: message`):
#   commit-subject     a non-empty subject line, then a blank line
#   commit-verified    the body has a `Test: <command and result>` or `Not verified: <what>` line
#   commit-decision    every `ADR-NNNN` names an existing docs/dev/decisions/NNNN-*.md (also in a PR description)
#   pr-section         the template's What changed, Commands run, Seen to fail and Not verified sections are
#                      present and not empty once its <!-- hints --> are removed
#
# Exit codes: 0 clean, 1 findings, 2 usage, missing python3 or a git failure (an unknown range).

set -uo pipefail

MODE="--range"
VALUE="origin/main..HEAD"
case "${1:-}" in
	-h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	"") ;;
	--range|--message|--pr-body)
		[ "$#" -eq 2 ] || { echo "ERROR: $1 needs exactly one value." >&2; exit 2; }
		MODE="$1"; VALUE="$2" ;;
	*) echo "ERROR: unknown argument '$1'. Try --help." >&2; exit 2 ;;
esac

PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 is required." >&2; exit 2; }
command -v git >/dev/null 2>&1 || { echo "ERROR: git is required." >&2; exit 2; }

exec python3 -I "$PLUGIN_DIR/Tools/lib/adsk_commit_check.py" "$PLUGIN_DIR" "$MODE" "$VALUE"
