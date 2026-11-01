// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKProcedure.h"

#include "Runtime/ADSKInstanceInternal.h"
#include "Trace/ADSKInsights.h"

FADSKProcedure::FADSKProcedure(FADSKInstance::FImpl& InOwner)
	: Owner(InOwner)
{
	Lanes.Add(MakeUnique<FADSKLane>());
	Lane = &Main();
	Main().Stack.Reserve(FADSKCompiler::MaxTreeDepth);
}

void FADSKProcedure::ResetForGeneration()
{
	Lanes.SetNum(1);
	Lane = &Main();
	Main().Stack.Reset();
	Main().DeferredNode.Reset();
	Main().bDeferredRecorded = false;
	PendingTerminal.Reset();
	NextLaneId = 1;
	Root = INDEX_NONE;
	OwnerState = INDEX_NONE;
	OwnerToken = 0;
	NextActivationId = 1;
	OpsThisTick = 0;
	bFinished = true;
}

void FADSKProcedure::ResetBudget()
{
	OpsThisTick = 0;
}

void FADSKProcedure::Start(int32 RootNode, int32 InOwnerState, uint64 InOwnerToken)
{
	const FADSKLane& MainLane = *Lanes[0];
	check(Lanes.Num() == 1 && MainLane.Stack.IsEmpty() && !MainLane.DeferredNode.IsSet());
	Root = RootNode;
	OwnerState = InOwnerState;
	OwnerToken = InOwnerToken;
	bFinished = false;
	PendingTerminal.Reset();
	FLaneScope Scope(*this, Main());
	StartNode(Root);
}

const FADSKNodeDefinition& FADSKProcedure::NodeDef(const FADSKActiveNode& Active) const
{
	return Owner.Definition->GetNodes()[Active.Node];
}

FADSKNodeContext FADSKProcedure::MakeContext(FADSKActiveNode& Active, float Delta)
{
	const FADSKNodeDefinition& Node = NodeDef(Active);
	const FADSKNodeDescriptor& Descriptor = *Node.Descriptor;
	FInstancedStruct& State = Owner.NodeStates[Active.Node];
	return FADSKNodeContext({
		.ConfigStruct = Node.Config.GetScriptStruct(),
		.Config = Node.Config.GetMemory(),
		.MemoryStruct = Active.Memory.GetScriptStruct(),
		.Memory = Active.Memory.GetMutableMemory(),
		.StateStruct = State.GetScriptStruct(),
		.State = State.GetMutableMemory(),
		.Ports = Descriptor.Ports,
		.PortSlots = Owner.Definition->GetPortBindings().Slice(Node.FirstPortBinding, Node.PortCount),
		.Args = Owner.Definition->GetNodeArgs().Slice(Node.FirstArg, Node.ArgCount),
		.Slots = Owner.Slots,
		.Delta = Delta,
		.Handle = {Owner.Generation, Active.ActivationId},
		.NodeId = Node.Id,
		.Host = Owner.Host,
		.Random = &Owner.Random,
		.Time = Owner.Clock,
		.ChildCount = Descriptor.Kind == EADSKNodeKind::Composite ? Node.ChildCount : 0,
		.SlotSink = Owner.Recorder ? &Owner : nullptr,
		.Node = Active.Node,
	});
}

bool FADSKProcedure::HasCallbacks(const FADSKNodeDefinition& Node)
{
	return Node.Descriptor->Kind == EADSKNodeKind::Leaf || Node.Descriptor->Kind == EADSKNodeKind::Scope || Node.Descriptor->Kind == EADSKNodeKind::Composite;
}

FADSKLane* FADSKProcedure::FindLane(int32 Id) const
{
	for (const TUniquePtr<FADSKLane>& Candidate : Lanes)
	{
		if (Candidate->Id == Id)
		{
			return Candidate.Get();
		}
	}
	return nullptr;
}

FADSKActivationHandle FADSKProcedure::TopLeafHandle(const FADSKLane& InLane) const
{
	if (InLane.Stack.IsEmpty() || NodeDef(InLane.Stack.Last()).Descriptor->Kind != EADSKNodeKind::Leaf)
	{
		return {};
	}
	return {Owner.Generation, InLane.Stack.Last().ActivationId};
}

FADSKLane* FADSKProcedure::FindLeafLane(FADSKActivationHandle Handle) const
{
	for (const TUniquePtr<FADSKLane>& Candidate : Lanes)
	{
		if (Handle.IsValid() && TopLeafHandle(*Candidate) == Handle)
		{
			return Candidate.Get();
		}
	}
	return nullptr;
}

void FADSKProcedure::EndTop(EADSKEndReason Reason, EADSKNodeStatus Status)
{
	check(!Lane->Stack.IsEmpty());
	FADSKActiveNode& Active = Lane->Stack.Last();
	if (Active.BackgroundLane != INDEX_NONE)
	{
		CancelLane(Active.BackgroundLane);
		Active.BackgroundLane = INDEX_NONE;
	}
	// Each child lane cancelled deepest first, in document order. Copied: cancelling never touches Active's lane.
	const TArray<int32> ChildLanes = MoveTemp(Active.ChildLanes);
	for (const int32 Id : ChildLanes)
	{
		CancelLane(Id);
	}
	const FADSKNodeDefinition& Node = NodeDef(Active);
	if (HasCallbacks(Node) && Node.Descriptor->OnEnd)
	{
		FADSKNodeContext Context = MakeContext(Active, 0.f);
		FADSKCallbackScope Scope(Owner);
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*Node.Descriptor->Name, AdamskAIChannel);
		Node.Descriptor->OnEnd(Context, Reason);
	}
	EADSKRecordReason RecordReason = EADSKRecordReason::None;
	switch (Reason)
	{
	case EADSKEndReason::Succeeded: RecordReason = EADSKRecordReason::Succeeded; break;
	case EADSKEndReason::Failed: RecordReason = EADSKRecordReason::Failed; break;
	case EADSKEndReason::Cancelled: RecordReason = EADSKRecordReason::Cancelled; break;
	case EADSKEndReason::PartialEntry: RecordReason = EADSKRecordReason::PartialEntry; break;
	}
	Owner.Record({.Kind = EADSKRecordKind::NodeEnded, .Reason = RecordReason, .Node = Active.Node, .Handle = {Owner.Generation, Active.ActivationId}, .Status = Status});
	Lane->Stack.Pop(EAllowShrinking::No);
}

void FADSKProcedure::FinishTop(EADSKNodeStatus Status, EADSKEndReason OverrideReason, bool bUseOverride)
{
	check(Status != EADSKNodeStatus::Running);
	EADSKEndReason Reason = Status == EADSKNodeStatus::Succeeded ? EADSKEndReason::Succeeded : EADSKEndReason::Failed;
	if (bUseOverride)
	{
		Reason = OverrideReason;
	}
	EndTop(Reason, Status);
	Propagate(Status);
}

void FADSKProcedure::CompleteRoot(EADSKNodeStatus Status)
{
	bFinished = true;
	Root = INDEX_NONE;
	Main().DeferredNode.Reset();
	if (!Owner.Completion.IsSet())
	{
		Owner.QueueCompletion(Status, OwnerState, OwnerToken);
	}
	else
	{
		// Keep no second event slot: publish after the earlier scoped completion is removed.
		PendingTerminal = Status;
	}
}

void FADSKProcedure::ApplyResult(FADSKActivationHandle Handle, EADSKNodeStatus Status)
{
	FADSKLane* LeafLane = FindLeafLane(Handle);
	check(LeafLane);
	FLaneScope Scope(*this, *LeafLane);
	FinishTop(Status);
}

void FADSKProcedure::Step(float Delta)
{
	if (PendingTerminal.IsSet() && !Owner.Completion.IsSet())
	{
		Owner.QueueCompletion(PendingTerminal.GetValue(), OwnerState, OwnerToken);
		PendingTerminal.Reset();
		return;
	}
	{
		FLaneScope Scope(*this, Main());
		StepLane(Delta);
	}
	// Background lanes in start order; one that a lane stepped before cancelled is skipped.
	TArray<int32, TInlineAllocator<4>> Ids;
	for (int32 Index = 1; Index < Lanes.Num(); ++Index)
	{
		Ids.Add(Lanes[Index]->Id);
	}
	for (const int32 Id : Ids)
	{
		FADSKLane* Background = FindLane(Id);
		if (Owner.bStopRequested)
		{
			return;
		}
		if (Background)
		{
			FLaneScope Scope(*this, *Background);
			StepLane(Delta);
		}
	}
}

float FADSKProcedure::GetNextWake()
{
	if (PendingTerminal.IsSet())
	{
		return 0.f;
	}
	float Wake = ADSK::NeverWake;
	auto Ask = [this, &Wake](FADSKActiveNode& Node, const FADSKNodeDescriptor& Descriptor)
	{
		if (!Descriptor.OnNextWake)
		{
			Wake = 0.f;
			return;
		}
		FADSKNodeContext Context = MakeContext(Node, 0.f);
		FADSKCallbackScope Scope(Owner);
		Wake = FMath::Min(Wake, FMath::Max(0.f, Descriptor.OnNextWake(Context)));
	};
	for (const TUniquePtr<FADSKLane>& Candidate : Lanes)
	{
		if (Candidate->RestartTick != 0 || Candidate->DeferredNode.IsSet())
		{
			return 0.f;
		}
		FLaneScope Scope(*this, *Candidate);
		for (FADSKActiveNode& Active : Candidate->Stack)
		{
			const FADSKNodeDefinition& Node = NodeDef(Active);
			const FADSKNodeDescriptor& Descriptor = *Node.Descriptor;
			if (Descriptor.SelectorPolicy == EADSKSelectorPolicy::Reactive)
			{
				// Guards change with slots only; any other earlier option is checked by its OnCheck.
				for (int32 Child = 0; Child < Active.ChildCursor; ++Child)
				{
					FADSKActiveNode Option;
					Option.Node = Owner.Definition->GetNodeChildren()[Node.FirstChild + Child];
					const FADSKNodeDescriptor& OptionDescriptor = *NodeDef(Option).Descriptor;
					if (OptionDescriptor.Kind != EADSKNodeKind::Guard && OptionDescriptor.OnCheck)
					{
						Ask(Option, OptionDescriptor);
					}
				}
			}
			else if (Descriptor.SelectorPolicy == EADSKSelectorPolicy::Utility)
			{
				const FADSKUtilityConfig& Config = *Node.Config.GetPtr<FADSKUtilityConfig>();
				if (Config.Reevaluate > 0.f)
				{
					const double Since = Owner.Clock - SelectorMemory<FADSKUtilitySelectorMemory>(Active).LastScored;
					Wake = FMath::Min(Wake, FMath::Max(0.f, static_cast<float>(Config.Reevaluate - Since)));
				}
			}
			if (Descriptor.OnUpdate && Descriptor.Kind != EADSKNodeKind::Guard)
			{
				Ask(Active, Descriptor);
			}
			if (Wake <= 0.f)
			{
				return 0.f;
			}
		}
	}
	return Wake;
}

void FADSKProcedure::StepLane(float Delta)
{
	if (Lane->RestartTick != 0)
	{
		if (Lane->RestartTick <= Owner.TickCount)
		{
			Lane->RestartTick = 0;
			StartNode(Lane->Root);
		}
		return;
	}
	if (Lane->DeferredNode.IsSet())
	{
		// Taken before the wrappers run: one that ends or switches drops this start, but a start it defers itself stays.
		const int32 Node = Lane->DeferredNode.GetValue();
		Lane->DeferredNode.Reset();
		Lane->bDeferredRecorded = false;
		if (UpdateWrappers(Delta))
		{
			StartNode(Node);
		}
		return;
	}
	if (Lane->Stack.IsEmpty() || !UpdateWrappers(Delta) || Lane->Stack.IsEmpty())
	{
		return;
	}
	FADSKActiveNode& Active = Lane->Stack.Last();
	const FADSKNodeDefinition& Node = NodeDef(Active);
	if (Node.Descriptor->Kind != EADSKNodeKind::Leaf || Active.StartedTick == Owner.TickCount)
	{
		return;
	}
	FADSKNodeContext Context = MakeContext(Active, Delta);
	EADSKNodeStatus Status = EADSKNodeStatus::Running;
	if (Node.Descriptor->OnUpdate)
	{
		FADSKCallbackScope Scope(Owner);
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*Node.Descriptor->Name, AdamskAIChannel);
		Status = Node.Descriptor->OnUpdate(Context, Delta);
	}
	Active.bPartiallyEntered = Active.bPartiallyEntered || Context.IsPartiallyEntered();
	if (Status != EADSKNodeStatus::Running)
	{
		FinishTop(Status);
	}
}

void FADSKProcedure::CancelFrom(int32 FirstIndex)
{
	while (Lane->Stack.Num() > FirstIndex)
	{
		EndTop(EADSKEndReason::Cancelled, EADSKNodeStatus::Failed);
	}
}

void FADSKProcedure::CancelLane(int32 Id)
{
	FADSKLane* Background = FindLane(Id);
	if (!Background)
	{
		return;
	}
	{
		FLaneScope Scope(*this, *Background);
		CancelFrom(0);
	}
	const int32 Index = Lanes.IndexOfByPredicate([Id](const TUniquePtr<FADSKLane>& Candidate) { return Candidate->Id == Id; });
	TUniquePtr<FADSKLane> Spare = MoveTemp(Lanes[Index]);
	Lanes.RemoveAt(Index, EAllowShrinking::No);
	*Spare = FADSKLane();
	SpareLanes.Add(MoveTemp(Spare));
}

void FADSKProcedure::Cancel()
{
	{
		FLaneScope Scope(*this, Main());
		Main().DeferredNode.Reset();
		CancelFrom(0);
	}
	PendingTerminal.Reset();
	Lanes.SetNum(1);
	Root = INDEX_NONE;
	OwnerState = INDEX_NONE;
	OwnerToken = 0;
	bFinished = true;
	Main().bDeferredRecorded = false;
}

FADSKActivationHandle FADSKProcedure::GetDeepestLeafHandle() const
{
	return TopLeafHandle(Main());
}

void FADSKProcedure::GetDeepestNodes(TArray<int32>& OutNodes, TArray<FADSKActivationHandle>& OutLeafHandles) const
{
	OutNodes.Reset();
	OutLeafHandles.Reset();
	for (const TUniquePtr<FADSKLane>& Candidate : Lanes)
	{
		if (!Candidate->Stack.IsEmpty())
		{
			OutNodes.Add(Candidate->Stack.Last().Node);
			OutLeafHandles.Add(TopLeafHandle(*Candidate));
		}
	}
}

bool FADSKProcedure::IsCurrentRunningLeaf(FADSKActivationHandle Handle) const
{
	return FindLeafLane(Handle) != nullptr;
}
