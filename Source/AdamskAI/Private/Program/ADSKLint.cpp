// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Program/ADSKLint.h"

#include "Program/ADSKDefinition.h"
#include "Algo/StableSort.h"

namespace ADSKLintPrivate
{
	struct FReach
	{
		TBitArray<> Reached;
		/** States whose entry children are already followed: reached as an ancestor alone does not enter them. */
		TBitArray<> Expanded;
		TArray<int32> Pending;
	};

	/** Entering a state enters its entry children (its initial one and, under select, every scored one); its ancestors are active already or entered with it. */
	void Enter(const FADSKDefinition& Definition, int32 State, FReach& Reach)
	{
		for (int32 Ancestor = State; Ancestor != INDEX_NONE && !Reach.Reached[Ancestor]; Ancestor = Definition.GetStates()[Ancestor].Parent)
		{
			Reach.Reached[Ancestor] = true;
			Reach.Pending.Add(Ancestor);
		}
		if (Reach.Expanded[State]) return;
		Reach.Expanded[State] = true;
		const FADSKStateDefinition& Parent = Definition.GetStates()[State];
		for (int32 Offset = 0; Offset < Parent.ChildCount; ++Offset)
		{
			const int32 Child = Definition.GetStateChildren()[Parent.FirstChild + Offset];
			if (Child == Parent.Initial || (Parent.Select != EADSKStateSelect::Initial && Definition.GetStates()[Child].bScored))
			{
				Enter(Definition, Child, Reach);
			}
		}
	}

	void MarkRead(const FADSKCondition& Condition, TBitArray<>& Read)
	{
		if (Condition.Slot != INDEX_NONE) Read[Condition.Slot] = true;
		if (Condition.OperandSlot != INDEX_NONE) Read[Condition.OperandSlot] = true;
	}
}

using namespace ADSKLintPrivate;

TArray<FADSKLintNote> ADSK::FindLintNotes(const FADSKDefinition& Definition)
{
	TArray<FADSKLintNote> Notes;
	const TConstArrayView<FADSKStateDefinition> States = Definition.GetStates();
	const TConstArrayView<FADSKTransitionDefinition> Transitions = Definition.GetTransitions();

	if (!States.IsEmpty())
	{
		FReach Reach;
		Reach.Reached.Init(false, States.Num());
		Reach.Expanded.Init(false, States.Num());
		Enter(Definition, 0, Reach);
		while (!Reach.Pending.IsEmpty())
		{
			const FADSKStateDefinition& State = States[Reach.Pending.Pop(EAllowShrinking::No)];
			for (int32 Index = 0; Index < State.TransitionCount; ++Index)
			{
				const int32 Target = Transitions[State.FirstTransition + Index].Target;
				if (Target != INDEX_NONE) Enter(Definition, Target, Reach);
			}
		}
		for (int32 State = 0; State < States.Num(); ++State)
		{
			if (!Reach.Reached[State])
			{
				Notes.Add({TEXT("unreachable-state"), States[State].Line,
					FString::Printf(TEXT("state '%s' is never entered: no initial chain, select or transition enters it"), *States[State].Id)});
			}
		}
	}

	const TConstArrayView<FADSKEventDefinition> Events = Definition.GetEvents();
	for (int32 Event = 0; Event < Events.Num(); ++Event)
	{
		if (!Events[Event].bReserved && !Transitions.ContainsByPredicate([Event](const FADSKTransitionDefinition& T) { return T.Event == Event; }))
		{
			Notes.Add({TEXT("unhandled-event"), Events[Event].Line,
				FString::Printf(TEXT("event '%s' has no On handler anywhere, so raising it does nothing"), *Events[Event].Name)});
		}
	}

	const TConstArrayView<FADSKSlotDefinition> Slots = Definition.GetSlots();
	TBitArray<> Read(false, Slots.Num());
	for (const FADSKNodeDefinition& Node : Definition.GetNodes())
	{
		for (int32 Port = 0; Port < Node.PortCount; ++Port)
		{
			if (Node.Descriptor->Ports[Port].Direction == EADSKPortDirection::In) Read[Definition.GetPortBindings()[Node.FirstPortBinding + Port]] = true;
		}
		MarkRead(Node.Condition, Read);
		if (Node.Assign.Source != INDEX_NONE) Read[Node.Assign.Source] = true;
	}
	for (const FADSKTransitionDefinition& Transition : Transitions)
	{
		MarkRead(Transition.Condition, Read);
		if (Transition.ChanceSlot != INDEX_NONE) Read[Transition.ChanceSlot] = true;
	}
	for (const FADSKCondition& Term : Definition.GetConditionTerms()) MarkRead(Term, Read);
	for (const FADSKStateDefinition& State : States)
	{
		if (State.ScoreSlot != INDEX_NONE) Read[State.ScoreSlot] = true;
	}
	for (const FADSKEventDefinition& Event : Events)
	{
		if (Event.ChangeSlot != INDEX_NONE) Read[Event.ChangeSlot] = true;
	}
	for (const FADSKEmitArgDefinition& Arg : Definition.GetEmitArgs())
	{
		if (Arg.Slot != INDEX_NONE) Read[Arg.Slot] = true;
	}
	for (const FADSKNodeArg& Arg : Definition.GetNodeArgs())
	{
		Read[Arg.Slot] = true;
	}
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (!Read[Slot])
		{
			const bool bInput = Slots[Slot].bIsInput;
			Notes.Add({bInput ? TEXT("unread-input") : TEXT("unread-local"), Slots[Slot].Line,
				FString::Printf(TEXT("%s '%s' is never read by a port, condition, change event, Emit, Param or Set"), bInput ? TEXT("Input") : TEXT("Local"), *Slots[Slot].Name)});
		}
	}
	// Code order, then declaration order within a code.
	Algo::StableSortBy(Notes, [](const FADSKLintNote& Note)
	{
		const FStringView Code(Note.Code);
		return Code == TEXT("unreachable-state") ? 0 : Code == TEXT("unhandled-event") ? 1 : Code == TEXT("unread-local") ? 2 : 3;
	});
	return Notes;
}
