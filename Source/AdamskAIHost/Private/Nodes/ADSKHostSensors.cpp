// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKHostNodes.h"

#include "Brain/ADSKBrainComponent.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Nodes/ADSKHostNodeTypes.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"

namespace ADSKHostSensorsPrivate
{
	AActor* HostActor(const FADSKNodeContext& Context)
	{
		const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
		return Brain ? Brain->GetHostActor() : nullptr;
	}

	AActor* ReadTarget(const FADSKNodeContext& Context)
	{
		uint64 Id = 0;
		return Context.ReadObject(0, Id) ? FADSKObjectHandles::Resolve<AActor>(Id) : nullptr;
	}

	FADSKNodeDescriptor Sensor(const TCHAR* Name, EADSKValueType Type, bool bTarget)
	{
		FADSKNodeDescriptor Node;
		Node.Name = Name;
		Node.Kind = EADSKNodeKind::Sensor;
		if (bTarget) { Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In}); }
		Node.Ports.Add({TEXT("value"), Type, EADSKPortDirection::Out});
		return Node;
	}

	FADSKNodeDescriptor Distance()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("Distance"), EADSKValueType::Float, true);
		Node.Description = TEXT("Writes the distance in cm from the host actor to the actor in `in-target`; with no target, or one destroyed, the largest Float, so no `below` passes.");
		Node.Guidance = TEXT("Test a range with `<When if=\"TargetDistance\" below=\"1500\"/>` or a Guard with `below`.");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const AActor* Self = HostActor(Context);
			const AActor* Target = ReadTarget(Context);
			Context.WritePort(1, FADSKValue::MakeFloat(Self && Target ? FVector::Dist(Self->GetActorLocation(), Target->GetActorLocation()) : TNumericLimits<float>::Max()));
		};
		return Node;
	}

	FADSKNodeDescriptor HasGameplayTag()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("HasGameplayTag"), EADSKValueType::Bool, true);
		Node.ConfigStruct = FADSKHasGameplayTagConfig::StaticStruct();
		Node.Fields.Add({TEXT("tag"), TEXT("Tag")});
		Node.ValidateConfig = [](const void* Value) { return static_cast<const FADSKHasGameplayTagConfig*>(Value)->Tag.IsValid(); };
		Node.Description = TEXT("Writes whether the actor in `in-target` implements IGameplayTagAssetInterface and has `tag`, including a child tag. False with no actor or interface; does not read AActor::Tags or search components.");
		Node.Guidance = TEXT("A target with Status.Disabled.Stunned matches tag=\"Status.Disabled\".\n")
			TEXT("Publish the host actor as an Object Input to query its tags. Tags must be registered in the project.\n")
			TEXT("Samples every brain tick and prevents idle sleep; a GAS change subscription belongs in a provider.");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const IGameplayTagAssetInterface* Tags = Cast<IGameplayTagAssetInterface>(ReadTarget(Context));
			Context.WritePort(1, FADSKValue::MakeBool(Tags && Tags->HasMatchingGameplayTag(Context.GetConfig<FADSKHasGameplayTagConfig>().Tag)));
		};
		return Node;
	}

	bool SeesTarget(const AActor& Self, const AActor& Target, const FADSKLineOfSightConfig& Config)
	{
		FVector Eyes;
		FRotator Unused;
		Self.GetActorEyesViewPoint(Eyes, Unused);
		const FVector Direction = (Target.GetActorLocation() - Self.GetActorLocation()).GetSafeNormal();
		if (Config.Cone < 180.f && FVector::DotProduct(Direction, Self.GetActorForwardVector()) <= FMath::Cos(FMath::DegreesToRadians(Config.Cone)))
		{
			return false;
		}
		FVector Center, Extent;
		Target.GetActorBounds(true, Center, Extent, false);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ADSKLineOfSight), false);
		Params.AddIgnoredActor(&Self);
		Params.AddIgnoredActor(&Target);
		const int32 Traces = FMath::Max(1, Config.Traces);
		FHitResult Hit;
		for (int32 Index = 0; Index < Traces; ++Index)
		{
			const float Height = Traces == 1 ? 0.f : Extent.Z - 2.f * Extent.Z * Index / (Traces - 1);
			if (!Self.GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Center + FVector(0.f, 0.f, Height), ECC_Visibility, Params))
			{
				return true;
			}
		}
		return false;
	}

	FADSKNodeDescriptor LineOfSight()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("LineOfSight"), EADSKValueType::Bool, true);
		Node.ConfigStruct = FADSKLineOfSightConfig::StaticStruct();
		Node.Fields.Add({TEXT("cone"), TEXT("Cone")});
		Node.Fields.Add({TEXT("traces"), TEXT("Traces")});
		Node.ValidateConfig = [](const void* Value)
		{
			const FADSKLineOfSightConfig& Config = *static_cast<const FADSKLineOfSightConfig*>(Value);
			return FMath::IsFinite(Config.Cone) && Config.Cone > 0.f && Config.Cone <= 180.f && Config.Traces >= 1 && Config.Traces <= 16;
		};
		Node.Description = TEXT("Writes whether the host actor sees the actor in `in-target`: inside `cone` degrees of its facing, and one of `traces` visibility traces from its eyes to the target's bounds clear. False with no target.");
		Node.Guidance = TEXT("Several traces from the target's top to its bottom see over low cover; one traces to its center.\n")
			TEXT("A sensor runs every brain tick: keep `traces` small on many agents.");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const AActor* Self = HostActor(Context);
			const AActor* Target = ReadTarget(Context);
			Context.WritePort(1, FADSKValue::MakeBool(Self && Target && SeesTarget(*Self, *Target, Context.GetConfig<FADSKLineOfSightConfig>())));
		};
		return Node;
	}

	FADSKNodeDescriptor Grounded()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("Grounded"), EADSKValueType::Bool, false);
		Node.Description = TEXT("Writes whether the host character walks on the ground (not falling, flying or swimming). False on a host that is not a character.");
		Node.Guidance = TEXT("React to a launch or a landing with a change event on its Input: `<Event name=\"GroundedChanged\" change=\"Grounded\"/>`.");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const ACharacter* Character = Cast<ACharacter>(HostActor(Context));
			Context.WritePort(0, FADSKValue::MakeBool(Character && Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsMovingOnGround()));
		};
		return Node;
	}

	FADSKNodeDescriptor PlayerPawn()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("PlayerPawn"), EADSKValueType::Object, false);
		Node.ConfigStruct = FADSKPlayerPawnConfig::StaticStruct();
		Node.Fields.Add({TEXT("player"), TEXT("Player")});
		Node.Fields.Add({TEXT("range"), TEXT("Range")});
		Node.ValidateConfig = [](const void* Value)
		{
			const FADSKPlayerPawnConfig& Config = *static_cast<const FADSKPlayerPawnConfig*>(Value);
			return Config.Player >= 0 && FMath::IsFinite(Config.Range) && Config.Range >= 0.f;
		};
		Node.Description = TEXT("Writes the local player's pawn (`player`, default the first); none when there is none or it is farther than `range` cm (0: any distance).");
		Node.Guidance = TEXT("A target for a single-player game; a game with teams or several candidates selects its target in the host (concept.md, principle 4).");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const FADSKPlayerPawnConfig& Config = Context.GetConfig<FADSKPlayerPawnConfig>();
			const AActor* Self = HostActor(Context);
			APawn* Pawn = Self ? UGameplayStatics::GetPlayerPawn(Self, Config.Player) : nullptr;
			const bool bInRange = Pawn && (Config.Range <= 0.f || FVector::Dist(Pawn->GetActorLocation(), Self->GetActorLocation()) <= Config.Range);
			Context.WritePort(0, FADSKObjectHandles::Make(bInRange ? Pawn : nullptr));
		};
		return Node;
	}

	FADSKNodeDescriptor GroupTarget()
	{
		FADSKNodeDescriptor Node = Sensor(TEXT("GroupTarget"), EADSKValueType::Object, false);
		Node.ConfigStruct = FADSKGroupTargetConfig::StaticStruct();
		Node.Fields.Add({TEXT("memory"), TEXT("Memory")});
		Node.ValidateConfig = [](const void* Value) { const float Memory = static_cast<const FADSKGroupTargetConfig*>(Value)->Memory; return FMath::IsFinite(Memory) && Memory >= 0.f; };
		Node.Description = TEXT("Writes the target a member of the host actor's group last reported (UADSKGroupSubsystem::ReportTarget), for `memory` seconds after the sighting; none outside a group, before a sighting or after it fades.");
		Node.Guidance = TEXT("One member's sighting alerts all: `<Event name=\"SquadAlerted\" change=\"GroupTarget\"/>` and an `On` that leaves the quiet states.\n")
			TEXT("The host decides who is in which group and what counts as a sighting; the file only reads it.");
		Node.OnSample = [](FADSKNodeContext& Context)
		{
			const AActor* Self = HostActor(Context);
			const UADSKGroupSubsystem* Groups = UADSKGroupSubsystem::Get(Self);
			AActor* Target = Self && Groups ? Groups->GetGroupTarget(*Self, Context.GetConfig<FADSKGroupTargetConfig>().Memory) : nullptr;
			Context.WritePort(0, FADSKObjectHandles::Make(Target));
		};
		return Node;
	}
}

void FADSKHostNodes::RegisterSensors(FADSKNodeRegistry& Registry)
{
	using namespace ADSKHostSensorsPrivate;
	const EADSKRegisterResult Results[] = {
		Registry.Register(Distance()),
		Registry.Register(HasGameplayTag()),
		Registry.Register(LineOfSight()),
		Registry.Register(Grounded()),
		Registry.Register(PlayerPawn()),
		Registry.Register(GroupTarget())};
	for (const EADSKRegisterResult Result : Results)
	{
		checkf(Result == EADSKRegisterResult::Accepted, TEXT("host sensor registration failed: %s"), LexToString(Result));
	}
}
