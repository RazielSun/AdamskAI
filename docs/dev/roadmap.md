# Roadmap: what is left

The one list of everything AdamskAI still wants: confirmed defects, checks nobody has run, open proposals and features not built.
Everything that is done is described in the other documents as how the plugin works, not here. When an item lands,
delete it from this file and describe the result where it belongs ([migration.md](../migration.md) for API changes).

## Verified so far

| Platform | Last run | Result |
|---|---|---|
| Win64, UE 5.8.4, MSVC 14.51 | 2026-10-30 | `adsk_all_win64.ps1 -World` (check, validate with schemas and catalogues, schema check, simulate, 610 `AdamskAI.*`, 51 `AdamskAIWorld.*`), strict includes, `BuildPlugin` editor + game Development + Shipping, `adsk_selftest_win64.ps1`, packaged Development and Shipping runs with brains loading |
| Linux, UE 5.8.4, clang | 2026-10-31 | `adsk_all.sh --world` (check, validate, schema check, simulate, `AdamskAI.*`, `AdamskAIWorld.*`), `adsk_host_check.sh` (the plugin alone in an empty host: build, validate, `AdamskAI.*`, strict includes); a 30-minute fuzzer soak: 15.2 million mutants, 948,470 compiled and ran, none broke a rule |
| Mac | never | — |

The Win64 run predates the 2026-10-31 changes ([migration.md](../migration.md): the kit fold, node arguments, the
ImGui panel leaving); run `adsk_all_win64.ps1 -World` and the strict includes there again.

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
- **PERF-5 — `TickBrain` overhead:** broadcast `OnPreTick` only when bound; measure the instance-call guards.
- **PERF-6 — a wider bench:** a behavior reacting to a value that changes every frame (where sleeping does not help),
  memory per agent for all three, `TickLOD`, the median of several runs in `adsk_bench`; then the numbers in perf.md
  and README.
- **PERF-7 — sensors and bridges keep a brain awake:** a sensor could declare its interval and the perception bridge
  could publish on change instead of in `OnPreTick`, so such brains sleep too.

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
| Win64 run of the current tree | `adsk_all_win64.ps1 -World`, `adsk_strict_includes_win64.ps1`, `adsk_buildplugin_win64.ps1`, `adsk_cook_check_win64.ps1` |
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
| B10 | `RunEQS` item counts in the trace: an empty query records how many items were generated and passed (today only the Visual Logger's `LogEQS` entry has them). Needs a reason or payload on async completions through the core | yes, at the next EQS investigation |
| B14 | Perception bridge filters: a required actor tag, a stimulus tag, and an Input for a heard location, so "sense enemies" needs no controller code | only if a second project wants it |

`Examples/chase_and_wait.xml` declares `Home` and `Reach` but nothing reads them (`note unread-input`); dropping them
changes its golden trace and compat tables.

## Checks that need a person or another machine

| Item | Details |
|---|---|
| Unreal Insights regions | Trace → Channels `AdamskAI`, `region`, `bookmark`, `cpu`; record 30 s of agents, open in Insights: regions `<controller> <state>`, CPU scopes named by node kind under `FADSKInstance::Tick` |
| Project Settings panel | Project Settings → Plugins → AdamskAI shows every `UADSKSettings` field; a change writes `[/Script/AdamskAIHost.ADSKSettings]` to `DefaultGame.ini` |
| Another `BehaviorRoot` | with the root moved (files too), PIE and a live reload work; validation and tests on such a root already pass |
| Mac | `RunUAT BuildPlugin -Plugin=<path>/AdamskAI.uplugin -Package=<out> -TargetPlatforms=Mac -StrictIncludes` |

## Tooling

| Item | Details |
|---|---|
| Linux twin of `adsk_selftest_win64.ps1` | the scripts' own failure paths and exit codes; `adsk_perf_compare.sh --project` has not run on Linux either |
| Perf baseline | start and reload were measured slower than `baseline-waw-007.json` (`start_us_per_instance` 0.36 → 0.44 µs, `reload_commit_us_per_brain` 3.2 → 4.5 µs, no ceiling crossed); profile `StartCost` against an older build, then refresh the baseline with `-Update` in that change |
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
