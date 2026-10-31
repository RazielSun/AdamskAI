# Networking

AdamskAI runs where the game runs its AI and replicates nothing itself, like Behavior Trees and StateTree: the server
(or standalone) owns every brain, and clients see the results through the engine's own replication and through
whatever the game replicates from the brain's state tags. Tests: `AdamskAIWorld.Network.*` (listen server and
dedicated server with a client, in PIE).

## What runs where

| Piece | Runs on | What clients get |
|---|---|---|
| `UADSKBrainComponent` on an `AAIController` | server only: an AI controller never reaches clients | nothing directly |
| `UADSKBrainComponent` on any other actor (a pawn, a spawner) | the server copy runs; a copy without authority (a client's copy of a replicated actor) refuses to initialize or start (`GetLastError()` is `not-authority`, one warning per brain) and never ticks. An actor a client spawns locally (not replicated) has authority there, so client-only cosmetic brains work | — |
| `UADSKWorldSubsystem` | every Game/PIE world; network clients skip `PrewarmFiles` unless `bPrewarmOnClients` | — |
| Managed tick LOD | distance to every player's view: remote players through the camera the client syncs; a controller with no pawn whose view target is itself (joining, dead) counts as no viewer | — |
| `MoveTo`, `MoveToActor` | server path following | position through `UCharacterMovementComponent` replication |
| `Focus`, `LookAt`, `FocusActor`, `LookAtActor` | server controller focus | pawn rotation through movement; aim pitch through `APawn::RemoteViewPitch` |
| `MoveSpeed` | server `MaxWalkSpeed` | nothing; a client AnimBP that reads `MaxWalkSpeed` sees the default |
| `RunEQS`, perception bridge | server | nothing |
| State tags (`<State tags="...">`) | server brain, `GetActiveTags()` / `OnActiveTagsChanged` | what the game replicates (recipe below) |
| `FADSKObjectHandles` | one process-wide table | ids are not network-stable; never replicate them |

Keep the last `TickLOD` distance that still ticks often at least the AI pawns' net cull distance
(`NetCullDistanceSquared`), or clients watch agents that think at the lowest rate.

## Showing AI state on clients: state tags

Animation, VFX, barks and UI need a short summary of what the AI is doing. Tag the states in XML and replicate the
tags; game code then never depends on state ids, which designers may rename.

```xml
<State id="engage" initial="attacking" select="best" tags="AI.State.Combat">
  <State id="attacking" initial="approach" tags="AI.Action.Attack">
```

- A state's tags hold while it or any descendant is active; the brain exposes their union as an
  `FGameplayTagContainer` (`GetActiveTags()`), empty while stopped.
- `OnActiveTagsChanged` fires once per change, at the end of the brain call that changed it, never from inside a node.
  A transition that keeps the same set does not fire it. A reload restarts the brain: the stop and the new start each
  fire, in the same frame, the new set last.
- Every tag must be a registered gameplay tag. A file with an unknown one fails `InitializeFromSource` and every reload
  with `unknown-tag: …`, and `adsk_validate` warns about it first. `adsk.Status` and the Gameplay Debugger print a
  `tags=` line.

The game replicates the container itself, on the pawn:

```cpp
// MyEnemy.h
UPROPERTY(ReplicatedUsing = OnRep_StateTags)
FGameplayTagContainer StateTags;
UFUNCTION()
void OnRep_StateTags(); // drive animation, VFX, UI

// MyEnemyAIController.cpp, after the brain is set up on the server
Brain->OnActiveTagsChanged.AddWeakLambda(this, [this](const FGameplayTagContainer& Tags)
{
	if (AMyEnemy* Enemy = Cast<AMyEnemy>(GetPawn()))
	{
		Enemy->FlushNetDormancy(); // only if the pawn goes dormant
		Enemy->StateTags = Tags;
	}
});
```

`AdamskAIWorld.Network.StateTagsReachTheClient` runs exactly this with a test pawn.

## Players acting on AI

A player who talks to an NPC, commands a squad or provokes a guard does it on their client. Send a server RPC on the
player's own controller or pawn, validate it there, then call `EnqueueEvent` / `SetInput` on the server brain. Never
call the brain API on a client: there is no brain there.

## Host nodes

A host node runs on the server. Effects clients must see go through replicated state, and completion must never
depend on rendering; the rules are in [authoring-nodes.md](authoring-nodes.md#networked-games).

## Debugging

Which tool works where (listen server, client, dedicated server) and the client-world hint of `adsk.*` and the panel:
[debugging.md](debugging.md#networked-games). Network replays (`DemoNetDriver`) hold only replicated state, so AI state
appears in replays through the replicated tags.

## Dedicated servers

PIE with a dedicated server works and is tested. A `Server` target has not been built: the engine used here is a
Launcher build, which cannot build one ([roadmap.md](dev/roadmap.md)).
