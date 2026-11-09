# Proofs

The evidence behind a claim that no automated test carries: a platform run, a benchmark, a soak, a packaged build, a
before/after a test cannot see. One folder per run, `YYYY-MM-DD-short-slug/`, with a `README.md` and whatever raw
output it cites (a summary log, a perf JSON, a screenshot). A row of [roadmap.md](../roadmap.md#verified-so-far) or
the `Evidence` of a [decision](../decisions/README.md) links here instead of restating numbers nobody can rerun.

## What a proof says

```markdown
# <What was shown>

Date: YYYY-MM-DD
Machine: CPU, OS, compiler, UE version (no host names, addresses or user names)
Commit: <plugin commit>, plus the host project and its commit when one was used

## Command
The exact commands, copy-pasteable.

## Result
The numbers or verdicts, with the raw files in this folder they come from.

## Seen to fail
For a fix or a new check: what was broken on purpose, what failed, and that it passed once restored.

## Not shown
What this run does not cover.
```

## Rules

- **Restore the bug.** A fix or a check is proved by breaking it on purpose, watching the specific test or check fail,
  restoring it and watching it pass. Record both outcomes. A test that has never been seen to fail is not yet
  evidence.
- **Raw output, small.** Keep the summary lines and the files the numbers come from; never a full editor log or a
  build tree. Strip paths that name a user or a machine.
- **A proof is never edited to fit a later run.** A rerun is a new folder; the old one stays as what was true then.

## Index

| Proof | Shows |
|---|---|
| [2026-11-09-linux-selftest](2026-11-09-linux-selftest/README.md) | `adsk_selftest.sh`: every Linux script's failure path gives its documented exit code |
