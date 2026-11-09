# Commit message and pull request description rules shared by Tools/adsk_commit_check.sh and its Win64 twin; see
# adsk_commit_check.sh for the rules. Usage: python3 -I adsk_commit_check.py <plugin-dir> (--range <a..b> |
# --message <file> | --pr-body <file>). Exit codes: 0 clean, 1 findings, 2 usage or git failure.

import re
import subprocess
import sys
from pathlib import Path

VERIFIED = re.compile(r"^(?:Test|Not verified): \S", re.M)
DECISION = re.compile(r"\bADR-(\d{4})\b")
COMMENT = re.compile(r"<!--.*?-->", re.S)
SECTION = re.compile(r"^\*\*([^*]+):\*\*", re.M)
CHECKBOX = re.compile(r"^\s*- \[[ xX]\] .*$", re.M)
PR_SECTIONS = ("What changed", "Commands run and their result", "Seen to fail", "Not verified")

plugin = Path(sys.argv[1])
decisions = plugin / "docs" / "dev" / "decisions"
findings = []


def usage(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(2)


def check_decisions(where, text):
    for number in DECISION.findall(text):
        if not list(decisions.glob(f"{number}-*.md")):
            findings.append(f"{where}: commit-decision: ADR-{number} names no docs/dev/decisions/{number}-*.md")


def check_message(where, text):
    lines = [line for line in text.splitlines() if not line.startswith("#")]
    if not lines or not lines[0].strip():
        findings.append(f"{where}: commit-subject: the first line is the subject and is not empty")
        return
    if len(lines) > 1 and lines[1].strip():
        findings.append(f"{where}: commit-subject: a blank line separates the subject from the body")
    if not VERIFIED.search("\n".join(lines[1:])):
        findings.append(f"{where}: commit-verified: the body has a 'Test: <command and result>' or "
                        "'Not verified: <what>' line ('Test: none, docs only' for a change nothing runs)")
    check_decisions(where, text)


def check_pr_body(where, text):
    text = CHECKBOX.sub("", COMMENT.sub("", text))
    parts = SECTION.split(text)
    sections = {parts[i].strip(): parts[i + 1].strip() for i in range(1, len(parts) - 1, 2)}
    for name in PR_SECTIONS:
        if name not in sections:
            findings.append(f"{where}: pr-section: no '**{name}:**' section (see .github/pull_request_template.md)")
        elif not sections[name]:
            findings.append(f"{where}: pr-section: '**{name}:**' is empty; write 'none' with a reason if nothing applies")
    check_decisions(where, text)


def git(*arguments):
    run = subprocess.run(["git", "-C", str(plugin), *arguments], capture_output=True, text=True)
    if run.returncode != 0:
        usage(f"git {' '.join(arguments)}: {run.stderr.strip()}")
    return run.stdout


if len(sys.argv) != 4 or sys.argv[2] not in ("--range", "--message", "--pr-body"):
    usage("expected <plugin-dir> --range <a..b> | --message <file> | --pr-body <file>")
mode, value = sys.argv[2], sys.argv[3]
if mode == "--range":
    # Commits that touch the plugin, so the same range works in the plugin's repository and in a host that vendors it.
    hashes = git("rev-list", "--no-merges", "--reverse", value, "--", ".").split()
    for commit in hashes:
        check_message(commit[:10], git("log", "-1", "--format=%B", commit))
    label = f"{len(hashes)} commits in {value}"
else:
    path = Path(value)
    if not path.is_file():
        usage(f"no file '{value}'")
    text = path.read_text(encoding="utf-8", errors="replace")
    (check_message if mode == "--message" else check_pr_body)(value, text)
    label = value

for finding in findings:
    print(finding)
print(f"{'FAIL' if findings else 'PASS'} adsk_commit_check: {label}, {len(findings)} findings")
sys.exit(1 if findings else 0)
