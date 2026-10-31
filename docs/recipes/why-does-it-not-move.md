# Recipe: why does it not move

An agent stands still, does not react, or loops without doing anything visible. Work down the steps in PIE; each says
what to look at and what it means. The tools are described in [../debugging.md](../debugging.md) and the console
commands in [../reload.md](../reload.md#console-commands). Filter the Output Log to `LogAdamskAIHost`; most causes
below log one line there.

1. **Find the brain.** Run `adsk.Status` in the PIE console (`~`). It prints one `adsk: brain=... pawn=... source=...
   running=... state=... ticks=... hash=... error=... diagnostics=...` line per brain, indented detail lines, then a
   brain count.
   - No line for the agent: the pawn is not possessed by the controller that owns the brain. Check the pawn's
     `AIControllerClass` and `AutoPossessAI` ([new-npc.md](new-npc.md) step 5).
   - The count is 0 and the console says `client world: brains run on the server`: this console is a client's. Brains
     run on the server or standalone; use the Gameplay Debugger, or `ServerExec adsk.Status` and read the server log
     (see "Networked games" in [../debugging.md](../debugging.md#networked-games)).

2. **Read `error=`.** A brain that failed to initialize or start shows why, and so does `GetLastError()`, which a
   controller should log ([new-npc.md](new-npc.md) step 4).
   - `source-read`: the file was not found or not read. The path is relative to the behavior root, ends in lowercase
     `.xml` and has no `..` ([../gotchas.md](../gotchas.md)).
   - `compile`: the file has errors. Check: `adsk_validate_win64.ps1 -Paths Content\AI\<file>.xml` lists them.
   - `required input missing: <names>`: `StartLogic` ran before those Inputs were written. The brain also logs
     `<brain> did not start: required input missing: ...`. Publish them between `InitializeFromSource` and
     `StartLogic`; a new file forgets the previous file's Inputs.
   - A provider error, or `catalogue-setup`: the node catalogue did not assemble, so no brain can start
     ([add-a-project-node.md](add-a-project-node.md) step 4).
   - Empty, with `running=false` and no `source=`: the controller never called `InitializeFromSource`. Its own
     `HasAuthority()` check, a missing pawn or an early return in `OnPossess` is the place to look.

3. **Check that it ticks.** Run `adsk.Status` twice; `ticks=` must rise.
   - It does not: nothing calls `TickBrain`. Exactly one owner ticks a brain: the controller's `Tick` (it needs
     `PrimaryActorTick.bCanEverTick = true`), `bAutoTick`, or `bManagedTick` ([../setup.md](../setup.md#check-the-deployment)).
     With both flags set the brain logs `bManagedTick and bAutoTick are both set; the managed tick wins`.
   - It rises slowly: a managed brain far from every player view ticks at its `TickLOD` interval (Project Settings →
     Plugins → AdamskAI), or at the brain's `ManagedTickInterval`. Elapsed time accumulates, so `Wait` lasts as long;
     reactions come later. A paused game does not tick managed brains.
   - A gameplay task holding the `Logic` resource lock pauses the brain until it releases it.

4. **Look for a hold.** The line under the brain reads `exec=running`, `exec=holding` or `exec=stopped`, followed by
   `node=<active node>` and `ops=<pending async operations>`.
   - `exec=holding`: the state's tree finished and no handler on the state path takes `tree.success` or `tree.failure`.
     The brain logs once per hold: `<brain>: the tree of state '<state>' finished with no handler for it; holding until
     an event moves the brain`. Add the handler. The validator predicts holds as `note hold-risk` lines.
   - `exec=stopped` with `running=false`: see step 2; a stop reason is in the trace (step 8).

5. **Check the facts.** The `input <Name>:<Type>=<value>` lines are what the behavior sees. A value that never changes
   from its default usually means the controller writes another name: outside Shipping each brain logs once
   `<brain>: <file> has no Input named '<Name>'; the write is ignored` (and `has no event named` for events). The
   trace records the refused write as `input-rejected`. Names are exact-case.
   - A transition that should follow a fact: is it a change event (`<Event change="Slot">`)? A change event never
     fires for the value the brain started with, nor for a value that changes and changes back between two ticks
     ([publish-a-fact-or-event.md](publish-a-fact-or-event.md)).
   - An event raised from C++ before `StartLogic` or after a stop is refused with `NotRunning`. To rule out the C++
     side, raise it by hand: `adsk.Event <brain> <Event> [Param=value]...`.

6. **The state is right but the pawn stays.** `node=` names the running node; `ops=MoveTo@<node>(<age>s)` is a move
   in progress.
   - `MoveTo`, `MoveToActor`, `FocusActor` and `LookAtActor` need the brain on an `AAIController`; on any other actor
     they fail at once. `MoveSpeed` needs a `Character`.
   - A goal off the navmesh fails the move, and usually the tree with it. A goal on the navmesh that no path
     reaches does **not** fail: as in the engine, the pawn walks to the closest reachable point and the move succeeds
     there. Check arrival yourself where it matters, for example with `if="TargetDistance" below="100"` on the
     handler after an approach.
     Check: `P` in the viewport shows the navmesh; the Visual Logger draws each goal as a green `<node> goal` circle
     with the acceptance radius.
   - A goal already inside `acceptanceRadius` succeeds at once, so a tree that only moves can loop through its state
     every tick. Put a `Wait` after the move, or a `Selector` falling back to a short `Wait` when the move fails
     (`follow_or_pause` in [follow_target.xml](../../Examples/follow_target.xml)).
   - `MoveTo` reads its goal once unless `repathDistance` is set; `MoveToActor` fails when its target is `none` or
     destroyed.
   - Two moves cannot run at once: in a `SimpleParallel` the second one fails.

7. **The EQS step fails.** `RunEQS` fails when the query returns no item, and the state's `tree.failure` handler runs,
   which looks like an AI decision.
   - Check the query in the Visual Logger: its `LogEQS` entry lists every item and the test that dropped it; a yellow
     `<node> point` marks the result when there is one. The Gameplay Debugger's EQS category shows the same live.
   - Parameter names and types must match the query: `adsk_validate_win64.ps1` warns `missing-asset` when the query
     declares no parameter of that name and type. `<Param>` is `Float` unless `type="Int"` or `type="Bool"`.
   - The query runs as the pawn. A custom EQS context that needs a player or a game class may give nothing in some
     situations (the Third Person template's `EnvQueryContext_Player` and `EnvQueryContext_Danger` had to); the Visual Logger's `LogEQS` entry lists every item and the test that dropped it.

8. **Read the trace.** `adsk.DumpTrace <brain>` writes the brain's recent records as JSONL under
   `Saved/AdamskAI/<world>/` and prints the path. Each record has a `kind` and a `reason`: `state-entered`,
   `transition-taken`, `node-ended` with reason `failed` or `cancelled`, `event-unhandled`, `input-rejected`, `hold`.
   The last few records are also in `adsk.Status` as `record` lines. With `RecorderCapacity=0`, and in Shipping,
   there is no recorder and the command reports `brain-not-initialized`. To replay a run, note the brain's
   `RandomSeed` with the trace ([../debugging.md](../debugging.md#networked-games)).

9. **Watch it over time.** Select the pawn in the Gameplay Debugger (`'`, category `AdamskAI`) for the live state,
   active nodes (main and background), inputs and locals. Record with the Visual Logger (Tools → Debug → Visual
   Logger): the agent's track shows one `from -> to` line per transition and the snapshot rows, plus the move goals,
   EQS points and focus segments ([../debugging.md](../debugging.md#gameplay-debugger-and-visual-logger)).

10. **Networked games.** Only the server (or standalone) runs brains, and AI controllers exist only there. A brain on
    a pawn or another replicated actor starts wherever game code initializes it, so guard that code with
    `HasAuthority()` ([../networking.md](../networking.md)). A pawn that moves on the server but not on a client is a
    replication problem of the pawn, not of the brain; project nodes must send visible effects through replicated
    state ([../authoring-nodes.md](../authoring-nodes.md#networked-games)).

If the behavior file itself is wrong, fix it, save it during PIE and watch the reload; `adsk.Restart <brain>` starts a
brain again on the current file ([../reload.md](../reload.md)).
