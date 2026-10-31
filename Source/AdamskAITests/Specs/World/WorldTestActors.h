// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "AIController.h"
#include "Nodes/ADSKActionPerformer.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagContainer.h"
#include "WorldTestActors.generated.h"

class UADSKBrainComponent;
class UADSKPerceptionBridgeComponent;
class UAIPerceptionStimuliSourceComponent;
class UStaticMeshComponent;

/**
 * The body of an AdamskAIWorld.* agent or target: a default character with a cube to see it by, registered as a sight
 * source itself rather than through the project's auto-registration setting. Tests possess it explicitly.
 */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldTestPawn : public ACharacter
{
	GENERATED_BODY()
public:
	AADSKWorldTestPawn();
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TObjectPtr<UAIPerceptionStimuliSourceComponent> StimuliSource;
};

/**
 * A test pawn that carries a brain itself (bAutoTick), so the pawn's network copies carry one too, and replicates
 * the brain's active state tags the way networking.md's recipe does.
 */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldBrainPawn : public AADSKWorldTestPawn
{
	GENERATED_BODY()
public:
	AADSKWorldBrainPawn();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY()
	TObjectPtr<UADSKBrainComponent> Brain;

	/** Written on the server from OnActiveTagsChanged; what animation or UI would read on a client. */
	UPROPERTY(Replicated)
	FGameplayTagContainer StateTags;
};

/** Owns the brain, which ticks itself (bAutoTick); stops it on unpossess, as setup.md's host contract asks. */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldTestController : public AAIController
{
	GENERATED_BODY()
public:
	AADSKWorldTestController();

	UPROPERTY()
	TObjectPtr<UADSKBrainComponent> Brain;

protected:
	virtual void OnUnPossess() override;
};

/**
 * Performs every action the kit's Act starts: records each call, and ends the running action only when the test calls
 * Finish. An action named `Refused` does not start.
 */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldActingController : public AADSKWorldTestController, public IADSKActionPerformer
{
	GENERATED_BODY()
public:
	virtual bool StartAction(FName Action, AActor* Target, FADSKActionDone Done) override;
	virtual void StopAction(FName Action) override;
	virtual void RetargetAction(FName Action, AActor* Target) override;

	/** Reports the running action's end, as the pawn would when its montage ends. */
	void Finish(bool bSucceeded);

	/** `Start <action> <target name or none>`, `Stop <action>`, `Retarget <action> <target>`, in call order. */
	TArray<FString> Calls;

private:
	FADSKActionDone Running;
};

/** The world subsystem ticks this one's brain (bManagedTick), at the TickLOD rate. */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldManagedController : public AADSKWorldTestController
{
	GENERATED_BODY()
public:
	AADSKWorldManagedController();
};

/**
 * Sight (2000 cm, 60 degrees each side, every affiliation) through a perception bridge into the Inputs and events of
 * Fixtures/world/world_sight.xml: a lost target is remembered for ForgetAfter seconds, the alert level reaches 1 in
 * half a second of sight.
 */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldSightController : public AADSKWorldTestController
{
	GENERATED_BODY()
public:
	static constexpr float SightRadius = 2000.f;
	static constexpr float ForgetAfter = 1.f;

	AADSKWorldSightController();

	UPROPERTY()
	TObjectPtr<UADSKPerceptionBridgeComponent> Bridge;
};

/** Same sight setup, accepting only explicitly tagged actors and stimuli. */
UCLASS(NotBlueprintable, Transient)
class AADSKWorldFilteredSightController : public AADSKWorldSightController
{
	GENERATED_BODY()
public:
	AADSKWorldFilteredSightController();
};
