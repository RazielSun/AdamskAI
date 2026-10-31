# Migrating

From 0.3 to 0.4 first, then [from 0.4 to 0.5](#from-04-to-05).

0.4 is the architecture-review release. XML v1 did not change: every file
that compiled under 0.3 compiles to the same tables (the compat baselines are byte-identical), with one new limit
below. The C++ API did change; this page lists every break and what to write instead.

## Compiler

| 0.3 | 0.4 |
|---|---|
| `#include "Program/ADSKLoader.h"` | `#include "Compiler/ADSKCompiler.h"` |
| `FADSKLoader::LoadFromString(Source, Text, Registry)` | `FADSKCompiler::Compile(Source, Text, Registry)` |
| `FADSKLoadResult` | `FADSKCompileResult` (same members) |
| `FADSKLoader::MaxSourceBytes` and the other limits | `FADSKCompiler::MaxSourceBytes`, ... |

## Definitions

| 0.3 | 0.4 |
|---|---|
| `FADSKTransitionDefinition::ConditionSlot`, `bConditionNegated`, `bHasConditionValue`, `CompareOp`, `ConditionValue`, `CompareSlot` | `Condition` (`FADSKCondition`: `Slot`, `bNegated`, `bCompares`, `Op`, `Operand`, `OperandSlot`); evaluate with `ADSK::EvaluateCondition` |
| `FADSKNodeDefinition::bHasCompareValue`, `CompareOp`, `CompareValue`, `CompareSlot` | `Condition`, with `Slot` set to the Guard's `condition` port binding |

## Node descriptors and contexts

| 0.3 | 0.4 |
|---|---|
| `Descriptor.bReactive = true` | `Descriptor.SelectorPolicy = EADSKSelectorPolicy::Reactive` and `MemoryStruct = FADSKReactiveSelectorMemory::StaticStruct()` |
| `Descriptor.bUtility = true` | `SelectorPolicy = EADSKSelectorPolicy::Utility`, `MemoryStruct = FADSKUtilitySelectorMemory::StaticStruct()`, `ConfigStruct = FADSKUtilityConfig::StaticStruct()` |
| `FADSKNodeContext(ConfigStruct, Config, ..., State)` (sixteen arguments) | `FADSKNodeContext(FADSKNodeContextParams{...})`; only code that builds contexts itself, such as node unit tests, is affected |
| `ADSK::MakeAsyncLeaf(Descriptor, Kind, Start, PayloadPort, BeforeEnd)` | unchanged; an optional last argument `CanStart` checks preconditions before the async entry opens |
| `FADSKNodeContext::WritePort` wrote the slot every call | it writes only a changed value (nothing a node can observe differs) and tells `FADSKNodeContextParams::SlotSink` so the instance can trace it |

A `RandomSelector` takes at most 64 children; more is `bad-arity` at compile time. Its order is now inline memory,
so an activation no longer allocates.

## Host

| 0.3 | 0.4 |
|---|---|
| `#include "Catalogue/ADSKCatalogue.h"` from a runtime module | the catalogue renderer lives in `AdamskAIEditor`; depend on it from editor code only |
| `UADSKSettings::IsStagedBy(Root, Entries)` | `ADSKStaging::IsStagedBy` in `AdamskAIEditor` (`Validate/ADSKStaging.h`) |
| `FADSKRegistryAssembly::AssembleForTest` | `FADSKRegistryAssembly::AssembleIsolated` |
| `UADSKBrainComponent::GetCachedInputs()` returned `TMap<FString, FADSKValue>` | returns `FADSKInputValues`, keyed by exact input name (`Brain/ADSKInputCache.h`) |
| brain state as `IsRunning()` / `IsPaused()` / `IsCleanedUp()` | unchanged, plus `GetState()` returning `EADSKBrainState` |
| `UADSKWorldSubsystem::GetAllBrains()` listed every brain debug views showed | it lists brains registered to a behavior file; `GetWorldBrains()` lists every brain of the world |
| `ADSKRunEQS::FTestHooks`, `ADSKRunEQS::SetTestHooks` | `IADSKHostServices` (`Brain/ADSKHostServices.h`): `RunQuery`/`AbortQuery`, installed per world with `UADSKWorldSubsystem::SetHostServices` |
| `UADSKPerceptionBridgeComponent::SetTimeForTest` | `IADSKHostServices::GetTimeSeconds` |
| `UADSKWorldSubsystem::SetViewersForTest` | `IADSKHostServices::GetViewers`; a project can also install services that report its own view points for tick LOD |

`ADSKDebugSnapshot.h`, `ADSKDebugActions.h` and `ADSKTraceExport.h` now declare nothing in Shipping: include them only
from code that is itself compiled out of Shipping.

## Console and logs

- `adsk.Restart` starts a stopped brain too, and a brain given its definition directly restarts on it instead of
  answering `not-running`; a brain that cannot start answers `adsk: start-failed`.
- `adsk.Reload` and the reload log name the result (`queued`, `unchanged`, `rejected`, `not-loaded`) instead of a number.
- Live reload logs to `LogAdamskAIReload` instead of `LogTemp`.
- The recorder writes `input-set` only when an input's value changes.
- Two record kinds were appended: `transition-taken` (after `event-consumed`: the transition index, its target and
  the condition slot's value as read) and `slot-written` (a `Bind` or an Out port that changed a slot).
  `FADSKRecord` gained `Transition`; the JSONL export writes `transition` and `target`. A tool that switched over
  every kind needs the two new cases.
- `AdamskAI` now depends on `TraceLog` for its Insights channel ([perf.md](dev/perf.md#measuring)).

## Tests

Spec names follow their folders: `AdamskAI.Loader.*` → `AdamskAI.Compiler.*`, `AdamskAI.ChangeEvents.*`,
`AdamskAI.Parallel.*` and `AdamskAI.Composites.*` → `AdamskAI.Execution.*`, `AdamskAI.Editor.*` moved to
`Specs/Editor/`, `AdamskAI.Validate.ClassAndTag` → `AdamskAI.Compiler.ClassAndTagValidation`,
`AdamskAI.Execution.EmitArgs` → `AdamskAI.Compiler.EmitArgs`. A filter that named an old prefix now matches nothing,
which `adsk_test` reports as a failure. New: `AdamskAI.Perf.*`, `AdamskAI.Validate.Staging`, `AdamskAI.Trace.Writes`,
`AdamskAI.Host.HostServices`, `AdamskAI.Fuzz.Compiler`, and the PIE suite `AdamskAIWorld.*`
([testing.md](dev/testing.md#world-tests-adamskaiworld)).

## From 0.4 to 0.5

Everything below landed between 0.4.0 (2026-10-29) and 0.5.0 (2026-10-31), dated by the day it landed.

Behavior a project may notice:

- Network clients no longer compile `PrewarmFiles`; `bPrewarmOnClients=True` restores it for games that run brains on
  client-local actors.
- The managed tick's distance LOD ignores a player controller with no pawn whose view target is itself (joining,
  dead): agents near where such a controller was spawned no longer tick at the near rate.
- Outside Shipping, `SetInput` / `SetEnumInput` / `EnqueueEvent` by an unknown name log one `LogAdamskAIHost`
  warning per name and brain. A test that writes an unknown name on purpose declares it with `AddExpectedError`.
- A file `Acquire` compiles on the game thread (not in `PrewarmFiles`) is logged once at `Display`.
- `adsk.*` in a client world with no brains prints where the brains are.
- **A brain whose owner has no authority is refused.** `InitializeFromSource`, `InitializeDefinition` and `StartLogic`
  fail with `GetLastError() == "not-authority"` (one warning per brain) and neither tick is enabled. A brain on a
  replicated pawn used to run a second copy on each client; now only the server's runs. Brains with no owner actor
  (tests, tools) and client-local actors are unaffected.
- `RecorderCapacity` defaults to 256 (was 512); a project that sets it keeps its value.
- A Guard records `guard-value` when it starts and then only when its value changes (was every tick), so traces and
  `adsk.DumpTrace` hold more history; golden traces made before this need recapturing (`ADSK_CAPTURE_GOLDEN=1`).
- A file with an unregistered gameplay tag in a `State tags` attribute fails initialization and reload (`unknown-tag`).
- `adsk_validate -Mode all` (`--all`) runs in one editor launch (the commandlet's new `-all`).

Additive XML: `<State tags="...">`, `MoveTo` / `MoveToActor` `allowPartialPath` (default `true`, the old behavior).
Existing files compile to the same tables. The catalogue gains an index, node descriptions and legacy marks, and the
fingerprint changes (new fields), so projects regenerate their catalogue.

New API: `FADSKInstance::GetAllocatedBytes`, `FADSKRecorder::GetAllocatedBytes`, `UADSKWorldSubsystem::GetManagedTickStats`
/ `ResetManagedTickStats` / `GetSynchronousCompileCount`, `ADSKDebugActions::ClientWorldHint` / `LogClientWorldHint`,
`Trace/ADSKStats.h` (`STATGROUP_AdamskAI`, CSV category and LLM tag `AdamskAI`), `FADSKInstance::GetActiveTags` /
`GetActiveTagsSerial`, `FADSKDefinition::GetStateTags`, `UADSKBrainComponent::GetActiveTags` / `OnActiveTagsChanged`
(`AdamskAIHost` now depends publicly on `GameplayTags`), `FADSKBrainDriver` and `FADSKInputRef` (`Brain/ADSKBrainDriver.h`),
`FADSKNodeDescriptor::Description`, `FADSKFieldMap::bLegacy`.

2026-10-30, tooling only: a project with node providers now commits `Content/<BehaviorRoot>/adamskai.xsd` beside its
catalogue (regenerate with `-projectxsd`; `adsk_validate -Mode all` fails `catalogue-stale` without it). New in
`AdamskAIEditor`: `FADSKSchemaXsd` (`Catalogue/ADSKSchemaXsd.h`), `FADSKSimulator` (`Simulate/ADSKSimulator.h`), the
commandlet modes `-xsd`, `-projectxsd` and `-simulate`, and `Tools/adsk_simulate[_win64]`.
2026-10-30, review fixes (behavior a project may notice, one renamed editor API):

- `SetInput` and event parameters refuse a NaN or infinite `Float` or `Vector` as `TypeMismatch`, and two NaNs now
  compare equal, so a NaN slot no longer raises its `change=` event on every tick.
- `SetInput(FADSKInputId)` refuses an id whose slot is out of range or a Local (`UnknownName`), and
  `EnqueueEvent(FADSKEventId)` refuses `tree.success`, `tree.failure` and change events (`Reserved`), as the
  by-name overloads always did.
- Catalogue assembly fails when any `Register` was refused, also when the provider ignored the result; the error is
  `<provider>: <node>: <result>`. `FADSKNodeRegistry::GetRejections` lists them.
- A start budget-deferred under a wrapper that ends or switches no longer drops a start that wrapper deferred
  itself (a lane could stall until the next transition).
- A stop requested from a composite's or utility selector's start leaves it to the deferred stop, which records
  `cancelled`, instead of ending it `failed` and propagating a failure.
- An async leaf whose start returns `Succeeded` without completing no longer runs its cancel callback.
- Compiler messages name the unknown attribute, node, port, slot, event, state or tree, with `did you mean '...'?`
  when one is close; codes and lines are unchanged.
- The validator adds `note` rows `unreachable-state`, `unhandled-event`, `unread-local` and `unread-input`
  (`ADSK::FindLintNotes`, `Program/ADSKLint.h`); exit codes are unchanged.
- Golden traces start with `# source` and `# catalogue` lines; old ones without them still compare.

New API: `FADSKValue::IsFinite`, `FADSKNodeRegistry::GetRejections`, `FADSKEventDefinition::Line`,
`FADSKNodeDescriptor::Guidance` (catalogue "Use it well" list; not in the fingerprint), `ADSK::FindLintNotes`.
Renamed: `FADSKValidateReport::AppendHoldNotes` is `AppendNotes`. New scripts: `Tools/adsk_all[_win64]`;
`adsk_perf_compare.sh` takes `--project` like the other scripts instead of a positional project directory.
Later the same day, no API break: `GetActiveTags`, `GetActiveTagsSerial`, `GetNodeMemoryBytes` and `GetAllocatedBytes`
of `FADSKInstance` assert the game thread (or the worker ticking that instance), like every other getter; Insights
names an instance by a host name taken on the game thread at construction and at each `Start`; a cancelled
`SimpleParallel` lane is kept for the next start instead of freed (`AdamskAI.Perf` `ParallelChurn`: about 10% less
time per tick for a fleet that restarts parallels), so `GetAllocatedBytes` counts those spare lanes.

2026-10-30, state-level choice and snapshots (additive XML; one behavior change on by default):

- Additive XML, existing files compile to the same tables: `<On ... chance="p"/>` and `<State select="best|weighted">`
  with `score` / `weight` / `bias` on its children ([xml-reference.md](xml-reference.md#chance) and
  [state selection](xml-reference.md#state-selection)). The schema (`adamskai.xsd`) changed: projects regenerate theirs
  with `-projectxsd`; the catalogue and its fingerprint did not change.
- **A live reload keeps state.** `UADSKSettings::bKeepStateOnReload` (default `true`) restarts each running brain from
  a snapshot of itself: the deepest state of its path the new file still has, its Locals, node state structs (a
  `Cooldown`'s ready time), change baselines, random stream and clock. Before, a reload restarted every brain in its
  initial state with its Locals reset. A test or tool that relied on that sets `bKeepStateOnReload=False`;
  `adsk.Restart` and `ReplaceDefinition` still start clean.
- Record kind `state-selected` (a `select` state's pick and its score; reason `no-score` when it fell back to
  `initial`) and reasons `no-score` and `restored` (`instance-started` from a snapshot) were appended. A tool that
  switched over every kind needs the new case.
- `ADSK::FindLintNotes` follows `select` children for `unreachable-state` and counts a `score` slot as read; a state
  reached first only as an ancestor now also has its initial chain followed, so an `unreachable-state` note that was
  wrong for such a state goes away.

New API: `FADSKInstance::CaptureSnapshot` and `Start(const FADSKSnapshot&)`, `FADSKSnapshot` (`Runtime/ADSKSnapshot.h`),
`FADSKStartReport::Dropped`, `UADSKBrainComponent::CaptureSnapshot` / `StartLogicFrom`,
`FADSKPreparedReplacement::Snapshot`, `FADSKTransitionDefinition::Chance` / `IsCertain`,
`FADSKStateDefinition::Select` / `bScored` / `ScoreSlot` / `ScoreWeight` / `ScoreBias`, `EADSKStateSelect`,
`UADSKSettings::bKeepStateOnReload`.

Snapshot fixes from a review the same day: a reload captures each brain inside its stop, after the running nodes are
cancelled (a `Cooldown` whose child the reload cancelled used to come back ready, so the attack it guards ran again at
once); a brain that was holding and returns to the same leaf holds again instead of rerunning its finished tree; a
change baseline comes back only while its event watches the same slot, of the same kind (Local or Input); a weighted
`select` whose scores add up past the float range picks the best child instead of always the last; `score`, `weight`
or `bias` on `<StateMachine>` is reported once, not twice. New API: `FADSKInstance::StopAndCaptureSnapshot`,
`UADSKBrainComponent::StopLogicAndCaptureSnapshot`, `FADSKSnapshot::bHolding`, `FADSKSnapshotBaseline` (the type of
`ChangeBaselines`, an `FADSKSnapshotValue` with the watched slot). `AdamskAI.Fuzz.Compiler` now also restarts every
compiled mutant from its own snapshot and from a snapshot of the unmutated file, as a live reload does.

2026-10-30, history states and the conformance suite (additive XML, no behavior change for existing files):

- `<State history="shallow|deep">` on a state with child states: entered again after being left, it resumes the
  child it left (deep: the whole path below) instead of `initial` or `select` ([xml-reference.md](xml-reference.md#history)).
  A `goto` to it from its own descendants keeps it active and uses no history, so `select` re-selection is
  unchanged. The schema changed (regenerate a project's with `-projectxsd`); the catalogue did not.
- Snapshots carry each state's remembered child (`FADSKSnapshot::History`, `FADSKSnapshotHistory`); a restore drops
  one whose child the edited file moved or removed (`Dropped`: `history <state>`).
- Record reason `history` (a `state-selected` record for a resumed child) was appended.
- [semantics.md](semantics.md) writes the tick order down; `Fixtures/conformance/` holds one self-driving file per
  rule, run by `AdamskAI.Conformance` (a file must reach a state named `pass`).

New API: `EADSKStateHistory`, `FADSKStateDefinition::History`, `FADSKSnapshot::History`, `FADSKSnapshotHistory`,
`EADSKRecordReason::History`.

2026-10-30, later (no API change):

- A start from a snapshot whose `AgentId` differs from the file's (a reload that renamed the agent) still restores
  everything that matches by name, and `Dropped` starts with `agent <old id>`, so the `restored without` log line
  says so. A hand-built snapshot with an empty `AgentId` is not reported.
- `Tools/adsk_schema_check[_win64]` checks every valid file against the generated schemas; `adsk_all` runs it after
  `validate`. The Linux twin needs `xmllint`.
- `AdamskAIWorld.Tick.PauseFreezesBothTicksAndDilationSlowsWaits` now clears `TickLOD` itself, as every world test
  that runs managed brains must: it failed once a host project configured one.
2026-10-30, after the first shooter port (behavior a host may notice, no API change):

- `UADSKBrainComponent::FindInput` and `FindEvent` warn once per name and brain (not in Shipping) when the file has no
  such Input or event, and `SetInput(FADSKInputId)` / `EnqueueEvent(FADSKEventId)` refuse an id that never resolved
  (`IsValid()` false) with `UnknownName` and no trace record. A host that resolved a name the file dropped used to
  fill the trace with an `input-rejected` record per write, every tick, with nothing in the log. A stale id (resolved
  against an older definition) is still passed to the instance and recorded as before.
- Live reload watches a behavior root that did not exist when the editor started once it appears (checked every two
  seconds, logged at `Display`); before, it warned and never watched it.
- A missing or stale project catalogue or schema names `-projectcatalog` / `-projectxsd` in its `catalogue-stale` row
  (it named the plugin's `-catalog` / `-xsd`).
- `RunEQS` logs `<node> found no point: <query> returned no items|failed|was aborted` to the Visual Logger, next to the
  engine's `LogEQS` entry with the item counts; its catalogue guidance says so and that it runs the query as the pawn.
  The catalogue text changed: projects with node providers regenerate theirs (`-projectcatalog`).
- Conformance: `parallel_nested_in_background_runs`, `parallel_nested_ends_with_its_main`.

2026-10-30, two small XML additions (existing files compile to the same tables and replay the same):

- `TimeLimit` takes `deviation`, drawn once per start from `duration` ± `deviation` like `Wait`; without it nothing is
  drawn. Its configuration struct is now `FADSKWaitConfig` (`FADSKDurationConfig` is Cooldown's only).
- `MoveTo` and `MoveToActor` take `strafe` (default `true`, what they always passed to the engine); `false` turns the pawn
  to its path while a focus is set (`FADSKMoveToConfig::bStrafe`, `FADSKMoveToActorConfig::bStrafe`).
- The schema and the catalogue changed: projects with node providers regenerate both (`-projectcatalog`, `-projectxsd`).

2026-10-30, compound conditions (additive XML, existing files compile to the same tables and replay the same):

- `<When>` and `<Any>` children on `On` and `Guard`: the condition is the attributes and every child together, an
  `Any` holding when one of its `When`s does ([xml-reference.md](xml-reference.md#several-conditions)).
- `When` and `Any` are reserved names: a provider that registered a node under either now gets `ReservedName`.
- New API: `FADSKDefinition::GetConditionTerms`, `FirstTerm`/`TermCount` on `FADSKTransitionDefinition` and
  `FADSKNodeDefinition`, `FADSKCondition::bAnyWithNext`, `ADSK::EvaluateTerms`, `ADSK::EvaluateTransitionCondition`.
  `FADSKTransitionDefinition::IsCertain` is false for a handler with terms. Code that evaluated a transition with
  `ADSK::EvaluateCondition(Transition.Condition, ...)` alone now misses its terms: call `EvaluateTransitionCondition`.
- Trace: a handler with only terms records its first term's slot in `transition-taken`; a `Guard` with terms records
  its outcome (Bool) in `guard-value`.
- The normalized dump prints `terms=` and a `[condition-terms]` section only when a file has terms.
- The schema changed (regenerate a project's with `-projectxsd`); the catalogue did not.
- Conformance: `when_all_must_hold`, `when_any_needs_one`, `guard_when_rechecked_every_tick`.

2026-10-30, sensors, Act, groups and tokens, chance from a slot, Radio (additive XML; existing files
compile to the same tables and replay the same, except the diagnostics below):

- `<Sensors>`: a new optional section between `Locals` and `Events`, so the section order is `Enums`,
  `Inputs`, `Locals`, `Sensors`, `Events`, `StateMachine`, `Tree` ([xml-reference.md](xml-reference.md#sensors)).
  `Sensors` is a reserved name: a provider that registered a node under it now gets `ReservedName`. New node kind
  `EADSKNodeKind::Sensor` with `FADSKNodeDescriptor::OnSample` ([authoring-nodes.md](authoring-nodes.md#sensors)); a
  tool that switched over every kind needs the new case. Host sensors `Distance`, `LineOfSight`, `Grounded`,
  `PlayerPawn`, `GroupTarget`.
- A controller can drop the code that computed and published a distance, line of sight, on-the-ground or the
  player's pawn: declare the sensor and delete the `SetInput` (for example `<LineOfSight ... cone="85" traces="5"/>` instead of a
  controller's own traces).
- New runtime module `AdamskAIKit` with `Act`, `HoldToken` and `Radio`. Project nodes that ran a
  pawn's attack become `<Act action="..." in-target="..."/>` with an `IADSKActionPerformer` on the pawn or the
  controller (add `AdamskAIKit` to the `Build.cs`, `AdamskAIHost` since 2026-10-31;
  [setup.md](setup.md#actions-and-groups)): a `ShootAt` project node becomes `<Act action="Fire" .../>`, and a
  provider that held only such nodes can go.
- `IADSKNodeProvider::IsPluginKit()` (default `false`) puts a provider's nodes into the plugin catalogue
  (`FADSKRegistryAssembly::BuildPluginCatalogue`); a project provider keeps the default.
- Groups: `UADSKGroupSubsystem` (`Groups/ADSKGroupSubsystem.h`) and `UADSKSettings::TokenPools`.
- `chance` on `On` takes the name of a Float slot ([xml-reference.md](xml-reference.md#chance)). A `chance` that
  is neither a number nor a slot name is now `unresolved-reference` (it was `type-mismatch`); a non-Float slot is
  `type-mismatch`.
- New API: `FADSKDefinition::GetSensors`, `FADSKTransitionDefinition::ChanceSlot` (`IsCertain` is false with one; the
  normalized dump prints ` chance-slot=N`), `FADSKHostNodes::RegisterSensors`, `IADSKActionPerformer` and
  `FADSKActionDone` (`Combat/ADSKActionPerformer.h`).
- The catalogue, its fingerprint and the schema changed: projects with node providers regenerate both
  (`-projectcatalog`, `-projectxsd`).
- Conformance: `chance_from_slot_zero_never_taken`, `chance_from_slot_follows_the_slot`. Tests:
  `AdamskAIWorld.Sensors`, `AdamskAIWorld.Kit.Combat`, `AdamskAIWorld.Kit.Groups`; `adsk_strict_includes` runs 10
  builds.

2026-10-31, `AdamskAIKit` folded into `AdamskAIHost` (C++ break for code that named the kit; XML unchanged, the
catalogue and its fingerprint are identical):

- The `AdamskAIKit` module is gone. `Act`, `HoldToken` and `Radio` are host nodes, registered by
  `FADSKHostNodes::RegisterActions` and `RegisterGroups` after the host sensors, in the order the kit registered them.
  Remove `AdamskAIKit` from a `Build.cs`; `AdamskAIHost` is enough.
- `#include "Combat/ADSKActionPerformer.h"` → `#include "Nodes/ADSKActionPerformer.h"`. `IADSKActionPerformer` is
  exported as `ADAMSKAIHOST_API`. The config and memory structs (`FADSKActConfig`, `FADSKHoldTokenConfig`, ...) are in
  `Nodes/ADSKHostNodeTypes.h`.
- `IADSKNodeProvider::IsPluginKit()` is removed: a provider that overrode it drops the override.
  `FADSKRegistryAssembly::BuildPluginCatalogue` is the core and host nodes alone.
- Tests: `AdamskAIWorld.Kit.Combat` → `AdamskAIWorld.Act`, `AdamskAIWorld.Kit.Groups` → `AdamskAIWorld.GroupNodes`;
  `adsk_strict_includes` runs 8 builds.

2026-10-31, node arguments and parameters on group calls (additive; XML files that compiled still compile, the
plugin catalogue fingerprint changes because `Radio` now takes arguments):

- `FADSKNodeDescriptor::bTakesArgs`: the node takes `<Param name from/>` children naming slots (no enums), read with
  `FADSKNodeContext::ReadArgs()`. Leaf, Scope and Composite only, not with `ParamsProperty`
  (`InvalidParamsProperty`). The schema JSON marks such nodes `"args":true`.
- `FADSKNamedValue` moved from `Runtime/ADSKInstance.h` to `Runtime/ADSKNode.h` (which `ADSKInstance.h` includes);
  code that includes either still compiles.
- `UADSKGroupSubsystem::Broadcast(Sender, Event, Delay, Parameters = {})` and `Radio` pass event parameters; a
  receiver whose file declares other parameters refuses the call.
- The `unread-input` / `unread-local` notes count a `<Param from>` as a read.

2026-10-31, the ImGui panel left the plugin (break for anyone who used it):

- The `ImGui` plugin dependency, `ADSK_WITH_IMGUI` and the `adsk.Debug` console variable are gone; `AdamskAIDebug`
  keeps the Gameplay Debugger category and `adsk.Labels`. Console commands are unchanged.
- A project that wants the panel draws it from `FADSKDebugSnapshot` and `ADSKDebugActions`
  ([debugging.md](debugging.md#a-projects-own-panel)).
- `adsk_check` rule `imgui-debug-only` became `no-imgui`, and `ImGui` and `RHI` left its dependency allowlist.

2026-10-31, `Set`, `Retry` and `OnCheck` (additive XML; the plugin catalogue fingerprint changes):

- New core nodes `Set` (writes a literal or a slot to a Local) and `Retry`. A project provider that registered a node
  named `Set` or `Retry` now gets `DuplicateName`; rename it.
- `EADSKNodeKind::Set` is a new intrinsic kind, appended after `Sensor`; a tool that switches over every kind needs
  the case. `FADSKNodeDefinition::Assign` holds its target and source.
- `FADSKNodeDescriptor::OnCheck` (`FADSKCheckFn`): a `ReactiveSelector` re-checks every earlier option that has one,
  as it does a `Guard`'s condition. `Cooldown` has one, so a `Cooldown` option of a `ReactiveSelector` now interrupts
  the option running after it when it becomes ready; before, it was passed over until the selector restarted. The
  schema JSON marks such nodes `"checks":true`.
- `adsk_check` adds `semantics-citation`: every rule in `docs/semantics.md` names the conformance file or the test
  that checks it.
- `AdamskAI.Fuzz.Compiler` takes `ADSK_FUZZ_SECONDS` (a soak of repeated rounds) and `ADSK_FUZZ_SEED`.
- New tool `adsk_graph` (`.sh` and `_win64.ps1`): a behavior file's state machine as Mermaid or DOT.

2026-10-31, `Parallel` (additive XML; the plugin catalogue fingerprint changes):

- New core node `Parallel`: 2 to 8 children, each in its own lane, run once; `success` and `failure` take `all`,
  `any` or a count. A project provider that registered a node named `Parallel` now gets `DuplicateName`.
- `EADSKNodeKind::Concurrent` is a new intrinsic kind, appended after `Set`; a tool that switches over every kind
  needs the case. Its config and memory are `FADSKParallelConfig` and `FADSKParallelMemory`, and
  `ADSK::ParallelThreshold` turns a policy into a child count.
- `FADSKInstance::GetActiveNodes` reports the `Parallel` node for its own lane and each running child for its lane.

2026-10-31, the review fixes (no XML change):

- `UADSKWorldSubsystem::CommitPendingReloads` publishes the definition before stopping the brains, skips a brain whose
  instance or file a stop callback changed (`UADSKBrainComponent::GetInstanceSerial`, new), and pauses again a brain
  that was paused.
- `UADSKBrainComponent` keeps generations rising across `Cleanup` and re-initialization, so an old
  `FADSKAsyncTicket` completes as `Stale`.
- `UADSKGroupSubsystem::TakeToken` counts takes per holder; each take needs a `ReturnToken`, and a holder keeps its
  token until the last one. Code that took a token twice and returned it once now still holds it.
- Path keys are exact-case: `TADSKExactMap`, `FADSKExactStringSet` and their key functions are in `Core/ADSKTypes.h`
  (they replace the host's `TADSKExactNameKeyFuncs`). A project that acquired `Guard.xml` as `guard.xml` on a
  case-sensitive file system now gets a missing file.
- `ADSKXml::Read` passes the whole string to the parser; a NUL inside it is `xml-malformed` at its line.

2026-10-31, brains sleep (no XML change; on by default):

- `UADSKSettings::bSleepWhenIdle` (default `true`): a brain skips ticks until `FADSKInstance::GetNextWake` and gets
  the skipped time in one tick. Code that counted ticks (`GetTickCount`, `OnPreTick` without a handler bound) sees
  fewer; a test of the tick itself sets the setting to `false`. An auto-ticking brain changes its component's tick
  interval and disables its tick in a hold.
- New: `FADSKInstance::GetNextWake`, `FADSKInstance::SetQueueListener`, `FADSKNodeDescriptor::OnNextWake`,
  `FADSKNextWakeFn`, `ADSK::NeverWake`, `UADSKBrainComponent::IsAsleep`. `Wait`, `TimeLimit`, `Cooldown`, `MoveTo`
  and `Act` answer `OnNextWake`; any other node with `OnUpdate`, project nodes included, keeps its brain awake while
  it runs, as before.
- A brain never sleeps while `OnPreTick` is bound or its file declares sensors.
