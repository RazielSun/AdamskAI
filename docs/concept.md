# Concept

What AdamskAI is for and the rules that decide where a new feature goes. Read it before adding a node kind, a
language construct or a host bridge; what exists is in [xml-reference.md](xml-reference.md), what is left in
[roadmap.md](dev/roadmap.md).

## One sentence

**A deterministic text language for agent behavior: the host senses and acts, the file decides.** XML compiles into
an immutable flat program (`FADSKDefinition`) shared by every agent; each agent runs it in its own `FADSKInstance`;
Unreal lives only in the host.

## Relatives

| Relative | What AdamskAI shares with it |
|---|---|
| BehaviorTree.CPP v4 | XML authoring, typed in/out ports bound to named slots, a node factory, subtrees with remapping (`Run` + `Param`) |
| SCXML / Harel statecharts | the state machine: hierarchical `State`, `initial`, `On event goto` with a condition, shallow and deep history (as an attribute, not a pseudo-state). Not yet: parallel regions |
| UE StateTree | the execution model: states that run tasks, transitions on completion and events, asset vs instance data; a parent choosing its child by utility or weighted at random (`select`, StateTree's selection behaviors) |
| Halo 2/3 HFSM + BT, NodeCanvas | the hybrid itself: modes on top, procedures underneath |

In short: an SCXML-like statechart whose leaves are BehaviorTree.CPP-like trees, compiled into a StateTree-like
runtime. What none of them has is the text-first contract: strict diagnostics, replayable runs and files a coding
agent can write and check headlessly.

## Principles

1. **Sense → Decide → Act.** Facts reach the file as Inputs (host-written, every tick if they change) and edges the
   host cannot express as a fact as Events. The file decides. Effects happen only in host nodes. The host never
   decides, dice included: a probability belongs in the file (`chance` on a handler, `Chance` and `Roll` in a tree).
2. **A state is a mode, a tree is a procedure.** A state answers "what am I doing" and changes on events; a tree
   answers "how" and finishes with success or failure. Choosing between modes belongs to the state machine
   (conditions and `chance` on handlers, `select` on a parent state: by score or weighted), choosing steps to the
   tree. A tree that only emits an event to pick the next mode is a sign the choice belongs in a `select`.
3. **A failure edge must change something.** A state that decides again after a failure, on unchanged facts,
   decides the same thing: a melee enemy that flanked, failed its EQS and flanked again did so thousands of times a
   minute. Every failure edge waits, goes to a different mode, or follows a changed fact.
   `adsk_simulate` finds such a loop without PIE (`simulate-livelock`; [testing.md](dev/testing.md#simulation)).
4. **One target, not a set.** The file sees a selected target, counts, distances and scores. Choosing among many
   actors is a host sensor or selector (`UADSKPerceptionBridgeComponent::SelectTarget`), never a language feature.
5. **All of an agent's state lives in its instance**: slots, node memory and state structs, the random stream,
   instance time. Node callbacks keep nothing in lambda captures, statics or host-actor fields that the host does
   not own anyway. This is what makes replay, golden traces and snapshot/restore (`FADSKSnapshot`, used by live
   reload to keep state) possible.
6. **Agents do not reach each other directly.** Coordination goes through host services or events: the group
   service (`UADSKGroupSubsystem`) holds groups, each group's target and attack tokens, the host puts actors in
   groups and reports sightings, and sensors (`GroupTarget`), `HoldToken` and `Radio` carry it into files. No node
   holds a pointer to another brain.
7. **Small core, genre kits.** Genre nodes and sensors (combat, GAS, smart objects, RTS orders) live in provider
   modules like a project's own nodes ([authoring-nodes.md](authoring-nodes.md)). The core changes only when a kit
   cannot express something with ports, scopes and async leaves.
8. **Determinism and text are the product.** Same seed, inputs, events and deltas give the same run; files diff,
   review and validate without an editor. A feature that breaks either needs a strong reason.

## Where a new thing goes

| It is | Put it in |
|---|---|
| a fact about the world (distance, health, has line of sight) | an Input the host publishes; a reusable one, a sensor the file declares in `Sensors` (or a host bridge) |
| something the host sees happen once (died, was hit, an order arrived) | an Event, parameters bound with `Bind` |
| a change of a fact the file should react to | `<Event change="Slot">`, not a host-raised edge |
| an action in the world | a host or project node (async leaf, or a scope for "while the child runs") |
| a choice between modes | the state machine: conditions, `chance` on a handler, `select` on the parent state |
| a choice between steps | a selector in the tree |
| a choice among many actors | a host selector feeding one Object Input |
| behavior shared by several agents | a host service, never a node reaching another brain |

## Genres

What fits today and what each genre waits for; the items are in [roadmap.md](dev/roadmap.md) section 4.

| Genre | Fits today | Waits for |
|---|---|---|
| Ambient pawns, crowds | small files, managed tick + LOD, pure files tick on workers (~6 KB, ~0.2 µs per agent) | Mass host for thousands; no AIController per agent |
| Bosses | phases as parent states with `history` (back to the phase after a stagger), `change` events on health thresholds, attacks picked by a `select="weighted"` state, attack trees with `RandomSelector`, `Cooldown`, `TimeLimit`; replayable fights | an animation kit |
| Shooter soldiers | perception bridge, sensors (line of sight, distance), fire with `Act`, attack tokens (`HoldToken`), a squad target (`GroupTarget`) and radio calls (`Radio`), cover by `RunEQS`, fire beside movement in `SimpleParallel`, tactics by `UtilitySelector`, several facts in one handler (`When`/`Any`) | cover reservation (roadmap H3), suppression |
| RPG enemies and companions | orders and reactions as events with `Bind`, `Tag` fields | a GAS kit (abilities as async leaves, tags as facts), a host threat table, snapshots saved to disk (Object ids mapped) |
| Strategy | a unit as a small order state machine | order queues, orders to a group, a commander on utility, Mass; long-horizon planning as a planner node, not language |
| Villager simulation | schedule as an enum Input with a change event, needs by `UtilitySelector` or a `select="best"` state | response curves for `Score`, a smart-object kit, snapshots saved to disk, an agent that outlives its actor |
