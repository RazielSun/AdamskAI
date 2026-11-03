# Live debugging

AdamskAI ships no UI toolkit. Everything below uses what the engine has: console commands, the Gameplay Debugger,
the Visual Logger and debug strings. `AdamskAIDebug` is the development module that registers the Gameplay
Debugger category and draws the labels; it is denied in Shipping and Test builds.

`adsk.Status` writes the legacy per-brain line unchanged, then indented continuation lines from
the same value-owned snapshot, and one final aggregate brain count. `adsk.DumpTrace` writes the
recorder tail. `adsk.Event`, `adsk.Restart` and `adsk.Reload` act on brains by address; stale addresses and invalid
arguments return the normal inline/console diagnostic. Restart replaces the instance with the currently published
definition, restores its prior running state and restarts execution ticks. Brain-owned replacement seeds the new
instance from the stopped instance's generation, so a successful running replacement advances the
issued generation by two. Fresh standalone instances still start at zero. Generation is `uint32`:
the odd issued generations repeat after roughly `2^31` running replacements on one brain. Reload
requests the source again.

Set `adsk.Labels 1` for one-frame `state / node` world labels over every brain's pawn. The labels collect a light
snapshot on the game thread only while the variable is on.

## A project's own panel

A project with an in-game UI of its own (ImGui, Slate, UMG) draws AdamskAI from two public `AdamskAIHost` APIs, both
non-Shipping:

- `FADSKDebugSnapshot::Collect(World)` (`Debug/ADSKDebugSnapshot.h`) returns every server or standalone brain as
  value-owned rows: brain address and names, pawn label position, source, hash and diagnostics, state and node,
  generation and ticks, error, cached host writes, Inputs, Locals, queued events, move request, event schemas and
  the newest 16 trace entries. It retains no UObject, definition or recorder pointer.
- `ADSKDebugActions` (`Debug/ADSKDebugActions.h`) is what the console commands call: `Event`, `Restart`, `Reload`,
  and `ClientWorldHint` for a client world with no brains.

When the active states have `tags`, `adsk.Status` and the Gameplay Debugger add a `tags=` line. In traces a Guard
records `guard-value` when it starts and then only when its value changes.

`adsk.Status` and the Gameplay Debugger show pending operations as `ops=<kind>@<node>(<age>s)[,...]|none` and `stale=<count>`. Age is diagnostic only; accepted completions can remain for the one-tick result latency.

## Gameplay Debugger and Visual Logger

- **Gameplay Debugger.** `AdamskAIDebug` registers the category `AdamskAI` (enabled in game and simulate). Select an
  AI pawn with the debugger (`'` by default) and it shows the debug snapshot of the brain on that actor, or on its
  controller: the status line, `exec`, active node and `background=[...]` nodes, inputs, locals, cached inputs
  and the last records. It is the text `adsk.Status` prints for that brain, collected on the server.
- **Visual Logger.** Every brain logs its state transitions to `LogAdamskAIHost` on its owner
  (`(stopped) -> mode/searching`, `mode/searching -> mode/engage/attacking/approach`, `... -> (stopped)`), once per change,
  only while the Visual Logger records. A Visual Logger snapshot of a controller carries the same snapshot lines
  under the `AdamskAI` status category (`UADSKBrainComponent::DescribeSelfToVisLog`). Host nodes draw on the same
  owner: `MoveTo`/`MoveToActor` their goal as a location with the acceptance radius (`<node> goal`, green), `RunEQS`
  the point it returned (`<node> point`, yellow), and `LookAt`/`Focus`/`LookAtActor`/`FocusActor` a segment from the
  pawn to what it now faces (`<node> focus`, cyan), each when it changes.
- `showdebug` and anything else that reads `UBrainComponent::GetDebugInfoString` get
  `AdamskAI <source>: <state path> [<active nodes>]`.

## Networked games

Brains run on the server (or standalone); clients have none ([networking.md](networking.md)). Where each tool works:

| Tool | Listen server host | Client | Dedicated server |
|---|---|---|---|
| Gameplay Debugger `AdamskAI` category | works | works: the server collects the lines and replicates them (`AdamskAIWorld.Network.Dedicated`) | works from any client |
| `adsk.*` commands | works | finds no brains and says so | `ServerExec adsk.Status` from a client (non-Shipping); the output goes to the server log |
| `adsk.Labels` | works | nothing to draw | not applicable |
| Visual Logger, `adsk.DumpTrace` | the server world | — | on the server process |

In a client world with no brains, `adsk.Status`, an `adsk.Restart` / `adsk.Event` / `adsk.DumpTrace` that matches
nothing print `client world: brains run on the server; use the Gameplay Debugger, or ServerExec
adsk.Status`.

To reproduce a server bug offline, note the brain's `RandomSeed` and take `adsk.DumpTrace` on the server: the seed
and the trace's inputs and events replay the same decisions (perception and navigation differ between machines, so
there is nothing to gain on a client). The plugin computes floats the same way in every build;
values from the project's own C++ may not ([limitations.md](limitations.md)).
