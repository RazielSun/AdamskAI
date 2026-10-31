// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKHostNodes.h"

#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKActionPerformer.h"
#include "Nodes/ADSKAsyncLeaf.h"
#include "Nodes/ADSKHostNodeTypes.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKNodeRegistry.h"

namespace ADSKActionNodesPrivate
{
	/** The host actor when it performs actions, else the brain's owner (a controller); null when neither does. */
	UObject* FindPerformer(const FADSKNodeContext& Context)
	{
		const UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
		if (!Brain) { return nullptr; }
		for (UObject* Candidate : {static_cast<UObject*>(Brain->GetHostActor()), static_cast<UObject*>(Brain->GetOwner())})
		{
			if (Candidate && Candidate->GetClass()->ImplementsInterface(UADSKActionPerformer::StaticClass())) { return Candidate; }
		}
		return nullptr;
	}

	AActor* ReadTarget(const FADSKNodeContext& Context, uint64& OutId)
	{
		OutId = 0;
		return Context.ReadObject(0, OutId) ? FADSKObjectHandles::Resolve<AActor>(OutId) : nullptr;
	}

	FADSKNodeDescriptor Act()
	{
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("Act");
		Node.Description = TEXT("Starts the action named `action` at the actor in `in-target` (none allowed) through the pawn's IADSKActionPerformer (else its controller's), and ends when the performer reports the action's end. Fails when nothing performs actions or the action does not start; leaving the node stops it.");
		Node.Guidance = TEXT("One node for attacks, shots and abilities: the pawn knows how, the file decides when.\n")
			TEXT("An action that lasts until it is left (firing) never ends by itself: bound it with `TimeLimit` or run it beside another lane.\n")
			TEXT("A target slot that names another actor while it runs is passed to RetargetAction.");
		Node.ConfigStruct = FADSKActConfig::StaticStruct();
		Node.MemoryStruct = FADSKActMemory::StaticStruct();
		Node.Fields.Add({TEXT("action"), TEXT("Action")});
		Node.Ports.Add({TEXT("target"), EADSKValueType::Object, EADSKPortDirection::In});
		Node.ValidateConfig = [](const void* Value) { return !static_cast<const FADSKActConfig*>(Value)->Action.IsNone(); };
		ADSK::MakeAsyncLeaf(Node, TEXT("Act"), [](FADSKNodeContext& Context, const FADSKAsyncTicket& Ticket)
		{
			UObject* Performer = FindPerformer(Context);
			uint64 Id;
			AActor* Target = ReadTarget(Context, Id);
			const FName Action = Context.GetConfig<FADSKActConfig>().Action;
			Context.GetMemory<FADSKActMemory>().Target = Id;
			const bool bStarted = CastChecked<IADSKActionPerformer>(Performer)->StartAction(Action, Target, [Ticket](bool bSucceeded)
			{
				Ticket.Complete(bSucceeded ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed);
			});
			if (!bStarted) { return EADSKNodeStatus::Failed; }
			Ticket.SetCancel([WeakPerformer = TWeakObjectPtr<UObject>(Performer), Action]()
			{
				if (IADSKActionPerformer* Alive = Cast<IADSKActionPerformer>(WeakPerformer.Get())) { Alive->StopAction(Action); }
			});
			return EADSKNodeStatus::Running;
		}, INDEX_NONE, nullptr, [](FADSKNodeContext& Context) { return FindPerformer(Context) != nullptr; });
		Node.OnUpdate = [](FADSKNodeContext& Context, float)
		{
			uint64 Id;
			AActor* Target = ReadTarget(Context, Id);
			uint64& Current = Context.GetMemory<FADSKActMemory>().Target;
			if (Id != Current)
			{
				Current = Id;
				if (IADSKActionPerformer* Performer = Cast<IADSKActionPerformer>(FindPerformer(Context)))
				{
					Performer->RetargetAction(Context.GetConfig<FADSKActConfig>().Action, Target);
				}
			}
			return EADSKNodeStatus::Running;
		};
		// The update only retargets after the target slot changed; the action's end arrives as a result.
		Node.OnNextWake = [](FADSKNodeContext&) { return ADSK::NeverWake; };
		return Node;
	}
}

void FADSKHostNodes::RegisterActions(FADSKNodeRegistry& Registry)
{
	const EADSKRegisterResult Result = Registry.Register(ADSKActionNodesPrivate::Act());
	checkf(Result == EADSKRegisterResult::Accepted, TEXT("host action node registration failed: %s"), LexToString(Result));
}
