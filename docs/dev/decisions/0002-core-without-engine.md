# 0002: The core module does not depend on Engine

Status: Accepted, 2026-10-27
Evidence: [Source/AdamskAI/AdamskAI.Build.cs], [Tools/lib/adsk_check.py]
Commits: SimpleCommando fe277f8 (first enforced as `core-engine-free` in `adsk_check`)

## Context

The core compiles a behavior file into an immutable program and executes it ([contract.md](../../contract.md)). It
is what the validator, the simulator and the world-free tests run. Anything it reaches in Engine (actors, worlds,
components) would tie a decision to a live world and make headless runs and replay depend on it.

## Decision

The `AdamskAI` module depends on `Core` and `CoreUObject` publicly and `Expat` and `Json` privately, and nothing else.
It includes no `Engine/`, `GameFramework/` or `Components/` header. Everything that touches a world lives in
`AdamskAIHost` and the modules above it.

## Rejected

Not recorded.

## Consequences

- A world fact reaches the core only as an Input, an Event or through a host node; a node that needs an actor is a
  host or project node ([authoring-nodes.md](../../authoring-nodes.md)).
- `adsk_check` fails on an `Engine` dependency in the core's `Build.cs` and on an engine include in its sources.
