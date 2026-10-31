# Automation verdict shared by Tools/adsk_test.sh and Tools/adsk_test_win64.ps1.
# Usage: python3 -I adsk_test_report.py <index.json> <log> <filter> <editor-status> <timed-out 0|1>
# Exit codes: 0 every executed test passed; 1 a test failed or none ran; 3 timeout, crash or incomplete run.

import json
import re
import sys

report_path, log_path, test_filter, status, timed_out = sys.argv[1:6]
status, timed_out = int(status), timed_out == "1"


def note(message):
    print(message, file=sys.stderr)


def load_report():
    try:
        with open(report_path, encoding="utf-8-sig") as handle:
            report = json.load(handle)
    except (OSError, ValueError):
        return None
    if not isinstance(report, dict):
        return None
    counts = {}
    for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess"):
        value = report.get(key, 0)
        if not isinstance(value, int) or isinstance(value, bool):
            return None
        counts[key] = value
    tests = report.get("tests", [])
    if not isinstance(tests, list) or not all(isinstance(test, dict) for test in tests):
        return None
    counts["tests"] = tests
    return counts


MAX_MESSAGES = 5
MAX_LINES = 30


def failure_messages(test):
    """A failed test's error messages, each where it failed, capped so one test cannot flood the console."""
    out = []
    errors = [entry for entry in test.get("entries", []) if isinstance(entry, dict)
              and isinstance(entry.get("event"), dict) and entry["event"].get("type") == "Error"]
    for entry in errors[:MAX_MESSAGES]:
        event = entry["event"]
        lines = str(event.get("message", "")).splitlines() or [""]
        where = f"{entry.get('filename')}:{entry.get('lineNumber')}: " if entry.get("filename") and entry.get("lineNumber", 0) > 0 else ""
        out.append(where + lines[0])
        out.extend(f"  {line}" for line in lines[1:MAX_LINES])
        if len(lines) > MAX_LINES:
            out.append(f"  ... {len(lines) - MAX_LINES} more lines in the log")
    if len(errors) > MAX_MESSAGES:
        out.append(f"... {len(errors) - MAX_MESSAGES} more errors in the log")
    return out


def from_report(counts):
    tests = counts["tests"]
    states = [test.get("state") for test in tests]
    failing = [test for test in tests if test.get("state") == "Fail"]
    passed = counts["succeeded"] + counts["succeededWithWarnings"]
    skipped = sum(1 for state in states if state == "Skipped")
    print(f"Tests: {passed} passed, {counts['failed']} failed, {skipped} skipped [{test_filter}]")
    for test in failing:
        print(f"FAILED {test.get('fullTestPath') or test.get('testDisplayName') or '<unnamed test>'}")
        for message in failure_messages(test):
            print(f"  {message}")
    if timed_out:
        note(f"Timed out. Full log: {log_path}")
        return 3
    if counts["failed"] > 0 or failing:
        note(f"Full log: {log_path}")
        return 1
    if counts["notRun"] > 0 or counts["inProcess"] > 0 or any(state in ("NotRun", "InProcess") for state in states):
        note(f"Run did not complete: notRun/inProcess results present. Full log: {log_path}")
        return 3
    if status != 0:
        note(f"Editor exited {status} with no proven test failure. Full log: {log_path}")
        return 3
    if any(state == "Success" for state in states):
        return 0
    note(f"No test succeeded for filter '{test_filter}'. Full log: {log_path}")
    return 1


def from_log():
    if timed_out:
        note(f"Timed out and no usable report. Full log: {log_path}")
        return 3
    try:
        with open(log_path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        note(f"No structured report and no readable log at {log_path}.")
        return 3
    completions = re.findall(r"\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*", text)
    queues = re.findall(r"Automation Test Queue Empty (\d+) tests performed", text)
    # A filter that matches nothing completes without a queue line or a report (UE 5.8): it ran, and
    # nothing ran, which is exit 1, not an incomplete run.
    if completions and not queues and "No automation tests matched" in text:
        print(f"Tests: 0 performed [{test_filter}] (log markers; no structured report)")
        note(f"No automation test matched filter '{test_filter}'. Full log: {log_path}")
        return 1
    if not completions or not queues:
        note(f"No trustworthy result: no report and no completed test pass in the log. Full log: {log_path}")
        return 3
    performed = int(queues[-1])
    print(f"Tests: {performed} performed [{test_filter}] (log markers; no structured report)")
    if performed == 0:
        note(f"Zero tests performed for filter '{test_filter}'. Full log: {log_path}")
        return 1
    if int(completions[-1]) != 0:
        note(f"Test pass reported exit {completions[-1]}. Full log: {log_path}")
        return 1
    if status != 0:
        note(f"Editor exited {status} despite a clean test pass. Full log: {log_path}")
        return 3
    return 0


counts = load_report()
sys.exit(from_report(counts) if counts is not None else from_log())
