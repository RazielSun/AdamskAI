# Performance

Where AdamskAI spends time and memory, the numbers measured on Win64, and how to measure your own project.

## How the cost is shaped

- **Compile once per file.** XML → `FADSKDefinition`: flat arrays of states, transitions, nodes and port bindings
  addressed by index, immutable, shared by every agent. `PrewarmFiles` compile on a worker at world creation;
  otherwise the first `Acquire` compiles on the game thread and logs once (`… was compiled on the game thread at its
  first use; list it in PrewarmFiles`). Two medium files (about 200 lines each) together cost 0.45 ms per world.
- **Instance per agent, allocated up front.** Slots, a 64-entry event queue with its parameter block, the result
  queue, one node-memory arena with a fixed slice per node (a node start never allocates), inline lane stacks.
- **Tick** (`FADSKInstance::Tick`): pending inputs → action results → change events → event dispatch (at most 16) →
  `FADSKProcedure::Step` (wrappers root to leaf, then the leaf's `OnUpdate`), at most 64 operations. The worst case is
  bounded.
- **Host.** A brain ticks from its own `TickComponent` (`bAutoTick`), from the owner's code (`TickBrain`), or from the
  managed pass (`bManagedTick`: one loop per world before the actors, distance LOD from `TickLOD`, phases spread over
  the interval, accumulated delta, optional `ParallelFor` for definitions made only of thread-safe nodes). Async
  leaves (`MoveTo`, `MoveToActor`, `RunEQS`) finish through `PostActionResult`. The perception bridge publishes before
  every brain tick and sorts its candidates only when the set changes.
- **Recorder.** `RecorderCapacity` records per brain (default 256, 29 KB; 112 B per record) in every build but
  Shipping. A Guard records its value when it starts and then only when the value changes.

## Numbers (Win64, Development editor, 2026-10-29)

| Benchmark | Workload | Result |
|---|---|---|
| `AdamskAI.Perf.CoreFleet` | 1000 instances, core nodes only, 300 frames | 0.20 µs per agent tick with a 256-record recorder, 0.17–0.18 µs without one (Shipping-like); 0.13 ms per frame on workers; 5.75 KB per instance plus 128 B node memory; 0.73 records per agent frame |
| `AdamskAI.Perf.ParallelChurn` | 1000 instances whose tree is one `SimpleParallel` restarting every few frames (76 starts per agent in 300 frames) (2026-10-30) | 0.20 µs per agent tick serially, 0.10–0.11 ms per frame on workers; 0.22 µs and 0.11–0.12 ms before cancelled lanes were kept as spares |
| `AdamskAI.Perf.ManagedTick` | the same file on managed brains, every brain every frame | 0.24 µs per brain at 250 and at 1000 brains: linear |
| `AdamskAI.Perf.PerceptionBridge` | one brain plus a bridge choosing among N sensed actors; `candidates_16_ignoring_8` sets `IgnoreGameplayTags` with half the candidates tagged | 0.16 µs with no bridge; 0.7 at 4, 1.4–1.9 at 16, 4.8 at 64 candidates; Linux 2026-11-03: 1.2–1.6 at 16, 1.6–1.7 ignoring 8 of 16 (each candidate's tags read twice a tick) |
| `AdamskAI.Perf.ProjectCompile` | read and compile the project's behavior files | a 200-line file 0.35 ms, a 60-line file 0.10 ms |
| `AdamskAI.Perf.StartCost` | `Start` and `Stop` of 1000 instances of the core file, 20 rounds; one reload commit over 200 brains on one file, 10 commits (2026-10-30) | 0.36 µs per `Start`, 0.09 µs per `Stop`; a commit 0.65 ms for 200 brains (3.2 µs per brain: prepare, stop, publish, restart) |
| `AdamskAIWorld.Perf.ShuttlingFleet` | 200 agents walking A↔B with `MoveTo`, PIE, `-nullrhi` | 5.0 ms per whole frame, of which the managed brain pass 0.124 ms (2.5%); all serial, since `MoveTo` is a host node |
| `AdamskAIWorld.Perf.ShuttlingFleetTickLOD` | the same fleet with `TickLOD` `{1000 cm, 0.2 s}`, `{2000 cm, 0.5 s}` and one viewer crossing it (2026-10-30) | 0.23 brain ticks per agent frame (−77%); the pass 0.076 ms (−42%): about 0.2 µs per brain per frame is the pass's own distance and interval bookkeeping, ticking or not. 150 of 200 agents walking at the end (196 without LOD): a far agent starts its next move up to one interval late |

On Linux (Development editor, `-nullrhi`, 2026-10-31) the demo's fleet of 1000 core-node brains on the managed tick
costs 0.13 ms per frame with recorders and 0.10 ms on workers without them, sleeping through their waits; awake every
frame 0.40 ms and 0.23 ms (`AdamskAIWorld.Demo.Fleet`, `FleetParallel`; [demo.md](../demo.md#fleet)). The 200
shuttling agents of `AdamskAIWorld.Perf.ShuttlingFleetSleeping` cost 0.038 ms per frame against 0.15 ms awake. Against
a Behavior Tree and a StateTree running the same behavior: [roadmap.md](roadmap.md#performance-against-behavior-trees-and-statetree)
(`Tools/adsk_bench.sh`).

**0.4 against 0.5 on Win64** (2026-10-31, three identical empty hosts, one plugin copy each, three interleaved
rounds, medians): `Start` 0.45 → 0.47 µs and `Stop` 0.10 → 0.11 µs per instance (the cost of `Concurrent`, `Set`, the
per-tick input flags and the sleep bookkeeping); 6660 → 6980 bytes per instance; a reload commit over 200 brains 0.98
→ 1.00 ms; the core tick, the 200-agent frame and the brain pass unchanged; 1000 managed brains 0.27 → 0.08 ms per
frame by sleeping. The 2026-10-29 table above was measured on 0.4: the same 0.4 code read 0.45 µs per `Start` and 0.98
ms per commit two days later, so a baseline snapshot is no reference across days; compare two builds in one session.

Runs differ by ±20% on the same machine; compare with the tool below, not by eye. Parallel ticking helps little
(1.4× even when every node is thread-safe): each instance is about 0.2 µs of work against `ParallelFor`'s scheduling.
Engine work (movement, navigation, EQS) dominates a real frame.

## Measuring

**Benchmarks.** `Tools/adsk_test_win64.ps1 AdamskAI.Perf.` (world-free) and `AdamskAIWorld.Perf.` (PIE) write
`Saved/AdamskAI/perf/<test>.json`; they never fail on a number. `Tools/adsk_perf_compare_win64.ps1` (or `.sh`) prints
each number against this machine's baseline `Fixtures/perf/baseline-<machine>.json`, marks one more than 20% worse
`WORSE`, and one above its limit in `Fixtures/perf/ceilings.json` `OVER CEILING` (warnings; `-Strict` exits 1).
Re-baseline deliberately with `-Update`, in the change that explains why. The ceilings:

| Number | Ceiling |
|---|---|
| Core µs per agent tick, recorder on / off | 0.5 / 0.3 |
| Managed pass µs per brain, 1000 ÷ 250 brains | ≤ 1.3 (stays linear) |
| Records per agent frame | ≤ 1.0 |
| Node memory per agent / instance bytes | ≤ 256 B / ≤ 8 KB |
| Bridge µs per brain tick at 16 candidates, with or without `IgnoreGameplayTags` | ≤ 3 (one process reads 1.4–2.2 on Win64, another 0.4 or 0.5 build alike, by where its objects lie) |
| Synchronous compiles in a prewarmed world | 0 (asserted by `AdamskAIWorld.Reload`) |

**In game.** `stat AdamskAI`: cycle stats for the managed pass, brain ticks, the perception bridge and `Acquire`;
counters for brains ticked, events dispatched, node starts, records written and synchronous compiles. CSV category
`AdamskAI` (`ManagedPass`, `BrainTick`) for `csvprofile` captures of packaged runs; keep those short, since with 1000
actors the profiler itself grows memory, so a soak measures memory without it. `-benchmarkseconds` also caps the total
`DeltaTime`, so a long hitch at start ends the run. LLM tag `AdamskAI` (instances,
recorders, compiles) in `stat LLM` and `memreport`. `UADSKWorldSubsystem::GetManagedTickStats()` gives the pass's
time and tick counts to code.

**Unreal Insights.** CPU scopes on `FADSKManagedTicker::Tick`, `UADSKBrainComponent::TickBrain`,
`FADSKInstance::Tick`/`Start`, `FADSKParallelTick::Tick`, `UADSKPerceptionBridgeComponent::OnPreTick`,
`FADSKCompiler::Compile`, `UADSKWorldSubsystem::Acquire`/`RequestReload`/`CommitPendingReloads`. Start with
`-trace=cpu,frame,bookmark,region,AdamskAI` (or `Trace.Enable AdamskAI` at run time) and set `RecorderCapacity=0`
first, so the recorder is not in the profile. The `AdamskAI` channel adds, from the instance (so with recorders off):

- a timing region per active state, `<host> <state>`, in the Regions track (nested states overlap their parents);
- a CPU scope per node callback named by node kind (`MoveTo`, `Wait`, a project node);
- bookmarks for what usually means a stuck agent: `AdamskAI hold: <host> in <state>`, `AdamskAI start failed: <host>
  misses <inputs>`.

With hundreds of brains the per-instance scopes add tracing cost of their own: read the pass-level scope for totals.

## Guidance

- List every behavior file a level uses in `PrewarmFiles`.
- Keep `bSleepWhenIdle` on: a brain that waits or walks costs nothing until its wait ends or its move reports.
  Publish facts as they change, not every frame, so the brain can sleep between them.
- Use the managed tick with `TickLOD` once a level has more than a few dozen agents.
- `bParallelManagedTick` only pays for definitions made of core nodes; one host node makes a definition serial.
- Profile with `RecorderCapacity=0`; Shipping never records.
