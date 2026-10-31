// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Samples/SampleNodes.h"

#include "Brain/ADSKBrainComponent.h"
#include "GameFramework/Actor.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKNode.h"
#include "Runtime/ADSKNodeRegistry.h"

namespace ADSKSampleNodesPrivate
{
	FADSKNodeDescriptor MakeTurnToward()
	{
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("SampleTurnToward");
		Node.Description = TEXT("Turns the brain's host actor toward `in-point` (yaw only, plus `yawOffset` degrees) and succeeds. Fails without a host actor or a point.");
		Node.Guidance = TEXT("Snap the actor to face a point at once; `LookAt` turns the controller's focus over time instead.");
		Node.ConfigStruct = FADSKSampleTurnConfig::StaticStruct();
		Node.Fields.Add({TEXT("yawOffset"), TEXT("YawOffset")});
		Node.Ports.Add({TEXT("point"), EADSKValueType::Vector, EADSKPortDirection::In});
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			// The host is valid only inside this callback: never keep it in config or memory.
			const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			AActor* Actor = Brain ? Brain->GetHostActor() : nullptr;
			FVector Point;
			if (!Actor || !Context.ReadVector(0, Point))
			{
				return EADSKNodeStatus::Failed;
			}
			const FVector Direction = (Point - Actor->GetActorLocation()).GetSafeNormal2D();
			if (!Direction.IsNearlyZero())
			{
				Actor->SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw + Context.GetConfig<FADSKSampleTurnConfig>().YawOffset, 0.f));
			}
			return EADSKNodeStatus::Succeeded;
		};
		return Node;
	}

	FADSKNodeDescriptor MakeSendTo()
	{
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("SampleSendTo");
		Node.Description = TEXT("Queues `event` in the brain of the actor in `in-target`, with the `<Param name from/>` children as its parameters. Succeeds when that brain accepts it.");
		Node.Guidance = TEXT("The receiver's file declares the event with the same parameters; on its next tick it handles it and `Bind`s them.");
		Node.ConfigStruct = FADSKSampleSendToConfig::StaticStruct();
		Node.Fields.Add({TEXT("event"), TEXT("Event")});
		Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.bTakesArgs = true;
		Node.ValidateConfig = [](const void* Value) { return !static_cast<const FADSKSampleSendToConfig*>(Value)->Event.IsNone(); };
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			uint64 Id = 0;
			const AActor* Target = Context.ReadObject(0, Id) ? FADSKObjectHandles::Resolve<AActor>(Id) : nullptr;
			UADSKBrainComponent* Brain = UADSKGroupSubsystem::FindBrain(Target);
			const bool bAccepted = Brain
				&& Brain->EnqueueEvent(Context.GetConfig<FADSKSampleSendToConfig>().Event.ToString(), Context.ReadArgs()) == EADSKInputResult::Accepted;
			return bAccepted ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		};
		return Node;
	}
}

void FADSKSampleNodeProvider::RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors)
{
	TArray<FADSKNodeDescriptor> Nodes;
	Nodes.Add(ADSKSampleNodesPrivate::MakeTurnToward());
	Nodes.Add(ADSKSampleNodesPrivate::MakeSendTo());
	for (FADSKNodeDescriptor& Node : Nodes)
	{
		const FString Name = Node.Name;
		const EADSKRegisterResult Result = Registry.Register(MoveTemp(Node));
		if (Result != EADSKRegisterResult::Accepted)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Name, LexToString(Result)));
		}
	}
}
