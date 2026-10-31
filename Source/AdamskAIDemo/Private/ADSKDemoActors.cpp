// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKDemoActors.h"

#include "ADSKDemoLayout.h"
#include "Brain/ADSKBrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Runtime/ADSKInstance.h"

namespace ADSKDemoActorsPrivate
{
	UStaticMeshComponent* MakeMesh(AActor& Owner, const TCHAR* Name)
	{
		UStaticMeshComponent* Mesh = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetGenerateOverlapEvents(false);
		return Mesh;
	}

	/** Loaded in BeginPlay, not in constructors, so building a class default object loads no content. */
	void SetShape(UStaticMeshComponent& Mesh, const TCHAR* Shape)
	{
		Mesh.SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	}
}

using namespace ADSKDemoActorsPrivate;

FLinearColor ADSKDemoColors::ForState(const FString& State)
{
	static const TMap<FString, FLinearColor> Fixed = {
		{TEXT("patrol"), FLinearColor(0.15f, 0.65f, 0.25f)},
		{TEXT("watch"), FLinearColor(0.15f, 0.65f, 0.25f)},
		{TEXT("return"), FLinearColor(0.2f, 0.45f, 0.9f)},
		{TEXT("quiet"), FLinearColor(0.15f, 0.65f, 0.25f)},
		{TEXT("chase"), FLinearColor(0.9f, 0.15f, 0.1f)},
		{TEXT("attack"), FLinearColor(0.9f, 0.15f, 0.1f)},
		{TEXT("engage"), FLinearColor(0.9f, 0.15f, 0.1f)},
		{TEXT("search"), FLinearColor(0.95f, 0.8f, 0.1f)},
		{TEXT("investigate"), FLinearColor(0.95f, 0.8f, 0.1f)},
		{TEXT("alert"), FLinearColor(0.95f, 0.5f, 0.05f)},
		{TEXT("circle"), FLinearColor(0.6f, 0.3f, 0.85f)},
	};
	if (const FLinearColor* Color = Fixed.Find(State))
	{
		return *Color;
	}
	const uint8 Hue = static_cast<uint8>(GetTypeHash(State) & 0xFF);
	return FLinearColor::MakeFromHSV8(Hue, 160, 220);
}

void FADSKDemoTint::Make(TConstArrayView<UStaticMeshComponent*> Meshes)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Base)
	{
		return;
	}
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Materials.Add(Mesh->CreateDynamicMaterialInstance(0, Base));
	}
}

void FADSKDemoTint::Set(const FLinearColor& Color)
{
	for (UMaterialInstanceDynamic* Material : Materials)
	{
		if (Material) { Material->SetVectorParameterValue(TEXT("Color"), Color); }
	}
}

AADSKDemoAgent::AADSKDemoAgent()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = AADSKDemoController::StaticClass();
	Body = MakeMesh(*this, TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	// The default capsule is 68 cm wide and 176 cm tall; the cylinder mesh is 100 cm.
	Body->SetRelativeScale3D(FVector(0.6, 0.6, 1.7));
	Nose = MakeMesh(*this, TEXT("Nose"));
	Nose->SetupAttachment(GetCapsuleComponent());
	Nose->SetRelativeLocation(FVector(40.0, 0.0, 50.0));
	Nose->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Nose->SetRelativeScale3D(FVector(0.3, 0.3, 0.4));
	GetCharacterMovement()->MaxWalkSpeed = 350.f;
	// Facing follows the controller's focus while strafing, the path otherwise, as FocusActor and MoveTo expect.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AADSKDemoAgent::BeginPlay()
{
	Super::BeginPlay();
	SetShape(*Body, TEXT("Cylinder"));
	SetShape(*Nose, TEXT("Cone"));
	Tint.Make({Body, Nose});
}

void AADSKDemoAgent::SetTint(const FLinearColor& Color)
{
	Tint.Set(Color);
}

AADSKDemoController::AADSKDemoController()
{
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	Brain->bAutoTick = true;
	// The scene publishes the inputs, then starts the brain.
	bStartAILogicOnPossess = false;
	PrimaryActorTick.bCanEverTick = true;
}

void AADSKDemoController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AADSKDemoAgent* Agent = Cast<AADSKDemoAgent>(GetPawn());
	if (!Agent)
	{
		return;
	}
	if (Running)
	{
		ActionLeft -= DeltaSeconds;
		Agent->SetTint(FLinearColor::White);
		if (ActionLeft <= 0.f)
		{
			FADSKActionDone Done = MoveTemp(Running);
			Done(true);
		}
		return;
	}
	const FADSKInstance* Instance = Brain->GetInstance();
	const TArray<FString> Path = Instance ? Instance->GetActiveStatePath() : TArray<FString>();
	Agent->SetTint(Path.IsEmpty() ? FLinearColor::Gray : ADSKDemoColors::ForState(Path.Last()));
}

bool AADSKDemoController::StartAction(FName Action, AActor* Target, FADSKActionDone Done)
{
	Running = MoveTemp(Done);
	ActionLeft = ActionSeconds;
	return true;
}

void AADSKDemoController::StopAction(FName Action)
{
	Running = nullptr;
}

void AADSKDemoController::OnUnPossess()
{
	if (Brain->IsADSKRunning())
	{
		Brain->StopLogic(TEXT("Unpossessed"));
	}
	Super::OnUnPossess();
}

AADSKDemoTarget::AADSKDemoTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = MakeMesh(*this, TEXT("Body"));
	SetRootComponent(Body);
	Body->SetRelativeScale3D(FVector(0.8));
	// Query-only overlaps: it blocks nothing, but sight sensors aim at an actor's colliding bounds, which it now has.
	Body->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
}

void AADSKDemoTarget::BeginPlay()
{
	Super::BeginPlay();
	SetShape(*Body, TEXT("Sphere"));
	Tint.Make({Body});
	Tint.Set(FLinearColor(0.1f, 0.8f, 0.95f));
}

void AADSKDemoTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bWalking)
	{
		return;
	}
	const FVector Goal = ADSKDemoLayout::TargetLoop[Next] + FVector(0.0, 0.0, Height);
	const FVector Here = GetActorLocation();
	const double Step = Speed * DeltaSeconds;
	if (FVector::Dist(Here, Goal) <= Step)
	{
		SetActorLocation(Goal);
		Next = (Next + 1) % UE_ARRAY_COUNT(ADSKDemoLayout::TargetLoop);
		return;
	}
	SetActorLocation(Here + (Goal - Here).GetSafeNormal() * Step);
}

AADSKDemoDot::AADSKDemoDot()
{
	Body = MakeMesh(*this, TEXT("Body"));
	SetRootComponent(Body);
	Body->SetRelativeScale3D(FVector(0.3));
	Body->SetMobility(EComponentMobility::Movable);
	Brain = CreateDefaultSubobject<UADSKBrainComponent>(TEXT("Brain"));
	// The world subsystem's managed pass ticks the whole fleet, honouring TickLOD and the parallel setting.
	Brain->bAutoTick = false;
	Brain->bManagedTick = true;
}

void AADSKDemoDot::BeginPlay()
{
	Super::BeginPlay();
	SetShape(*Body, TEXT("Cube"));
}

void AADSKDemoDot::SetTint(const FLinearColor& Color)
{
	// Made on the first tint, so a fleet that is never coloured allocates no materials.
	if (Tint.Materials.IsEmpty()) { Tint.Make({Body}); }
	Tint.Set(Color);
}
