// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Samples/SampleController.h"

#include "Brain/ADSKBrainComponent.h"
#include "GameFramework/Pawn.h"

AADSKSampleController::AADSKSampleController()
{
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	bStartAILogicOnPossess = false;       // the driver starts the brain once the required inputs are set
	PrimaryActorTick.bCanEverTick = true; // this controller owns the one brain tick
}

void AADSKSampleController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!InPawn)
	{
		return;
	}
	// NoisePoint is required: until a noise is reported, the pawn looks where it already faces.
	LastNoise = InPawn->GetActorLocation() + InPawn->GetActorForwardVector() * 100.0;
	// A copy without authority (a network client) is refused by the brain itself.
	Driver.Begin(*Brain, BehaviorFile, [this](UADSKBrainComponent& Target) { PublishFacts(Target); });
}

void AADSKSampleController::OnUnPossess()
{
	Driver.End();
	Super::OnUnPossess();
}

void AADSKSampleController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Driver.Tick(DeltaSeconds);
}

void AADSKSampleController::PublishFacts(UADSKBrainComponent& Target)
{
	NoisePoint.Set(Target, FADSKValue::MakeVector(LastNoise));
}
