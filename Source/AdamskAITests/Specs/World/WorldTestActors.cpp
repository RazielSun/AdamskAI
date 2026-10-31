// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "World/WorldTestActors.h"

#include "Brain/ADSKBrainComponent.h"
#include "Bridges/ADSKPerceptionBridgeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"

AADSKWorldTestPawn::AADSKWorldTestPawn()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = AADSKWorldTestController::StaticClass();
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	// The default capsule is 68 cm wide and 176 cm tall; the cube mesh is 100 cm.
	Body->SetRelativeScale3D(FVector(0.6, 0.6, 1.7));
	StimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("StimuliSource"));
}

void AADSKWorldTestPawn::BeginPlay()
{
	Super::BeginPlay();
	// Loaded here rather than in the constructor, so building the class default object loads no content.
	Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	StimuliSource->RegisterForSense(UAISense_Sight::StaticClass());
}

AADSKWorldBrainPawn::AADSKWorldBrainPawn()
{
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	Brain->bAutoTick = true;
}

void AADSKWorldBrainPawn::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Brain->OnActiveTagsChanged.AddWeakLambda(this, [this](const FGameplayTagContainer& Active) { StateTags = Active; });
	}
}

void AADSKWorldBrainPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AADSKWorldBrainPawn, StateTags);
}

AADSKWorldTestController::AADSKWorldTestController()
{
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	Brain->bAutoTick = true;
	// The test sets the required inputs and then calls StartLogic itself.
	bStartAILogicOnPossess = false;
}

void AADSKWorldTestController::OnUnPossess()
{
	if (Brain->IsADSKRunning())
	{
		Brain->StopLogic(TEXT("Unpossessed"));
	}
	Super::OnUnPossess();
}

bool AADSKWorldActingController::StartAction(FName Action, AActor* Target, FADSKActionDone Done)
{
	Calls.Add(FString::Printf(TEXT("Start %s %s"), *Action.ToString(), Target ? *Target->GetName() : TEXT("none")));
	if (Action == TEXT("Refused")) { return false; }
	Running = MoveTemp(Done);
	return true;
}

void AADSKWorldActingController::StopAction(FName Action)
{
	Calls.Add(TEXT("Stop ") + Action.ToString());
	Running = nullptr;
}

void AADSKWorldActingController::RetargetAction(FName Action, AActor* Target)
{
	Calls.Add(FString::Printf(TEXT("Retarget %s %s"), *Action.ToString(), Target ? *Target->GetName() : TEXT("none")));
}

void AADSKWorldActingController::Finish(bool bSucceeded)
{
	if (FADSKActionDone Done = MoveTemp(Running)) { Done(bSucceeded); }
}

AADSKWorldManagedController::AADSKWorldManagedController()
{
	Brain->bAutoTick = false;
	Brain->bManagedTick = true;
}

AADSKWorldSightController::AADSKWorldSightController()
{
	UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	UAISenseConfig_Sight* Sight = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
	Sight->SightRadius = SightRadius;
	Sight->LoseSightRadius = SightRadius + 200.f;
	Sight->PeripheralVisionAngleDegrees = 60.f;
	Sight->DetectionByAffiliation.bDetectEnemies = true;
	Sight->DetectionByAffiliation.bDetectNeutrals = true;
	Sight->DetectionByAffiliation.bDetectFriendlies = true;
	Perception->ConfigureSense(*Sight);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
	SetPerceptionComponent(*Perception);

	Bridge = CreateDefaultSubobject<UADSKPerceptionBridgeComponent>(TEXT("Bridge"));
	FADSKPerceptionBinding Binding;
	Binding.Sense = UAISense_Sight::StaticClass();
	Binding.HasTargetInput = TEXT("HasTarget");
	Binding.TargetLocationInput = TEXT("TargetLocation");
	Binding.TargetInput = TEXT("Target");
	Binding.TargetSeenEvent = TEXT("TargetSeen");
	Binding.TargetLostEvent = TEXT("TargetLost");
	Binding.ForgetAfter = ForgetAfter;
	Binding.SensedInput = TEXT("Sensed");
	Binding.AlertInput = TEXT("Alert");
	Binding.AlertRise = 2.f;
	Binding.AlertedEvent = TEXT("Alerted");
	Bridge->Bindings.Add(Binding);
}
