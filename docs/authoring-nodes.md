# Authoring native nodes

A native node is a `FADSKNodeDescriptor` registered by value. Authoring XML never needs this page;
adding a node type does. A new or changed descriptor changes the catalogue and needs an editor
restart ([reload.md](reload.md)).

## Descriptor

- `Name` — exact-case XML element name, unique, not reserved (`Agent`, `Tree`, `State` ... are taken).
- `Description` — one or two sentences for the catalogue: what the node does, when it succeeds and when it fails.
  The catalogue shows its first sentence in the index and all of it under the node. Not part of the fingerprint.
- `Guidance` — optional advice, one point per line: when to pick the node, when another node fits better, and its
  traps. The catalogue lists it under "Use it well"; not part of the fingerprint either.
- `Kind` — `Leaf` (callbacks, no children), `Scope` (callbacks around exactly one child; `MinChildren` and
  `MaxChildren` both 1, `OnStart` required), `Composite` (a control node or decorator that picks its children; see
  below), `Sensor` (`OnSample` only, declared in a file's `Sensors`; see [Sensors](#sensors)), or the intrinsic `Sequence`/`Selector`/`Guard`/`SimpleParallel`/`Emit` shapes the executor steps itself.
- `ConfigStruct` + `Fields` — a `USTRUCT` of immutable configuration; each `FADSKFieldMap` maps an
  XML attribute to a property: `bool`, `float`, `int32`, `FVector`, `FSoftObjectPath` (ObjectPath, preloaded),
  `FSoftClassPath` (ClassPath, preloaded; set `FADSKFieldMap::MetaClass` to the base class it must derive from),
  `FGameplayTag` (Tag; the core reads it by reflection and needs no GameplayTags dependency), a `UENUM` (Choice,
  authored by enumerator name; the generated `_MAX` is not authorable), `FName` (Name) or `FString` (String).
  `TSubclassOf` and object pointers are not value-only: use ClassPath. Enum ports are rejected. The property's
  initial value is the canonical default printed in the catalogue. A field kept only so older files still compile
  sets `FADSKFieldMap::bLegacy`; the catalogue marks it.
- `ParamsProperty` — optional name of a `TArray<FADSKNamedParam>` config property; the node then takes
  `<Param name value type/>` children, filled in document order; each keeps its `Type` (Float, Int, Bool) and the
  matching `Value`, `IntValue` or `bBoolValue`. Anything else is `InvalidParamsProperty`.
- `bTakesArgs` — the node takes `<Param name from/>` children naming slots, read at run time with
  `Context.ReadArgs()` as `TArray<FADSKNamedValue>` (document order, current values), the shape
  `FADSKInstance::EnqueueEvent` and `UADSKGroupSubsystem::Broadcast` take. For nodes that pass values to other agents
  or systems: `Radio` is one, and [recipes/talk-between-agents.md](recipes/talk-between-agents.md) writes another.
  Leaf, Scope and Composite only, and not with `ParamsProperty` (`InvalidParamsProperty`).
- `Ports` — typed slot bindings (`in-<name>` / `out-<name>`); port and field names must not collide.
- `MemoryStruct` — per-activation scratch, reset on every start.
- `StateStruct` — per-instance state of one node that outlives its activations (a cooldown's ready time), reset
  when the instance starts; `Context.GetState<T>()`. Value-only, like memory.
- `bNeverFails` — the node only ever succeeds; the validator's hold check then expects no failure from it.
- `bThreadSafe` — every callback reads and writes only its context and never the host; a definition made only of
  such nodes and core intrinsics may tick on a worker (`FADSKParallelTick`, `UADSKSettings::bParallelManagedTick`).
  All built-ins with callbacks set it; host nodes never do.
- `OnScore` — optional; the node's utility as an option of a `UtilitySelector`, read from its context like a port
  without starting it (`Score` is the built-in one).
- `OnCheck` — optional; whether the node, started now, would get going. A `ReactiveSelector` calls it every tick on
  each earlier option and switches to one that turned true, as it does for a `Guard`'s condition (`Cooldown` is the
  built-in one). The context has config and state, no memory; it must change nothing.
- `OnNextWake` — optional; seconds of tick time until the node next needs its `OnUpdate` (running) or its `OnCheck`
  (as an option), or `ADSK::NeverWake` when only a slot write, an event or a result can change what it does. Brains
  sleep until the nearest such time (`UADSKSettings::bSleepWhenIdle`, on by default) and hand the skipped time to the
  waking tick. A node with `OnUpdate` and no `OnNextWake` keeps its brain ticking every frame while it runs, which is
  always correct; add the hook when the update only counts time (`Wait`, `TimeLimit`, `Cooldown`) or only reacts to
  its slots (`MoveTo`, `Act`). A node that polls the world (a target that may be destroyed, a focus to track) has
  none.
- `SelectorPolicy` — `Selector` kind only: `Ordered` (default), `Reactive` or `Utility`, the `ReactiveSelector` and
  `UtilitySelector` semantics ([xml-reference.md](xml-reference.md)). Both take at most 64 children and their own memory
  struct (`FADSKReactiveSelectorMemory` / `FADSKUtilitySelectorMemory`); `Utility` also needs `FADSKUtilityConfig`.
- `MinChildren`/`MaxChildren`, `ValidateConfig` (reject values the type cannot express, e.g. a
  negative radius; it runs inside the compiler, possibly on a worker thread, so it only reads the config), `ValidateAssets` (once ObjectPath fields resolve: reject what the asset cannot serve, e.g.
  an EQS param the query does not declare; reported by the validator and logged at preload, never fatal), and
  the callbacks `OnStart`, `OnUpdate`, `OnEnd`.

A scope's `OnStart` returns `Running` (or `Succeeded`) to run the child and `Failed` to skip it. Its
`OnUpdate` runs every tick before the child's; `Running` keeps going, any result cancels the child and ends the
scope with that result. `OnEnd` always follows the child's end. A scope cannot be the target of an async result
(only the running leaf is), so keep external work in leaves and use scopes for "while" state: focus, speed, a
tag, a held resource.

A composite (`Kind = Composite`, `MinChildren` at least 1) is a control node or, with one child, a decorator. It
never runs a leaf itself; it decides which child runs next:

- `OnCompositeStart(Context)` returns `FADSKCompositeStep::Start(i)`, `Succeed()` or `Fail()`.
- `OnChildFinished(Context, Child, Status)` gets the index and result of the child that ended and returns the next
  step. `Finish(Status)` passes the child's result on. Starting an index outside `[0, GetChildCount())` fails the
  composite.
- `OnUpdate` (optional) runs every tick while a child runs, before the child, from the tick after the start;
  `Running` keeps going, a result cancels the child and ends the composite with it (that is `TimeLimit`).
- `OnEnd` follows the composite's end, whatever the reason.
- `Context.GetTime()` is the instance's clock: the sum of tick deltas since it started.

A child that ends at once and is started again (a `Repeat` of an instant leaf) runs within the per-tick operation
budget and continues on the next tick, so it never spins. The built-in `RandomSelector`, `Repeat`, `Cooldown`,
`TimeLimit` and `ForceSuccess` in `ADSKBuiltinNodes.cpp` are written this way and are the reference.

`Context.GetRandom()` is the instance's seeded stream; draw from it, never from `FMath::Rand`, so seeded runs
replay. Draw only when the configuration asks for randomness, as `Wait` does, so adding the node does not
shift every later draw of an unrelated file.

Registration rejects UObject references, delegates, containers and nested structs that carry them:
config and memory are **value-only**. State that must outlive an activation (a delegate handle, a
request id) is stored as a plain value in memory; UObject access goes through the callback-scoped weak
host, `Context.GetHost<T>()`, which is valid only during that callback.

## Sensors

A sensor (`Kind = EADSKNodeKind::Sensor`) measures a fact for the brain: a file declares it in its `Sensors` section
([xml-reference.md](xml-reference.md#sensors)) and the brain calls its `OnSample(FADSKNodeContext&)` on the game
thread before every brain tick and once before the start. `OnSample` reads its `In` ports and config and writes its
`Out` ports; the brain then writes each `Out` value to its Input with `SetInput`. Ports bind like a node's, except
that an `Out` port must name an Input. Registration requires `OnSample`, at least one `Out` port, no children and no
`OnStart`, memory or state struct. A sensor never runs inside the instance, so it does not make a definition
non-thread-safe and needs no `bThreadSafe`. It runs every tick of every brain whose file declares it: keep it cheap.
The host's own are in [ADSKHostSensors.cpp](../Source/AdamskAIHost/Private/Nodes/ADSKHostSensors.cpp);
`FADSKDefinition::GetSensors()` lists a definition's.

## Lifecycle contract

`Start` is paired with exactly one `End`. Call `Context.MarkPartiallyEntered()` as soon as `Start`
has acquired something, so an immediate failure ends as `PartialEntry` and `End` releases it.
Cancellation ends deepest-first with `Cancelled`. `Update` receives the supplied simulation delta, never
wall-clock time. Work completing on another stack (a movement callback) must not advance control
flow: post an action result to the instance and let the next brain tick classify it.

A callback may reach its own instance or brain (a pawn dies inside `OnUpdate`, the controller
unpossesses). `SetInput`, `EnqueueEvent` and `PostActionResult` queue as usual. `Stop`, `StopLogic` and
`Cleanup` are deferred: nothing new starts, and the stop happens before the outer `Start`/`Tick`/`Stop`
returns, so every started leaf still gets its `End`. `RestartLogic` runs after that call returns. A
nested `Start` or `Tick` returns `Reentrant`. The brain keeps an instance that is mid-call alive until
the call returns; destroying a bare `FADSKInstance` from its own callback is a fatal error.

## What the host must be

A brain lives on an `AAIController` (the host actor is its pawn) or on any other actor (the host actor is that
actor, `UADSKBrainComponent::GetHostActor`). Core nodes need neither. Host nodes check what they need in `CanStart`
or `OnStart` and fail plainly without it:

| Node | Needs | Why |
|---|---|---|
| `MoveTo`, `MoveToActor` | an `AAIController` | path following and `ReceiveMoveCompleted` live on the controller |
| `LookAt`, `LookAtActor`, `Focus`, `FocusActor` | an `AAIController` | the gameplay focus is the controller's |
| `MoveSpeed` | an `ACharacter` host actor | it changes `UCharacterMovementComponent::MaxWalkSpeed` |
| `RunEQS` | a host actor | the query runs as the host actor |
| `UADSKPerceptionBridgeComponent` | a `UAIPerceptionComponent` on the same actor | it binds to that component |
| `Distance`, `LineOfSight`, `PlayerPawn` sensors | a host actor | they measure from it (`LineOfSight` traces from its eyes, `GetActorEyesViewPoint`) |
| `Grounded` sensor | an `ACharacter` host actor | it reads the character movement; false otherwise |
| `HasGameplayTag` sensor | a target implementing `IGameplayTagAssetInterface` | hierarchical GameplayTag matching; false without that interface, never a component search |
| `Act` | an `IADSKActionPerformer` on the host actor, else on the brain's owner (the controller) | the performer does the action and reports its end |
| `HoldToken` | a brain in a world (no group needed) | the tokens are that world's `UADSKGroupSubsystem`'s, held by the brain |
| `Radio`, `GroupTarget` sensor | a host actor the host put in a group (`UADSKGroupSubsystem::Join`) | they reach the other members only through the group service |

So a brain on a plain actor (a turret, a unit moved by its own mover) runs core nodes, `RunEQS`, sensors and kit
nodes; moving and facing without a controller wait for a locomotion interface (roadmap H8).

`Act`, and project nodes meant for more than one pawn class, reach the pawn through an interface the project
implements, looked up on `GetHostActor()` (`IADSKActionPerformer` for `Act`), and other agents through a host service
(`UADSKGroupSubsystem`); never through a cast to a project class or to `AAIController`. What needs the controller
stays in a host node. A project node may cast to its own pawn class (a strategy unit's `UnitArrived` might), at the price
of working with that class only.

## Networked games

A host node runs on the server (or standalone), never on a client ([networking.md](networking.md)).

- Every effect clients must see goes through replicated state: movement, a replicated property with an `OnRep`, a
  multicast. A local call such as `Montage_Play` on the server shows nothing on clients.
- Completion must not depend on rendering. A node that ends on a montage's end delegate needs the mesh to tick
  montages on a dedicated server: `ACharacter`'s default `AlwaysTickPose` does, a rendered-only
  `VisibilityBasedAnimTickOption` does not, and the node then never ends.
- In XML, wrap a node that waits on an external effect in `TimeLimit`, so a missed callback fails the node instead
  of holding the state.

## Provider

For an external async action, `ADSK::MakeAsyncLeaf` (`Nodes/ADSKAsyncLeaf.h`) writes `OnStart`/`OnEnd`: your start function
gets an `FADSKAsyncTicket` to copy into completion callbacks (`Complete`) and to install a weak-capturing `SetCancel`,
an optional `BeforeEnd` unbinds what it bound, and an optional `CanStart` checks preconditions (a controller, a target)
before anything opens, so a node that cannot start fails plainly instead of ending as a partial entry. By hand the protocol is: validate, create the brain async entry, mark
partial entry, start the external operation, then install a weak-capturing cancel callback. Completion calls `CompleteAsync`; `OnEnd` removes the entry before cancellation and writes its one optional payload. The cancel runs only for an entry that never completed and did not end `Succeeded`: a start that returns `Succeeded` at once is not cancelled, one that returns `Failed` after starting something is. Do not retain `FADSKNodeContext`, activation memory or raw UObjects in callbacks.

```cpp
class FMyNodes final : public IADSKNodeProvider
{
	virtual FString GetProviderName() const override { return TEXT("MyGame"); }
	virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) override;
};
```

Register it with `IModularFeatures::Get().RegisterModularFeature(IADSKNodeProvider::GetFeatureName(), &Provider)`
in `StartupModule`, before anything triggers assembly. Assembly registers core nodes, host nodes, then
providers sorted by exact-case name; a duplicate or empty provider name, a provider-reported error or a
registration error fails the **whole** assembly (no partial catalogue; brains stay stopped and the
commandlet exits `2`). The registry is then frozen and fingerprinted from its schema JSON.

On shutdown, call `FADSKRegistryAssembly::ReleaseAssembled()` before unregistering, while provider code is
still loaded: retained callbacks must not be destroyed after their module unloads. It refuses (returns
`false`) while any world subsystem or definition still holds the registry.

## After adding or changing a node

1. Build, then regenerate the catalogue(s) — [testing.md](dev/testing.md#regenerate-the-catalogue-and-the-schema).
2. Add a valid example or fixture and, for each new rejection, an invalid fixture + `.expect`.
3. Add `AdamskAI.*` automation tests (core/host nodes; a kit's are `AdamskAI.Kit.<kit>.*` and
   `AdamskAIWorld.Kit.<kit>.*`) or project tests (project nodes).
4. Restart the editor; XML reload cannot pick up native changes.
