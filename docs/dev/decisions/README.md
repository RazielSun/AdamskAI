# Decisions

One file per decision that shaped the plugin, kept when the work lands. [roadmap.md](../roadmap.md) holds what is
left and forgets an item once it is done; this folder keeps why it was done that way, what else was on the table,
and what proves it still holds. Read the decisions that touch an area before changing it: reversing one is a new
decision, not a cleanup.

## When to write one

- a choice between real alternatives that a later reader could undo without knowing why (a build setting, a default,
  an ownership or dependency rule, a format);
- a roadmap item closed by choosing one of its options;
- a reversal of an earlier decision: write the new one and mark the old one `Superseded by NNNN`.

A bug fix, a refactor that keeps behavior, or a choice the code already states plainly needs none.

## Format

`NNNN-short-slug.md`, numbered in order, never renumbered. Every file starts like this:

```markdown
# NNNN: Title in a few words

Status: Accepted, YYYY-MM-DD
Evidence: [AdamskAI.Some.Test], [Fixtures/conformance/some_rule], [Source/AdamskAI/AdamskAI.Build.cs]
Commits: abc1234

## Context
What forced the choice: the defect, the measurement, the constraint.

## Decision
What was chosen, in one paragraph.

## Rejected
Each alternative and why not. "Not recorded" when the decision predates this folder and the reason is lost;
never reconstruct a reason nobody wrote down.

## Consequences
What it costs and what it now forbids.
```

`Status` is `Proposed`, `Accepted, <date>` or `Superseded by NNNN`. `Evidence` names what checks the decision still
holds: a registered `AdamskAI.*` / `AdamskAIWorld.*` test (a class or one of its methods), a conformance fixture, or a
path in this plugin. A decision with no check names the measurement in `docs/dev/proofs/` that supports it.

`adsk_check` enforces the format (rule `decision-evidence`): the name and number match, the status is one of the
three, a superseding decision exists, and every evidence name resolves. A renamed test or a deleted file fails the
check instead of leaving a decision that cites nothing.

## Index

| # | Decision | Status |
|---|---|---|
| [0001](0001-precise-floating-point.md) | Precise floating point in every build | Accepted |
| [0002](0002-core-without-engine.md) | The core module does not depend on Engine | Accepted |
| [0003](0003-idle-sleep-on-by-default.md) | Idle sleep on by default | Accepted |
| [0004](0004-content-saved-with-oldest-engine.md) | Content saved with the oldest supported engine | Accepted |
