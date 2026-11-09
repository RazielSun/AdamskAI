# Every Linux script's failure path gives its documented exit code

Date: 2026-11-09
Machine: AMD Ryzen 7 8845HS, Linux 7.0, UE 5.8.4 (bundled clang 20.1.8 toolchain), python3, xmllint
Commit: plugin 9af8ad5 plus the working tree that adds `adsk_selftest.sh`; host project SimpleCommando 22c4904,
editor target built before the run, no editor running

## Command

```bash
Plugins/AdamskAI/Tools/adsk_selftest.sh            # all 16 cases, from the project root
Plugins/AdamskAI/Tools/adsk_selftest.sh --static   # the 9 that need no engine (what CI runs)
```

## Result

All 16 cases, 42 s wall time ([output.txt](output.txt)). `--static` alone: the first 9, 0 failed.

## Seen to fail

- **The selftest itself.** A copy of the plugin with `Mutate` added to `adsk_check`'s question allowlist: `--static`
  reported `FAIL check-side-effect: exit 0, expected 1` and exit 1. The rule restored, 9 of 9 passed.
- **`decision-evidence`.** A copy with a decision citing a test that does not exist, one superseded by a decision that
  does not exist, a duplicate number, an unknown status and a misnamed file: 8 findings, exit 1.
- **A defect the first run found.** `test-no-match` failed with exit 3, where testing.md promises 1: on Linux the
  UnrealTraceServer the editor forks overwrote the tail of the redirected stdout, so `adsk_test.log` lacked the
  `No automation tests matched` and `TEST COMPLETE` lines that the engine's own `Saved/Logs/<Project>.log` held.
  `adsk_test.sh` now passes `-abslog=Saved/Logs/adsk_test.log` and sends stdout to `adsk_test.stdout.log`. After
  the fix the case passes, and `adsk_test.sh --standalone AdamskAI.Host.Sleep` still reports 9 passed, exit 0.

## Not shown

- The Win64 twin was not run against these changes (`adsk_commit_check_win64.ps1` has never run).
- The editor-open cases exist only on Win64; Linux refuses through the host preflight, covered by the two
  `preflight-*` cases.
- `adsk_validate.sh` reads its marker from redirected stdout too; no run has lost it so far, but the same overwrite
  could hit it.
- The GitHub workflow has not run yet.
