# Roadmap: what is left

The one list of everything AdamskAI still wants: confirmed defects, checks nobody has run, open proposals and features not built.
Everything that is done is described in the other documents as how the plugin works, not here. When an item lands,
delete it from this file and describe the result where it belongs ([migration.md](../migration.md) for API changes).

## Verified so far

A run recorded from 2026-11-09 on has its command, machine and raw output in [proofs/](proofs/README.md); the rows
below predate that and are the only record of their runs.

| Platform | Last run | Result |
|---|---|---|
| Win64, UE 5.8.4, MSVC 14.51 | 2026-10-31 | 0.5.0 in three host projects: build, check, validate, schema check, simulate, 656 `AdamskAI.*`, 63 `AdamskAIWorld.*`, game Development + Shipping; strict includes, `BuildPlugin -StrictIncludes`, cook check, `adsk_selftest_win64.ps1`; a 5-minute fuzzer soak; `-game -nullrhi` smoke of five project maps with no AdamskAI warning. 2026-11-01: packaged Development (pak) with brains starting from the pak, a packaged listen server and client (brains on the server only), MSVC static analysis clean, `AdamskAI.*` under ASan, a 30-minute fuzzer soak; a Shipping package running 5 minutes; live reload on a project map with the project's controller (keep-state, a rejected save, restore); 40 PIE map changes with no leaked brain, subsystem or UObject; 1000 managed brains for 14 minutes with flat memory |
| Win64, UE 5.6.1 and 5.7.4 | 2026-11-02 | `BuildPlugin -StrictIncludes`, a host project build, 660 `AdamskAI.*`, 65 `AdamskAIWorld.*` each; no project XML, game targets or packaging |
| Linux, UE 5.6.1, clang | 2026-11-02 | `adsk_host_check.sh` (build, check, validate, 660 `AdamskAI.*`, strict includes), 65 `AdamskAIWorld.*`; `Content/` regenerated with it, and 5.8.4 reads it (65/0) |
| Linux, UE 5.8.4, clang | 2026-10-31 | `adsk_all.sh --world` (check, validate, schema check, simulate, `AdamskAI.*`, `AdamskAIWorld.*`), `adsk_host_check.sh` (the plugin alone in an empty host: build, validate, `AdamskAI.*`, strict includes); a 30-minute fuzzer soak: 15.2 million mutants, 948,470 compiled and ran, none broke a rule |
| Android, UE 5.8.4, NDK r27c (built on Win64) | 2026-11-02 | `BuildPlugin -TargetPlatforms=Win64+Android -StrictIncludes`; a ThirdPersonGame `libUnreal.so` for arm64 and x64; an ASTC APK with the project XML in the UFS manifest; on an x86_64 emulator (Android 16) `adsk.Status` shows a running brain, no ensure or crash. Not run: a phone, Shipping, the test suite |
| Mac | never | — |

Not run on Win64 for 0.5.0: scripted PIE scenarios through Unreal MCP. The fixes the Win64 runs found are applied on
Linux since 2026-11-01; the Win64 scripts, the ASan refusal and the `schema-refused` selftest case have not run on
Windows in this form.

## Confirmed defects

None open. Record a defect here with its owner, a reproduction and the regression that closes it; P1 is a crash or a
completion applied to the wrong lifetime, P2 incorrect behavior or validation, P3 documentation. Delete it only after
its regression passes and the affected contract is documented. The review of a 0.5.0 candidate (`0488ef4`, 2026-10-31)
found BUG-001 to BUG-007 and DOC-001; all are closed in 0.5.0 with regressions (CHANGELOG, Fixed).

## Performance against Behavior Trees and StateTree

`Tools/adsk_bench.sh` (Bench/AdamskAIBench) runs one behavior written three ways side by side: Linux, Development
editor, `-nullrhi`, 1000 agents, three runs, per agent and frame over a world of bare controllers. On 2026-10-31,
before brains slept: Behavior Tree 0.10–0.16 µs, StateTree 0.22–0.32 µs, AdamskAI managed 0.33–0.61 µs, `bAutoTick`
0.76–1.25 µs. With `bSleepWhenIdle` (the same day): AdamskAI `bAutoTick` 0.08–0.11 µs, managed 0.09–0.15 µs,
Behavior Tree 0.12–0.13 µs, StateTree 0.20–0.22 µs. The cores alone, ticked every frame: AdamskAI 0.13–0.15 µs,
StateTree 1.31–1.48 µs. Delete a row when it lands and re-run the bench.

- **PERF-4 — managed pass bookkeeping:** a sleeping brain still costs the pass a weak-pointer `Get` and an `IsAsleep`
  per frame, and `RemoveAll` walks every entry per pass; a wake queue would visit only due brains. The viewer
  distance is no longer measured without `TickLOD` levels.
- **PERF-5 — `TickBrain` overhead:** measure the instance-call guards.
- **PERF-6 — a wider bench:** a behavior reacting to a value that changes every frame (where sleeping does not help),
  memory per agent for all three, `TickLOD`, the median of several runs in `adsk_bench`; then the numbers in perf.md
  and README.
- **PERF-7 — sensors and bridges keep a brain awake:** a sensor could declare its interval and the perception bridge
  could publish on change instead of in `OnPreTick`, so such brains sleep too. The same holds for a project
  controller: every Win64 host controller but one binds `OnPreTick` to publish facts, so the −70% of sleeping never
  reaches it. Options: a pre-tick called only before a tick already decided on, the host waking the brain with
  `SetInput` on a change; sensors with a `period=` they return from `GetNextWake`.
- **PERF-8 — no `ChildLanes` member:** child lanes get consecutive ids, so a `Parallel` can keep its first lane's id
  in `BackgroundLane` (a node is a `SimpleParallel` or a `Parallel`, never both) and its child count from the
  definition: another 256 bytes per instance, about 6724 in all.
- **PERF-9 — perception bridge per candidate:** about four weak-pointer resolves, two or three `TMap` finds by weak
  key and a `GetActorLocation` per candidate per tick, all cache misses: 7–9 µs per brain at 64 candidates. A dense
  `TArray` sorted by `SortKey` with the raw pointer cached once per tick, no second `FindChecked` in `OnPreTick`, and
  the forgotten-candidate sweep only when one is lost or a `RequiredActorTag` is set.
- **PERF-10 — a self-ticking fleet after sleep:** `AdamskAIWorld.Perf.SelfTickingFleetAfterSleep`, N self-ticking
  brains that `Wait` once and then stay awake. On Win64 4000 such brains cost 11.05 ms per frame against 8.51 awake
  from the start, before awake brains stopped calling `SetComponentTickIntervalAndCooldown` (8.33 after, 2026-11-01).

## Waves

The order the work is taken in now; each wave ends with `adsk_all --world`, the fuzzer, the strict includes and a
fresh [comparison.md](../comparison.md). Delete a line when it lands.

0. **Stabilize the existing contracts.** BUG-001 to BUG-007 and DOC-001 of the 2026-10-31 review are closed (CHANGELOG,
   migration.md); brains sleep (PERF-1 to PERF-3, 2026-10-31); then PERF-6, PERF-4, PERF-5 and PERF-7. Add the engine-free CI gate
   already listed under the release debt, then proceed to the host project pilot.
1. **A host project pilot.** H2 roles, H3 `ReservePoint`, H5 noise and last known position, and orders with an
   answer on node arguments (H7), designed against a game's existing tactical layer and moving it onto the plugin.
   Ends with a one-hour soak of 200 agents with reloads.
2. **Stealth and animation.** H6 suspicion, H12 `PlayMontage`, K7 response curves if utility needs them.
3. **On a trigger only.** K8, R1, K10, H8, H9.
4. **The next release** after 0.5.0 (next section), with speed, determinism and coverage numbers for the comparison.

## Owed by the 0.5.0 release

0.5.0 is the first release from the plugin's own repository (github.com/RazielSun/AdamskAI), published from a
snapshot of the host project's tree without its history. What it did not get yet, in order; delete a row when done.

| Step | Details |
|---|---|
| CI | GitHub Actions running `adsk_check.sh` and a Markdown link check, both engine-free; building and testing need an Unreal Engine on a self-hosted runner, which a free runner does not have |
| README picture | a capture of the demo ([demo.md](../demo.md)): `adsk.Demo Squad` or `Tokens` with labels, and the Fleet line |
| Mac | the build in "Checks that need a person or another machine" |

## Open proposals

Each needs a maintainer's yes or no; the recommendation is in the last column.

| # | Proposal | Recommendation |
|---|---|---|
| B5 | `.clang-format` taken from the current code, checked by `adsk_check` | optional |
| B6 | One monitor interface (recorder, Visual Logger, Insights, debugger as subscribers) and an execution gate for breakpoints such as `file:node:enter`, keeping the debugger out of the runtime | when a breakpoint is first wanted |
| B7 | A save-and-restore round trip after every tick of every golden trace. A restore restarts the leaf state's tree, so records diverge by design; comparing state paths and Locals at quiet points is possible. `AdamskAI.Fuzz.Compiler` already restarts every mutant from snapshots | only if snapshot bugs show up |
| B11 | `AdamskAIDemo` as a `DeveloperTool` module, or denied to `Game` targets: today it is allowed in Development game builds, adds the `adsk.Demo*` commands and mounts `@AdskDemo` on `Plugins/AdamskAI/Demo`, which is never staged | yes; setup.md already keeps `/AdamskAI/Demo` out of the cook |
| B10 | `RunEQS` item counts in the trace: an empty query records how many items were generated and passed (today only the Visual Logger's `LogEQS` entry has them). Needs a reason or payload on async completions through the core | yes, at the next EQS investigation |

`Examples/chase_and_wait.xml` declares `Home` and `Reach` but nothing reads them (`note unread-input`); dropping them
changes its golden trace and compat tables.

## Checks that need a person or another machine

| Item | Details |
|---|---|
| Unreal Insights regions | Trace → Channels `AdamskAI`, `region`, `bookmark`, `cpu`; record 30 s of agents, open in Insights: regions `<controller> <state>`, CPU scopes named by node kind under `FADSKInstance::Tick` |
| Project Settings panel | Project Settings → Plugins → AdamskAI shows every `UADSKSettings` field; a change writes `[/Script/AdamskAIHost.ADSKSettings]` to `DefaultGame.ini` |
| Another `BehaviorRoot` | with the root moved (files too), PIE and a live reload work; validation and tests on such a root already pass |
| Precise floats on Win64 | `AdamskAI` and `AdamskAIHost` set `FPSemantics = Precise`: a Win64 game build's log shows `/fp:precise` for them; `adsk_perf_compare_win64.ps1` measures the cost against the baseline; the golden traces replay in a packaged Win64 game |
| Mac | `RunUAT BuildPlugin -Plugin=<path>/AdamskAI.uplugin -Package=<out> -TargetPlatforms=Mac -StrictIncludes` |

## Tooling

| Item | Details |
|---|---|
| `adsk_perf_compare.sh --project` on Linux | it has not run there |
| `adsk_perf_ab` | a baseline snapshot does not reproduce across days (`baseline-waw-007.json`: 0.36 µs per `Start`, the same 0.4 code read 0.45 two days later): two plugin builds (a tag and the working tree) in two clean hosts, interleaved runs, N ≥ 3, median and spread; record the CPU load before a run and warn on a busy machine; refresh a baseline only from such a run |
| Win64 twins | `adsk_bench_win64.ps1` (the bench has run on Win64 only by hand) and `adsk_host_check_win64.ps1` |
| Catalogue regeneration | `adsk_validate --regenerate` / `-Regenerate` writing the project's catalogue and schema, and the full command in the `catalogue-stale` line, which names only the flag |
| A map smoke with a verdict | `-ExecCmds` runs before NPCs spawn, so a `-game` smoke sees only errors and crashes: `adsk.StatusEvery <sec>` or a status at exit, or a project CQTest on `FMapTestSpawner` checking every brain runs with no `GetLastError` and changed state |
| `adsk_smoke` | a packaged or `-game` run with `-trace=cpu,frame`, exported with `UnrealInsights -NoUI -AutoQuit`: `FADSKInstance::Start` equal to the NPC count and `TickBrain` above 0 prove brains run on a project map |
| Generated files in the pak | the staged behavior root ships `adamskai.xsd` and `node-catalog.md`; the engine has no per-file staging exclusion, so they would move out of the root |
| Foreign-namespace files | `adsk_schema_check_win64.ps1` does not validate a file whose root is in another namespace (`xmlns="..."`): .NET reports only a warning; turn on `ReportValidationWarnings` and count "no schema for element" as an error |
| Shared Python tooling | `Tools/lib/adsk.py` for engine and project resolution, the editor check, logs and exit codes, the `.sh` and `_win64.ps1` scripts as thin wrappers; worth it only when a script needs a third copy of the same logic |

## Features not built

Tier A needs no trigger; tier B waits for the trigger its row names (a real file or project that needs it), so it is
designed against real use; tier C waits for measurements or a platform. Where a feature goes and why:
[concept.md](../concept.md).

### Where new code goes

- **Core** (`AdamskAI`): language and runtime only, no Engine.
- **Host** (`AdamskAIHost`): what every Unreal game needs to run brains: movement, perception, EQS, groups and tokens,
  `Act` through `IADSKActionPerformer`, host services.
- **Genre kits** (combat, stealth, RTS, turn-based, animation, smart objects, GAS) are not part of this plugin. A
  project writes them as an `IADSKNodeProvider` in its own module, or ships them as a separate plugin that depends on
  `AdamskAIHost`. A kit reaches the pawn through interfaces the project implements, never through casts to game
  classes, and other agents through host services, never another brain ([concept.md](../concept.md) principle 6).
- A new module inside this plugin only where host code needs another engine plugin, listed as `"Optional": true` in
  the `.uplugin` so a project without it still loads AdamskAI.

### A. Next, no trigger needed

Nothing.

### B. On a trigger

Core changes, each with what a file does until then:

| # | Feature | What it unlocks | Until then | Cost | Take it when |
|---|---|---|---|---|---|
| K7 | Response curves for `Score` and `select` scores, instead of `weight * value + bias` only | needs and fatigue in a simulation | the host evaluates the curve and publishes a Float Input | 1–2 days | the first utility choice a line cannot express |
| K8 | Composite switching: a composite `OnUpdate` that may switch to another child, so a project writes its own reactive composites and `ReactiveSelector`/`UtilitySelector` become ordinary composites (undoes the `EADSKSelectorPolicy` split) | project-defined reactive control nodes | none needed yet | 3–4 days | a project composite that needs to switch children |
| R1 | Parallel regions in the state machine (SCXML `<parallel>`, HFSM2 orthogonal regions): one active leaf and tree per region | two independent machines in one agent (alertness beside movement) | nested `SimpleParallel`, or two brains | 1–3 weeks | a mode that nested parallels can no longer express readably |
| K13 | Eventless transitions: an `On` with `if` and no event, taken when its condition holds after the tree ends | a Behavior Tree root selector ported as it is (Lyra's bot) | a `decide` state whose `tree.success` and `tree.failure` handlers dispatch: one extra tick per decision and dispatcher lines | 1–2 days | a recipe "port a BT root selector" turns out too clumsy; write the recipe first |
| K10 | Snapshots on disk: a host helper (or an `IADSKHostServices` hook) that serializes `FADSKSnapshot` and maps its Object ids to something stable (an actor GUID, a soft path) and back | save games | the project serializes the snapshot and maps the ids itself | about 1 day | the first project with save games |

Host features:

| # | Feature | Take it when |
|---|---|---|
| H2 | Roles: an enum Input the host assigns per group member (leader, flanker, suppressor) | a file that branches on its role |
| H3 | `ReservePoint`: a scope reserving a point, and an EQS test dropping reserved points, so cover and firing points are not shared | two agents pick the same point |
| H4 | Threat table: many candidates, one Object Input | an agent that must choose among several targets |
| H5 | Noise and last known position: a heard stimulus as a Vector Input with an event, and search points | the first file that investigates a sound or searches |
| H6 | Suspicion: rise modifiers on the perception bridge's alert input and `Suspicious`/`Spotted` events | the first file with a gradual detection |
| H7 | An order queue: push, pop, clear; the file sees `CurrentOrder` (enum) and its target and reports `OrderDone` | the first agent that takes queued orders |
| H8 | A locomotion interface (`IADSKLocomotion`) so `MoveTo`, `MoveToActor` and the focus nodes work without an `AAIController` (crowds, flow fields, custom movers) | a host that does not move through an AI controller |
| H9 | An agent that outlives its actor (the brain owned by a subsystem, the actor an optional avatar) and an off-screen LOD tier | World Partition unloading agents that must keep living |
| H10 | A time-of-day service publishing an enum Input | the first schedule-driven file |
| H11 | A turn driver: ticks a brain only on its turn, until `EndTurn` | the first turn-based host |
| H12 | `PlayMontage` async leaf and AnimNotify to Event with `Bind` | the first file that waits on a montage or an animation window |
| H13 | `RunEQSActor`: the query's best actor into an Object slot (`out-target`), so `MoveToActor` follows a moving target; queries whose generators yield no actors fail validation | a hunt where a point goes stale; Lyra's `EQS_FindTarget` works through a point today |
| H16 | `AdamskAIGameFeatures`, an optional module: a `UGameFeatureAction` that calls `FADSKBehaviorRoots::Register` on activation and `Unregister` on deactivation, with the activation test in C | behavior files shipped inside a Game Feature plugin (Lyra's ShooterCore); `Content/AI` of the project works without it |
| H17 | One target for several senses: a binding option that feeds sight and damage into the same `TargetInput` | a file that must fire at an attacker it cannot see; Lyra's BT does, the port turns to face the attacker and lets sight pick it |
| H18 | `Act` finds its performer among the host's components, not only on the pawn or the controller | a performer that is neither (a weapon component); changes the "pawn, else controller" contract |

Kits outside this plugin (a separate plugin each, depending on `AdamskAIHost`):

| Kit | Contents | Take it when |
|---|---|---|
| `AdamskAIGAS` | `ActivateAbility` (async leaf, cancels the ability on leave), `SendGameplayEvent` (succeeds when the event is delivered: a passive ability waiting on it is not counted as activated), `WithLooseTag` scope; sensors for the brain's own ASC tags, tag counts, attributes, can-activate and cooldown; a bridge component subscribing to tag and attribute changes so the brain may sleep; `FADSKGASActionMap` turning an `Act` action into an event or ability for a performer. 4–6 days | a GAS project adopts AdamskAI as its pilot (Lyra: its bots run on the plugin today with no change to it, 21 : 21 against Lyra's BT bots) |

A Lyra recipe ("port Lyra bots": the controller, the bot subsystem, `lyra_bot.xml`) goes into [recipes](../recipes/README.md)
rather than a kit: Lyra's classes are not exported, so the code lives in the game module. Write it with the pilot.

Not planned: names, strings or lists as slot types (concept.md principle 4: lists live in the host, the file sees one
target).

### C. Scale, platform, multiplayer and internals

| Feature | What it unlocks | What stands in the way |
|---|---|---|
| K11: exact resume of procedures | a snapshot that continues a running procedure instead of restarting the leaf state's tree: rollback, network prediction | 1–3 weeks and an `OnResume` contract every node author must keep |
| K12: raised limits | 1024 XML elements, 64 states, 4096 nodes are constants in `FADSKCompiler` | raise one when a real file hits it |
| Yielding parallel tick | definitions with host nodes run their pure part on workers, finishing on the game thread | measured, not needed yet: brains are 2.5% of a 200-agent frame |
| Mass | the instance in a fragment, processors over chunks; the core is already value-only with one memory arena per instance | host effects still need a game-thread phase |
| Cross-world definition cache | dedicated servers and PIE with clients compile each file once per world | measured 0.45 ms for two files per world |
| Dedicated `Server` target build | no `UADSKDebugSubsystem` on `NM_DedicatedServer`, a packaged server run | needs a source-built engine; PIE dedicated server already works |
| Replication component | `UADSKReplicatedStateComponent` mirroring `OnActiveTagsChanged` into a replicated container | wait until two projects have written the same component by hand |
| Game Feature mount activation test | a world test activating a test Game Feature plugin with a behavior mount | needs a test Game Feature plugin; mounts are covered through `BehaviorMounts` |
| Internal procedure boundary (A6) | a cleaner split between the procedure stepper and the instance | no user-visible gain |

### D. Replay and rewind

A brain's decisions depend only on its file, its state, the Inputs and events the host gives it, the tick deltas and
its seed; the world reaches it only as Inputs. So a run that kept those can be run again headless and must give the
same records, and "rewind to tick N" is a replay to tick N. What exists: golden traces for the examples
(`AdamskAI.Examples.Run`), seed replay tests, `AdamskAI.Trace.Equivalence`, `adsk_simulate` scenarios written by hand,
`adsk.DumpTrace` and `FADSKSnapshot`. What does not: turning a recorded game run into a replay, a whole run kept
from its start, a timeline to step through, and the editor's Rewind Debugger. The rows go in order; each one needs
the one before it.

| # | Feature | What it unlocks | Until then | Cost | Take it when |
|---|---|---|---|---|---|
| D1 | Check what `adsk.DumpTrace` keeps against what a replay needs: every Input write with its value, events with parameters, each tick's delta, the seed, when each host leaf finished and how, and Object values as stable ids. Write down what is missing | knowing how far D2 is | the manual recipe in [debugging.md](../debugging.md#networked-games): seed plus the dump, read by a person | an hour or two | before any other row |
| D2 | `adsk_replay`: a dump becomes an `adsk_simulate` scenario (Inputs and events at their ticks, host leaves finishing when and how they did), runs headless and diffs its trace with the dump. Proved on a recorded `adsk.Demo Chase` run with an empty diff, then on a SimpleCommando enemy | a bug from a play session reproduced offline and kept as a test; determinism proved on a real game run, not only on examples | `adsk_simulate` scenarios written by hand | 2–3 days | the first bug that only shows in play, or the owner's call |
| D3 | A whole run recorded from its start: `adsk.Record <address> on\|off` streams one brain's records to a file instead of the 256-record ring, which drops the oldest into a `Gap` | D2 on runs longer than a few seconds | `RecorderCapacity` raised for the session | about 1 day | D2 meets a `Gap` |
| D4 | A trace viewer: a static HTML page from a trace or dump, one lane per agent with its states over time; a click shows the Inputs at that tick, the transition taken and the condition value it read; two traces side by side show where they diverge | reading "why did it do that" without the log; pictures for the README and posts | `grep transition-taken` on the trace | 2–3 days | after D2, or earlier for the docs and marketing |
| D5 | Rewind to tick N: replay to N from the start, or from the nearest snapshot taken on a state entry (a snapshot restarts the leaf tree, K11, so only entries are exact), and step forward one tick at a time with the state, Locals and node statuses shown | stepping through a decision like a debugger | replaying the whole run and reading the records | 2–3 days on top of D2 | after D2 and D4 |
| D6 | Unreal's Rewind Debugger: a trace channel and a Gameplay Insights track per brain (state path and active node over time), scrubbed in the editor together with animation and physics, as StateTree has | rewind inside the editor, in step with the rest of the game | D4 and D5 outside the editor | 1–2 weeks; overlaps B6 (one monitor interface, breakpoints) | a project asks for it, or after D5 proves the format |

Limits that stay: Shipping has no recorder; values the host computes in its own modules can differ in the last bit
between the editor and a packaged Win64 game ([limitations.md](../limitations.md)); a replay reproduces the brain's
decisions, not the world, so perception and navigation are replayed as the Inputs and completions they produced.
