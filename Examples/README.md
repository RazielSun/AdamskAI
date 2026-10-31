# AdamskAI examples

Valid behavior files that use only the core nodes (`Sequence`, `Selector`, `Guard`, `SimpleParallel`, `RandomSelector`, `Repeat`, `Cooldown`, `TimeLimit`, `ForceSuccess`, `Wait`, `Roll`, `Chance`, `Emit`) and
the `AdamskAIHost` nodes (`MoveTo`, `LookAt`, `Focus`, `MoveSpeed`, `MoveToActor`, `LookAtActor`, `FocusActor`). They are the files an author copies from. The
default validator mode compiles every one of them against the plugin catalogue
([`docs/node-catalog.md`](../docs/node-catalog.md)), and `AdamskAI.Examples.Run` executes each through a scenario on the
real core, with the host nodes replaced by scripted doubles; a new example needs a scenario there. None of them holds.

| File | Shows |
|---|---|
| [`guard_patrol.xml`](guard_patrol.xml) | a required Vector input, a Bool `Guard` condition slot, `MoveTo` then `Wait`, a `tree.failure` rest state |
| [`chase_and_wait.xml`](chase_and_wait.xml) | events with a Vector parameter, `Bind` into a Local, two trees, `LookAt`, `tree.success` / `tree.failure` transitions |
| [`selector_fallback.xml`](selector_fallback.xml) | a `Selector` that falls back from a `Guard`ed `MoveTo` to `Wait` |
| [`emit_handoff.xml`](emit_handoff.xml) | an `Emit` that raises a parameterless event handled on the next tick, which makes the tree's own completion stale |
| [`run_reuse.xml`](run_reuse.xml) | `Run` reusing one tree body at two call sites, with call-site-prefixed node ids |
| [`skirmish.xml`](skirmish.xml) | a random plan (`Roll`), a distance threshold held in an Input (`unless ... below="Reach"`), the scopes `MoveSpeed` and `Focus` around a move, a jittered `Wait` |
| [`hunt_actor.xml`](hunt_actor.xml) | a change event (`change="Target"`), an `Object` Input and Local, an `Object` event parameter, `is="none"` and `is` another Object slot, `FocusActor` around `MoveToActor`, `LookAtActor` |
| [`guard_post.xml`](guard_post.xml) | the decorators: `TimeLimit` around an endless `Repeat` of a `RandomSelector`, a `Cooldown` spacing one option, `ForceSuccess` around a move |
| [`retreat_watching.xml`](retreat_watching.xml) | a `SimpleParallel`: `MoveTo` beside a looping, `Guard`ed `LookAtActor`; `is="none" invert="true"` |
| [`follow_target.xml`](follow_target.xml) | `MoveTo repathDistance` after a moving target, a reactive `Guard below` with a slot operand, `Chance` in a `Selector` |

Check them without a renderer or a client:

```bash
Plugins/AdamskAI/Tools/adsk_validate.sh --default
```

Invalid inputs and the diagnostic each must produce live in [`../Fixtures/`](../Fixtures/).
