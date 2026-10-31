# Recipe: port a StateTree or a Behavior Tree

How the pieces of a StateTree or a Behavior Tree map to an AdamskAI behavior file and its controller. The mapping
comes from porting the UE 5.8 Third Person template's two StateTrees: `ST_CombatEnemy` became `combat_enemy.xml`
with a controller that performs its two attacks for `Act`, and `ST_SideScrollingNPC` became `sidescrolling_npc.xml`.
Those files live in the host project that ported them; the examples column names their states. Every element and attribute named below is in
[../xml-reference.md](../xml-reference.md) or [../node-catalog.md](../node-catalog.md).

## Steps

1. **Inventory the asset.** List its states or branches, every task, condition, decorator, service and evaluator,
   every parameter and Blackboard key, and who writes each value. Mark the pieces that are project code (custom
   tasks, conditions, EQS contexts).
   Check: each piece has a row in the tables below or is a project node to write.

2. **Split host and behavior.** Whatever reads the world every tick (find the player, measure a distance, detect a
   hit) moves into the controller and becomes an Input or an event ([publish-a-fact-or-event.md](publish-a-fact-or-event.md)),
   or into a sensor the file declares when the catalogue has one (`PlayerPawn`, `Distance`, `LineOfSight`,
   `Grounded`). Decisions, thresholds, chances and timings move into the file.
   Check: the file's header comment lists what the controller publishes and raises ([../xml-style.md](../xml-style.md)).

3. **Write the file** from the tables, one leaf state per StateTree leaf state or Behavior Tree mode, with a
   `tree.success` and, when the tree can fail, a `tree.failure` handler on each.
   Check: `adsk_validate_win64.ps1 -Paths Content\AI\<file>.xml` exits `0`; read every `note hold-risk` line.

4. **Write the missing project nodes** ([add-a-project-node.md](add-a-project-node.md)) and the controller
   ([new-npc.md](new-npc.md) step 4).
   Check: `adsk_build_win64.ps1`, `adsk_validate_win64.ps1` and `adsk_test_win64.ps1 AdamskAI.Project.` exit `0`.

5. **Compare in PIE.** Swap the pawn's `AIControllerClass` (a copy of the enemy Blueprint with the new controller)
   and play both side by side.
   Check: `adsk.Status` and the Gameplay Debugger show the state path you expect while the original does the same
   thing. Write the checks down as rows (what to do, what to expect, the result) so a later run repeats them.

## StateTree to AdamskAI

| StateTree | AdamskAI | In the port |
|---|---|---|
| StateTree asset run by the AI controller | one behavior file under the behavior root; a `UADSKBrainComponent` on the controller, which calls `TickBrain` after publishing facts | both controllers |
| Root and parent states | `StateMachine` and `State`s with child states; a parent names its `initial` child | `engage`, `attacking` |
| Leaf state with tasks | leaf `State` with `tree="..."`; the tree's root node is the task, or the tasks nested as below | every leaf state |
| Several tasks in one state that run together (face the target while moving, set the speed) | scopes around the main node: `MoveSpeed`, `Focus`, `FocusActor`; `SimpleParallel` for a second task that loops beside the main one; `Parallel` for tasks that each run once, with completion Any (`success="any"`) or All | `fallback`: `MoveSpeed` > `FocusActor` > `MoveTo` |
| A leaf state that only waits for something (no task) | a leaf `State` without `tree`, left by an event handler | `launched` waits for `GroundedChanged` (the template's Wait for Landing task) |
| Final state | a leaf `State` without `tree` or handlers | `dead` |
| Transition on state completed, succeeded, failed | `<On event="tree.success" .../>`, `<On event="tree.failure" .../>`; both when either outcome goes to the same place | `combo`, `fallback` |
| Transition to the same state | `goto` the state's own id: it exits and enters again, restarting its tree | `searching`, `sidescrolling_npc` `active` |
| Transitions on a parent state, which apply to every child | `On` on the parent: a handler search starts at the active leaf and continues outward | `engage`: `Died`, `GroundedChanged`, `TargetChanged` |
| Transition on a gameplay event | a declared `Event`, raised by the host with `EnqueueEvent`; `Param`s and `Bind` for its payload | `Died`, `Danger` |
| Transition on tick with a condition | an Input for the condition's value and an `<Event change="Input">` handled with `if`/`unless`; or a `Guard` in the tree that fails it | `TargetChanged`, `GroundedChanged` |
| Transition priority | event `priority`; within a state, handler document order | `Died` 100 down to `TargetChanged` 30 |
| Transition or enter condition comparing values | `if`/`unless` with `is`, `below` or `above` on the handler that goes there; the operand may be another slot | `if="TargetDistance" below="100"` replaced the controller's `InMeleeRange` |
| Random choice between children (the parent's Try Select Children At Random) | `select="weighted"` on the parent, `weight` on each child; or `Roll` into an Int Local, then conditional `tree.success` handlers; or `RandomSelector` inside a tree | `strike` (`AttackKind`) |
| A transition taken with a random chance | `chance` on the handler; `Chance` inside a tree | `chance="0.6"` replaced three `EvadeIf*` rolls |
| Utility-based selection of a child state (Try Select Children With Highest Utility, considerations) | `select="best"` on the parent, `score`/`weight`/`bias` on each child (`weight * score + bias`, like `Score`) | `engage` selects `attacking` or `flanking` by `TargetDistance` |
| Utility-based selection of a step | `UtilitySelector` over options wrapped in `Score` (`weight * in-value + bias`) | — |
| Evaluators, global tasks and tasks that only read the world (the template's Get Player, GetPlayerInfo) | sensors in the file's `Sensors`, the controller's per-tick facts, or `UADSKPerceptionBridgeComponent` for perception | `PlayerPawn`, `Distance`, `Grounded` sensors; `HasTarget`, `TargetLocation`, `Deactivated` |
| Conditions implemented in C++ (Character is Grounded, Character is in Danger) | a sensor writes the fact, or the controller computes it and publishes an Input or raises an event; the file compares | `Grounded` sensor; `Danger` |
| Task outputs bound to later tasks' inputs | a Local written through an `out-` port and read through an `in-` port | `RunEQS out-point="FlankPoint"` then `MoveTo in-goal="FlankPoint"` |
| StateTree parameters | Inputs with a `default` when the host sets them, configuration attributes in the file when designers tune them | — |
| Delay task, with random deviation | `Wait duration` with `deviation` | `recover`, `idling` |
| Move To task (a location / an actor) | `MoveTo in-goal` (with `repathDistance` to follow a moving point) / `MoveToActor in-target` | `fallback`; `approach` |
| Run Env Query task | `RunEQS query pick out-point`, query parameters as `<Param name value [type]/>` | `fallback_find`, `flanking_find`, `evading_find` |
| Face Towards Actor / Location (template tasks) | `FocusActor` / `Focus` around what runs meanwhile; `LookAtActor` / `LookAt` for a timed turn | every `*_face` scope |
| Set Character Speed (template task) | `MoveSpeed speed` around the move; the previous speed comes back when it ends | `approach_speed`, `fallback_speed` |
| Combo Attack, Charged Attack (template tasks) | `<Act action="ComboAttack" in-target="Target"/>`, `action="ChargedAttack"`; the controller performs them as an `IADSKActionPerformer` and reports the end on the montage end | `combo_attack`, `charged_attack` |
| Send Event task | `Emit event` (delivered on the next tick), with `<Param>` children for a payload | — |
| Linked subtree with parameters | `<Run id tree>` of a `Tree` that declares `<Param name type/>`, bound per call with `<Param name from/>` | — |

One known difference: a ported enemy that starts with the player
already in range waits out its first one-second `searching`, because start values raise no change event; the
`tree.success` handler with the same condition then engages.

## Behavior Tree to AdamskAI

A Behavior Tree has no state machine. The smallest port is one state whose tree is the BT's root and which goes back
to itself on success and failure, as `sidescrolling_npc.xml` does. Top
branches that are really modes (patrol, combat, flee) read better as states with transitions between them.

| Behavior Tree | AdamskAI |
|---|---|
| Behavior Tree and Blackboard run by the AI controller | one behavior file; a `UADSKBrainComponent` on the controller, ticked by it |
| Selector | `Selector`: no re-check of earlier children once it moved on |
| Sequence | `Sequence` |
| Simple Parallel | `SimpleParallel`: main child plus a background child that loops; the background is cancelled when the main child ends (the Immediate finish mode). For the Delayed finish mode, or more than two children, `Parallel success="all"` |
| Blackboard decorator, observer aborts None | a condition on the transition into the state; inside a tree a `Guard`, which re-checks every tick like Self |
| Blackboard decorator, observer aborts Self | `Guard` (`in-condition`, with `is`, `below` or `above`, `invert`): re-checked every tick, a false condition cancels its child and fails |
| Blackboard decorator, observer aborts Lower Priority or Both | the `Guard` as an option of a `ReactiveSelector`: when its condition rises it cancels the running later option and starts; it reacts to the rise only |
| Inverse condition | `invert="true"` on `Guard`, `unless` on `On` |
| Cooldown decorator | `Cooldown duration` |
| Time Limit decorator | `TimeLimit duration` |
| Loop decorator | `Repeat count` (`0` repeats until the child fails) |
| Force Success decorator | `ForceSuccess` |
| Custom decorator | a project node of kind `Composite` with one child ([../authoring-nodes.md](../authoring-nodes.md)) |
| Service that updates a Blackboard key | a sensor in `Sensors`, or the controller publishes the Input before every brain tick; a service only for one branch is a fact the branch reads |
| Set default focus service | `Focus` / `FocusActor` scope around the branch |
| Custom service doing work while a branch runs | a project node of kind `Scope` (`OnUpdate` runs every tick before the child) |
| Wait task, with random deviation | `Wait duration deviation` |
| Move To task, acceptable radius, observe Blackboard value | `MoveTo in-goal acceptanceRadius repathDistance`; `MoveToActor in-target` for an actor key |
| Rotate to face BB entry | `LookAt in-point` / `LookAtActor in-target` with `duration` |
| Run EQS Query task | `RunEQS`, see EQS below |
| Run Behavior task | `Run tree`, with tree parameters for what differs per call |
| Custom latent task (`InProgress`, then finish later) | project async leaf with `ADSK::MakeAsyncLeaf` |
| Custom instant task | project leaf that returns `Succeeded` or `Failed` from `OnStart` |
| Blackboard keys the game writes | Inputs (`Bool`, `Float`, `Int`, `Vector`, `Object` for actors, enums) |
| Blackboard keys tasks write | Locals, written by `out-` ports or `Bind` |
| A Blackboard key observer | `<Event change="Slot">` |
| An AI message or gameplay event | a declared `Event` raised with `EnqueueEvent` |

## EQS

`RunEQS` runs the query in `query` (an object path) as the brain's host actor, the pawn for a controller-hosted brain,
and writes the chosen item's location to a Vector Local through `out-point`. `pick` is `Best`, `RandomBest5` or
`RandomBest25` (the engine's single best item, random best 5 % and random best 25 %). Query parameters are `<Param>`
children: `Float` by default, `type="Int"` or `type="Bool"` for the others; the validator warns (`missing-asset`)
when the query does not declare a parameter of that name and type. The node fails when the query returns no item,
which reads as an AI decision; check them in PIE with the Visual Logger's `LogEQS` entries, which list every item and the test that dropped it. Do not
author the older `randomBest25` field; use `pick`.

## What has no equivalent

From [../limitations.md](../limitations.md): Blueprint tasks, conditions and decorators (behavior is XML and nodes
are C++, by decision); observer aborts from a decorator that has no `OnCheck` (`Guard`, `Cooldown` and project nodes
with one re-check under a `ReactiveSelector`); parallel regions in the state machine; value types other than the six (no names, strings, rotators or
classes as slots); EQS results other than one point; parameters other than `Float`, `Int` and `Bool`; and
replication of AI state to clients ([../networking.md](../networking.md)).
