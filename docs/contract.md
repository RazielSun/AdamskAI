# Runtime contract

What the core module guarantees and what the host adds: ownership and lifetime, the XML v1 bounds, the order of a
tick, the recorder, the compatibility boundary and the modules. Why it is built this way: [concept.md](concept.md).

## Overview

The core `AdamskAI` module is a world-independent runtime for compiling strict XML v1 into an
immutable AI program and executing it deterministically (text, HSM, procedure and trace semantics).
It has no actor, movement, perception, file or reload code; those live in `AdamskAIHost`, described
in the last section. Unproved boundaries: feel, animation quality, cover/EQS, cooked-build reload
and packaged execution.

## Ownership and lifetime

`FADSKNodeRegistry` owns native descriptors and callbacks. Registration is exact-case; freeze is
permanent. `FADSKDefinition` retains its frozen registry and owns immutable linked tables.
`FADSKInstance` retains one definition and owns all per-agent values, queues, HSM state and node
memory. An optional `FADSKRecorder` is write-only from execution and cannot affect a decision. Give each
instance its own: records carry no instance, so a shared ring mixes agents, and `FADSKParallelTick` ticks
the instances after the first on a shared recorder on the game thread.

Instances and recorders are game-thread-only. Destroy every instance, definition and non-default
registry before unloading code that supplied registry callbacks; retained `TFunction` callbacks
may otherwise point into an unloaded module. Normal packaged runtime modules do not unload.

## XML v1

One `Agent` contains optional `Enums`, `Inputs`, `Locals`, `Sensors` and `Events`, exactly one `StateMachine`, then
zero or more `Tree` elements. Names are case-sensitive. States are hierarchical and leaf states
may name one tree. `On` handles a declared event or `tree.success` / `tree.failure`; missing
`goto` is internal. `Bind` copies a named event parameter to a same-typed Local. A handler may be taken with a
`chance`, a parent state may `select` the child it enters by score or weighted at random
([xml-reference.md](xml-reference.md#state-selection)) and resume the child it left with `history`. When
each thing happens in a tick: [semantics.md](semantics.md).

```xml
<Agent version="1" id="patrol">
  <Inputs>
    <Input name="Goal" type="Vector" required="true"/>
    <Input name="Enabled" type="Bool" default="true"/>
  </Inputs>
  <Events><Event name="Go" priority="10"/></Events>
  <StateMachine id="mode" initial="idle">
    <State id="idle" tree="patrol"><On event="tree.success" goto="idle"/></State>
  </StateMachine>
  <Tree id="patrol">
    <Guard id="enabled" in-condition="Enabled">
      <Sequence id="route">
        <MoveTo id="move" in-goal="Goal" acceptanceRadius="50"/>
        <Wait id="pause" duration="1"/>
      </Sequence>
    </Guard>
  </Tree>
</Agent>
```

`in-<port>` and `out-<port>` bind descriptor ports to slots. Other node attributes must be
explicit descriptor configuration fields, such as `duration` and `acceptanceRadius`; port
bindings are never literals. Inputs are host-written, by the controller or by the file's declared sensors. Tree
node outputs and transition binds may write Locals only. Supported values are `Bool`, `Float`, `Int`, `Vector`, `Object` (an actor the host resolves) and definition-scoped enums (Inputs, Locals, event and tree parameters). `MoveTo` in the example is a
host-registered exact-case descriptor; the core supplies 16 nodes (`Sequence`, `Selector`, `Guard`, `Wait`, `Emit`, `Roll`, `Chance`, `SimpleParallel` and the decorators and selectors listed in [node-catalog.md](node-catalog.md)); `Run` is compile-time syntax.

Parsing uses bundled Expat as a private syntax boundary. DTD, processing instructions, CDATA,
non-whitespace text, unknown syntax and external entities are rejected. Validation then rejects
unknown names, bad arity/types/references and duplicate IDs/transitions. Diagnostics contain
stable code, severity, source, line and authored node ID. Any error yields no definition;
warnings may accompany one.

Bounds: 256 KiB UTF-8 source, 1024 XML elements, tree depth 16, state depth 8 and 64 states.

## Execution contract

Set required inputs before `Start`. Inputs persist across `Stop`; Locals and all execution state
reset at each successful start. Missing required inputs fail atomically without entering states.
Active input writes coalesce by slot and apply on the next valid tick.

Each finite non-negative tick performs, in order:

1. apply inputs in slot order;
2. classify queued action results in enqueue order;
3. dispatch at most 16 ready events by priority, then enqueue sequence;
4. resume or update the active leaf procedure;
5. start at most 64 procedure nodes across all phases.

External and result queues each accept 64 entries and reject the newest overflow. A deferred
procedure start resumes next tick with zero delta. Wait consumes supplied simulation delta only.
Node `Start` is paired with exactly one `End`; immediate failure after
`MarkPartiallyEntered()` ends as `PartialEntry`, while interruption ends deepest-first as
`Cancelled`. Action handles are generation-scoped; duplicate and stale results are classified
during tick, never in the posting call. Within the documented generation range, a brain does not
reuse an activation handle across replacement.

Event handlers are searched active leaf to root. Internal transitions bind only. External
transitions cancel the procedure, exit deepest-first through the transition domain, enter the
target path ancestor-first, follow initial children and start the new leaf procedure. A scoped
tree completion becomes eligible next tick; an obsolete state token makes it stale. Unhandled
completion enters Hold once, and a later external transition escapes it.

`FADSKInstance::GetNextWake` says how long the instance can go untouched: zero while work is queued or a node polls,
the time to the nearest `Wait`, `TimeLimit` or `Cooldown` otherwise, `ADSK::NeverWake` in a hold or while only
action results can move it. Ticking only when due, with the skipped time in one delta, ends where ticking every frame
ends; the host's idle sleep is built on it ([setup.md](setup.md)).

## Recorder, compatibility and rollback

Recorder storage is bounded. Overflow is represented by one oldest `Gap` whose `LostCount`
equals all records it replaces; newest records remain. Recorder absence, capacity and overflow
never enter runtime branches.

Tests live in this plugin: the `AdamskAITests` `DeveloperTool` module
(`Source/AdamskAITests/Specs/<Area>/`, shared non-registering helpers in `Specs/Support`) registers
`AdamskAI.<Area>.*`. Run them with `Tools/adsk_test.sh` (Windows: `adsk_test_win64.ps1`), which hands
over to the host project's own test runner when it has one (`--standalone` forces the plugin's runner).
Game test modules need not link this plugin.

The core module's public headers expose Core/CoreUObject types only. Its dependencies are public `Core`
and `CoreUObject`, private `Expat` and `Json`; it has no game, Engine, AIModule, StateTree or editor
dependency. XML v1 and the public enums are the compatibility boundary. The plugin owns no assets or
saved data, so removing it means disabling it, deleting the game module's `AdamskAI`/`AdamskAIHost`
dependencies and the code that hosts brains; the behavior files under `Content/AI/` become inert text.

## Host, debug, editor module, examples and documentation

The core module above stays Engine-free, and no module depends on a plugin outside the engine.
`AdamskAIHost` (Runtime) adds the Unreal host: provider-assembled registry, world subsystem, brain
component and the host nodes (`MoveTo`, `LookAt`, `RunEQS`, the scopes `Focus` and `MoveSpeed`, `Act` through
`IADSKActionPerformer`, and `HoldToken` and `Radio` on the group service), the host sensors (`Distance`,
`LineOfSight`, `Grounded`, `PlayerPawn`, `GroupTarget`) and the group service. `AdamskAIDebug` (Runtime, denied Shipping/Test) adds the
Gameplay Debugger category and world labels; see [live debugging](debugging.md). `AdamskAIEditor` (Editor)
adds the headless `AdamskAIValidate` commandlet, the fixture runner and the behavior-root watcher. Agent-facing guidance lives in [AGENTS.md](../AGENTS.md)
and the other pages here: [concept](concept.md), [setup](setup.md), [XML reference](xml-reference.md), the generated
[node catalogue](node-catalog.md), [XML style](xml-style.md), [authoring nodes](authoring-nodes.md),
[reload](reload.md), [testing](dev/testing.md), [Linux](linux.md), [Windows](windows.md), [gotchas](gotchas.md), [glossary](glossary.md), [references](references.md), [roadmap](dev/roadmap.md)
and [limitations](limitations.md).

[Examples/](../Examples/README.md) are valid files; [Fixtures/](../Fixtures/) holds one exact invalid case per
diagnostic code and limit. `adsk_check`, `adsk_validate` and `adsk_test` check all of it without a renderer
or client; `adsk_strict_includes`, `adsk_host_check` (Linux) and `adsk_buildplugin_win64` (Windows) prove
portability. Every script is in `Tools/`, as `.sh` for Linux and `_win64.ps1` for Windows ([testing](dev/testing.md)).
