# Execution semantics

The exact order in which an instance does things, so a file's behavior can be predicted from the file alone. Each
rule names what checks it in brackets: a file in [../Fixtures/conformance/](../Fixtures/conformance/), or, where a file
cannot drive it, an automation spec (`AdamskAI.Execution.Input.Start`). `adsk_check` fails a rule that names none, a
file that does not exist and a spec that no test registers. What each element means is in
[xml-reference.md](xml-reference.md); this page is only about when.

## Terms

- **Configuration**: the active states, root to leaf (`GetActiveStatePath`). Exactly one leaf is active.
- **Procedure**: the leaf state's tree while it runs. It has a main lane, one background lane per running
  `SimpleParallel` and one lane per child of a running `Parallel`.
- **Tick n**: the n-th call of `Tick(Δ)` since `Start`; `Start` itself is tick 0.
- **Eligible**: an event queued for tick n can be dispatched by tick n or a later one, never earlier.

## Start

1. Refused without a change when the instance runs, is inside one of its own callbacks, or misses a required Input
   [AdamskAI.Execution.Input.Start, AdamskAI.Execution.Reentry].
2. A new generation: Inputs take their last accepted values, Locals their defaults; the event queue, pending results
   and the pending completion are cleared; the clock is 0; every node state struct is reset; the random stream is
   reseeded; each change event's baseline is its slot's current value, so a start raises no change
   [AdamskAI.Execution.Input.Start, AdamskAI.Execution.ChangeEvents.Execution, AdamskAI.Execution.Random,
   AdamskAI.Execution.Composites.Api, AdamskAI.Execution.MemoryArena].
3. The configuration is entered from the root down: each state with child states enters its **entry child**: the
   child its `history` resumes, when the state was just entered after having been left (below a `deep` history, every
   state's remembered child); otherwise the one its `select` picks, or its `initial` [entry_initial_chain,
   select_best_ties_in_document_order, select_no_score_enters_initial, select_nested_chooses_at_each_level,
   select_weighted_root_at_start, history_shallow_resumes_the_child_it_left, history_shallow_is_one_level,
   history_deep_resumes_the_path, history_after_select]. A state remembers its active
   child each time it is left; a start forgets all of them.
4. The leaf's tree starts within `Start`: nodes start depth first, and a leaf's `OnStart` runs now. A tree that ends
   during its start queues its completion for tick 1 [AdamskAI.Execution.Composites.Output,
   AdamskAI.Execution.Lifecycle.Completion].

A start from a snapshot replaces steps 2 and 3 with the restore described in [reload.md](reload.md): it continues the
snapshot's stream and clock and enters the deepest state of its path the definition still has
[AdamskAI.Execution.Snapshot].

## Tick(Δ)

In this order; nothing of a later step runs before an earlier one ends.

1. **Clock.** The tick count grows by one and the clock by Δ; the node-start budget (64 per tick) refills
   [AdamskAI.Execution.Composites.Budget, AdamskAI.Execution.Composites.Output].
2. **Inputs.** Every Input written since the last tick becomes its slot's value, the last write of each winning. A
   brain samples its file's sensors just before calling the tick, after its `OnPreTick` bridges, and their Inputs
   arrive here like any host write ([xml-reference.md](xml-reference.md#sensors)); the brain also samples them
   once before `Start` [AdamskAI.Execution.Input.Results, AdamskAIWorld.Sensors].
3. **Results.** Action results posted since the last tick are taken in posting order. One for the running leaf of a
   lane ends that leaf, and its parents continue at once: a `Sequence` starts its next child, a tree that ends queues
   its completion for the next tick. A result for anything else is stale, a second one for the same leaf a
   duplicate; both are recorded and dropped [AdamskAI.Execution.Lifecycle.Results,
   AdamskAI.Execution.Lifecycle.ResultQueue].
4. **Change events.** For each `change` event in declaration order whose slot differs from its baseline, the event is
   queued, eligible now, and the baseline becomes the slot's value [change_event_after_bind]. A slot written in
   step 5 or 6 (a `Bind`, an out port, a `Set`) is seen by the next tick's step 4 [AdamskAI.Execution.Set].
5. **Dispatch.** Up to 16 events, one at a time:
   1. The next is the eligible event with the highest `priority`, ties in queue order; the pending completion
      competes like an event of priority 0 [event_priority_order, event_queue_order, completion_has_priority_zero].
      A completion whose state has been left is dropped (`completion-stale`) [emit_before_completion].
   2. Its handler is found from the active leaf outward; within a state in document order, the first whose condition
      passes and, with a `chance`, wins its draw [handler_search_inner_first, condition_fails_to_parent,
      chance_lost_falls_through]; a handler that loses its draw applies none of its `Bind`s
      [chance_lost_applies_no_bind]. A `chance` named by a slot is read each time its handler is tried
      [chance_from_slot_zero_never_taken, chance_from_slot_follows_the_slot]. Conditions read slots before any `Bind` of this event [condition_before_bind].
      A condition is the attributes and every `When` and `Any` child together [when_all_must_hold]; an `Any` needs
      one of its `When`s [when_any_needs_one].
   3. No handler: the event is dropped (`event-unhandled`) [unhandled_event_dropped]; for a completion the instance
      **holds**: it stays in that leaf, with no procedure running, until another event moves it, which may be a later
      event of the same tick [chance_lost_completion_is_unhandled].
   4. A handler: its `Bind`s write the event's parameters to Locals, then its transition runs:
      - no `goto`: nothing else; the procedure keeps running [internal_transition_keeps_tree];
      - a `goto`: the procedure is cancelled deepest first (every running node ends `cancelled`), states are exited
        leaf first up to the **domain** (the source's parent when the target is the source or below it, otherwise
        the least common ancestor), states are entered down to the target (a `select` state entered only on the
        way chooses nothing [select_skipped_when_only_an_ancestor]), then the target's entry children (a
        `select` scores with the slots as the `Bind`s left them [bind_before_select,
        select_reselects_on_reentry]; a target that was the domain itself stayed active, so it uses no history
        [history_needs_the_state_left]), and the new leaf's tree starts as in Start step 4.
   5. The next event is dispatched against the new configuration [AdamskAI.Execution.Dispatch].
6. **Procedure.** The main lane, then every other lane in the order it started (a `SimpleParallel`'s background, a
   `Parallel`'s children in document order):
   1. a start deferred by the budget runs first [AdamskAI.Execution.Composites.Budget];
   2. the wrappers on the lane's stack are visited outermost first: a `ReactiveSelector` re-checks its earlier
      options that can be checked (`Guard` conditions, `OnCheck`) [AdamskAI.Execution.ReactiveSelector], a `UtilitySelector` rescores when due, a `Guard` re-evaluates its condition, `When` and `Any`
      children included [guard_rechecked_every_tick, guard_when_rechecked_every_tick], a scope or composite runs `OnUpdate`. One that switches or ends stops the visit,
      and the lane's leaf is not updated this tick;
   3. the lane's running leaf runs `OnUpdate` [AdamskAI.Execution.Lifecycle.Nodes, AdamskAI.Execution.Scope].
   No node is updated in the tick it started. A background lane that ends starts again next tick; the main child of
   a `SimpleParallel` ending ends the parallel and cancels its background [parallel_background_runs]. A
   `SimpleParallel` in a background lane does the same in that lane: its background runs beside its main child
   [parallel_nested_in_background_runs] and is cancelled when that child ends, while the outer one runs on
   [parallel_nested_ends_with_its_main].
   A `Parallel` stays the top of its own lane while its children run in theirs; a child that ends is counted and never
   restarts. Once the counts decide (`success` reached; `failure` reached, or too few children left to reach
   `success`) the `Parallel` ends and cancels the children still running, deepest first, in document order
   [parallel_all_waits_for_every_child, parallel_any_ends_on_first_success, parallel_failure_cancels_the_rest,
   parallel_count_policy, AdamskAI.Execution.Parallel.Policy].
7. **Stop.** A stop requested by a callback during this tick happens now: every running node ends, deepest first
   [AdamskAI.Execution.Reentry].

## When an event becomes eligible

| Raised by | Eligible from | Checked by |
|---|---|---|
| the host (`EnqueueEvent`) | the next tick | [AdamskAI.Execution.Input.Results] |
| `Emit` | the tick after the one it ran in, even when it ran during `Start` | [AdamskAI.Execution.Emit, emit_before_completion] |
| a `change` event | the tick it is detected (step 4) | [change_event_after_bind, AdamskAI.Execution.ChangeEvents.Execution] |
| a tree's completion | the tick after the tree ended | [completion_has_priority_zero, AdamskAI.Execution.Lifecycle.Completion] |

So an `Emit` that is a tree's last step is queued before that tree's completion, and wins against it at equal
priority [emit_before_completion].

## Budgets

At most 16 events are dispatched and 64 nodes started per tick; what is left waits for the next tick, in order. A
deferred start is recorded as `budget-deferred`. Both keep a file that loops at once (a tree that ends at its start
and goes back to its own state) from spinning inside one tick [AdamskAI.Execution.Composites.Budget,
AdamskAI.Execution.Dispatch].

## Conformance files

[../Fixtures/conformance/](../Fixtures/conformance/) holds one behavior file per rule, built from core nodes only and
driving itself with `Emit`. `AdamskAI.Conformance` runs each one: it must compile with no diagnostic, and, started with
seed 1 and ticked at 0.1 s, enter a state named `pass` within 100 simulated seconds (the start counts) without entering
one named `fail` or stopping; a second run must pass on the same tick. A hold is not a failure by itself, since a
later event may end it, but a file that holds until the time is up fails, and the message says it held. Keep the
emitting trees running (a long `Wait` last) where a completion would race the event under test. A new rule gets a file here, not a C++ spec, unless it
needs the host (Inputs, action results, stops from callbacks), which the `AdamskAI.Execution.*` specs cover.
