# Recipe: publish a new fact or event from C++

The host tells a behavior about the world in two ways. An **Input** is a fact with a current value (a distance, the
target actor, whether the pawn is on the ground); the host writes it and the file reads it in conditions, `Guard`s
and `in-` ports. An **event** is something that happens once (a death, a hit, an order with a location); the host
queues it and the file's `On` handlers react to it, with parameters copied into Locals by `Bind`. Syntax is in
[../xml-reference.md](../xml-reference.md); the host contract is in
[../setup.md](../setup.md#runtime-wiring-host-contract).

## Input or event

| What the host has | Publish it as | In the behavior file |
|---|---|---|
| A value a catalogue sensor measures (a distance, line of sight, on the ground, the player's pawn) | nothing in C++ | `<Input>` and a sensor in `Sensors` writing it ([../xml-reference.md](../xml-reference.md#sensors)) |
| A value that holds for a while | an Input, written before every brain tick | `<Input>`; `if`/`unless` on `On`, `Guard`, `in-` ports |
| A transition that should happen when that value changes | the same Input; nothing else in C++ | `<Event name="XChanged" change="X"/>` and conditional `On` handlers |
| Something that happens once and may repeat with the same values | an event, `EnqueueEvent` | `<Event>` with `<Param>` children, `On` with `Bind` |

Prefer an Input with a change event over raising an edge event from C++. The core raises a change event once per
tick, after that tick's Input writes, whenever the slot differs from the value it last raised for; the controller
keeps no edge flags, and a reload or restart does not invent an edge, because the values an instance starts with are
the baseline ([../xml-reference.md](../xml-reference.md#change-events)). A port of a melee
enemy removed the events `TargetFound`, `TargetLost`, `Launched` and `Landed`, a delegate and two edge flags from its
controller this way: its `PlayerPawn` and `Grounded` sensors write `Target` and `Grounded`, and the file declares
`TargetChanged` and `GroundedChanged` ([hunt_actor.xml](../../Examples/hunt_actor.xml) declares `TargetChanged` the
same way). A sensor's write raises a change event like any host write.

Two consequences to design for. A start raises nothing, so a state entered with the fact already true must also check
it on its own completion (`search` in [hunt_actor.xml](../../Examples/hunt_actor.xml): `<On event="tree.success" goto="chase"/>` after
the `if="Target" is="none"` handler). A value that changes and changes back between two ticks raises nothing, so anything where each
occurrence counts stays an event: a melee enemy's host raises `Danger` once per incoming attack, and each state answers it
with its own chance (`<On event="Danger" goto="evading" chance="0.25"/>`). The dice are the file's: a host never
publishes a roll for the file to compare.

## Add an Input

1. **Declare it** in the file's `Inputs` section with a `type` and either `required="true"` or a `default`. A
   required Input must be written before `StartLogic`, or the brain does not start.
   Check: `adsk_validate_win64.ps1 -Paths Content\AI\<file>.xml` exits `0`.

2. **Use it** in a condition, a `Guard` or an `in-` port. For a transition on change, declare
   `<Event name="..." priority="..." change="<Input>"/>` (priority orders it among queued events, higher first) and
   add `On` handlers for it, usually with `if`/`unless` on the same Input. The host may not raise a change event.
   Check: validate as in step 1.

3. **Publish it** in the controller, before `TickBrain`. A per-tick Input is written by id through an
   `FADSKInputRef` (`Brain/ADSKBrainDriver.h`), which resolves the id again after a reload brings a new definition.
   `PublishFacts` in [SampleController.cpp](../../Source/AdamskAITests/Specs/Samples/SampleController.cpp) does it
   for one Input; without `FADSKInputRef`, call `FindInput` again whenever `GetDefinition()->GetSerial()` changes.
   Writing by name, `SetInput(TEXT("Name"), Value)`, is fine for rare writes.
   Values are `FADSKValue::MakeBool`, `MakeFloat`, `MakeInt` and `MakeVector`; an actor is
   `FADSKObjectHandles::Make(Actor)` (`Objects/ADSKObjectHandles.h`; a null actor is `none`); an enum Input is
   written by value name with `SetEnumInput(Input, ValueName)`.
   Check: `adsk_build_win64.ps1` exits `0`. `SetInput` returns `EADSKInputResult::Accepted`; outside Shipping a name
   the file does not declare logs one `LogAdamskAIHost` warning per brain:
   `<brain>: <file> has no Input named '<Name>'; the write is ignored`.

4. **Test it.**
   Check: `adsk_test_win64.ps1 AdamskAI.Project.` exits `0`. In PIE, `adsk.Status` prints an
   `input <Name>:<Type>=<value>` line per Input; the value must follow the game.

## Add an event

1. **Declare it** in the `Events` section: `<Event name="..." priority="...">` with a `<Param name="..." type="..."/>`
   child per parameter, and handle it with `<On event="..." goto="...">`, a `<Bind param="..." to="<Local>"/>` per
   parameter the behavior needs. Handlers in a state are tried in document order; one that no active state has is
   ignored.
   Check: validate as above.

2. **Raise it** from C++ with `Brain->EnqueueEvent(TEXT("Name"))`, or with parameters as an array of
   `FADSKNamedValue` (`Name`, `Value`). A frequent event can be resolved once with `FindEvent`, like an Input. Events
   are refused (`NotRunning`) before `StartLogic` and after a stop; Inputs are not. A player acting on an NPC raises
   it through a server RPC on the player's own controller or pawn, never on a client
   ([../setup.md](../setup.md#runtime-wiring-host-contract)).
   Check: `EnqueueEvent` returns `EADSKInputResult::Accepted`; log any other result. An undeclared name logs `<brain>: <file> has no event named '<Name>'; the write is
   ignored` once per brain outside Shipping.

3. **Try it without the C++ path.** In PIE, `adsk.Event <brain> <Event> [Param=value]...` queues the same event (a
   Vector is `x,y,z`; [../reload.md](../reload.md#console-commands)).
   Check: `adsk.Status` shows the new state path; `adsk.DumpTrace <brain>` shows `event-enqueued`, then
   `event-consumed` and `transition-taken`, or `event-unhandled` when no active state handles it.
