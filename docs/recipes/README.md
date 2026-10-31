# Recipes

One task per file, from the first edit to the command that proves it works. Each step says what to check and which
command shows it; the reasons and the details stay in the documents the steps link to. Read
[../../AGENTS.md](../../AGENTS.md) first for the rules every task follows.

| Task | Recipe |
|---|---|
| A new NPC: behavior file, controller, inputs, prewarm, validation, tests, PIE | [new-npc.md](new-npc.md) |
| Give a behavior a new fact or event from C++ | [publish-a-fact-or-event.md](publish-a-fact-or-event.md) |
| Add a project node: a leaf, an async leaf or a scope | [add-a-project-node.md](add-a-project-node.md) |
| Make agents talk: group calls, messages with parameters, requests and answers | [talk-between-agents.md](talk-between-agents.md) |
| Port a StateTree or a Behavior Tree to XML (mapping tables) | [port-a-statetree-or-behavior-tree.md](port-a-statetree-or-behavior-tree.md) |
| An agent that does not move, does not react or stands still | [why-does-it-not-move.md](why-does-it-not-move.md) |

## Commands

Commands are written for Windows, run from the project root with the editor closed:
`powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\<script>_win64.ps1`. Each has a Linux twin,
`Plugins/AdamskAI/Tools/<script>.sh`, with the same modes (`-Paths` is `--paths`, `-Mode default` is `--default`),
logs and exit codes: `0` clean, `1` diagnostics or failed tests, `2` usage or setup, `3` crash or timeout, never a
pass. The full list is in [../testing.md](../dev/testing.md); Windows traps are in [../windows.md](../windows.md).

## C++ starting points

The plugin compiles and tests two samples, so they keep building as the API changes; copy them into the game module
and rename them:

- [SampleController.h](../../Source/AdamskAITests/Specs/Samples/SampleController.h) /
  [.cpp](../../Source/AdamskAITests/Specs/Samples/SampleController.cpp): an `AAIController` that owns one brain and
  its one tick through an `FADSKBrainDriver` (load, publish, start; publish and tick; stop) and writes a `NoisePoint`
  Input through an `FADSKInputRef`, which resolves the id again after a reload.
- [SampleNodes.h](../../Source/AdamskAITests/Specs/Samples/SampleNodes.h) /
  [.cpp](../../Source/AdamskAITests/Specs/Samples/SampleNodes.cpp): an `IADSKNodeProvider` that registers two host
  leaves: `SampleTurnToward`, with a `yawOffset` field and an `in-point` Vector port, and `SampleSendTo`, which
  sends an event with `<Param name from/>` parameters to another actor's brain.
- [SamplesSpec.cpp](../../Source/AdamskAITests/Specs/Samples/SamplesSpec.cpp): the test that runs both
  (`AdamskAI.Samples`), with the behavior file they expect.

Larger behavior files, each with a golden trace, are in [Examples/](../../Examples/README.md); the world tests'
controllers in [WorldTestActors.h](../../Source/AdamskAITests/Specs/World/WorldTestActors.h) show `Act` performed by
an `IADSKActionPerformer`.
