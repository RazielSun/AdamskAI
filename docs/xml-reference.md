# XML v1 reference

The compiler is strict: anything not listed here is a diagnostic. Names are case-sensitive. Examples:
[../Examples/guard_patrol.xml](../Examples/guard_patrol.xml) and
[../Examples/chase_and_wait.xml](../Examples/chase_and_wait.xml); scopes, randomness, comparisons and `Param` in
[../Fixtures/valid/scopes_random_compare.xml](../Fixtures/valid/scopes_random_compare.xml). Every diagnostic code has an exact
invalid case in [../Fixtures/invalid/](../Fixtures/invalid/). Comments and blank lines are accepted
and dropped; the expected layout is [xml-style.md](xml-style.md). The same rules as an XML Schema for IDE completion:
[adamskai.xsd](adamskai.xsd), generated (a project with its own nodes has `Content/<BehaviorRoot>/adamskai.xsd`).

## Document

One `Agent` (`version="1"` exactly, `id` required) containing, in order: optional `Enums`, `Inputs`,
`Locals`, `Sensors` and `Events`, exactly one `StateMachine`, then zero or more `Tree`. Each section appears at most
once.

| Element | Attributes | Children |
|---|---|---|
| `Enum` (inside `Enums`) | `name` | `Value name` (1–32) |
| `Input` | `name`, `type`, `required` (`true`/`false`) or `default` — never both | — |
| `Local` | `name`, `type` | — |
| sensor (inside `Sensors`) | `id`, its ports and fields, like a node (see "Sensors") | — |
| `Event` | `name`, `priority` (int32, default 0; higher first), optional `change` (an Input or Local) | `Param name type` (none with `change`) |
| `StateMachine` | `id`, `initial` (a direct child state), optional `select` | `State` |
| `State` | `id`; a leaf may have `tree`; a state with child states has `initial` (a direct child) and no `tree`, and optional `select` and `history`; optional `tags` (see "State tags"); under a `select` parent, optional `score`, `weight`, `bias` (see "State selection") | `State`, `On` |
| `On` | `event` (declared event, `tree.success` or `tree.failure`), `goto` (omit for an internal transition), optional `if`/`unless` (+ one of `is`, `below`, `above`), optional `chance` | `Bind param to`, `When`, `Any` (see "Several conditions") |
| `Tree` | `id` | `Param name type` (its parameters; see `Run`), then exactly one root node |

`type` is `Bool`, `Float`, `Int` (`-?[0-9]+`, within `int32`), `Vector` (`"x,y,z"`) or `Object`; an **Input** `type`
may also name an enum declared in `Enums`, and so may a Local, an event parameter and a tree parameter; descriptor
ports are never enums (`type-mismatch`). A `Bind` copies a named event parameter into a Local of the same type
(the same enum, for an enum).
`tree.success`/`tree.failure` are reserved event names.

### Objects

An `Object` slot holds an opaque id that the host maps to an object (`FADSKObjectHandles` in `AdamskAIHost`): the
same object always has the same id, so `is` tests identity, and the id of a destroyed object stays in the slot but
resolves to nothing. Its only literal is `none`, which is also its zero value. An Object is an Input, a Local, an
event parameter or a port; it is never a configuration field. Conditions and `Guard` compare it with `is` only:
`is="none"` or `is="<Object slot>"` (`unless="Target" is="none"` means "has a target").

### Change events

`<Event name="TargetChanged" priority="30" change="Target"/>` is raised by the core whenever the slot's value
differs from the value it last raised for, so a file reacts to facts without the host turning each fact edge into
an event: `<On event="TargetChanged" goto="engage" unless="Target" is="none"/>`.

- The check runs once per tick, after the tick's Input writes and action results, before events dispatch. A slot
  written during dispatch or by a node (a `Bind`, an `out-` port) raises on the next tick; a value that changes and
  changes back between two checks raises nothing.
- The values the instance starts with are the baseline: a start never raises one, and neither does a restart.
- It is an ordinary queued event: its `priority` orders it among host events, its handlers take conditions, and the
  slot is read when it dispatches. A full queue refuses it, and it is raised again on the next tick.
- It takes no parameters (`bad-arity`), `change` must name a declared slot (`unresolved-reference`), and neither
  the host nor `Emit` may raise it (`Reserved`, `type-mismatch`), just like `tree.success`.

### Sensors

A sensor measures a fact for the host, so the controller does not compute and publish it:

```xml
<Sensors>
  <LineOfSight id="sight" in-target="Target" out-value="HasLineOfSight" cone="85" traces="5"/>
  <Distance id="range" in-target="Target" out-value="TargetDistance"/>
</Sensors>
```

- Each child is a catalogue node of kind `sensor`, written like a node: an `id` (unique among the file's node ids,
  ignoring case), `in-<port>` bound to an Input or Local, `out-<port>` bound to an **Input** of the port's type, and
  its configuration fields.
- The brain samples every sensor right after its `OnPreTick` bridges, before each tick, and once before
  `StartLogic` starts the instance, so a required Input a sensor writes has its first reading. An `in-` port reads
  the latest value the host wrote, that tick's included; an `out-` value goes through `SetInput` like any host write,
  so it is traced and a `change` event on it works.
- A sensor never runs inside the instance: no memory, no result, and it does not keep a file from ticking on a
  worker. `adsk_simulate` runs no sensors; a scenario sets their Inputs ([testing.md](dev/testing.md#simulation)).
- Diagnostics: a sensor in a tree, and a node or `Run` in `Sensors`, are `unknown-node`; an `out-` port on a Local
  is `type-mismatch` (a sensor writes an Input, not a Local); two sensors writing one Input are `duplicate-id`.

The host's sensors are `Distance` (cm to the actor in `in-target`; the largest Float with none), `LineOfSight`
(Bool; `cone`, `traces`), `Grounded` (Bool), `PlayerPawn` (Object; `player`, `range`) and `GroupTarget` (Object, the
target the host actor's group last reported; `memory`). Their fields and ports are in
[node-catalog.md](node-catalog.md). A project adds its own in a node provider
([authoring-nodes.md](authoring-nodes.md#sensors)).

### Enums

Up to 16 enums of up to 32 values; names are a letter followed by letters, digits or `-`, exact-case. An
enum is definition-scoped. An enum Input with `default` resolves that value name; with neither `required` nor
`default` it starts at the first value, as an enum Local always does. A host raises an event with an enum
parameter by passing `FADSKDefinition::MakeEnumValue(Enum, Value)`; another enum, or a value out of range, is
`TypeMismatch`. The host writes one by **value name** with
`SetEnumInput(Input, ValueName)` (`UADSKBrainComponent` forwards it): an unknown Input is `UnknownName`, a
non-enum Input or unknown value is `TypeMismatch`. Raw enum values are not portable between definitions.
The brain's input cache keeps the enum type name and value name, so a reload that reorders the values
republishes the same symbolic value; a reload that drops the Input, changes its type or removes the cached value
is rejected (`input binding changed: <name>`) and leaves the running definition untouched.

### Conditional handlers

`ObjectPath` configuration fields use Unreal object-path syntax (for example `/Game/AI/EQS/MyQuery.MyQuery`). They are preloaded by the world subsystem before publication; a missing asset is a validator warning, but any diagnostic still makes validation exit `1`.

`<On event="IntentChanged" goto="engage" if="Intent" is="Engage"/>`. `if="Slot"` / `unless="Slot"` take a Bool
slot, or compare the slot with at most one of:

| Attribute | Slot type | Operand | Passes when |
|---|---|---|---|
| `is` | Int, enum or Object | an Int literal, an enum value name, `none`, or the name of an Object slot | equal |
| `below` | Int or Float | a literal of the slot's type, or the name of a slot of the same type | slot `<` operand |
| `above` | Int or Float | as `below` | slot `>` operand |

`unless` negates the result (`unless="Dist" below="3"` means `Dist >= 3`). A slot operand is read at the same
moment as the condition, so a threshold can itself be an Input: `if="Distance" below="Reach"`. `if` with
`unless`, or two of `is`/`below`/`above`, is `bad-arity`; a comparison without `if`/`unless` is `missing-required`;
a wrong slot type or an operand that is neither a literal nor a same-typed slot is `type-mismatch`. The condition
reads slots when events dispatch, **before** any `Bind` of that handler. Within a state, handlers for an event are
tried in document order and the first passing one wins; if none passes the search continues in the parent state.
Two handlers with the same event, condition, operator and operand are duplicates (`duplicate-transition`); a
handler after an unconditional one for the same event can never run and is the warning `unreachable-transition`.

### Several conditions

A handler or a `Guard` that tests more than one fact lists the others as children:

```xml
<On event="SeeEnemy" goto="attack" unless="Target" is="none">
  <When if="HasLineOfSight"/>
  <When if="TargetDistance" below="1500"/>
  <Any>
    <When if="Ammo" above="0"/>
    <When if="HasSidearm"/>
  </Any>
</On>
```

- `<When>` takes the attributes of a one-condition handler: `if` or `unless`, with at most one of `is`, `below`,
  `above`, and the same types, operands and diagnostics. A `When` with neither `if` nor `unless` is
  `missing-required`.
- `<Any>` holds when at least one of its `When` children holds. It takes no attributes and only `When` children,
  at least two (`bad-arity`); an `Any` inside an `Any` is `unknown-element`.
- The condition is all of them together: the attributes (when present), every `When` and every `Any`. A handler may
  have only children (`<On event="Go" goto="x"><When .../><When .../></On>`). They are read at the same moment as
  the attributes, before any `Bind`; `Bind`, `When` and `Any` may come in any order.
- Two handlers are duplicates only when their attributes and their `When`/`Any` children are the same, in the same
  order. A handler with any condition, children included, is not unconditional.
- On a `Guard` the `When`/`Any` children come before its child node (`bad-arity` after it) and must hold besides its
  `in-condition`; `invert` applies to the whole. On any other node they are `unknown-element`. `When` and `Any` are
  reserved: no node may be registered under those names.
- Trace: a taken handler with only children records its first `When`'s slot in `transition-taken`; a `Guard` with
  children records its outcome as a Bool in `guard-value`, not its port's slot.

### Chance

`<On event="Danger" goto="evading" chance="0.25"/>` is taken with that probability once its condition passes;
when the draw loses, the next handler is tried, in this state and then its ancestors, as if the condition had
failed. `chance` is a number above 0 and at most 1 (`out-of-range`), or the name of a Float slot (an Input or a
Local), read each time the handler is tried and clamped to [0, 1]; a name that is no slot is
`unresolved-reference`, a slot that is not Float `type-mismatch` ("chance needs a number or a Float slot"). The draw
comes from the instance's random stream ([Randomness](#randomness)) and only for a handler whose condition passed and
whose chance is below 1, so a handler that is never tried draws nothing. A handler with a `chance` below 1, or with
one from a slot, hides nothing after it: the next one for the same event is reachable, and two with the same
condition are not duplicates while the first has a chance. For the hold check a `chance` handler counts as
conditional: a `tree.failure` handled only by chances can hold.

```xml
<On event="Danger" goto="evading" chance="0.25"/>         <!-- one in four -->
<On event="Danger" goto="dodge" if="Near" chance="0.5"/>   <!-- otherwise, when near, one in two -->
<On event="Danger" goto="evading" chance="EvadeChance"/>  <!-- as likely as the slot says now -->
```

### State tags

`<State id="engage" initial="attacking" tags="AI.State.Combat, AI.Mood.Angry">` gives a state gameplay tags for the
host: they hold while the state or any descendant is active, and the brain publishes their union
(`UADSKBrainComponent::GetActiveTags`, `OnActiveTagsChanged`) so a game can replicate what its AI is doing
([networking.md](networking.md#showing-ai-state-on-clients-state-tags)). The value is a comma-separated list, spaces
around commas allowed; each tag is dotted segments of letters, digits and `_`, a segment not starting with a digit
(otherwise `out-of-range`); a tag twice on one state is `duplicate-id`. The `StateMachine` element takes none. Tags
must be registered gameplay tags: the validator warns (`missing-asset`) and a brain refuses the file
(`unknown-tag`) otherwise. Tags never affect execution.

### State selection

A state with child states (or the `StateMachine`) may choose the child it enters instead of always entering
`initial`. This is how the state machine chooses a mode by utility or at random:

```xml
<State id="engage" initial="attacking" select="best">
  <State id="attacking" initial="approach" score="TargetDistance" weight="-0.004" bias="3">...</State>
  <State id="flanking" tree="flanking" score="TargetDistance" weight="0.004" bias="-0.6">...</State>
  <State id="evading" tree="evading">...</State>
</State>
```

- Each child with `score`, `weight` or `bias` scores `weight * score + bias` when the parent is entered: `score` is
  a Float or Int slot (an Input, a Local), read as 1 when left out, so `weight="3"` alone is a constant weight;
  `weight` defaults to 1, `bias` to 0, as on the `Score` node. A score that is not above 0 does not count. A child
  with none of the three is never selected; transitions and `initial` still enter it (`evading` above).
- `select="best"` enters the highest-scoring child, ties in document order; `select="weighted"` draws one in
  proportion to the scores from the instance's random stream. When no child scores above 0, `initial` is entered,
  so `initial` stays required.
- The choice is made each time the parent is entered: at the start, by a transition to it, and by a transition to
  it from one of its own descendants (`<On event="tree.success" goto="engage"/>` inside `attacking`), which leaves
  the child and selects again without leaving `engage`. A handler on `engage` itself that goes to `engage` leaves
  and re-enters it (an external transition), and selects too. A transition straight to a child enters that child.
- The slots are read as they are at that moment, after the `Bind`s of the transition that enters.
- Diagnostics: `select` other than `best`/`weighted` is `unknown-enum-value`, on a leaf `unknown-attribute`; `score`,
  `weight` or `bias` under a parent without `select` is `unknown-attribute`; an unknown `score` slot
  `unresolved-reference`, one not Float or Int `type-mismatch`, a `weight` or `bias` not a number `type-mismatch`; a
  `select` state with no scored child is `missing-required`.
- The trace has a `state-selected` record before the child's `state-entered`, with the winner's score as its value,
  or reason `no-score` when it fell back to `initial`.

A selection does not undo principle 3 of [concept.md](concept.md): a failure edge that goes back to the parent on
unchanged facts selects the same child again. Send a failure to a specific state instead (a `flanking` child whose
`tree.failure` goes to `attacking`, not back to the parent).

### History

A state with child states may remember where it was when it was left, and resume there when it is entered again:

```xml
<State id="fight" initial="phase-one" history="shallow">
  <State id="phase-one" tree="phase-one">...</State>
  <State id="phase-two" tree="phase-two"><On event="Staggered" goto="stagger"/></State>
</State>
<State id="stagger" tree="stagger"><On event="tree.success" goto="fight"/></State>  <!-- back to phase-two -->
```

- `history="shallow"` resumes the child that was active when the state was left; that child then enters its own
  entry child as usual (its `initial`, `select` or own `history`). `history="deep"` resumes the whole path below the
  state, down to the leaf it left.
- It applies only when the state was **left and is entered again**: by a transition from outside it, and by a
  handler on the state itself that goes to it (which leaves and re-enters). A transition to it from one of its own
  descendants keeps it active, so its entry child is chosen as usual, which is what `select` re-selection relies on.
  Before the first time it is left there is nothing to resume: `initial` or `select` decides.
- A transition straight to a child or deeper state enters that state; to enter a history state fresh, go to the
  child you want.
- With `select` too: `select` picks on the first entry, history on the later ones.
- A start forgets every history; a snapshot keeps them ([reload.md](reload.md)), dropping one whose child the edited
  file no longer has under that state.
- `history` other than `shallow`/`deep` is `unknown-enum-value`; on a leaf or on the `StateMachine` (never left
  while it runs) it is `unknown-attribute`.
- The trace has a `state-selected` record with reason `history` for each resumed child.

## Tree nodes

Every node has an `id`, unique ignoring case. Which elements exist, their configuration attributes, ports and child
counts are exactly the rows of the catalogue ([node-catalog.md](node-catalog.md)).

| Node | Kind | What it does |
|---|---|---|
| `Sequence`, `Selector` | core composite | 1+ children in order; first failure / first success ends it |
| `Guard` | core, one child | runs the child while `in-condition` passes; `invert`, `is`/`below`/`above` as in conditions, and `When`/`Any` children before the child ("Several conditions") |
| `SimpleParallel` | core, two children | runs the first (main) child and, beside it, loops the second (background); ends with the main result |
| `Parallel` | core, 2 to 8 children | runs every child at once, each once; succeeds when `success` children succeeded (`all`, the default, `any` or a count), fails when `failure` failed (`any`, the default, `all` or a count) or success is out of reach; then cancels the rest. A count above the child count means all |
| `RandomSelector` | core composite | 1+ children in an order drawn once per start from the instance's stream; first success ends it |
| `Repeat` | core decorator | runs its child again after each success; fails with it; succeeds after `count` successes (0, the default: never) |
| `Retry` | core decorator | runs its child again after each failure; succeeds with it; fails after `count` failed attempts (0, the default: never) |
| `Cooldown` | core decorator | fails at once until `duration` seconds of instance time have passed since its child last ran to an end, cancelled included; a restart forgets it |
| `TimeLimit` | core decorator | fails, cancelling its child, once the child has run `duration` seconds (`deviation` draws it from `duration ± deviation`, floored at 0, once per start); otherwise ends with the child |
| `ForceSuccess` | core decorator | succeeds whatever its child does |
| `Emit` | core leaf | queues `event` for the next tick |
| `Set` | core leaf | writes `value` or the slot named by `from` to the Local in `to` and succeeds ("Set") |
| `Wait` | core leaf | `duration` seconds; `deviation` draws it from `duration ± deviation` (floored at 0) once per start |
| `Roll` | core leaf | writes a uniform Int in `[0, count)` to `out-value` and succeeds |
| `Chance` | core leaf | succeeds with `probability`, fails otherwise |
| `MoveTo` | host leaf | path-follows to `in-goal` within `acceptanceRadius`; re-paths when the slot moves farther than `repathDistance` (0, the default: read once). With `allowPartialPath` (default `true`, as in the engine) a goal no path reaches is approached as far as the navmesh allows and the move **succeeds** there; `allowPartialPath="false"` fails it instead. A goal off the navmesh fails. `strafe` (default `true`, the engine's default) lets a pawn with a focus keep facing it while it walks; `false` turns it to its path |
| `LookAt` | host leaf | faces `in-point` for `duration`, re-reading the slot every tick |
| `RunEQS` | host leaf | runs `query`, writes the item to `out-point`; `pick` = `Best`, `RandomBest5` or `RandomBest25`; `Param` children. The older `randomBest25="true"` still compiles (legacy; use `pick`) |
| `Focus` | host scope | faces `in-point`, re-read every tick, while its one child runs |
| `MoveSpeed` | host scope | a character walks at `speed` while its one child runs; the previous speed is restored |
| `MoveToActor` | host leaf | path-follows the actor in `in-target` (an Object) within `acceptanceRadius`; fails when it is `none` or destroyed; `allowPartialPath` and `strafe` as in `MoveTo` |
| `LookAtActor` | host leaf | faces the actor in `in-target` for `duration`; fails when it is gone |
| `FocusActor` | host scope | keeps the controller's focus on the actor in `in-target` while its one child runs; fails when it is gone |
| `Act` | host leaf | starts the named `action` at `in-target` through the pawn's (else the controller's) `IADSKActionPerformer`; ends when the performer says so, stops it when left |
| `HoldToken` | host scope | runs its one child while holding one of `pool`'s tokens on `in-target`; fails at once, child not started, when others hold them all or the target is `none` |
| `Radio` | host leaf | after `delay` seconds queues `event` in every other member of the host actor's group, with its `<Param name from/>` children as the event's parameters, read when it runs; fails outside a group |

`Act`, `HoldToken` and `Radio` reach the pawn and the group service through interfaces and host services; what they need from the host is in [authoring-nodes.md](authoring-nodes.md#what-the-host-must-be). Sensors are
not tree nodes ("Sensors" above).

- `in-<port>` / `out-<port>` bind a descriptor port to the name of an Input or Local of the same type.
  Port bindings are never literals.
- Any other attribute must be a declared configuration field; its default is the catalogue default. A
  `Choice` field takes one of the names the catalogue lists, exact case (`pick="RandomBest5"`); anything else is
  `unknown-enum-value`. A `Name` field takes any text shorter than 1024 characters (case is not significant), a
  `String` field any text verbatim.
- A node whose catalogue entry says **Params** takes `<Param name="..." value="..." type="..."/>` children:
  `type` is `Float` (the default), `Int` or `Bool` and the value a literal of it; names distinct ignoring case
  (`duplicate-id`), not tree children (they never count against the child range). `RunEQS` passes them to the
  query as named parameters of that type; the validator warns (`missing-asset`) when the query declares no
  parameter of that name and type.
- A node whose catalogue entry says **Params** with `from` takes `<Param name="..." from="Slot"/>` children
  (`Radio`, and project nodes that pass values on): each names an Input, Local or tree parameter of any type but an
  enum, whose value the node reads each time it runs. No `value`; names distinct, exact case (`duplicate-id`); an
  enum slot is `type-mismatch`, since its value means nothing outside this file. Receivers of what such a node sends
  declare the event with the same parameter names and types. `Param` under any other node but `Emit` and `Run` is
  `unknown-element`.
- A `ClassPath` field takes a class path (`/Script/Engine.Character`, `/Game/AI/BP_Grunt.BP_Grunt_C`); the
  validator warns when the class is missing or does not derive from the field's base class. A `Tag` field takes
  a gameplay tag name (`Enemy.Alert`); the validator warns when the tag is not registered. Both are
  `type-mismatch` when malformed.
- Inputs are host-written (a sensor writes for the host). Tree node outputs and `Bind` may write Locals only.
- A **scope** (`Focus`, `MoveSpeed`, or a game's own) wraps exactly one child: it starts before the child, is
  updated every tick before it (outermost first, not in the tick it started), ends after it, and ends with the
  child's result. A scope that fails to start skips its child; one whose update returns a result cancels the
  child and finishes with that result. Use it for anything a StateTree does "alongside" a task: face the target
  while moving, run while fleeing.

### Randomness

`Roll`, `Chance`, `Wait` and `TimeLimit` `deviation`, an `On` with `chance` and a `select="weighted"` state draw
from one stream per instance, reseeded from the instance's seed on every start (a start from a snapshot continues
the snapshot's stream instead), so the same seed, inputs, events and deltas replay the same choices. The seed is
`UADSKBrainComponent::RandomSeed` (0, the default, draws a fresh seed on every `StartLogic`) or
`FADSKInstance::SetRandomSeed`. A `Wait` or `TimeLimit` without `deviation` draws nothing.
- `Selector` is **non-reactive fallback**: it starts child 0, advances on failure and succeeds on the first
  success; earlier options are never re-checked. `ReactiveSelector` and `UtilitySelector` (below) are the
  re-checking ones. A `Guard` that is itself an option and turns false fails that
  option and the next one starts in the same tick; an outer false `Guard` cancels the whole selector.
- `Guard` without a comparison needs a Bool slot; `invert="true"` negates the result. With `is`, `below` or `above`
  it compares its slot exactly as a conditional handler does (same types, operands and diagnostics); `invert`
  applies after the comparison and its `When`/`Any` children.
- `SimpleParallel` (as in Behavior Trees): the main child runs on the tree's path; the background child runs beside
  it in its own lane, with its own guards, scopes and leaves, and is updated after the main path every tick. When
  the background finishes, with either result, it starts again on the next tick; it never ends the parallel. When
  the main child finishes, the background is cancelled and the parallel ends with the main result. A main child
  that finishes at once never starts the background. A guard or scope above the parallel, a state exit or a stop
  cancels both. Both lanes share the slots and the per-tick operation budget; `GetActiveNodeId` and the leaf handle
  stay the main path's, `FADSKInstance::GetActiveNodes` lists every lane. Two moves at once cannot both run: the
  brain binds one move request, so the second fails.
- `Emit event="Name"` queues a declared event that is neither reserved nor a change event on the **shared** event
  queue and succeeds. It becomes eligible on the **next tick** at the earliest, ordered with host events by
  priority then enqueue order. A full queue fails the node (`InputRejected/QueueFull` in the trace). An event
  with parameters needs each one exactly once as a child `<Param name="..." value="..."/>` (a literal of the
  parameter's type; an enum value name; `none` for an Object) or `<Param name="..." from="Slot"/>` (a slot of
  the same type, read when the `Emit` runs). A missing parameter is `missing-required`, an unknown one
  `unresolved-reference`, both or neither of `value`/`from` `bad-arity`/`missing-required`, a wrong literal or
  slot `type-mismatch`.
- `Set` writes one Local: `<Set id="mark" to="Alerted" value="true"/>` or `<Set id="keep" to="LastSeen" from="TargetLocation"/>`.
  `to` names a Local or a tree parameter bound to one (an Input is `type-mismatch`: the host writes Inputs); `value`
  is a literal of that slot's type (an enum value name, `none` for an Object), `from` a slot of the same type.
  Exactly one of `value` and `from` (`bad-arity`/`missing-required`). Later nodes read the new value at once; a
  `change` event on the Local is raised on the next tick, so a `Set` can hand a decision to the state machine.
- `ReactiveSelector` (at most 64 options) is a `Selector` that, every tick before the running option updates,
  checks again each earlier option that can be checked: a `Guard` (its condition), a `Cooldown` (ready again) or a
  project node with an `OnCheck`. One that turned **true** since it was last seen cancels the running option and
  starts in its place (BT observer aborts on lower priority). It reacts to the rise only, so an option that failed
  while it held does not restart every tick; it waits for it to fall and rise again. Other options are never
  re-checked.
- `UtilitySelector` (at most 64 options; `reevaluate` seconds, default 0, and `margin`, default 0) tries its
  options in order of their score, highest first, and skips those scoring 0 or less (none left: it fails). An
  option's score comes from its root's descriptor (`OnScore`); the built-in `Score` scope gives
  `weight * in-value + bias` and runs its one child unchanged: `<Score id="s" in-value="Hunger" weight="2">`.
  Other options score 0. With `reevaluate` it scores again that often (instance time) and switches, cancelling
  the running option, when another option beats it by more than `margin`.

### `Run` (compile-time subtree reuse)

`<Run id="reload1" tree="reload"/>` takes `id` and `tree`, no children, and is replaced at load time by a copy of
that tree's body; the runtime, recorder and trace never see a `Run`. It has no catalogue entry and no descriptor.
Expansion starts from the authored trees, so one call site never changes another's copy.

- Every copied node id is prefixed with the call-site id and `/` (`reload1/aim`); nested `Run`s accumulate
  (`outer/inner/aim`), and trace and debug output show the prefixed ids.
- Copies keep their slot bindings and config by value: **every call site reads and writes the same slots**,
  except a tree's parameters, below.
- **Parameters.** A tree declares `<Param name="Goal" type="Vector"/>` children before its root (any value type
  or enum); its nodes use `Goal` like a slot (ports, `Guard` conditions, operands and `When` children, `Emit` `from`). Every `Run`
  of it binds each parameter to a slot visible where it stands: `<Run id="home" tree="go"><Param name="Goal"
  from="Home"/></Run>`. The copy reads and writes that slot; nothing exists at runtime but the substitution. A
  parameter is visible only inside its tree (`unresolved-reference` elsewhere), may not repeat a slot name
  (`duplicate-id`), and an `out-` port on it needs a Local at the call site (`type-mismatch`). A missing binding
  is `missing-required`, an unknown one `unresolved-reference`, a slot of another type `type-mismatch`. A tree
  with parameters is reachable only through `Run` (a `State` naming it is `missing-required`); its authored
  nodes are not in the compiled table (its `RootNode` is -1), only its copies.
- A missing target or a `Run` cycle (reported as `a -> b -> a`) is `unresolved-reference`; a generated id that equals
  an authored or generated id ignoring case is `duplicate-id`.
- Limits, all `limit-exceeded`: tree depth 16 *after* expansion at the call site, 4096 nodes in the final table,
  1 Mi generated id characters in total, and every generated id shorter than `NAME_SIZE` (1024). The failing
  branch stops at its first such diagnostic.
- A tree used only through `Run` is referenced (no `unreferenced-tree`). The reference passes, `Run` included, run
  only when the source walk reported no error.

## Syntax that is rejected

DTD and entities, processing instructions, CDATA, non-whitespace text, unknown elements and
attributes, duplicate ids, duplicate transitions, unresolved references, wrong types and arities.

## Limits

256 KiB UTF-8 source, 1024 XML elements, tree depth 16, state depth 8, 64 states. The state machine
itself counts as one state, so 63 declared states is the most the compiler accepts.

## Diagnostics

`path:line: severity code [node]: message`. Codes (kebab-case, stable): `unknown-node`,
`unknown-element`, `unknown-attribute`, `unknown-port`, `type-mismatch`, `unresolved-reference`,
`duplicate-id`, `duplicate-transition`, `out-of-range`, `limit-exceeded`, `missing-required`,
`bad-arity`, `unknown-enum-value`, `xml-malformed`, `version-unsupported` (errors) and `unreferenced-tree`,
`unreachable-transition`, `missing-asset` (warnings). `missing-asset` comes from the editor validator only: an
ObjectPath not in the Asset Registry, or an asset that cannot serve the node (a `RunEQS` `Param` the query does
not declare); at runtime the world subsystem logs the same once when it preloads the file.
`source-read` and `catalogue-stale` are report codes added by the validator for an unreadable file
(including one whose name does not end in lowercase `.xml`) and an out-of-date catalogue. Any error yields no definition; a warning does not, but the
validator still exits `1` for it.
