// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Nodes/ADSKActionPerformer.h"
#include "ADSKDemoActors.generated.h"

class UADSKBrainComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** A tint for a leaf state: fixed colours for the names the demo files use, a stable hue for any other. */
namespace ADSKDemoColors
{
	ADAMSKAIDEMO_API FLinearColor ForState(const FString& State);
}

/** Engine basic shapes and a dynamic material over `BasicShapeMaterial`, whose `Color` parameter the demo tints. */
USTRUCT()
struct FADSKDemoTint
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	void Make(TConstArrayView<UStaticMeshComponent*> Meshes);
	void Set(const FLinearColor& Color);
};

/** A demo agent's body: a character with a cylinder for a body and a cone for a nose, so its facing shows. */
UCLASS(NotBlueprintable)
class ADAMSKAIDEMO_API AADSKDemoAgent : public ACharacter
{
	GENERATED_BODY()
public:
	AADSKDemoAgent();
	virtual void BeginPlay() override;
	void SetTint(const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Nose;

private:
	UPROPERTY()
	FADSKDemoTint Tint;
};

/**
 * Owns the brain, which ticks itself, and tints its pawn by the leaf state. Performs `Act` for every action: the pawn
 * flashes white for ActionSeconds, then the action succeeds.
 */
UCLASS(NotBlueprintable)
class ADAMSKAIDEMO_API AADSKDemoController : public AAIController, public IADSKActionPerformer
{
	GENERATED_BODY()
public:
	static constexpr float ActionSeconds = 0.6f;

	AADSKDemoController();
	virtual void Tick(float DeltaSeconds) override;

	virtual bool StartAction(FName Action, AActor* Target, FADSKActionDone Done) override;
	virtual void StopAction(FName Action) override;
	virtual void RetargetAction(FName Action, AActor* Target) override {}

	UPROPERTY()
	TObjectPtr<UADSKBrainComponent> Brain;

protected:
	virtual void OnUnPossess() override;

private:
	FADSKActionDone Running;
	float ActionLeft = 0.f;
};

/** The stand-in for the player: a sphere walking ADSKDemoLayout::TargetLoop at Speed, unless stopped. */
UCLASS(NotBlueprintable)
class ADAMSKAIDEMO_API AADSKDemoTarget : public AActor
{
	GENERATED_BODY()
public:
	static constexpr float Speed = 250.f;
	static constexpr double Height = 90.0;

	AADSKDemoTarget();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool bWalking = true;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

private:
	int32 Next = 0;

	UPROPERTY()
	FADSKDemoTint Tint;
};

/** A cheap brain host for the fleet: a small cube with the brain on the actor itself, no movement component. */
UCLASS(NotBlueprintable)
class ADAMSKAIDEMO_API AADSKDemoDot : public AActor
{
	GENERATED_BODY()
public:
	AADSKDemoDot();
	virtual void BeginPlay() override;
	void SetTint(const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TObjectPtr<UADSKBrainComponent> Brain;

private:
	UPROPERTY()
	FADSKDemoTint Tint;
};
