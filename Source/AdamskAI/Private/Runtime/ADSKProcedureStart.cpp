// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKInstanceInternal.h"
#include "Trace/ADSKInsights.h"
#include "Trace/ADSKStats.h"

FADSKProcedure::EStartOutcome FADSKProcedure::StartNode(int32 NodeIndex)
{
	// A callback asked the instance to stop: start nothing new, the deferred Stop unwinds the stack.
	if (Owner.bStopRequested)
	{
		return EStartOutcome::Started;
	}
	if (OpsThisTick == ADSK::ProcedureOperationBudget)
	{
		Lane->DeferredNode = NodeIndex;
		if (!Lane->bDeferredRecorded)
		{
			Owner.Record({.Kind = EADSKRecordKind::BudgetDeferred, .Reason = EADSKRecordReason::BudgetDeferred, .Node = NodeIndex});
			Lane->bDeferredRecorded = true;
		}
		return EStartOutcome::Deferred;
	}
	++OpsThisTick;
	const FADSKNodeDefinition& Node = Owner.Definition->GetNodes()[NodeIndex];
	FADSKActiveNode& Active = Lane->Stack.AddDefaulted_GetRef();
	Active.Node = NodeIndex;
	Active.ActivationId = NextActivationId++;
	// 0 names no activation; a long-running server brain can wrap.
	NextActivationId += NextActivationId == 0;
	Active.StartedTick = Owner.TickCount;
	if (Node.Descriptor->MemoryStruct)
	{
		Active.Memory.Initialize(Node.Descriptor->MemoryStruct, Owner.NodeMemory(NodeIndex));
	}
	Owner.Record({.Kind = EADSKRecordKind::NodeStarted, .Reason = EADSKRecordReason::Started, .Node = NodeIndex, .Handle = {Owner.Generation, Active.ActivationId}});
	INC_DWORD_STAT(STAT_ADSKNodeStarts);
	switch (Node.Descriptor->Kind)
	{
	case EADSKNodeKind::Sequence:
	case EADSKNodeKind::Selector:
		if (Node.Descriptor->SelectorPolicy == EADSKSelectorPolicy::Utility)
		{
			StartUtility(Active, Node);
			return EStartOutcome::Started;
		}
		Active.ChildCursor = 0;
		StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild]);
		return EStartOutcome::Started;
	case EADSKNodeKind::Guard:
		StartGuard(Active, NodeIndex, Node);
		return EStartOutcome::Started;

	case EADSKNodeKind::Leaf:
		StartLeaf(Active, Node);
		return EStartOutcome::Started;
	case EADSKNodeKind::Scope:
		StartScope(Active, Node);
		return EStartOutcome::Started;
	case EADSKNodeKind::Parallel:
		StartParallel(Active, Node);
		return EStartOutcome::Started;
	case EADSKNodeKind::Concurrent:
		StartConcurrent(Active, Node);
		return EStartOutcome::Started;
	case EADSKNodeKind::Composite:
		StartComposite(Active, Node);
		return EStartOutcome::Started;
	case EADSKNodeKind::Emit:
		// Arguments are read now, as the node runs; the queued event carries the values.
		for (int32 Arg = 0; Arg < Node.EmitArgCount; ++Arg)
		{
			const FADSKEmitArgDefinition& Definition = Owner.Definition->GetEmitArgs()[Node.FirstEmitArg + Arg];
			Owner.EventParamValues[Arg] = Definition.Slot != INDEX_NONE ? Owner.Slots[Definition.Slot] : Definition.Value;
		}
		if (Owner.EnqueueInternal(Node.EmitEvent, Owner.TickCount + 1) == EADSKInputResult::Accepted) FinishTop(EADSKNodeStatus::Succeeded);
		else FinishTop(EADSKNodeStatus::Failed);
		return EStartOutcome::Started;
	case EADSKNodeKind::Set:
	{
		// Like an out port: written now, seen by later nodes at once and by change events on the next tick.
		const FADSKValue Value = Node.Assign.Source != INDEX_NONE ? Owner.Slots[Node.Assign.Source] : Node.Assign.Value;
		if (Owner.Slots[Node.Assign.Target] != Value)
		{
			Owner.Slots[Node.Assign.Target] = Value;
			Owner.SlotWritten(NodeIndex, Node.Assign.Target, {Owner.Generation, Active.ActivationId});
		}
		FinishTop(EADSKNodeStatus::Succeeded);
		return EStartOutcome::Started;
	}
	case EADSKNodeKind::Sensor:
		// The compiler keeps sensors out of trees.
		checkNoEntry();
		FinishTop(EADSKNodeStatus::Failed);
		return EStartOutcome::Started;
	}
	return EStartOutcome::Started;
}

// Both helpers take `Active` by reference into `Stack`; neither touches it after a call that can
// push to the stack (StartNode) or pop it (FinishTop).
void FADSKProcedure::StartGuard(FADSKActiveNode& Active, int32 NodeIndex, const FADSKNodeDefinition& Node)
{
	const FGuardResult Result = EvaluateGuard(Active);
	Active.GuardValue = Result.Raw;
	Owner.Record({.Kind = EADSKRecordKind::GuardValue, .Node = NodeIndex, .Handle = {Owner.Generation, Active.ActivationId}, .bHasValue = true, .Value = Result.Raw});
	if (!Result.bPass)
	{
		FinishTop(EADSKNodeStatus::Failed);
	}
	else
	{
		StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild]);
	}
}

void FADSKProcedure::StartLeaf(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	FADSKNodeContext Context = MakeContext(Active, 0.f);
	EADSKNodeStatus Status = EADSKNodeStatus::Failed;
	if (Node.Descriptor->OnStart)
	{
		FADSKCallbackScope Scope(Owner);
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*Node.Descriptor->Name, AdamskAIChannel);
		Status = Node.Descriptor->OnStart(Context);
	}
	Active.bPartiallyEntered = Context.IsPartiallyEntered();
	if (Status != EADSKNodeStatus::Running)
	{
		const bool bPartialFailure = Status == EADSKNodeStatus::Failed && Active.bPartiallyEntered;
		FinishTop(Status, EADSKEndReason::PartialEntry, bPartialFailure);
	}
}

void FADSKProcedure::StartScope(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	EADSKNodeStatus Status = EADSKNodeStatus::Failed;
	{
		FADSKNodeContext Context = MakeContext(Active, 0.f);
		FADSKCallbackScope Scope(Owner);
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*Node.Descriptor->Name, AdamskAIChannel);
		Status = Node.Descriptor->OnStart(Context);
		Active.bPartiallyEntered = Context.IsPartiallyEntered();
	}
	// Running or Succeeded both enter the scope; only a failed start skips the child.
	if (Status == EADSKNodeStatus::Failed)
	{
		FinishTop(EADSKNodeStatus::Failed, EADSKEndReason::PartialEntry, Active.bPartiallyEntered);
		return;
	}
	StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild]);
}

void FADSKProcedure::StartParallel(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	FADSKLane& Background = *Lanes.Add_GetRef(SpareLanes.IsEmpty() ? MakeUnique<FADSKLane>() : SpareLanes.Pop(EAllowShrinking::No));
	Background.Id = NextLaneId++;
	Background.Root = Owner.Definition->GetNodeChildren()[Node.FirstChild + 1];
	Active.BackgroundLane = Background.Id;
	const uint32 ParallelId = Active.ActivationId;
	FADSKLane& MainLane = *Lane;
	StartNode(Owner.Definition->GetNodeChildren()[Node.FirstChild]);
	// A main child that finished at once ended the parallel, and with it the background lane.
	const bool bStillActive = MainLane.Stack.ContainsByPredicate([ParallelId](const FADSKActiveNode& Candidate) { return Candidate.ActivationId == ParallelId; });
	if (bStillActive && !Owner.bStopRequested)
	{
		FLaneScope Scope(*this, Background);
		StartNode(Background.Root);
	}
}

void FADSKProcedure::StartConcurrent(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	const uint32 ParallelId = Active.ActivationId;
	const int32 OwnerLaneId = Lane->Id;
	FADSKLane& OwnerLane = *Lane;
	for (int32 Child = 0; Child < Node.ChildCount; ++Child)
	{
		FADSKLane& ChildLane = *Lanes.Add_GetRef(SpareLanes.IsEmpty() ? MakeUnique<FADSKLane>() : SpareLanes.Pop(EAllowShrinking::No));
		ChildLane.Id = NextLaneId++;
		ChildLane.Root = Owner.Definition->GetNodeChildren()[Node.FirstChild + Child];
		ChildLane.OwnerLane = OwnerLaneId;
		ChildLane.OwnerActivation = ParallelId;
		ChildLane.ChildIndex = Child;
		Active.ChildLanes.Add(ChildLane.Id);
	}
	// Copied: a child that decides the Parallel at once ends it, which cancels the lanes and pops Active.
	const TArray<int32, TInlineAllocator<ADSK::MaxParallelChildren>> ChildLanes = Active.ChildLanes;
	for (const int32 Id : ChildLanes)
	{
		FADSKLane* ChildLane = FindLane(Id);
		if (!ChildLane || Owner.bStopRequested || FindActivation(OwnerLane, ParallelId) == INDEX_NONE)
		{
			return;
		}
		FLaneScope Scope(*this, *ChildLane);
		StartNode(ChildLane->Root);
	}
}

int32 FADSKProcedure::FindActivation(const FADSKLane& InLane, uint32 ActivationId)
{
	return InLane.Stack.IndexOfByPredicate([ActivationId](const FADSKActiveNode& Candidate) { return Candidate.ActivationId == ActivationId; });
}

void FADSKProcedure::ReportToParallel(EADSKNodeStatus Status)
{
	FADSKLane* OwnerLane = FindLane(Lane->OwnerLane);
	const int32 Index = OwnerLane ? FindActivation(*OwnerLane, Lane->OwnerActivation) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}
	FADSKActiveNode& Parallel = OwnerLane->Stack[Index];
	const FADSKNodeDefinition& Node = NodeDef(Parallel);
	FADSKParallelMemory& Memory = *reinterpret_cast<FADSKParallelMemory*>(Parallel.Memory.GetMutableMemory());
	const uint8 Bit = uint8(1u << Lane->ChildIndex);
	Memory.Done |= Bit;
	if (Status == EADSKNodeStatus::Succeeded) { Memory.Succeeded |= Bit; }
	const FADSKParallelConfig& Config = *Node.Config.GetPtr<FADSKParallelConfig>();
	const int32 Children = Node.ChildCount;
	const int32 Succeeded = FMath::CountBits(Memory.Succeeded);
	const int32 Failed = FMath::CountBits(Memory.Done) - Succeeded;
	const int32 Running = Children - FMath::CountBits(Memory.Done);
	const int32 Success = ADSK::ParallelThreshold(Config.Success, Children);
	TOptional<EADSKNodeStatus> Decided;
	if (Succeeded >= Success) { Decided = EADSKNodeStatus::Succeeded; }
	else if (Failed >= ADSK::ParallelThreshold(Config.Failure, Children) || Succeeded + Running < Success) { Decided = EADSKNodeStatus::Failed; }
	if (!Decided.IsSet())
	{
		return;
	}
	// Nothing runs above a Parallel in its own lane, so it is that lane's top; ending it cancels the other children.
	check(Index == OwnerLane->Stack.Num() - 1);
	FLaneScope Scope(*this, *OwnerLane);
	FinishTop(Decided.GetValue());
}

int32 FADSKProcedure::CompositeChild(const FADSKNodeDefinition& Node, int32 Index) const
{
	if (Index >= 0 && Index < Node.ChildCount)
	{
		return Owner.Definition->GetNodeChildren()[Node.FirstChild + Index];
	}
	return INDEX_NONE;
}

void FADSKProcedure::StartComposite(FADSKActiveNode& Active, const FADSKNodeDefinition& Node)
{
	FADSKCompositeStep Step;
	{
		FADSKNodeContext Context = MakeContext(Active, 0.f);
		FADSKCallbackScope Scope(Owner);
		Step = Node.Descriptor->OnCompositeStart(Context);
		Active.bPartiallyEntered = Context.IsPartiallyEntered();
	}
	ApplyCompositeStep(Step);
}

void FADSKProcedure::ApplyCompositeStep(const FADSKCompositeStep& Step)
{
	// Like StartNode: once a callback asked to stop, nothing ends or starts here; the deferred Stop cancels the stack.
	if (Owner.bStopRequested)
	{
		return;
	}
	FADSKActiveNode& Active = Lane->Stack.Last();
	const FADSKNodeDefinition& Node = NodeDef(Active);
	const int32 Child = Step.Action == FADSKCompositeStep::EAction::StartChild ? CompositeChild(Node, Step.Child) : INDEX_NONE;
	if (Child != INDEX_NONE)
	{
		Active.ChildCursor = Step.Child;
		StartNode(Child);
		return;
	}
	FinishTop(Step.Action == FADSKCompositeStep::EAction::Succeed ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed);
}
