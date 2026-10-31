# Changelog

What changed for someone who uses the plugin, newest first. Every C++ break and what to write instead is in
[docs/migration.md](docs/migration.md); this file only summarizes. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow the `VersionName` in
`AdamskAI.uplugin`. XML v1 stays compatible across all of them: a file that compiled still compiles.

## 0.5.0 — 2026-10-31

The first release from its own repository: new nodes (`Parallel`, `Set`, `Retry`), sensors, actions, groups and
tokens, node arguments, live reload that keeps state, brains that sleep while nothing is due, a benchmark against
Behavior Trees and StateTree, and a demo map.

### Added
- Brains sleep while nothing is due (`UADSKSettings::bSleepWhenIdle`, on by default, as Behavior Trees and StateTree
  do) and get the skipped time when they wake; inputs, events and async results wake them at once. A thousand demo
  brains cost 0.10–0.13 ms per frame instead of 0.27–0.42 ms; 200 walking agents 0.04 ms instead of 0.15 ms.
  `FADSKNodeDescriptor::OnNextWake` lets a project node say when it is due; without it a running node with
  `OnUpdate` keeps its brain awake.
- `Tools/adsk_bench.sh`: the same behavior as AdamskAI, a Behavior Tree and a StateTree on 1000 agents; the numbers
  are in the README.
- Demo: module `AdamskAIDemo`, a code-generated arena (`Content/Demo/Maps/L_AdskDemo`) and seven console scenes
  (`adsk.Demo <Scene>`: Patrol, Chase, Interrupt, Squad, Tokens, Reload, Fleet), each also a headless test
  ([docs/demo.md](docs/demo.md)).
- `Parallel`: up to 8 children at once, each once, with `success` and `failure` policies (`all`, `any`, a count).
- `Set`: write a literal or another slot to a Local. `Retry`: run a child again after each failure, up to `count`.
- `FADSKNodeDescriptor::OnCheck`: a `ReactiveSelector` re-checks any option that has one; a `Cooldown` option now
  interrupts the running one the moment it is ready.
- `adsk_graph`: a behavior file's state machine as a Mermaid or Graphviz diagram.
- `adsk_check` rule `semantics-citation`; fuzzer soak (`ADSK_FUZZ_SECONDS`, `ADSK_FUZZ_SEED`).
- `docs/comparison.md`: feature matrix and pattern catalogue against UE BT, StateTree, BehaviorTree.CPP, py_trees,
  HFSM2 and SCXML.
- Node arguments: a descriptor with `bTakesArgs` takes `<Param name from/>` children naming slots, read with
  `FADSKNodeContext::ReadArgs()`, so project nodes can pass values to other agents.
- `Radio` and `UADSKGroupSubsystem::Broadcast` carry event parameters (a threat and where it was seen).
- Recipe [talk-between-agents.md](docs/recipes/talk-between-agents.md), with `SampleSendTo` compiled in the samples.
- Sensors (`Distance`, `LineOfSight`, `Grounded`, `PlayerPawn`, `GroupTarget`) declared in a file's `<Sensors>`.
- `Act` through `IADSKActionPerformer`, groups and attack tokens (`HoldToken`), `Radio`; `chance` from a Float slot;
  compound conditions (`When`/`Any`).
- `docs/linux.md`, `docs/glossary.md`, `docs/references.md`, `CONTRIBUTING.md`, this file.
- A live reload keeps each brain's state (`bKeepStateOnReload`), through `FADSKSnapshot`; `history` and scored
  `select` states; `deviation` on `TimeLimit`, `strafe` on `MoveTo`. The full list, dated: [docs/migration.md](docs/migration.md#from-04-to-05).

### Changed
- The README is a short overview with the comparison and the measured numbers; its runtime contract (ownership, XML
  bounds, tick order, recorder, modules) moved to [docs/contract.md](docs/contract.md).
- The plugin lives in its own repository. Test data moved under `Fixtures/` (`world/`, `perf/`, `scenarios/`, was
  `Tests/Behaviors`, `Tests/Perf`, `Tests/Scenarios`); developer documents moved to `docs/dev/`, which a
  `BuildPlugin` package leaves out.
- The `AdamskAIKit` module folded into `AdamskAIHost`; `Act`, `HoldToken` and `Radio` are host nodes.
- `AdamskAIDebug` no longer draws an ImGui panel and no longer depends on ImGui: the Gameplay Debugger category,
  the labels and the console commands remain, and a project draws its own panel from the public debug snapshot.

### Removed
- `IADSKNodeProvider::IsPluginKit()`, the `adsk.Debug` console variable, `ADSK_WITH_IMGUI`.

### Fixed
- A reload commit no longer crashes when a stop callback acquires files, no longer overwrites a file a stop callback
  gave the brain, and keeps a paused brain paused.
- A completion ticket from before `Cleanup` and a re-initialization is stale for the next lifetime's leaf.
- Nested `HoldToken` scopes on one target keep the token until the outer scope ends.
- Behavior file paths that differ only in case are two files (the cache, the brain registry, the watcher).
- A NUL byte in a behavior file is `xml-malformed`; before, the text after it was ignored, also by the hash.

## 0.4.0 — 2026-10-29

The architecture-review release: the C++ API was reorganized (`FADSKLoader` became `FADSKCompiler`, condition fields
became `FADSKCondition`, selector policies, context parameters), the catalogue renderer moved to the editor module,
and the runtime checks a brain's network authority.

## 0.3

The first version used in a game project.
