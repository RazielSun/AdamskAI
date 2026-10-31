# Compares the benchmark numbers in <project>/Saved/AdamskAI/perf/*.json with a committed baseline; shared by
# Tools/adsk_perf_compare.sh and Tools/adsk_perf_compare_win64.ps1 (see either for usage).
# Usage: python3 -I adsk_perf_compare.py <project-dir> <plugin-dir> [--baseline FILE] [--update] [--strict]
# Exit codes: 0 compared (or baseline written), 1 a number got worse and --strict was given, 2 setup failure.

import json
import re
import socket
import sys
from pathlib import Path

# Times, bytes and per-agent rates: higher is worse. Everything else (agent counts, frames) is shown, never judged.
WORSE_WHEN_HIGHER = re.compile(r"(_ms|_us|_bytes|records_per_|_share_of_)")
THRESHOLD = 0.20


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(2)


def numbers(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"cannot read {path}: {error}")
    # bool is an int in Python; flags are configuration, not measurements.
    return {key: value for key, value in data.items() if isinstance(value, (int, float)) and not isinstance(value, bool)}


def check_ceilings(path, current):
    """Absolute limits agreed for the numbers (Fixtures/perf/ceilings.json); prints and counts each one exceeded."""
    if not path.is_file():
        return 0
    try:
        ceilings = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"cannot read {path}: {error}")
    managed = current.get("ManagedTick", {})
    if managed.get("brains_250_us_per_brain_tick"):
        # Per-brain cost growing with the fleet means superlinear work.
        managed["brains_1000_over_250_per_brain"] = managed.get("brains_1000_us_per_brain_tick", 0) / managed["brains_250_us_per_brain_tick"]
    over = 0
    for name, limit in sorted(ceilings.items()):
        bench, _, key = name.partition(".")
        value = current.get(bench, {}).get(key)
        if value is not None and value > limit:
            print(f"{name}: {value:.6g} OVER CEILING {limit:.6g}")
            over += 1
    return over


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        fail("usage: adsk_perf_compare.py <project-dir> <plugin-dir> [--baseline FILE] [--update] [--strict]")
    project, plugin, rest = Path(args[0]), Path(args[1]), args[2:]
    baseline = plugin / "Fixtures" / "perf" / f"baseline-{socket.gethostname().lower()}.json"
    update = strict = False
    index = 0
    while index < len(rest):
        if rest[index] == "--baseline" and index + 1 < len(rest):
            baseline = Path(rest[index + 1])
            index += 2
            continue
        if rest[index] == "--update":
            update = True
        elif rest[index] == "--strict":
            strict = True
        else:
            fail(f"unknown argument '{rest[index]}'")
        index += 1

    perf_dir = project / "Saved" / "AdamskAI" / "perf"
    files = sorted(perf_dir.glob("*.json"))
    if not files:
        fail(f"no benchmark results in {perf_dir}; run adsk_test with AdamskAI.Perf. and AdamskAIWorld.Perf. first")
    current = {path.stem: numbers(path) for path in files}
    over = check_ceilings(plugin / "Fixtures" / "perf" / "ceilings.json", current)

    if update:
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_text(json.dumps(current, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        print(f"baseline written: {baseline} ({len(current)} benchmark(s))")
        return 0
    if not baseline.is_file():
        fail(f"no baseline {baseline}; write one deliberately with --update")
    try:
        reference = json.loads(baseline.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"cannot read {baseline}: {error}")

    worse = 0
    for bench in sorted(set(reference) | set(current)):
        if bench not in current:
            print(f"{bench}: not run")
            continue
        if bench not in reference:
            print(f"{bench}: new, not in the baseline")
            continue
        for key in sorted(set(reference[bench]) | set(current[bench])):
            old, new = reference[bench].get(key), current[bench].get(key)
            if old is None or new is None:
                print(f"{bench}.{key}: {'new' if old is None else 'gone'}")
                continue
            change = (new - old) / old if old else 0.0
            judged = WORSE_WHEN_HIGHER.search(key) is not None
            flag = ""
            if judged and change > THRESHOLD:
                flag = "  WORSE"
                worse += 1
            elif judged and change < -THRESHOLD:
                flag = "  better"
            print(f"{bench}.{key}: {old:.6g} -> {new:.6g} ({change:+.1%}){flag}")
    print(f"summary: {worse} number(s) more than {THRESHOLD:.0%} worse than {baseline.name}, {over} over a ceiling")
    return 1 if strict and (worse or over) else 0


if __name__ == "__main__":
    sys.exit(main())
