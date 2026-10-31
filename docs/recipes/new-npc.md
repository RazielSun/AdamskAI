# Recipe: a new NPC end to end

A pawn possessed by the game's own AI controller runs a behavior file from the behavior root. This assumes the plugin
is deployed ([../setup.md](../setup.md) steps 1 and 2: the plugin enabled, `AdamskAI` and `AdamskAIHost` in the game
module's `Build.cs`). Paths use the default behavior root `Content/AI/`; `guard.xml` stands for the new file.
Commands are the Windows scripts from the project root; the Linux twins are in [README.md](README.md#commands).

1. **Write down the contract.** List what the controller knows (facts: Inputs), what happens to the NPC from outside
   (events) and what the behavior decides itself. Publish raw facts and keep thresholds and chances in the file
   ([../setup.md](../setup.md#runtime-wiring-host-contract)). A fact whose change should cause a transition is an
   Input with a change event, not an event raised from C++ ([publish-a-fact-or-event.md](publish-a-fact-or-event.md)).
   A fact a catalogue sensor measures (distance, line of sight, on the ground, the player's pawn) is a sensor in the
   file, not controller code, and an attack or ability is `Act` with the pawn or controller as its performer
   ([../setup.md](../setup.md#actions-and-groups)).
   Check: every value has a type from [../xml-reference.md](../xml-reference.md#document) (`Bool`, `Float`, `Int`,
   `Vector`, `Object`, or an enum the file declares).

2. **Write the behavior file** `Content/AI/guard.xml`. Copy the closest file from
   [../../Examples/README.md](../../Examples/README.md): `guard_patrol.xml` for a patrol, `hunt_actor.xml` for chasing
   an actor, `guard_post.xml` for a timed routine, `follow_target.xml` for following. Use only nodes from
   [../node-catalog.md](../node-catalog.md), plus the project's `Content/AI/node-catalog.md` when the project
   registers its own nodes. Lay it out as in [../xml-style.md](../xml-style.md). An Input the behavior cannot work
   without is `required="true"`; every other Input has a `default`. Every leaf state with a tree handles
   `tree.success`, and `tree.failure` when the tree can fail, or the brain holds ([../gotchas.md](../gotchas.md)).
   Check: `adsk_validate_win64.ps1 -Paths Content\AI\guard.xml` exits `0`. A `note hold-risk` line does not change the
   exit code; read each one and add the missing handler unless the hold is meant.

3. **Prewarm it.** Add `+PrewarmFiles=guard.xml` under `[/Script/AdamskAIHost.ADSKSettings]` in
   `Config/DefaultGame.ini`, so the first NPC does not compile the file and load its assets on the game thread
   ([../setup.md](../setup.md#steps) step 4). A packaged build must stage the behavior root (step 5).
   Check: `adsk_validate_win64.ps1 -Mode default` prints a `note:` line when the behavior root is not staged.

4. **Write the controller.** Copy
   [SampleController.h](../../Source/AdamskAITests/Specs/Samples/SampleController.h) and
   [SampleController.cpp](../../Source/AdamskAITests/Specs/Samples/SampleController.cpp) into the game module and
   rename them. Its `FADSKBrainDriver` keeps the order: `Begin` in `OnPossess` calls `InitializeFromSource`, your
   publish function and `StartLogic()`; `Tick` publishes facts, then calls `TickBrain` once; `End` in `OnUnPossess`
   stops the brain before `Super`. A client copy without authority is refused by the brain itself. Log `GetLastError()` when initialization or the start fails, and make `BehaviorFile` an `EditAnywhere` property when Blueprints pick the file. Write per-tick Inputs by
   id ([publish-a-fact-or-event.md](publish-a-fact-or-event.md) step 3). To let the world subsystem tick far agents
   less often, set `bManagedTick` on the brain instead and stop calling `TickBrain`
   ([../setup.md](../setup.md#check-the-deployment)).
   Check: `adsk_build_win64.ps1` exits `0`.

5. **Assign the controller.** On the pawn class or Blueprint set `AIControllerClass` to the new controller and
   `AutoPossessAI` to `PlacedInWorldOrSpawned`, so placed and spawned pawns are both possessed. When the file moves
   the pawn, the level needs a navmesh where it walks.
   Check: in the level viewport `P` shows green where the NPC walks.

6. **Validate and test headless.**
   Check: `adsk_validate_win64.ps1` (examples, project files, fixtures and both catalogues) exits `0`, and
   `adsk_test_win64.ps1 AdamskAI.Project.` exits `0`. That test starts every behavior file of the project on the
   project catalogue with random inputs, events and node results, fails when one holds, and logs how many leaf
   states each file reached; a file that reaches few of them has unreachable states. Scenarios with real
   expectations belong to the project's own tests.

7. **Play in PIE.** Start PIE and open the console (`~`).
   Check: `adsk.Status` lists the brain with `running=true`, the expected state path, a rising `ticks=` and an empty
   `error=`; its `input` lines show the values the controller publishes. The Gameplay Debugger (`'`, category
   `AdamskAI`) shows the same for the selected pawn ([../debugging.md](../debugging.md)). Edit and save the XML during
   PIE: the editor reloads it and the brains restart with their inputs kept; a broken edit keeps the running version
   ([../reload.md](../reload.md)). C++ changes need the editor closed, a build and a restart.

When the NPC does not move or react, go to [why-does-it-not-move.md](why-does-it-not-move.md).
