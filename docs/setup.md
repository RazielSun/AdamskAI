# Setup — deploying AdamskAI into a project

Verified with Unreal Engine 5.8 on Linux (clang) for 0.5.0, and on Win64 (MSVC 14.51) for 0.4.0 (a re-run is owed);
Mac is not built yet (see [limitations.md](limitations.md), [roadmap.md](dev/roadmap.md)). Nothing here needs an editor window.
Every `Tools/<name>.sh` script has a Windows twin, `Tools\<name>_win64.ps1`, with the same modes, logs and
exit codes; prerequisites and traps per platform are in [linux.md](linux.md) and [windows.md](windows.md).

## Modules

| Module | Type | Use |
|---|---|---|
| `AdamskAI` | Runtime | language, registry, definitions, executor, recorder; `Core`/`CoreUObject` only |
| `AdamskAIHost` | Runtime | `Engine`/`AIModule` host: settings, provider assembly, world subsystem, brain, the host nodes (`MoveTo`, `LookAt`, `RunEQS`, `Focus`, `MoveSpeed`, `Act`, `HoldToken`, `Radio`), `IADSKActionPerformer`, sensors (`Distance`, `LineOfSight`, `Grounded`, `PlayerPawn`, `GroupTarget`), the group service (`UADSKGroupSubsystem`), validation service, catalogue renderer; debug snapshot/actions/trace export outside Shipping |
| `AdamskAIDebug` | Runtime, denied Shipping/Test | Gameplay Debugger category and world labels ([debugging.md](debugging.md)) |
| `AdamskAIDemo` | Runtime, denied Shipping/Test | demo scenes on a generated arena, `adsk.Demo <Scene>` ([demo.md](demo.md)); nothing depends on it |
| `AdamskAIEditor` | Editor | `AdamskAIValidate` commandlet, report writer, fixture runner and the behavior-root watcher; never linked by a game target |
| `AdamskAITests` | DeveloperTool | `AdamskAI.*` automation tests; `AdamskAIWorld.*` tests in PIE on the plugin's test map (`Content/Tests/`) |

Direction: `AdamskAITests` → `AdamskAIHost` → `AdamskAI`; the editor module is an editor-only
dependency of the tests. None depends on project game code. The editor module loads in the
`Default` phase, not `PostEngineInit`: the engine resolves `-run=<Commandlet>` before post-engine-init
modules load and reports "could not find the class" otherwise.

## Steps

1. Enable the plugin in the project's `.uproject` `Plugins` list (`AdamskAI`) and keep it under
   `Plugins/AdamskAI/`.
2. In the game module's `Build.cs`, add `AdamskAI` and `AdamskAIHost` as private dependencies (`AdamskAIHost`
   also declares `IADSKActionPerformer`, [Actions and groups](#actions-and-groups)).
   Forward-declare plugin types in public headers; include them in `.cpp` files. Includes carry the
   feature folder, as in engine plugins: `#include "Brain/ADSKBrainComponent.h"`,
   `#include "Catalogue/ADSKNodeProvider.h"`, `#include "Runtime/ADSKNode.h"`. Folders: core
   `Core/ Program/ Runtime/ Nodes/ Trace/`; host `Brain/ Catalogue/ Groups/ Nodes/ Sources/ Settings/ Debug/`; kit
   `Combat/`.
3. To add native nodes, implement an `IADSKNodeProvider` and register it as a modular feature in the
   module's `StartupModule`, **before the first assembly** (first brain start, first validation or
   first `FADSKRegistryAssembly::TryGet`). Unregister it in `ShutdownModule` after calling
   `FADSKRegistryAssembly::ReleaseAssembled()` while provider code is still loaded. Details:
   [authoring-nodes.md](authoring-nodes.md).
4. Put project behavior files under the behavior root, `Content/<BehaviorRoot>/<name>.xml`. The root is
   **Project Settings → Plugins → AdamskAI → Behavior Root** (`BehaviorRoot`, Content-relative, default
   `AI`), stored in `Config/DefaultGame.ini`:

   ```ini
   [/Script/AdamskAIHost.ADSKSettings]
   BehaviorRoot=AI
   RecorderCapacity=256
   +PrewarmFiles=guard.xml
   ```

   `PrewarmFiles` compile on a worker as each Game/PIE world is created and stream their ObjectPath assets in, so
   the first brain on them does not hitch. Network clients skip them: their brains run on the server. A game that
   runs brains on client-local actors sets `bPrewarmOnClients=True`. `bKeepStateOnReload` (on by default) makes a
   live reload restart each brain where it was instead of in its initial state ([reload.md](reload.md)).

   A project that registers node providers commits its generated full catalogue and schema as
   `Content/<BehaviorRoot>/node-catalog.md` and `Content/<BehaviorRoot>/adamskai.xsd` (regenerate them as in
   [testing.md](dev/testing.md)).
5. Staging: behavior files are read as plain files at runtime, so a packaged build must stage the
   behavior root as UFS: `+DirectoriesToAlwaysStageAsUFS=(Path="AI")` under
   `[/Script/UnrealEd.ProjectPackagingSettings]` in `Config/DefaultGame.ini`, with your `BehaviorRoot`
   as the path (`DirectoriesToAlwaysStageAsNonUFS`, or staging a parent directory, also counts). The
   default validator run prints a `note:` line when the root is not staged; the note never fails the
   run. The assets behavior files name through ObjectPath fields (an EQS query) are referenced by nothing else, so
   the editor module adds their packages to every cook by the book (`LogAdamskAICook`). This is configuration
   only; no packaged run is claimed.
6. Live debugging needs nothing more: the `adsk.*` console commands, the Gameplay Debugger category and the labels
   come with the plugin ([debugging.md](debugging.md)). A project that wants an in-game panel draws it from the
   public debug snapshot ([debugging.md](debugging.md#a-projects-own-panel)).
7. Exclude the plugin's test content from cooking. `Plugins/AdamskAI/Content/Tests/` (`/AdamskAI/Tests/`) holds
   the map and EQS assets of the `AdamskAIWorld.*` tests ([testing.md](dev/testing.md#world-tests-adamskaiworld)); nothing
   else in the plugin references them, but a cook of all maps would pick the test map up. A plugin cannot set
   this itself, it is a project packaging setting, in `Config/DefaultGame.ini`:

   ```ini
   [/Script/UnrealEd.ProjectPackagingSettings]
   +DirectoriesToNeverCook=(Path="/AdamskAI/Tests")
   ```

   The test assets use engine classes only, so a cook that misses this line still succeeds; it just ships them.
8. Point coding agents at the plugin. Agents read the project root's `AGENTS.md` (Claude Code reads `CLAUDE.md`,
   and a nested one only when it works in that folder). Add one line to the project's own file:

   ```markdown
   - AI behavior uses the AdamskAI plugin: read Plugins/AdamskAI/AGENTS.md before touching behavior files, AI controllers or the plugin.
   ```

   A project without either file can create `AGENTS.md` with that line and a `CLAUDE.md` holding only `@AGENTS.md`.
   The plugin's own `CLAUDE.md` is that one line too; everything lives in plain Markdown any agent can read.
9. Optional editor completion. With no visual editor, the IDE is the editor: map the behavior files to the generated
   XML Schema and it completes nodes, attributes, ports and Choice values and shows each node's description on hover.
   Use `Content/<BehaviorRoot>/adamskai.xsd` when the project has provider nodes, otherwise the plugin's
   `docs/adamskai.xsd`. The file itself must not name the schema (`xsi:` attributes are unknown to the compiler), so
   map it in the IDE. VS Code with the Red Hat XML extension, in the workspace's `.vscode/settings.json` (paths
   relative to the workspace folder):

   ```json
   "xml.fileAssociations": [
     { "pattern": "**/Content/AI/**/*.xml", "systemId": "Content/AI/adamskai.xsd" }
   ]
   ```

   Rider: Settings → Languages & Frameworks → Schemas and DTDs → add the schema for the behavior directory. Visual
   Studio: the XML editor's Properties → Schemas. The schema checks shape only; `adsk_validate` stays the authority.

## Runtime wiring (host contract)

The controller of an AI pawn owns a `UADSKBrainComponent` and drives it itself, in this order:

1. possess the pawn;
2. bind pawn delegates and publish every required initial fact (`SetInput`), including home position; an Input a
   sensor in the file writes needs nothing from the controller;
3. call `StartLogic()` once;
4. every controller tick: refresh facts (`SetInput`) and events (`EnqueueEvent`) first, then call
   `TickBrain(DeltaTime)` exactly once;
5. on death or unpossess call `StopLogic()` before releasing pawn-owned state.

Inputs are cached and survive a restart; events are not: `EnqueueEvent` before `StartLogic` or after a stop
returns `NotRunning`. Publish raw facts (a distance, a roll in `[0,1)`) and keep thresholds and chances in the
behavior file (`if="TargetDistance" below="100"`), so designers tune them without a build. A fact a catalogue sensor
measures (a distance, line of sight, on the ground, the player's pawn) is declared in the file's `Sensors` instead of
published ([Actions and groups](#actions-and-groups)). Random nodes draw
from the brain's stream: set `RandomSeed` (0 = fresh each start) to replay a run.

The brain does not tick on its own, so ordering never depends on actor/component tick order. Async
callbacks (movement completion) only post a result; classification and control flow advance on the
next `TickBrain`. `PauseLogic`/`ResumeLogic` (also driven by AI resource locks) freeze and resume
ticking without accumulating delta; `RestartLogic` is `StopLogic` plus `StartLogic` with the cached
inputs; `IsRunning`/`IsPaused` report the real state to the engine's AI tooling. A `MoveTo` stores its exact `FAIRequestID`, accepts only that completion, and aborts
only that request when it ends. Brains are server/standalone authoritative: on a copy without authority (a
client's copy of a replicated actor) `InitializeFromSource`, `InitializeDefinition` and `StartLogic` refuse with
`not-authority` and the brain never ticks ([networking.md](networking.md)).

Players acting on AI (talking to an NPC, commanding a squad, provoking a guard) do it on their client. Send a server
RPC on the player's own controller or pawn, validate it there, then call `EnqueueEvent` / `SetInput` on the server
brain. Never call the brain API on a client: there is no brain there.

Files are loaded per world by `UADSKWorldSubsystem` (Game and PIE worlds only) from
the behavior root, with a size check before the read. Each brain gets a recorder of `RecorderCapacity`
records; `0`, and every Shipping build, means none. An initial read or compile failure leaves the
brain stopped; it is observable through the brain's last error.

## Minimal controller

The order above as code. `AAIController::PostInitializeComponents` adopts the brain as its
`BrainComponent` and calls its `Cleanup()` on unpossess, so the engine's AI tooling and resource locks
see it; `bStartAILogicOnPossess` stays false because the required inputs must be set first.

Re-possessing is safe: every successful `InitializeFromSource` starts a new life with no inputs cached
from the previous pawn, a failed one leaves the running brain untouched, and the brain is cleaned again
at `EndPlay` however many times the controller was unpossessed. A `StartLogic` that lacks a required input
leaves the brain stopped and `GetLastError()` names the missing inputs. A resource lock held when the
brain starts (a gameplay task owning `Logic`) starts it paused.

`FADSKBrainDriver` (`Brain/ADSKBrainDriver.h`) runs that order for you: `Begin` loads the file, calls your publish
function and starts; `Tick` publishes and ticks once; `End` stops. `FADSKInputRef` writes a per-tick Input by id and
resolves it again after a reload by itself. Embed both in the controller; no base class is needed.

```cpp
// MyAIController.h: UADSKBrainComponent* Brain; FADSKBrainDriver Driver; FADSKInputRef TargetLocation{TEXT("TargetLocation")};

AMyAIController::AMyAIController()
{
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	bStartAILogicOnPossess = false;       // the driver starts the brain once the required inputs are set
	PrimaryActorTick.bCanEverTick = true; // this controller owns the one brain tick
}

void AMyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!InPawn) { return; }
	// Logs the brain's GetLastError() when the file does not load or a required input is missing.
	Driver.Begin(*Brain, TEXT("guard.xml"), [this](UADSKBrainComponent& Target) { PublishFacts(Target); });
}

void AMyAIController::OnUnPossess()
{
	Driver.End(); // while the pawn is still possessed, so ending nodes can release it
	Super::OnUnPossess();
}

void AMyAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Driver.Tick(DeltaSeconds); // publishes, then advances the brain once
}

void AMyAIController::PublishFacts(UADSKBrainComponent& Target)
{
	TargetLocation.Set(Target, FADSKValue::MakeVector(FindTarget()));
}
```

The same as compiled code, with a test: `Source/AdamskAITests/Specs/Samples/SampleController.h/.cpp`. Writing the
order out by hand (`InitializeFromSource`, `SetInput`, `StartLogic`, `TickBrain`, `StopLogic`) works the same way.

## Actions and groups

**Sensors instead of published facts.** The controller publishes what only the game knows (the selected target, a
point to investigate) and lets the file measure the rest from it:

```xml
<Sensors>
  <PlayerPawn id="player" out-value="Target" range="2500"/>
  <Distance id="range" in-target="Target" out-value="TargetDistance"/>
  <Grounded id="ground" out-value="Grounded"/>
</Sensors>
```

The brain samples them after `OnPreTick`, so a sensor reading `Target` sees the value the controller wrote that tick
([xml-reference.md](xml-reference.md#sensors)). A melee enemy that declares these three needs a controller that
raises only its events (`Died`, `Danger`); a shooter can publish `Target` itself and declare `LineOfSight` on it.

**Actions.** `<Act id="shoot" action="Fire" in-target="Target"/>` runs an action the pawn knows. Implement
`IADSKActionPerformer` (`Nodes/ADSKActionPerformer.h` in `AdamskAIHost`) on the pawn or,
when the pawn class is not yours to change, on the controller; `Act` asks the pawn first. `StartAction(Action,
Target, Done)` returns `false` when it cannot start (the node fails), otherwise calls `Done(true|false)` when the
action ends, or never for one that lasts until the node is left; `StopAction` stops it when the node is left early;
`RetargetAction` (optional) follows a new target. A shooter's controller performs `Fire` by starting and stopping
its weapon; a melee enemy performs each attack and calls `Done` from its attack-completed callback.

**Groups.** Agents share facts only through the world's `UADSKGroupSubsystem` (`Groups/ADSKGroupSubsystem.h`): the
host calls `Join(Pawn, Group)` and `Leave(Pawn)` (one group per actor; the earliest member still in it leads) and
`ReportTarget(Pawn, Target)` when a member sights its target. A member's `GroupTarget` sensor then reads the group's
target for `memory` seconds, and `Radio` queues an event in the other members' brains. A typical controller joins its pawn to a group named by a property on possess, leaves on unpossess and reports
every sighting.

**Tokens.** `HoldToken` caps how many agents act on one target at once. Pool sizes are per target, in
`Config/DefaultGame.ini`; a pool not named has one token per target:

```ini
[/Script/AdamskAIHost.ADSKSettings]
TokenPools=(("Attack", 2))
```

## Check the deployment

A brain may sit on any actor. Set `bAutoTick` only when that actor owns the one tick; otherwise call `TickBrain`
explicitly, or set `bManagedTick` and let the world subsystem tick it before the actors, at a rate by distance to
the nearest player view (`TickLOD` levels in the project settings; `ManagedTickInterval` fixes one) with the
elapsed time accumulated, so `Wait` and timers keep their totals. A player controller with no pawn whose view
target is itself (joining, dead) is no viewer. In a networked game, keep the last frequent `TickLOD` distance at
least the AI pawns' net cull distance (`NetCullDistanceSquared`), or clients watch agents that think at the lowest
rate. With `bParallelManagedTick` the managed brains
whose behavior uses only thread-safe nodes (sensors do not count) tick on workers at once; host facts are still
published (the `OnPreTick` bridges run, then the sensors) on the game thread before.

Brains sleep while nothing is due (`bSleepWhenIdle`, on by default, as Behavior Trees and StateTree do): a brain
whose tree waits, holds or runs a `MoveTo` skips its ticks and gets the skipped time in the tick that wakes it. Any
accepted `SetInput`, `EnqueueEvent` or async result wakes it at once, made through the brain or on its instance. A
brain with sensors or an `OnPreTick` handler (the perception bridge) never sleeps, since both publish before every
tick; a host that publishes facts every frame with `SetInput` keeps it awake the same way. An auto-ticking brain
sets its component's tick interval to the sleep, or disables its tick in a hold.

Place `UADSKPerceptionBridgeComponent` beside exactly one perception component and one brain on that same actor.
Its bindings map a unique sense to Bool/Vector/Object Inputs (has a target, its location, the actor itself) and
optional edge or stimulus events; it publishes immediately before the brain tick. A binding may also remember a
lost actor for `ForgetAfter` seconds at its last sensed location (a sensed actor always wins), publish whether the
target is sensed (`SensedInput`), how many candidates there are (`TargetCountInput`) and an alert level in [0, 1]
(`AlertInput`, rising by `AlertRise` per second while anything is sensed and falling by `AlertDecay`) with events
when it reaches 1 and returns to 0 (`AlertedEvent`, `CalmedEvent`). With a memory, `TargetLostEvent` fires when the
target is forgotten, not when it leaves sight. Brains and bridges are authoritative only.

`HasGameplayTag` samples the actor in an Object Input through `IGameplayTagAssetInterface`:

```xml
<HasGameplayTag id="target_disabled" tag="Status.Disabled" in-target="Target" out-value="TargetDisabled"/>
```

Place it in `Sensors`, declare `Target` as an Object Input and `TargetDisabled` as a Bool Input, and register the
tag in the project first. A child tag such as `Status.Disabled.Stunned` matches `Status.Disabled`. No actor or
interface gives false; the sensor does not search components or read `AActor::Tags`. Like every sensor it keeps
the brain awake. A `change` event on `TargetDisabled` lets the file react without a host-raised event.

Perception bindings have two optional filters: `RequiredActorTag` is an ordinary `AActor::Tags` name and
`RequiredStimulusTag` matches `FAIStimulus::Tag` exactly. `None` means no restriction. Set them before `BeginPlay`;
the bridge snapshots them for that lifetime. Both filters apply before a positive observation can add a candidate
or queue a stimulus, and also on resync after a stop/reload. Losing a previously accepted stimulus still clears
current sensing even if the loss carries no tag. Removing the required actor tag drops the candidate on the next
sample. Target-selection policy remains in `SelectTarget` or the project.

For a heard point, the existing `StimulusEvent` already carries one Vector parameter named `location`; bind it to
a Local in the handler. A rejected stimulus neither refreshes the observation nor queues that event. Hearing is
information, not firing permission.

Behavior files can live outside BehaviorRoot: `BehaviorMounts` (project settings) maps a mount name to a directory
relative to the project, and a Game Feature can call `FADSKBehaviorRoots::Register(Name, Directory)` on activation
(and `Unregister` on deactivation). A file there is named `@<Name>/<file>.xml` in `BehaviorFile`, `Acquire`,
`PrewarmFiles` and `adsk.Reload`; the editor watches, cooks and validates mounts like the root. A Game Feature's node
provider may register at activation: the catalogue is assembled again for files compiled after it.

Linux, after building the editor target with the project's own build script:

```bash
Plugins/AdamskAI/Tools/adsk_check.sh         # static plugin rules
Plugins/AdamskAI/Tools/adsk_validate.sh      # compiles examples, project AI, fixtures; checks catalogues and schemas
Plugins/AdamskAI/Tools/adsk_simulate.sh      # runs the scenarios in Tests/AdamskAI (and the plugin's)
Plugins/AdamskAI/Tools/adsk_test.sh          # AdamskAI.* automation tests
```

Windows, from the project root with the editor closed ([windows.md](windows.md)):

```powershell
powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_build_win64.ps1      # <Project>Editor Win64 Development
powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_check_win64.ps1
powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_validate_win64.ps1
powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_simulate_win64.ps1
powershell -ExecutionPolicy Bypass -File Plugins\AdamskAI\Tools\adsk_test_win64.ps1
```
