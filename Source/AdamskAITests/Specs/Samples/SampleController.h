// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "AIController.h"
#include "Brain/ADSKBrainDriver.h"
#include "CoreMinimal.h"
#include "SampleController.generated.h"

class UADSKBrainComponent;

/** docs/setup.md's minimal controller, compiled so it keeps building. Copy and rename. */
UCLASS(NotBlueprintable, Transient)
class AADSKSampleController : public AAIController
{
	GENERATED_BODY()
public:
	AADSKSampleController();

	virtual void Tick(float DeltaSeconds) override;

	/** Game code reports where a noise came from; the next tick publishes it as the NoisePoint Input. */
	void ReportNoise(const FVector& Location) { LastNoise = Location; }

	UPROPERTY()
	TObjectPtr<UADSKBrainComponent> Brain;

	/** Behavior file, relative to the behavior root (Content/AI by default). */
	FString BehaviorFile = TEXT("sample_watch.xml");

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void PublishFacts(UADSKBrainComponent& Target);

	FADSKBrainDriver Driver;
	FADSKInputRef NoisePoint{TEXT("NoisePoint")};
	FVector LastNoise = FVector::ZeroVector;
};
