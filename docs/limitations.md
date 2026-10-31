# Limitations

What AdamskAI does not do today, stated so nobody has to discover it.

- **Game thread, except compiling and pure agents.** Every instance, registry mutation and recorder call checks the
  game thread, or the worker `FADSKParallelTick` lent the instance to. `FADSKCompiler::Compile` runs on any
  thread (`Prewarm` and the editor's live reload use a task). Only an instance whose definition is entirely
  thread-safe nodes ticks on a worker; one that mixes in a host node ticks wholly on the game thread (it does not
  yield at its first host callback). There is no Mass integration.
- **No replication by the plugin.** Brains run where the game runs its AI (server or standalone); a copy without
  authority refuses to start. Clients see AI state only through what the game replicates, typically the brain's
  state tags ([networking.md](networking.md)).
- **Parallelism stays inside a tree.** `SimpleParallel` loops a background child beside a main one; `Parallel` runs
  up to 8 children once each, with a success and a failure policy. The state machine has one active leaf: no parallel
  regions (roadmap R1). Re-evaluation is `ReactiveSelector` (earlier
  `Guard`, `Cooldown` and `OnCheck` options, on their rise) and `UtilitySelector` (scores); a decorator without an
  `OnCheck` (`TimeLimit`, `Repeat`) is not re-checked.
- **`MoveTo` follows a slot; `MoveToActor` follows an actor.** With `repathDistance` `MoveTo` re-paths whenever the
  goal slot has moved that far; without it (the default) it reads the goal once.
- **Params are Float, Int or Bool.** No Vector or name parameters; EQS takes no others either.
- **Six value types.** `Bool`, `Float` (32-bit), `Int` (`int32`), `Vector` (double, like `FVector`), `Object` (an
  opaque id the host resolves; compared with `is` only) and definition-scoped enums (Inputs, Locals, event and tree
  parameters; never node ports). No names; a distance for `below`/`above` still comes from the host, through a `Distance` sensor or a published Input.
- **`Run` is compile-time.** Parameters bind slots, not values: a copy reads and writes its caller's slots, with no
  runtime call frames; the 4096-node and 1 Mi id-character budgets apply after expansion. `Emit` arguments are
  values read when it runs, delivered one tick later.
- **One payload and one target.** An async action has one optional payload; a perception binding publishes one
  selected target per sense (plus the candidate count, a remembered-or-sensed flag and an alert level), not a list.
- **Bounded completion queues.** Action results and external events each have capacity 64. A full result queue still takes one result per running leaf (one per `SimpleParallel` lane), so stale posts cannot strand one; any other completion it refuses stays pending and cancellable, with no automatic retry. Perception stimulus events that the brain refuses to queue (a full queue, for example) are dropped rather than retried or reordered.
- **Tick ownership.** Any actor can host a brain. It is explicitly ticked unless `bAutoTick` makes its component own
  that tick or `bManagedTick` hands it to the world subsystem; use exactly one.
- **Synchronous preload unless prewarmed.** ObjectPath preload may hitch an Acquire or a reload; files listed in
  `PrewarmFiles` (or passed to `Prewarm`) compile on a worker and stream their assets in instead. Direct `InitializeDefinition` bypasses world-subsystem preload, so hosts must retain/provide loaded objects themselves.
- **Snapshots resume states, not procedures.** `FADSKSnapshot` (live reload uses it) restores the state path,
  Locals, node state structs, change baselines, the random stream and the clock; the leaf state's tree starts again
  from its root, and queued events and pending results are dropped. Object values are process-wide ids, and the
  plugin does not write snapshots to disk ([reload.md](reload.md)).
- **No Blueprint, by decision.** Behavior is authored in XML and nodes in C++; there will be no Blueprint API,
  Blueprint-authored nodes or visual graph editor. Requests for one are out of scope, not backlog.
- **The demo is for the editor and PIE** ([demo.md](demo.md)): its module is left out of Shipping and Test builds and
  its behavior files are not staged.
- **Cooked builds have no live reload**, and a packaged build must stage the behavior root itself
  ([setup.md](setup.md)). Packaged Development and Shipping runs were made on Win64 for 0.4.0; the current tree owes
  that run again ([dev/roadmap.md](dev/roadmap.md)).
- **Platforms.** Built and tested with UE 5.8 on Linux for 0.5; on Win64 for 0.4 (a re-run is owed). Mac has not been compiled (no Mac here to build on). The headless
  scripts exist as bash (`Tools/<name>.sh`, Linux) and PowerShell (`Tools\<name>_win64.ps1`, Windows);
  `adsk_host_check` is Linux-only and `adsk_build_win64`/`adsk_buildplugin_win64` are Windows-only. Open verification items: [roadmap.md](dev/roadmap.md).
