# Comparison with other AI runtimes

What AdamskAI can express and check next to the systems it borrows from ([concept.md](concept.md#relatives),
[references.md](references.md)), feature by feature and pattern by pattern. Speed, stability and test coverage
come next (see the end); this page covers what each system has and what it takes to build a behavior in it.

Compared, read from source on 2026-10-31:

| System | Version | Read in |
|---|---|---|
| AdamskAI | 0.5.0 ([CHANGELOG.md](../CHANGELOG.md)) | this plugin |
| UE Behavior Tree | UE 5.8.4 | `Engine/Source/Runtime/AIModule/.../BehaviorTree/` |
| UE StateTree | UE 5.8.4 | `Engine/Plugins/Runtime/StateTree/`, `GameplayStateTree/` |
| BehaviorTree.CPP | v4, `a9d6e56` | github.com/BehaviorTree/BehaviorTree.CPP |
| py_trees | `786859d` | github.com/splintered-reality/py_trees |
| HFSM2 | `d286577` | github.com/andrew-gresyk/HFSM2 |
| SCXML | W3C Recommendation; uscxml `11013b7` | w3.org/TR/scxml, github.com/tklab-tud/uscxml |

Marks: **yes** built in; **part** built in with a limit the cell names; **code** you write it in the host language;
**no** not available (for UE: not found in the 5.8.4 runtime sources). BehaviorTree.CPP, py_trees, HFSM2 and SCXML are general libraries without
game primitives (movement, perception, EQS), so they appear in the control rows and patterns only.

## Feature matrix

### Structure and authoring

| Feature | AdamskAI | UE BT | UE StateTree | BT.CPP | py_trees | HFSM2 | SCXML |
|---|---|---|---|---|---|---|---|
| Authored as | XML text | graph asset | graph asset | XML text (+ Groot2 editor) | Python code | C++ templates | XML text |
| Hierarchical states | yes | no | yes | no | no | yes | yes |
| Parallel (orthogonal) regions | no | no | part: tasks of all active states run together; `RunParallelStateTree` task | no | no | yes | yes (`<parallel>`) |
| History (resume a sub-state) | yes: shallow and deep | no | no | no | no | part: resumable regions | yes: shallow and deep |
| Behavior trees | yes: one per leaf state | yes | no (tasks per state) | yes | yes | no | no |
| Reusable sub-behavior with arguments | yes: `Run` + `Param`, expanded at load | part: `RunBehavior`, `RunBehaviorDynamic`; no arguments, shared blackboard | yes: linked states and assets with parameters | yes: `SubTree` with port remapping | code | code | part: `<invoke>` of another machine |
| Typed slots bound by name | yes: Inputs, Locals, six types and enums | yes: blackboard keys | yes: property bindings | yes: ports on a blackboard | part: blackboard keys | code | part: datamodel |
| Native nodes in | C++ only | C++ or Blueprint | C++ or Blueprint | C++ (+ scripts) | Python | C++ | executable content |
| Visual editor | no: XSD completion in any XML editor; `adsk_graph` draws the state machine as Mermaid or DOT | yes | yes | yes (Groot2) | no (renders dot) | no | third-party |

### Decisions and control

| Feature | AdamskAI | UE BT | UE StateTree | BT.CPP | py_trees | HFSM2 | SCXML |
|---|---|---|---|---|---|---|---|
| Priority interrupt when a condition turns true | yes: `ReactiveSelector` of `Guard`s, event handlers with `priority` | yes: observer aborts on blackboard decorators | yes: tick and event transitions with priority | yes: `ReactiveSequence`/`ReactiveFallback`, `_while` | yes: selector without memory, `EternalGuard` | code | yes |
| Interrupt from any decorator (a cooldown becoming ready) | yes: `Guard`, `Cooldown` and any node with `OnCheck` under a `ReactiveSelector` | yes: `Cooldown` and other decorators with abort modes | yes: transition conditions | yes: any condition node under a reactive control | yes | code | yes |
| Utility selection | yes: `UtilitySelector`, `select="best"` | no | yes: considerations, highest utility | no | no | yes | no |
| Weighted random selection | yes: `select="weighted"`, `RandomSelector`, `Chance`, `Roll`, `chance` on handlers | no | yes: random and utility-weighted random | no | no | yes | no |
| Events with typed payload | yes: declared `Param`s, `Bind` into Locals | no (blackboard, AI messages) | yes: gameplay-tag event with a struct payload | no (blackboard) | no | part: transition payloads | yes: event data |
| Conditions | comparisons `is`/`below`/`above`, slot operands, `When`/`Any` | one decorator per test | conditions with AND/OR | a scripting language | Python | C++ | datamodel expressions |
| Parallel tasks | yes: `Parallel` (up to 8, success and failure policies), `SimpleParallel` (a looping background), scopes | part: `SimpleParallel` (two lanes) | yes: all tasks of a state, completion Any or All | yes: `Parallel` with thresholds, `ParallelAll` | yes: three parallel policies | yes: orthogonal regions | yes |
| Retry on failure | yes: `Retry` | no (`Loop` repeats) | code (transition to self) | yes: `RetryUntilSuccessful` | yes: `Retry` | code | code |
| Write a literal to a slot | yes: `Set` | yes: `SetKeyValue` | yes: bindings, tasks | yes: `SetBlackboard`, `Script` | yes | code | yes: `<assign>` |
| Timers | `Wait`, `Cooldown`, `TimeLimit` | `Wait`, `Cooldown`, `TimeLimit` | `Delay` | `Delay`, `Timeout`, `Sleep` | `Timeout`, timers | code | `<send delay>` |
| Message to another agent with data | yes: `Radio` with parameters, a `bTakesArgs` node ([recipe](recipes/talk-between-agents.md)) | code | code | code | code | code | yes: `<send target>` |
| Planning | no (a node may plan) | no | no | no | no | yes: plans | no |

### Runtime and tooling

| Feature | AdamskAI | UE BT | UE StateTree | BT.CPP | py_trees | HFSM2 | SCXML |
|---|---|---|---|---|---|---|---|
| Per-agent seeded randomness | yes: every draw from the instance's stream | no: `FMath::FRandRange` in `BTTask_Wait`, `BTService` | part: child selection uses the execution's stream; `Delay` and the random condition use `FMath` | no RNG in the core | no | yes (XoShiRo) | implementation-defined |
| Replay a run from seed, inputs and events | yes: golden traces | no | no | no | no | part: seeded RNG and serialized transition history, no recorded inputs | no |
| Save and restore a running agent | yes: `FADSKSnapshot` (disk mapping is the host's, roadmap K10) | no | part: `FStateTreeInstanceData::Serialize`, no restore contract for a running tree | no | no | yes: serialization | no |
| Reload while running, keeping state | yes: XML reload with snapshot | no: none in the runtime (`RestartLogic` restarts) | no: none in the runtime (`RestartLogic` restarts) | no (rebuild the tree) | no | no (compile time) | no |
| Rejects a broken file before running | yes: strict compiler, `path:line` diagnostics | in the editor | in the editor | yes: throws at load (unknown node, port types) | at run time | at C++ compile time | validator-dependent |
| Headless validation for CI | yes: commandlet, both catalogues, XML Schemas | no | part: `StateTreeCompileAll` commandlet recompiles and saves assets | part: load the XML in a test | part: unit tests | compile | yes: W3C test suite |
| Finds a state that can hold or loop forever | yes: `hold-risk`, `adsk_simulate` livelock | no | no | no | no | no | no |
| Live debugging | Gameplay Debugger, Visual Logger, Insights regions, traces, `adsk.*` | BT debugger, Gameplay Debugger | StateTree debugger with trace recording | Groot2, file/SQLite/minitrace loggers | ASCII and dot displays, visitors | logging, structure report | implementation tools |
| Ticks on worker threads | part: definitions of thread-safe nodes only | no | no: no parallel tick in the runtime | no | no | n/a | n/a |

## Pattern catalogue

Each pattern is something a designer asks for. **file**: built entirely in the authored behavior from built-in
nodes; **code**: needs native code written for it; **no**: not expressible without changing the runtime.

### Control patterns

| # | Pattern | AdamskAI | UE BT | UE StateTree | BT.CPP | py_trees | HFSM2 | SCXML |
|---|---|---|---|---|---|---|---|---|
| C1 | Try A, else B | file: `Selector` ([selector_fallback](../Examples/selector_fallback.xml)) | file | file | file | file | code | file |
| C2 | Drop what you do when a fact turns true | file: `ReactiveSelector`, or an `On` with `if` | file | file | file | file | code | file |
| C3 | Same, when a cooldown becomes ready | file: `Cooldown` as a `ReactiveSelector` option | file | file | file | file | code | file |
| C4 | Change mode on an event and use its data | file: `On` + `Bind` | code | file | code | code | code | file |
| C5 | Resume the interrupted mode afterwards | file: `history` | code | code | code | code | file | file |
| C6 | Pick at random, weighted | file: `select="weighted"`, `chance` | code | file | code | code | file | code |
| C7 | Pick by score | file: `select="best"`, `UtilitySelector` | code | file | code | code | file | code |
| C8 | Do two things at once | file: scopes, `SimpleParallel` | file | file | file | file | file | file |
| C9 | Three or more lanes with a success policy | file: `Parallel success failure` | code | file | file | file | file | file |
| C10 | Give up after a time limit | file: `TimeLimit` | file | file | file | file | code | file |
| C11 | Retry up to N times | file: `Retry count` | code | file: transition to self | file | file | code | file |
| C12 | Reuse a procedure with different arguments | file: `Run` + `Param` ([run_reuse](../Examples/run_reuse.xml)) | code | file | file | code | code | code |
| C13 | Keep a counter or flag | file: `Set` | file | file | file | file | code | file |
| C14 | Tell another agent something, with data | file: `Radio` with `Param`; code for other routing | code | code | code | code | code | file |
| C15 | Find a loop that never settles, before running | yes: `adsk_simulate` | no | no | no | no | no | no |

### Game patterns (Unreal only)

| # | Pattern | AdamskAI | UE BT | UE StateTree |
|---|---|---|---|---|
| G1 | Patrol waypoints | file ([guard_patrol](../Examples/guard_patrol.xml)); the host publishes the next point | file + a service or task for the point list | file + a task for the point list |
| G2 | Stand a timed watch at a post, idle or scan at random, walk back | file ([guard_post](../Examples/guard_post.xml)) | code: no random composite | file |
| G3 | Chase an actor, search when lost | file ([hunt_actor](../Examples/hunt_actor.xml)) | file | file |
| G4 | Sense enemies through AI Perception | file + `UADSKPerceptionBridgeComponent` publishing Inputs | code: a service or controller | code |
| G5 | Line of sight with a cone | file: `LineOfSight` sensor | part: `ConeCheck` decorator (no trace) | code |
| G6 | Find a point with EQS and go | file: `RunEQS` + `MoveTo` | file | file |
| G7 | Flank or attack, by distance score | file: `select="best"` | code | file |
| G8 | Face the target while strafing | file: `FocusActor` + `MoveTo strafe` | file: `DefaultFocus` service | file |
| G9 | Attack with a cooldown | file: `Act` + `Cooldown`; the attack itself in an `IADSKActionPerformer` | file + an attack task | file + an attack task |
| G10 | At most N attackers on one target | file: `HoldToken` | code | code |
| G11 | One sighting alerts the squad, with where | file: `Radio` with `Param`, `GroupTarget` | code | code |
| G12 | Boss phases that resume after a stagger | file: phases as states with `history` | code | code |
| G13 | Pick a melee or a charged attack at random | file: `select="weighted"` | code | file |
| G14 | Flee below a health threshold | file: change event, `if ... below` | file: blackboard decorator | file: condition |
| G15 | Wait for a montage | code: `Act` and the performer (`PlayMontage` is roadmap H12) | file: `PlayAnimation` | code |
| G16 | Investigate a noise, then search around it | code: the host publishes the point (H5) | code | code |
| G17 | Save a fight mid-way and load it | code: `FADSKSnapshot` + id mapping (K10) | no | code: instance data serializes, restoring a running tree is yours |

## What it shows

AdamskAI leads where the behavior is text that is checked and replayed: seeded randomness per agent and
replayable traces, history, typed event payloads, a validator that finds holds and livelocks without running the
game, and group primitives (`HoldToken`, `Radio`, `GroupTarget`) that BT and StateTree leave to code.

It trails on authoring comfort and a few control primitives: no visual editor or Blueprint nodes (by decision; a
read-only diagram comes from `adsk_graph`), no parallel regions in the state machine, no expression language, and
no montage node. The gaps have roadmap rows: H5 (noise and last known position), H12
(`PlayMontage`), and parallel regions in the state machine (the largest; SCXML and HFSM2 have them).

## Next: speed, stability, coverage

- **Speed.** The same behaviors in BT, StateTree and AdamskAI in one project: a core-only tree on 1000 agents, a
  200-agent `MoveTo` fleet, a reactive interrupt every few frames; µs per agent tick, bytes per agent, start and
  load cost, with Insights and the recorder off. BehaviorTree.CPP outside Unreal, core trees only. AdamskAI's own
  numbers and ceilings: [perf.md](dev/perf.md).
- **Stability.** Feed `Fixtures/invalid` and the compiler fuzzer's mutants to BehaviorTree.CPP's XML loader;
  run each system twice with the same inputs and compare what the agents did; a one-hour soak with reloads.
- **Coverage.** The W3C SCXML tests that apply to AdamskAI's statechart subset (uscxml ships them under
  `test/w3c/`), and line coverage of the plugin's tests with `llvm-cov`.
