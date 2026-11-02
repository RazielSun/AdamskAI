# Testing and verification

Everything here runs without a renderer or client. Exit codes are shared by every script on both
platforms: `0` pass, `1` diagnostics / test failure / stale output, `2` usage, setup or write failure (or,
for builds, a running editor), `3` timeout, signal, crash or missing completion (never a pass).

## Commands

Build the host project's editor target first; every command below except `adsk_check` launches its
`UnrealEditor-Cmd`. Linux runs `Plugins/AdamskAI/Tools/<script>.sh`; Windows runs
`powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\<script>_win64.ps1` with the same mode
as a named parameter ([windows.md](../windows.md)).

| What | Linux | Windows |
|---|---|---|
| Build the editor (or `-Target Game`) target | host project's script / UBT | `adsk_build_win64.ps1 [-Target Editor\|Game] [-Configuration <c>]` |
| Static plugin rules, no engine | `adsk_check.sh` | `adsk_check_win64.ps1` |
| default + fixtures + the plugin's and the project's catalogue and schema checks | `adsk_validate.sh` (`--all`) | `adsk_validate_win64.ps1` (`-Mode all`) |
| Examples (plugin catalogue), then the behavior root if present | `--default` | `-Mode default` |
| The valid/invalid fixtures | `--fixtures` | `-Mode fixtures` |
| `docs/node-catalog.md` against the plugin schema, no write | `--catalog` | `-Mode catalog` |
| `docs/adamskai.xsd` against the plugin schema, no write | `--xsd` | `-Mode xsd` |
| Files or directories, full catalogue | `--paths <p[+p]>` | `-Paths <p[+p]>` |
| Scenarios ([Simulation](#simulation)); default: the plugin's `Fixtures/scenarios` and the project's `Tests/AdamskAI` | `adsk_simulate.sh [p[+p]]` | `adsk_simulate_win64.ps1 [p[+p]]` |
| Every valid file against the generated XML Schemas (the hand-written language part of `adamskai.xsd` cannot drift from the compiler unnoticed); no engine | `adsk_schema_check.sh` (`xmllint`) | `adsk_schema_check_win64.ps1` (.NET) |
| Automation tests, default filter `AdamskAI.` | `adsk_test.sh [filter]` | `adsk_test_win64.ps1 [filter]` |

Every option also takes `--project <uproject|dir>` / `-Project`; otherwise the project is the Git root,
else the nearest folder above the plugin, holding exactly one `.uproject`. Engine resolution is `UE_ROOT`,
then the `.uproject` `EngineAssociation` (Linux: `~/.config/Epic/UnrealEngine/Install.ini`; Windows: the
launcher's registry keys).

Portability checks, run when the plugin's build surface changes and before a release:

| What | Linux | Windows |
|---|---|---|
| Every module compiled alone, unity and PCH off (10 target × module builds) | `adsk_strict_includes.sh` | `adsk_strict_includes_win64.ps1` |
| The plugin alone, outside the project | `adsk_host_check.sh [--keep]`: fresh empty host, build, check, validate, test | `adsk_buildplugin_win64.ps1 [-OutDir <d>]`: `RunUAT BuildPlugin -StrictIncludes`, editor + game Development + Shipping |

Tooling checks, run when a script changes:

| What | Linux | Windows |
|---|---|---|
| Every script's failure path against the real engine: a planted `check()` side effect, a broken and a missing `-Paths` file, a bad `-Project`, a filter matching nothing, a 3 s test timeout, `adsk_build` and `adsk_strict_includes` with an editor open; each must give its documented exit code | not written yet | `adsk_selftest_win64.ps1 [-SkipEditor]` |
| Benchmark numbers against this machine's baseline (`Fixtures/perf/baseline-<machine>.json`); `WORSE` marks a number 20% worse | `adsk_perf_compare.sh <project> [--update] [--strict]` | `adsk_perf_compare_win64.ps1 [-Update] [-Strict]` |

The static rules (`Tools/lib/adsk_check.py`), the test verdict (`Tools/lib/adsk_test_report.py`) and the perf
comparison (`Tools/lib/adsk_perf_compare.py`) are one Python file each, run by both platforms' scripts, so they
cannot drift.

`adsk_check` rules: MIT header line, `.generated.h` last, nothing but questions (`Is*`, `Has*`, `Find*`,
`Contains*`, argument-less `Get*()`, ...) inside `check()` because it compiles out of Shipping and Test,
Build.cs dependencies from an allowlist (so no game module), an Engine-free core module, no ImGui include
anywhere, and a resolving test named by every rule of [semantics.md](../semantics.md).

The scripts run a host-project preflight before launching an editor: `ADSK_PREFLIGHT=<command>` (nonzero
exit refuses the run), `ADSK_PREFLIGHT=none`, or, by default, the project's own `Tools/lib/preflight.sh`
(`preflight_require_clean_modules`) on Linux or `Tools\lib\preflight_win64.ps1` on Windows.

`adsk_test` delegates to the project's own runner when it has one (`Tools/run_tests.sh`,
`Tools\run_tests_win64.ps1`); `--standalone` / `-Standalone` forces the plugin's runner, which deletes and
recreates `Saved/AdamskAI/automation` and reads its `index.json` before the log. A filter that matches no
test is exit `1`.

Direct commandlet use (what the scripts run):

```bash
UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate [-paths=a+b | -fixtures | -describe=<out.json> |
  -catalog=<out.md> | -xsd=<out.xsd> [-check] [-registry=plugin|full] | -projectcatalog | -projectxsd [-check] |
  -simulate=<scenario.json|dir>[+...] | -all] -unattended -nullrhi -stdout
```

Exactly one mode (or none = default). Output is `path:line: severity code [node]: message`, a `summary:` line,
and the final line `ADSK_VALIDATE_COMPLETE exit=N`. Validation modes write `Saved/AdamskAI/validate.json`
(versioned, atomic: a temp sibling then replace, so a failed write keeps the previous file and exits `2`).
The `full` registry also includes the game's providers. `-projectcatalog` and `-projectxsd` target
`<Project>/Content/<BehaviorRoot>/node-catalog.md` and `adamskai.xsd` with the full registry; with `-check` and no
file, a project whose full registry equals the plugin catalogue passes with a `note:` line, and one with provider
nodes fails `catalogue-stale` (the code for a stale schema too). The default mode prints a `note:` when the
project's packaging settings do not stage the behavior root ([setup.md](../setup.md) step 5).

Every file that compiles also gets `note` rows, which never change the exit code: `hold-risk` ([gotchas.md](../gotchas.md))
and four lint codes from `ADSK::FindLintNotes`: `unreachable-state` (no initial chain or transition target enters
the state), `unhandled-event` (no `On` names the event anywhere, so raising it does nothing), `unread-local` and
`unread-input` (no port, condition, change event or `Emit` argument reads the slot; an Input the host then publishes
for nothing). Unknown names in errors carry a suggestion when one is close: `unknown attribute 'durration'; did you
mean 'duration'?`.

## Fixtures

[../Fixtures/](../../Fixtures/) holds `valid/*.xml` (must produce no diagnostic, warnings included) and
`invalid/*.xml`, each with a same-named `.expect` file. Each `.expect` line is `line severity code [node-id]`,
compared in the compiler's emitted order; messages are not compared. The only comment allowed is a first
line `# generate: pad-to-limit`, which pads the source in memory to the byte limit plus one. A missing or
orphan `.expect` file, duplicate basenames, bad syntax, diagnostics in a valid file, or any line difference
fail `AdamskAI.Fixtures.*`. Every `EADSKDiagnosticCode` and every limit has a case, including `Run` (missing
target, cycle, expanded depth, generated duplicate id).

[../Fixtures/conformance/](../../Fixtures/conformance/) holds behavior files that test the language's semantics by
running, not compiling: each drives itself with `Emit` and core nodes and must reach a state named `pass`
(`AdamskAI.Conformance`, rules in [semantics.md](../semantics.md#conformance-files)). A new semantic rule gets a file
there; a rule that needs the host (Inputs, action results) stays an `AdamskAI.Execution.*` spec.

`AdamskAI.Compat.NormalizedLegacyTablesStable` compares the compiled tables of the examples and valid
fixtures that existed when XML v1 shipped with the frozen baselines in `Fixtures/compat/`, and lists every
differing line. Baselines are captured once, with `ADSK_CAPTURE_BASELINES=1` and the
`AdamskAI.Compat.CaptureBaselines` test, which rewrites only the baselines that exist and otherwise logs that
it skipped; a normal run never rewrites them. `AdamskAI.Compiler.Run` and `AdamskAI.Execution.Run` cover `Run` expansion, its
limits and prefixed trace ids.

Golden traces: each `AdamskAI.Examples.Run` scenario ends by comparing every record it produced, formatted as
`adsk.DumpTrace` prints them, with `Fixtures/golden/<example>.trace`, and lists the differing lines. A change in
what an example does, or in the trace vocabulary, shows up there. When the change is intended, rerun the examples
with `ADSK_CAPTURE_GOLDEN=1` and review the diff of the rewritten files before keeping them. Each file starts with
`# source <sha1>` and `# catalogue <fingerprint>` from its capture; only the records are compared, and a failure
says whether the behavior file, the catalogue or neither changed since, the last meaning the runtime itself did.

`AdamskAI.Fuzz.Compiler` mutates every example and every valid and invalid fixture with fixed seeds (dropped or
doubled attributes and lines, odd numbers, values swapped within the file, stray characters, and from the catalogue:
a leaf turned into another node kind, a generated node inserted with its ports bound to the file's own names, an
attribute renamed to a catalogue field or port): each mutant must compile to a definition or to errors on lines the
file has, never crash, and a definition must pass the hold and lint analyses and start and tick. The default 24
mutants per file take about a second; `ADSK_FUZZ_MUTANTS=200` runs 11,000 in about 12 s (budget 20 s). A soak sets
`ADSK_FUZZ_SECONDS` (for example `1800`): rounds repeat with new seeds until the time is spent; `ADSK_FUZZ_SEED=<n>`
moves every seed so two soaks cover different mutants, and `ADSK_TEST_TIMEOUT` must exceed the budget. The file being compiled
is written to `Saved/AdamskAI/fuzz/current.xml` first, so a crash leaves it behind; a mutant that breaks a rule is
kept as `<file>_<seed>.xml`.

## Regenerate the catalogue and the schema

```bash
UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate -catalog=<abs>/Plugins/AdamskAI/docs/node-catalog.md -registry=plugin -unattended -nullrhi -stdout
UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate -xsd=<abs>/Plugins/AdamskAI/docs/adamskai.xsd -registry=plugin -unattended -nullrhi -stdout
UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate -projectcatalog -unattended -nullrhi -stdout   # the project's full catalogue
UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate -projectxsd -unattended -nullrhi -stdout       # the project's full schema
```

(Use an absolute output path: the commandlet's working directory is the engine's binaries folder.) Both renderings
are parsed from the same schema JSON the catalogue fingerprint hashes, sorted exact-case, LF, one trailing newline;
`-check` compares after CRLF normalization and never writes. A change to any descriptor makes both stale. The XML
Schema (`adamskai.xsd`) is for IDE completion ([setup.md](../setup.md) step 9): it checks elements, attributes,
literals and child counts, not references, slot types or ids, which stay the compiler's.

## Simulation

`adsk_simulate` runs a behavior file headless against a JSON scenario: no world, no PIE, fixed steps, seconds of
work. Core nodes run as shipped; every other leaf (host or project) is a double that ends as the scenario says, and
every other scope only runs its child, so the file compiles exactly as shipped against the full catalogue. Use it
for what the state machine decides over time: loops, holds, which states a run of facts reaches. How a move or an
EQS query plays out in the world is the [world tests](#world-tests-adamskaiworld)' job and a person's in PIE. Sensors do
not run either: a scenario sets the Inputs they write in `inputs` and `timeline`.

```json
{
  "behavior": "../../Content/AI/my_enemy.xml",
  "seed": 7, "step": 0.0333, "duration": 20,
  "inputs": { "Target": 1, "TargetDistance": 800 },
  "leaves": {
    "flanking_find": { "result": "Failed", "after": 0 },
    "RunEQS": { "after": 0.1, "out": { "point": "0,300,0" } },
    "*": { "after": 0.5 }
  },
  "timeline": [
    { "at": 6, "inputs": { "TargetDistance": 90 } },
    { "at": 9, "event": "Danger" }
  ],
  "expect": { "reach": ["strike"], "never": ["dead"], "maxTransitionsPerSecond": 10, "hold": false }
}
```

- `behavior` is relative to the scenario file. Keys are exact; an unknown key, an Input, event, parameter, port or
  state the file lacks, or a literal of the wrong type is a setup error (exit `2`) naming it.
- `seed` (default 1) seeds the random nodes; `step` (default 1/30 s) and `duration` (default 30 s) set the ticks.
- `inputs` are written before the start, `timeline` entries at their time `at` (inputs first, then the event; an
  event with parameters takes `"params": { "who": 7 }`).
  Literals: Bool `true`, Float/Int numbers, Vector `"x,y,z"` or `[x,y,z]`, enum value names, Object a number (an
  opaque id; `0` or `"none"` is none).
- `leaves` says how doubled leaves end, looked up by node id (ignoring case, `Run` prefixes included), then node type,
  then `*`: `result` `Succeeded` (default) or `Failed` after `after` seconds (default 0.5; 0 ends in the tick it
  starts), writing `out` ports on success. A leaf no entry covers succeeds after 0.5 s and is listed in a `note
  simulate-default`.
- Findings, each a row like the validator's: `simulate-livelock` when more than `maxTransitionsPerSecond` (default
  10) transitions happen within one simulated second, with the loop's last states; `simulate-hold` when the brain
  holds (unless `hold` is true); `simulate-expect` for a `reach` state never entered or a `never` state entered;
  `simulate-start` when required inputs are missing; compiler diagnostics as usual.

Output: one line per transition (`time  from -> to  (event)`, the first 200), a summary with the transition count,
the most in one second and the states entered, then the rows. Exit `0` clean, `1` with an error or warning row.
Every record of the run, prefixed with its time, is in `Saved/AdamskAI/simulate/<scenario>.trace`. The plugin's own
scenarios are `Fixtures/scenarios/`; a project keeps its own in `<Project>/Tests/AdamskAI/` (outside Content, so they
are never staged or cooked), for example `Tests/AdamskAI/<file>.json`.

## Automation tests

`AdamskAI.<Area>.*` lives in `Source/AdamskAITests/Specs/<Area>/`: the segment after `AdamskAI.` is always the
folder, and the only other prefix is `AdamskAIWorld.*`, whose specs live in `Specs/World/` (next section). Areas are of
three kinds, and a new spec goes in the first that fits:

- **Layers**, one per part of the pipeline: Reader, Compiler, Registry, Identity (definition serials and hashes),
  Execution, Trace, Host, Catalogue, Validate and Editor (the last three in editor builds). Inside a layer a spec is
  named after the feature, and a feature's compile-time checks may sit beside its runtime ones
  (`AdamskAI.Execution.Emit.Compiler`).
- **Value types**, each covered end to end from parsing to the host: Enum, Int, Object (a host-side spec of a type
  is in Host, such as `AdamskAI.Host.Enum`).
- **Corpora and harnesses**: Fixtures, Compat, Examples (every file in `Examples/` run through a scenario, host nodes
  replaced by scripted doubles from `Support/ScriptedRun.h`), GuardScenario (one guard behavior driven through its
  routine, search, alert and hold phases), Conformance (every file in `Fixtures/conformance/` run to its `pass`
  state; [semantics.md](../semantics.md#conformance-files)), Samples (the compiled sample controller and node provider the recipes
  point at), Project (every behavior file of the host project started on the project catalogue, driven with random
  inputs, outcomes and events, and checked never to hold; the log names the leaf states reached), Fuzz, Perf and
  Simulate (the simulator's findings and errors, and every scenario in `Fixtures/scenarios/`).

Live-observation coverage is `AdamskAI.Execution.Inspection`,
`AdamskAI.Host.DebugSnapshot` and `AdamskAI.Host.DebugActions`; the controller lifecycle (re-possession,
failed re-initialization, resource locks) is `AdamskAI.Host.BrainRepossess`. Run an area with its prefix as
the filter, for example `adsk_test.sh AdamskAI.Host.`. Host tests drive `FADSKInstance` and the brain with
fixed deltas; none claims movement quality or rendering.

## World tests (`AdamskAIWorld.*`)

A separate prefix, so the `AdamskAI.` run never starts PIE. Each runs a behavior in PIE (`-nullrhi`) on the plugin's
one test map with the seam removed: real navigation, EQS, perception, the file watcher, the managed tick, and
networked PIE (listen server or dedicated server with a client, `AdamskAIWorld.Network.*`), the host sensors
(`AdamskAIWorld.Sensors`) and `Act` and the group nodes (`AdamskAIWorld.Act`: `Act` ends on `Done`, follows a new target,
stops when left, fails when refused; `AdamskAIWorld.GroupNodes`: tokens taken in turns and returned on stop, group
target and leader, `Radio` delay and cut-off). About 50 tests.

- **Everything a test can build is code**, in `Source/AdamskAITests/Specs/World/`: the test pawn and controllers
  (`WorldTestActors.h`), per-test obstacles, an in-memory EQS query (`WorldQueries.h`), and the scenario harness
  (`WorldScenario.h`: opens the map in PIE, waits for the navmesh, spawns an agent on a file, fixes the time step,
  mounts `Fixtures/world/` as `@AdskWorld/` and a scratch directory as `@AdskWorldTemp/`).
- **Assets only where the asset itself is under test**, in `Content/Tests/` and generated by code:
  `ADSK_GENERATE_WORLD_MAP=1` with the filter `AdamskAIWorld.Content` rewrites `L_AdskWorldTest` (floor, an unreachable
  island, a sight-blocking wall, dynamic navmesh, engine game mode), `EQ_AdskTest_Rings` and the redirector
  `EQ_AdskTest_Old`. Do not edit them in the editor. Generate with the oldest supported engine (5.6): newer engines
  read its packages, older ones cannot read a newer engine's. `AdamskAIWorld.Map` checks what the specs assume of the map.
- The behaviors they run live in `Fixtures/world/` (validated with `adsk_validate -Paths`).
- **The demo scenes** (`AdamskAIWorld.Demo.*`, `Specs/Demo/`) open the demo arena `Content/Demo/Maps/L_AdskDemo` and
  run each scene of [demo.md](../demo.md) headless; `AdamskAIWorld.Demo.Fleet` and `FleetParallel` write their numbers to
  `Saved/AdamskAI/perf/`. `ADSK_GENERATE_DEMO_MAP=1` with the filter `AdamskAIWorld.Demo.Content` regenerates the arena
  from `ADSKDemoLayout`, also with 5.6.
- CQTest runs a test's body after `BEFORE_EACH`'s latent commands, so the worlds already exist when the body runs:
  settings read at world creation (`PrewarmFiles`, `bPrewarmOnClients`) go in `BEFORE_EACH`.

`Tools/adsk_cook_check_win64.ps1` cooks the host project for Windows and checks that the cook hook added every asset
the behavior files name and left `/AdamskAI/Tests` out (`-SkipCook` rechecks the last cook).

## Benchmarks

`AdamskAI.Perf.*` and `AdamskAIWorld.Perf.*` are benchmarks, not thresholds: they log their numbers and write them
to `Saved/AdamskAI/perf/<test>.json` (core cost per agent tick with and without a recorder, serial and parallel,
instance and recorder bytes; Start and Stop per instance and one reload commit over 200 brains; managed-pass cost
per brain at two fleet sizes; the perception bridge at 0–64 candidates; the brain pass's share of a 200-agent PIE
frame, with every brain ticking each frame and with `TickLOD` and a viewer crossing the fleet). Compare them with `adsk_perf_compare` before and after
a change. What the numbers cover and what they miss: [perf.md](perf.md).

## Writing specs

Helpers in `Specs/Support/TestChecks.h` return an `FADSKCheck`, which says why it failed (the
actual state path, the diagnostics, the trace tail); assert it with `ADSK_ASSERT(...)`, not
`ASSERT_THAT(IsTrue(...))`. Fixtures and helpers never `check`: they fail the running test with
`ADSKTest::Fail` / `Expect`, and a fixture that does not compile yields a stand-in definition
(`ADSKTest::LoadOrStandIn`), so one broken test cannot take down the run. A rooted world-free brain is an
`FADSKTestBrain` (`Support/TestBrain.h`), released even when an assertion returns early.

## What a pass does not prove

Perception bridge tests use fixed `TickBrain` deltas and test hooks that inject stimuli; they prove
deterministic publication and teardown, not in-world sensing quality or pixels.

No visual or navigation-quality behavior, nothing of a packaged build. Reload, watcher and trace export are covered
by `AdamskAI.*` tests; a host project may add PIE tests of its own. How a run *looks* is still a human
observation. What has not been run yet, and what only a person in the editor can check: [roadmap.md](roadmap.md).
