// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKInstanceInternal.h"
#include "Trace/ADSKInsights.h"

void FADSKProcedure::Propagate(EADSKNodeStatus Status)
{
	while (true)
	{
		if (Lane->Stack.IsEmpty())
		{
			if (Lane == &Main())
			{
				CompleteRoot(Status);
			}
			else if (Lane->OwnerLane != INDEX_NONE)
			{
				// A Parallel's child ends once; it never restarts.
				ReportToParallel(Status);
			}
			else
			{
				// A background subtree loops while its parallel runs; next tick, so a subtree that ends at once cannot spin.
				Lane->RestartTick = Owner.TickCount + 1;
			}
			return;
		}
		FADSKActiveNode& Parent = Lane->Stack.Last();
		const FADSKNodeDefinition& ParentNode = NodeDef(Parent);
		if (ParentNode.Descriptor->Kind == EADSKNodeKind::Composite)
		{
			FADSKCompositeStep Step;
			{
				FADSKNodeContext Context = MakeContext(Parent, 0.f);
				FADSKCallbackScope Scope(Owner);
				Step = ParentNode.Descriptor->OnChildFinished(Context, Parent.ChildCursor, Status);
			}
			ApplyCompositeStep(Step);
			return;
		}
		// A Guard or Scope ends with its one child's result, a SimpleParallel with its main child's.
		if (ParentNode.Descriptor->Kind == EADSKNodeKind::Guard || ParentNode.Descriptor->Kind == EADSKNodeKind::Scope
			|| ParentNode.Descriptor->Kind == EADSKNodeKind::Parallel)
		{
			EndTop(Status == EADSKNodeStatus::Succeeded ? EADSKEndReason::Succeeded : EADSKEndReason::Failed, Status);
			continue;
		}
		check(ParentNode.Descriptor->Kind == EADSKNodeKind::Sequence || ParentNode.Descriptor->Kind == EADSKNodeKind::Selector);
		const EADSKNodeStatus StopStatus = ParentNode.Descriptor->Kind == EADSKNodeKind::Sequence
			? EADSKNodeStatus::Failed
			: EADSKNodeStatus::Succeeded;
		if (Status == StopStatus)
		{
			EndTop(Status == EADSKNodeStatus::Succeeded ? EADSKEndReason::Succeeded : EADSKEndReason::Failed, Status);
			continue;
		}
		if (ParentNode.Descriptor->SelectorPolicy == EADSKSelectorPolicy::Utility)
		{
			FADSKUtilitySelectorMemory& Utility = SelectorMemory<FADSKUtilitySelectorMemory>(Parent);
			if (++Utility.Cursor < Utility.Count)
			{
				Parent.ChildCursor = Utility.Order[Utility.Cursor];
				StartNode(Owner.Definition->GetNodeChildren()[ParentNode.FirstChild + Parent.ChildCursor]);
				return;
			}
			Status = EADSKNodeStatus::Failed;
			EndTop(EADSKEndReason::Failed, Status);
			continue;
		}
		// A reactive selector passes over an option remembering its condition, so only a later change re-triggers it.
		if (ParentNode.Descriptor->SelectorPolicy == EADSKSelectorPolicy::Reactive)
		{
			const uint64 Bit = uint64(1) << Parent.ChildCursor;
			uint64& Seen = SelectorMemory<FADSKReactiveSelectorMemory>(Parent).Seen;
			Seen = OptionPasses(ParentNode, Parent.ChildCursor) ? Seen | Bit : Seen & ~Bit;
		}
		++Parent.ChildCursor;
		if (Parent.ChildCursor < ParentNode.ChildCount)
		{
			const int32 Child = Owner.Definition->GetNodeChildren()[ParentNode.FirstChild + Parent.ChildCursor];
			StartNode(Child);
			return;
		}
		Status = StopStatus == EADSKNodeStatus::Failed ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		EndTop(Status == EADSKNodeStatus::Succeeded ? EADSKEndReason::Succeeded : EADSKEndReason::Failed, Status);
	}
}

bool FADSKProcedure::UpdateWrappers(float Delta)
{
	for (int32 Index = 0; Index < Lane->Stack.Num(); ++Index)
	{
		const FADSKNodeDescriptor& Descriptor = *NodeDef(Lane->Stack[Index]).Descriptor;
		if ((Descriptor.SelectorPolicy == EADSKSelectorPolicy::Reactive && ReactToEarlierOptions(Index))
			|| (Descriptor.SelectorPolicy == EADSKSelectorPolicy::Utility && RescoreUtility(Index)))
		{
			return false;
		}
		FADSKActiveNode& Active = Lane->Stack[Index];
		const FADSKNodeDefinition& Node = NodeDef(Active);
		EADSKNodeStatus Status = EADSKNodeStatus::Running;
		if (Node.Descriptor->Kind == EADSKNodeKind::Guard)
		{
			const FGuardResult Result = EvaluateGuard(Active);
			if (!(Result.Raw == Active.GuardValue))
			{
				Active.GuardValue = Result.Raw;
				Owner.Record({.Kind = EADSKRecordKind::GuardValue, .Node = Active.Node, .Handle = {Owner.Generation, Active.ActivationId}, .bHasValue = true, .Value = Result.Raw});
			}
			Status = Result.bPass ? EADSKNodeStatus::Running : EADSKNodeStatus::Failed;
		}
		// Like a leaf, a scope or composite is not updated in the tick it started.
		else if ((Node.Descriptor->Kind == EADSKNodeKind::Scope || Node.Descriptor->Kind == EADSKNodeKind::Composite)
			&& Node.Descriptor->OnUpdate && Active.StartedTick != Owner.TickCount)
		{
			FADSKNodeContext Context = MakeContext(Active, Delta);
			FADSKCallbackScope Scope(Owner);
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*Node.Descriptor->Name, AdamskAIChannel);
			Status = Node.Descriptor->OnUpdate(Context, Delta);
		}
		if (Owner.bStopRequested)
		{
			return false;
		}
		if (Status != EADSKNodeStatus::Running)
		{
			CancelFrom(Index + 1);
			EndTop(Status == EADSKNodeStatus::Succeeded ? EADSKEndReason::Succeeded : EADSKEndReason::Failed, Status);
			Propagate(Status);
			return false;
		}
	}
	return true;
}
