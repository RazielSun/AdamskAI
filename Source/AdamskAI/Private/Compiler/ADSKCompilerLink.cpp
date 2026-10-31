// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Compiler/ADSKCompilerInternal.h"

#include "Algo/AllOf.h"

#include <atomic>

using namespace ADSKCompilerInternal;

namespace ADSKCompilerLinkPrivate
{
	void LinkSlotsAndEvents(const FCompileContext& Context,
		TArray<FADSKSlotDefinition>& Slots, TArray<FADSKEventDefinition>& Events, TArray<FADSKEventParamDefinition>& Params)
	{
		for (const FSlot& Slot : Context.Slots)
		{
			// Tree parameters come after every Input and Local and were replaced by Run expansion.
			if (Slot.FormalTree != INDEX_NONE)
			{
				break;
			}
			FADSKSlotDefinition& Out = Slots.AddDefaulted_GetRef();
			Out.Name = Slot.Name;
			Out.Type = Slot.Type;
			Out.bIsInput = Slot.bInput;
			Out.bRequired = Slot.bRequired;
			Out.Default = Slot.Default;
			Out.Enum = Slot.Enum;
			Out.Line = Slot.Line;
		}
		for (const FEventInfo& Event : Context.Events)
		{
			Events.Add({Event.Name, Event.Priority, Params.Num(), Event.Params.Num(), Event.bReserved, Event.ChangeSlot, Event.Line});
			for (const FParam& Param : Event.Params)
			{
				Params.Add({Param.Name, Param.Type, Param.Enum});
			}
		}
	}

	void LinkStates(const FCompileContext& Context, TArray<FADSKStateDefinition>& States, TArray<int32>& Children,
		TArray<FADSKTransitionDefinition>& Transitions, TArray<FADSKBindDefinition>& Binds, TArray<FName>& Tags, TArray<FADSKCondition>& Terms)
	{
		for (int32 Index = 0; Index < Context.States.Num(); ++Index)
		{
			const FState& State = Context.States[Index];
			FADSKStateDefinition& Linked = States.AddDefaulted_GetRef();
			Linked.Id = State.Id;
			Linked.FirstTag = Tags.Num();
			Linked.TagCount = State.Tags.Num();
			Tags.Append(State.Tags);
			Linked.Parent = State.Parent;
			Linked.FirstChild = Children.Num();
			Linked.ChildCount = State.Children.Num();
			Children.Append(State.Children);
			Linked.Initial = State.InitialState;
			Linked.Depth = State.Depth;
			Linked.RootNode = State.RootNode;
			Linked.Line = State.Line;
			Linked.Select = State.Select;
			Linked.History = State.History;
			Linked.bScored = State.bScored;
			Linked.ScoreSlot = State.ScoreSlot;
			Linked.ScoreWeight = State.ScoreWeight;
			Linked.ScoreBias = State.ScoreBias;
			Linked.FirstTransition = Transitions.Num();
			Linked.TransitionCount = State.Transitions.Num();
			for (const FTransition& Transition : State.Transitions)
			{
				FADSKTransitionDefinition& Out = Transitions.AddDefaulted_GetRef();
				Out.Source = Index;
				Out.Event = Transition.Event;
				Out.Target = Transition.bHasGoto ? Transition.Target : INDEX_NONE;
				Out.FirstBind = Binds.Num();
				Out.BindCount = Transition.Binds.Num();
				Out.Condition = Transition.Condition;
				Out.FirstTerm = Terms.Num();
				Out.TermCount = Transition.Terms.Num();
				Terms.Append(Transition.Terms);
				Out.Chance = Transition.Chance;
				Out.ChanceSlot = Transition.ChanceSlot;
				Out.Line = Transition.Line;
				for (const FBind& Bind : Transition.Binds)
				{
					Binds.Add({Bind.Param, Bind.Slot});
				}
			}
		}
	}

	void LinkNodes(const FCompileContext& Context, TArray<FADSKNodeDefinition>& Nodes, TArray<int32>& Children, TArray<int32>& PortBindings,
		TArray<FADSKEmitArgDefinition>& EmitArgs, TArray<FADSKNodeArg>& Args, TArray<FADSKCondition>& Terms)
	{
		for (const FNode& Node : Context.Nodes)
		{
			FADSKNodeDefinition& Linked = Nodes.AddDefaulted_GetRef();
			Linked.Descriptor = Node.Descriptor;
			Linked.Id = FName(*Node.Id);
			Linked.AuthoredId = Node.Id;
			Linked.Line = Node.Line;
			Linked.Parent = Node.Parent;
			Linked.FirstChild = Children.Num();
			Linked.ChildCount = Node.Children.Num();
			Children.Append(Node.Children);
			Linked.FirstPortBinding = PortBindings.Num();
			Linked.PortCount = Node.PortSlots.Num();
			Linked.EmitEvent = Node.EmitEvent;
			Linked.FirstEmitArg = EmitArgs.Num();
			Linked.EmitArgCount = Node.EmitBindings.Num();
			for (const FEmitBinding& Binding : Node.EmitBindings)
			{
				EmitArgs.Add({Binding.Slot, Binding.Value});
			}
			Linked.Assign = Node.Assign;
			Linked.FirstArg = Args.Num();
			Linked.ArgCount = Node.Args.Num();
			Args.Append(Node.Args);
			if (Node.Descriptor->Kind == EADSKNodeKind::Guard)
			{
				Linked.Condition = Node.Condition;
				const int32 Port = Node.Descriptor->FindPort(TEXT("condition"));
				Linked.Condition.Slot = Port == INDEX_NONE ? INDEX_NONE : Node.PortSlots[Port];
			}
			Linked.FirstTerm = Terms.Num();
			Linked.TermCount = Node.Terms.Num();
			Terms.Append(Node.Terms);
			PortBindings.Append(Node.PortSlots);
			Linked.Config = Node.Config;
		}
	}
}

using namespace ADSKCompilerLinkPrivate;

TSharedRef<const FADSKDefinition> ADSKLink::FLinker::Link(const FCompileContext& Context)
{
	TSharedRef<FADSKDefinition> Definition = MakeShareable(new FADSKDefinition(Context.Registry));
	Definition->SourceName = Context.Source;
	Definition->AgentId = Context.AgentId;
	Definition->SourceHash = Context.Document.SourceHash;
	Definition->CatalogueFingerprint = Context.Registry->GetCatalogueFingerprint();
	uint8 CompositeBytes[sizeof(Definition->SourceHash.Hash) + sizeof(Definition->CatalogueFingerprint.Hash)];
	FMemory::Memcpy(CompositeBytes, Definition->SourceHash.Hash, sizeof(Definition->SourceHash.Hash));
	FMemory::Memcpy(CompositeBytes + sizeof(Definition->SourceHash.Hash), Definition->CatalogueFingerprint.Hash,
		sizeof(Definition->CatalogueFingerprint.Hash));
	FSHA1::HashBuffer(CompositeBytes, sizeof(CompositeBytes), Definition->CompositeHash.Hash);
	Definition->LanguageVersion = 1;
	static std::atomic<uint64> NextSerial{0};
	Definition->Serial = ++NextSerial;
	LinkSlotsAndEvents(Context, Definition->Slots, Definition->Events, Definition->EventParams);
	for (const FEnum& Enum : Context.Enums)
	{
		FADSKEnumDefinition& Out = Definition->Enums.AddDefaulted_GetRef();
		Out.Name = Enum.Name;
		Out.FirstValue = Definition->EnumValueNames.Num();
		Out.ValueCount = Enum.Values.Num();
		Definition->EnumValueNames.Append(Enum.Values);
	}
	LinkStates(Context, Definition->States, Definition->StateChildren, Definition->Transitions, Definition->Binds, Definition->StateTags,
		Definition->ConditionTerms);
	LinkNodes(Context, Definition->Nodes, Definition->NodeChildren, Definition->PortBindings, Definition->EmitArgs, Definition->NodeArgs,
		Definition->ConditionTerms);
	for (const FTree& Tree : Context.Trees)
	{
		Definition->Trees.Add({Tree.Id, Tree.Root, Tree.Line});
	}
	for (int32 Index = 0; Index < Context.Nodes.Num(); ++Index)
	{
		if (Context.Nodes[Index].bSensor)
		{
			Definition->Sensors.Add(Index);
		}
	}
	Definition->bThreadSafe = Algo::AllOf(Definition->Nodes, [](const FADSKNodeDefinition& Node)
	{
		const EADSKNodeKind Kind = Node.Descriptor->Kind;
		const bool bIntrinsic = Kind == EADSKNodeKind::Sequence || Kind == EADSKNodeKind::Selector || Kind == EADSKNodeKind::Guard
			|| Kind == EADSKNodeKind::Emit || Kind == EADSKNodeKind::Parallel || Kind == EADSKNodeKind::Set
			|| Kind == EADSKNodeKind::Concurrent;
		// The host samples sensors on the game thread, outside the instance's tick.
		return (bIntrinsic && !Node.Descriptor->OnScore) || Node.Descriptor->bThreadSafe || Kind == EADSKNodeKind::Sensor;
	});
	return Definition;
}
