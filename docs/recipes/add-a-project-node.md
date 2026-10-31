# Recipe: add a project node

A project node is a native node the game registers through an `IADSKNodeProvider` in its own module; the plugin never
includes game code. The descriptor fields, the lifecycle contract and the async protocol are in
[../authoring-nodes.md](../authoring-nodes.md); this recipe is the order of work. Commands are the Windows scripts
from the project root; the Linux twins are in [README.md](README.md#commands).

1. **Check that it is needed.** Read [../node-catalog.md](../node-catalog.md) and the project's
   `Content/<BehaviorRoot>/node-catalog.md`. Moving, facing, walk speed, EQS, waits, randomness, retries, time
   limits, cooldowns and parallel work already exist, and a reused piece of XML is a `Run` of a tree with parameters
   ([../xml-reference.md](../xml-reference.md#run-compile-time-subtree-reuse)), not a node. So do sensors for
   distance, line of sight, ground and the player's pawn, attack tokens (`HoldToken`) and group calls (`Radio`). An
   attack, a shot or an ability the pawn runs by name is `Act` with an `IADSKActionPerformer` on the pawn or controller
   ([../setup.md](../setup.md#actions-and-groups)), not a node. A project node is for what only the game knows and
   `Act` cannot carry: a pawn's own state, a result the file must read.
   Check: no catalogue entry does the job.

2. **Pick the kind.**

   | The node... | Kind | Start from |
   |---|---|---|
   | finishes in its start callback, or over a few ticks of `OnUpdate` | leaf | [SampleNodes.cpp](../../Source/AdamskAITests/Specs/Samples/SampleNodes.cpp) (`SampleTurnToward`) |
   | waits for an engine callback (a montage end, an ability end, a query) | async leaf, `ADSK::MakeAsyncLeaf` | `Act` in [ADSKActionNodes.cpp](../../Source/AdamskAIHost/Private/Nodes/ADSKActionNodes.cpp); `RunEQS` in [ADSKHostNodes.cpp](../../Source/AdamskAIHost/Private/Nodes/ADSKHostNodes.cpp) |
   | holds something while its one child runs (a speed, a focus, a tag, a resource) | scope, `EADSKNodeKind::Scope` | `MoveSpeed`, `Focus` and `FocusActor` in [ADSKHostNodes.cpp](../../Source/AdamskAIHost/Private/Nodes/ADSKHostNodes.cpp) |
   | decides which of its children runs (a custom decorator or control node) | composite, `EADSKNodeKind::Composite` | `Repeat`, `Cooldown`, `TimeLimit` in [ADSKBuiltinNodes.cpp](../../Source/AdamskAI/Private/Nodes/ADSKBuiltinNodes.cpp) |
   | measures a fact before every tick and writes it to an Input | sensor, `EADSKNodeKind::Sensor` ([../authoring-nodes.md](../authoring-nodes.md#sensors)) | the host sensors in [ADSKHostSensors.cpp](../../Source/AdamskAIHost/Private/Nodes/ADSKHostSensors.cpp) |

   External work belongs in a leaf: only the running leaf can receive an async result, a scope cannot
   ([../authoring-nodes.md](../authoring-nodes.md#descriptor)).

3. **Write the descriptor.** `Name` is the exact-case XML element, unique in the catalogue. Configuration is a
   `USTRUCT` (`ConfigStruct`) with one `FADSKFieldMap` per XML attribute (`{TEXT("yawOffset"), TEXT("YawOffset")}`);
   slots are `Ports` (`{TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::In}` is authored `in-point`);
   per-activation scratch is `MemoryStruct`. Config and memory are value-only: reach the world through
   `Context.GetHost<UADSKBrainComponent>()` and `GetHostActor()` inside a callback, never stored. Set `bNeverFails`
   when the node only succeeds, so the validator's hold check does not expect a failure; never set `bThreadSafe` on a
   node that touches the host. Reject impossible configuration in `ValidateConfig`.
   For an async leaf, `MakeAsyncLeaf` writes `OnStart` and `OnEnd`: the start function returns `Running` and the
   engine callback calls `Ticket.Complete(...)`; `Ticket.SetCancel` installs a weak-capturing cancel; the `BeforeEnd`
   argument unbinds what the start bound; `CanStart` fails the node plainly before anything opens. A completion only
   posts a result, which the next brain tick applies. Effects clients must see go through replicated state, and
   completion must not depend on rendering ([../authoring-nodes.md](../authoring-nodes.md#networked-games)).
   Check: `adsk_build_win64.ps1` exits `0`.

4. **Write the provider.** A class implementing `IADSKNodeProvider` (`Catalogue/ADSKNodeProvider.h`) with a unique
   `GetProviderName()` and a `RegisterNodes` that calls `Registry.Register(MoveTemp(Node))` for each descriptor and
   adds every result other than `EADSKRegisterResult::Accepted` to `OutErrors`, as
   [SampleNodes.cpp](../../Source/AdamskAITests/Specs/Samples/SampleNodes.cpp) does. One rejected descriptor fails
   the whole assembly: no catalogue, every brain stays stopped.

5. **Register it before the first assembly.** In the game module's `StartupModule`, call
   `IModularFeatures::Get().RegisterModularFeature(IADSKNodeProvider::GetFeatureName(), &Provider)`; in
   `ShutdownModule`, call `FADSKRegistryAssembly::ReleaseAssembled()` (`Catalogue/ADSKRegistryAssembly.h`) and then
   unregister; [SampleNodes.h](../../Source/AdamskAITests/Specs/Samples/SampleNodes.h) describes the same pattern. The first brain start, validation or `FADSKRegistryAssembly::TryGet` assembles the catalogue; a
   provider registered after that (a Game Feature) makes the next lookup assemble it again, and files compiled
   before keep the catalogue they were compiled with until they reload ([../reload.md](../reload.md)).
   Check: `adsk_build_win64.ps1` exits `0`.

6. **Restart the editor.** A new or changed node, descriptor, provider, config or memory struct is native code: XML
   reload never picks it up. Build with the editor closed, then start it again ([../reload.md](../reload.md)).

7. **Regenerate the project catalogue.** With a provider, the project commits its full catalogue as
   `Content/<BehaviorRoot>/node-catalog.md`. Regenerate it with
   `UnrealEditor-Cmd <Project>.uproject -run=AdamskAIValidate -projectcatalog -unattended -nullrhi -stdout`
   ([../testing.md](../dev/testing.md#regenerate-the-catalogue-and-the-schema); on Windows `-stdout` is required) and commit the file.
   Check: `adsk_validate_win64.ps1` exits `0`; a missing or stale project catalogue fails it with `catalogue-stale`.

8. **Use it and test it.** Author it in a behavior file like any catalogue node, for the sample
   `<SampleTurnToward id="watch_turn" in-point="NoisePoint" yawOffset="0"/>`. Wrap a node that waits on an external
   effect in `TimeLimit`, so a missed callback fails the node instead of holding the state.
   Check: `adsk_validate_win64.ps1 -Paths Content\AI\<file>.xml` exits `0`, and `adsk_test_win64.ps1 AdamskAI.Project.`
   exits `0` (it runs project nodes as scripted doubles, so it proves the file, not the node). Test the node itself
   in the project's own tests; [SamplesSpec.cpp](../../Source/AdamskAITests/Specs/Samples/SamplesSpec.cpp) registers
   a provider in a test catalogue, writes a behavior file to a temporary root and checks the node's effect on a
   pawn.
