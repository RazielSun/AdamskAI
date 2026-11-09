# 0001: Precise floating point in every build

Status: Accepted, 2026-11-03
Evidence: [Source/AdamskAI/AdamskAI.Build.cs], [Source/AdamskAIHost/AdamskAIHost.Build.cs], [AdamskAI.Examples.Run], [AdamskAI.Trace.Equivalence]
Commits: fe8664c, e1d0955 (roadmap B12)

## Context

Determinism is the product ([concept.md](../../concept.md), principle 8): the same seed, inputs, events and deltas
give the same run. Win64 games and editors before UE 5.8 compiled the plugin's modules with `/fp:fast`, so a decision
at a float threshold could differ between the editor and a packaged game. e1d0955 already had to make float equality
NaN-safe under `/fp:fast`.

## Decision

`AdamskAI` and `AdamskAIHost` set `FPSemantics = FPSemanticsMode.Precise` in every build: editor and game, MSVC and
clang, UE 5.6 to 5.8. The guarantee covers the plugin's own math only; values a project module computes keep that
module's floating point mode ([limitations.md](../../limitations.md)).

## Rejected

- Keeping the engine default and documenting that runs may differ per build: it gives up the determinism the trace,
  replay and golden tests rely on.
- Other alternatives: not recorded.

## Consequences

- Before 5.8 UBT builds these two modules without a precompiled header, so they compile slower there.
- A host value from a `/fp:fast` project module can still differ in the last bit; a threshold on it can take another
  branch. Not yet run on Win64: a golden trace in a packaged game (roadmap).
- A module added to the plugin's runtime path must set the same mode.
