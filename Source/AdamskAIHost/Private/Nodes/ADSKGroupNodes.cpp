// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKHostNodes.h"

#include "Brain/ADSKBrainComponent.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Nodes/ADSKHostNodeTypes.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKNodeRegistry.h"

namespace ADSKGroupNodesPrivate
{
	UADSKGroupSubsystem* Groups(const FADSKNodeContext& Context)
	{
		return UADSKGroupSubsystem::Get(Context.GetHost<UADSKBrainComponent>());
	}

	FADSKNodeDescriptor HoldToken()
	{
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("HoldToken");
		Node.Kind = EADSKNodeKind::Scope;
		Node.MinChildren = 1;
		Node.MaxChildren = 1;
		Node.Description = TEXT("Takes one of `pool`'s tokens on the actor in `in-target` and runs its one child while holding it; the token goes back when the child ends, cancelled included. Fails at once, without starting the child, when every token on that target is held by others or the target is none.");
		Node.Guidance = TEXT("\"At most two shoot at the player\": wrap the burst, not the whole attack, so a token frees up between bursts.\n")
			TEXT("It fails at once when no token is free: put it in a Selector whose other option waits or does something else, never one that retries at once.\n")
			TEXT("Pool sizes live in Project Settings > AdamskAI > TokenPools; a pool it does not name has one token per target.");
		Node.ConfigStruct = FADSKHoldTokenConfig::StaticStruct();
		Node.MemoryStruct = FADSKHoldTokenMemory::StaticStruct();
		Node.Fields.Add({TEXT("pool"), TEXT("Pool")});
		Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value) { return !static_cast<const FADSKHoldTokenConfig*>(Value)->Pool.IsNone(); };
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			uint64 Id = 0;
			const AActor* Target = Context.ReadObject(0, Id) ? FADSKObjectHandles::Resolve<AActor>(Id) : nullptr;
			UADSKGroupSubsystem* Subsystem = Groups(Context);
			const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			if (!Target || !Subsystem || !Brain || !Subsystem->TakeToken(Context.GetConfig<FADSKHoldTokenConfig>().Pool, Target, *Brain))
			{
				return EADSKNodeStatus::Failed;
			}
			Context.GetMemory<FADSKHoldTokenMemory>().Target = Id;
			return EADSKNodeStatus::Running;
		};
		Node.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason)
		{
			const uint64 Id = Context.GetMemory<FADSKHoldTokenMemory>().Target;
			UADSKGroupSubsystem* Subsystem = Groups(Context);
			const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			if (Id != 0 && Subsystem && Brain)
			{
				Subsystem->ReturnToken(Context.GetConfig<FADSKHoldTokenConfig>().Pool, FADSKObjectHandles::Resolve<AActor>(Id), *Brain);
			}
		};
		return Node;
	}

	FADSKNodeDescriptor Radio()
	{
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("Radio");
		Node.Description = TEXT("Calls the host actor's group: after `delay` seconds every other member's brain receives `event`, with the `<Param name from/>` children as its parameters read now, unless this actor is destroyed or has left the group by then. Succeeds at once; fails when the host actor is in no group.");
		Node.Guidance = TEXT("\"Contact!\": the squad hears it a moment later, and killing the caller before then keeps it quiet.\n")
			TEXT("Say what was seen with parameters: `<Param name=\"who\" from=\"Target\"/>`; receivers declare the event with the same parameters and `Bind` them.\n")
			TEXT("Every member's file declares the event and handles it; a member whose file does not know it, or declares other parameters, ignores the call.");
		Node.bTakesArgs = true;
		Node.ConfigStruct = FADSKRadioConfig::StaticStruct();
		Node.Fields.Add({TEXT("event"), TEXT("Event")});
		Node.Fields.Add({TEXT("delay"), TEXT("Delay")});
		Node.ValidateConfig = [](const void* Value)
		{
			const FADSKRadioConfig& Config = *static_cast<const FADSKRadioConfig*>(Value);
			return !Config.Event.IsNone() && FMath::IsFinite(Config.Delay) && Config.Delay >= 0.f;
		};
		Node.OnStart = [](FADSKNodeContext& Context)
		{
			const FADSKRadioConfig& Config = Context.GetConfig<FADSKRadioConfig>();
			const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
			const AActor* Self = Brain ? Brain->GetHostActor() : nullptr;
			UADSKGroupSubsystem* Subsystem = Groups(Context);
			return Self && Subsystem && Subsystem->Broadcast(*Self, Config.Event, Config.Delay, Context.ReadArgs()) ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		};
		return Node;
	}
}

void FADSKHostNodes::RegisterGroups(FADSKNodeRegistry& Registry)
{
	using namespace ADSKGroupNodesPrivate;
	const EADSKRegisterResult Results[] = {
		Registry.Register(HoldToken()),
		Registry.Register(Radio())};
	for (const EADSKRegisterResult Result : Results)
	{
		checkf(Result == EADSKRegisterResult::Accepted, TEXT("host group node registration failed: %s"), LexToString(Result));
	}
}
