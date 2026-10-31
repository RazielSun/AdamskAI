# Glossary

The words these documents use, with what plays the same part in Unreal's Behavior Trees and StateTree. The full
porting tables, piece by piece, are in [recipes/port-a-statetree-or-behavior-tree.md](recipes/port-a-statetree-or-behavior-tree.md).

## The file

| Term | Meaning | Behavior Tree | StateTree |
|---|---|---|---|
| behavior file | one `Agent` in strict XML v1 under the behavior root ([xml-reference.md](xml-reference.md)) | Behavior Tree + Blackboard assets | StateTree asset |
| behavior root | the content folder behavior files live in, `UADSKSettings::BehaviorRoot` (default `Content/AI/`) | — | — |
| slot | a named, typed value: an Input or a Local | Blackboard key | parameter, property |
| Input | a slot only the host writes (a fact) | key the controller or a service sets | parameter, evaluator output |
| Local | a slot the file writes through `out-` ports and `Bind` | key a task sets | task output, bound property |
| value types | `Bool`, `Float`, `Int`, `Vector`, `Object` and enums declared in the file | key types | property types |
| event | a declared, named signal with typed parameters, queued and handled by `On` | AI message, gameplay event | event (gameplay tag) |
| change event | an event the core raises when a slot changes (`<Event change="Slot">`) | Blackboard observer | transition on a condition |
| sensor | a catalogue node declared under `<Sensors>` that writes Inputs before every tick | service | evaluator, global task |

## Decisions

| Term | Meaning | Behavior Tree | StateTree |
|---|---|---|---|
| state | a mode ("what am I doing"); parent states hold child states | top-level branch | state |
| leaf state | a state with no children; runs one tree | — | leaf state with tasks |
| transition | `<On event goto>`, with conditions, `chance` and `priority` | decorator abort, branch order | transition |
| `select` | a parent state choosing its child by score or weight | — | selection behavior (utility, random) |
| `history` | re-entering a parent state goes back to the child it left | — | — |
| tree | a procedure ("how"); ends in success or failure | Behavior Tree | the tasks of a state |
| node | one element of a tree, from the catalogue | task, composite, decorator | task |
| leaf | a node with no children that does the work | task | task |
| scope | a node with one child, active while it runs (`Focus`, `MoveSpeed`) | decorator + service on a branch | task running beside others in one state |
| composite | a node that decides which child runs next (`Repeat`, `RandomSelector`) | composite, decorator | — |
| `Parallel` | runs every child at once, each once, until a success or failure count decides | Simple Parallel (two only) | tasks of one state, completion Any/All |
| `Guard` | runs its child while a condition holds | Blackboard decorator | enter condition |
| `ReactiveSelector` | earlier options that can be checked (`Guard`, `Cooldown`, `OnCheck`) interrupt the running one when they become true | observer aborts Lower Priority | — |
| `Set` | writes a literal or another slot to a Local | `SetKeyValue` task | property binding |
| `Run` | a reusable tree inserted at load time, with `Param` bindings | Run Behavior | linked subtree |
| hold | a state that can stay forever on unchanged facts; the validator reports it (`hold-risk`) | — | — |

## Native code

| Term | Meaning |
|---|---|
| catalogue | every node a file may use, with fields and ports ([node-catalog.md](node-catalog.md), and a project's own) |
| descriptor | the C++ definition of a node: name, kind, fields, ports, callbacks ([authoring-nodes.md](authoring-nodes.md)) |
| field | an XML attribute mapped to a configuration struct property, fixed when the file compiles |
| port | an `in-`/`out-` attribute binding a node to a slot, read or written while it runs |
| `Param` | a child element: event or tree parameters, literal node parameters, or slot arguments (`bTakesArgs`) |
| provider | an `IADSKNodeProvider` a project registers to add its nodes |
| host | the Unreal side: brain component, world subsystem, host nodes and services (`AdamskAIHost`) |
| brain | a `UADSKBrainComponent`: one running file on an actor or controller |
| definition | a compiled, immutable file (`FADSKDefinition`), shared by every brain that runs it |
| instance | one agent's values, queues and state (`FADSKInstance`) |
| recorder | a bounded ring of what an instance did, for traces and replay |
| golden trace | a recorded run a test compares against, to catch a change in behavior |
