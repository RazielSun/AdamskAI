#!/usr/bin/env bash
# Run the scripts' own failure paths and check each exit code: a check that has never been seen to fail proves
# nothing. Linux; the Win64 twin is Tools/adsk_selftest_win64.ps1.
#
# Usage: Plugins/AdamskAI/Tools/adsk_selftest.sh [--project <uproject|dir>] [--static]
#
# Static cases (no engine, no project; needs python3, git and xmllint):
#   check-clean          adsk_check on this plugin                                       exit 0
#   check-side-effect    adsk_check on a copy with check(Mutate()) planted                exit 1
#   check-decision       adsk_check on a copy with a decision citing no test              exit 1
#   commit-clean         adsk_commit_check --message with a Test: line                    exit 0
#   commit-unverified    adsk_commit_check --message with neither Test: nor Not verified: exit 1
#   commit-decision      adsk_commit_check --message naming ADR-0999                      exit 1
#   commit-range         adsk_commit_check --range <a revision that does not exist>       exit 2
#   pr-template          adsk_commit_check --pr-body on the unfilled template             exit 1
#   schema-refused       adsk_schema_check on a project with a file the schema refuses    exit 1
# Engine cases (left out with --static; build the editor target first, no editor of the project running):
#   validate-broken      adsk_validate --paths <a broken .xml>                            exit 1
#   validate-missing     adsk_validate --paths <a relative path to no file>               exit 1, "no such file"
#   validate-project     adsk_validate --project <a directory without a .uproject>        exit 2
#   test-no-match        adsk_test --standalone <a filter matching nothing>                exit 1
#   test-timeout         adsk_test --standalone with ADSK_TEST_TIMEOUT=3                   exit 3
#   preflight-refused    adsk_test --standalone with ADSK_PREFLIGHT exiting 2              exit 2
#   preflight-missing    adsk_test --standalone with ADSK_PREFLIGHT not executable         exit 2
# The Linux scripts leave refusing a running editor to the host preflight, which the two preflight cases cover; the
# Win64 twin's editor-open cases have no Linux counterpart.
#
# Exit codes: 0 every case got its exit code; 1 a case did not; 2 setup (a tool, the project, a running editor).

set -uo pipefail

PROJECT_ARG=""
STATIC=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		-h|--help) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		--static) STATIC=1; shift ;;
		--project)
			[ "$#" -ge 2 ] || { echo "ERROR: --project needs a value." >&2; exit 2; }
			PROJECT_ARG="$2"; shift 2 ;;
		*) echo "ERROR: unknown argument '$1'. Try --help." >&2; exit 2 ;;
	esac
done

TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PLUGIN_DIR="$(dirname "$TOOLS")"
for tool in python3 git xmllint; do
	command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool is required." >&2; exit 2; }
done

if [ "$STATIC" -eq 0 ]; then
	. "$TOOLS/lib/adsk_env.sh"
	adsk_resolve_project "$PROJECT_ARG" || exit 2
	adsk_resolve_engine || exit 2
	# Listed, never killed: a running editor holds the modules and would answer for the scripts.
	RUNNING=""
	for proc in /proc/[0-9]*; do
		case "$(readlink "$proc/exe" 2>/dev/null)" in
			*/UnrealEditor|*/UnrealEditor-Cmd)
				tr '\0' ' ' <"$proc/cmdline" 2>/dev/null | grep -qF "$ADSK_UPROJECT" && RUNNING+=" ${proc#/proc/}" ;;
		esac
	done
	[ -z "$RUNNING" ] || { echo "ERROR: an editor for $ADSK_UPROJECT is running (PID$RUNNING); close it first." >&2; exit 2; }
fi

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/adsk_selftest.XXXXXX")" || { echo "ERROR: cannot create a scratch directory." >&2; exit 2; }
trap 'rm -rf -- "$SCRATCH"' EXIT
FAILED=0

# expect <case> <code> <text or ""> <command...>: runs the command, compares its exit code and output.
expect() {
	local name="$1" code="$2" contains="$3" out="$SCRATCH/out" actual
	shift 3
	"$@" >"$out" 2>&1
	actual=$?
	if [ "$actual" -eq "$code" ] && { [ -z "$contains" ] || grep -qF -- "$contains" "$out"; }; then
		echo "PASS $name (exit $actual)"
		return
	fi
	FAILED=$((FAILED + 1))
	echo "FAIL $name: exit $actual, expected $code${contains:+ with '$contains'}"
	tail -n 15 "$out" | sed 's/^/    /'
}

HEADER="// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE."

expect check-clean 0 "" "$TOOLS/adsk_check.sh"

# Copies of Source and Tools: adsk_check reads the plugin its script sits in.
COPY="$SCRATCH/SideEffect/AdamskAI"
mkdir -p "$COPY" && cp -R "$PLUGIN_DIR/Source" "$PLUGIN_DIR/Tools" "$COPY/"
printf '%s\n\nstatic int Mutate() { return 1; }\nstatic void Planted() { check(Mutate()); }\n' "$HEADER" \
	>"$COPY/Source/AdamskAI/Private/SelfTestPlanted.cpp"
expect check-side-effect 1 "check-side-effect" "$COPY/Tools/adsk_check.sh"

COPY="$SCRATCH/Decision/AdamskAI"
mkdir -p "$COPY/docs/dev/decisions" && cp -R "$PLUGIN_DIR/Source" "$PLUGIN_DIR/Tools" "$COPY/"
printf '# 0999: Planted\n\nStatus: Accepted, 2026-01-01\nEvidence: [AdamskAI.NoSuchSelfTest]\n' \
	>"$COPY/docs/dev/decisions/0999-planted.md"
expect check-decision 1 "no test registers 'AdamskAI.NoSuchSelfTest'" "$COPY/Tools/adsk_check.sh"

printf 'fix: planted\n\nBody.\nTest: adsk_selftest.sh\n' >"$SCRATCH/clean.msg"
expect commit-clean 0 "" "$TOOLS/adsk_commit_check.sh" --message "$SCRATCH/clean.msg"
printf 'fix: planted\n\nBody without a verification line.\n' >"$SCRATCH/unverified.msg"
expect commit-unverified 1 "commit-verified" "$TOOLS/adsk_commit_check.sh" --message "$SCRATCH/unverified.msg"
printf 'fix: planted\n\nTest: none, docs only\nRefs: ADR-0999\n' >"$SCRATCH/decision.msg"
expect commit-decision 1 "ADR-0999" "$TOOLS/adsk_commit_check.sh" --message "$SCRATCH/decision.msg"
expect commit-range 2 "" "$TOOLS/adsk_commit_check.sh" --range "adsk-selftest-no-such-revision..HEAD"
expect pr-template 1 "pr-section" "$TOOLS/adsk_commit_check.sh" --pr-body "$PLUGIN_DIR/.github/pull_request_template.md"

# A scratch project around a copy of Tools and docs, whose behavior root holds a file the schema refuses.
SCHEMA_HOST="$SCRATCH/SchemaHost"
mkdir -p "$SCHEMA_HOST/Plugins/AdamskAI" "$SCHEMA_HOST/Content/AI"
printf '{ "FileVersion": 3 }\n' >"$SCHEMA_HOST/SchemaHost.uproject"
cp -R "$PLUGIN_DIR/Tools" "$PLUGIN_DIR/docs" "$SCHEMA_HOST/Plugins/AdamskAI/"
printf '<Agent version="1" id="refused" selftest="1"/>\n' >"$SCHEMA_HOST/Content/AI/refused.xml"
expect schema-refused 1 "FAIL adsk_schema_check" \
	"$SCHEMA_HOST/Plugins/AdamskAI/Tools/adsk_schema_check.sh" --project "$SCHEMA_HOST"

if [ "$STATIC" -eq 0 ]; then
	P=(--project "$ADSK_UPROJECT")
	printf '<Agent version="1" id="broken"><StateMachine' >"$SCRATCH/broken.xml"
	expect validate-broken 1 "" "$TOOLS/adsk_validate.sh" "${P[@]}" --paths "$SCRATCH/broken.xml"
	expect validate-missing 1 "no such file" \
		"$TOOLS/adsk_validate.sh" "${P[@]}" --paths "Content/AI/no_such_selftest_file.xml"
	mkdir -p "$SCRATCH/NoProject"
	expect validate-project 2 "" "$TOOLS/adsk_validate.sh" --project "$SCRATCH/NoProject"

	expect test-no-match 1 "" "$TOOLS/adsk_test.sh" "${P[@]}" --standalone "AdamskAI.NoSuchSelfTestFilter."
	expect test-timeout 3 "" env ADSK_TEST_TIMEOUT=3 "$TOOLS/adsk_test.sh" "${P[@]}" --standalone "AdamskAI."

	printf '#!/bin/sh\necho "selftest preflight refuses"\nexit 2\n' >"$SCRATCH/refuse.sh"
	chmod +x "$SCRATCH/refuse.sh"
	expect preflight-refused 2 "selftest preflight refuses" \
		env ADSK_PREFLIGHT="$SCRATCH/refuse.sh" "$TOOLS/adsk_test.sh" "${P[@]}" --standalone "AdamskAI."
	expect preflight-missing 2 "is not executable" \
		env ADSK_PREFLIGHT="$SCRATCH/no-such-preflight" "$TOOLS/adsk_test.sh" "${P[@]}" --standalone "AdamskAI."
fi

echo "selftest: $FAILED case(s) failed"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
