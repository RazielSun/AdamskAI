// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKInstanceInternal.h"
#include "Trace/ADSKInsights.h"
#include "Trace/ADSKStats.h"
#include "Algo/Compare.h"

void FADSKInstance::FImpl::EnterState(int32 State)
{
	check(Definition->GetStates().IsValidIndex(State));
	ActivePath.Add(State);
	ActiveTokens.Add(NextStateToken++);
	uint64 Region = 0;
	if (ADSK_TRACE_ENABLED())
	{
		Region = TRACE_BEGIN_REGION_WITH_ID(*FString::Printf(TEXT("%s %s"), *TraceName(), *Definition->GetStates()[State].Id), TEXT("AdamskAI"));
	}
	ActiveRegions.Add(Region);
	Record({.Kind = EADSKRecordKind::StateEntered, .State = State});
}

void FADSKInstance::FImpl::ExitTopState()
{
	const int32 State = ActivePath.Last();
	// States leave leaf first: the one popped just before is this state's child only when this state leaves with it.
	if (LastExited != INDEX_NONE && Definition->GetStates()[LastExited].Parent == State)
	{
		LastChild[State] = LastExited;
	}
	LastExited = State;
	Record({.Kind = EADSKRecordKind::StateExited, .State = State});
	ActivePath.Pop(EAllowShrinking::No);
	ActiveTokens.Pop(EAllowShrinking::No);
	if (const uint64 Region = ActiveRegions.Pop(EAllowShrinking::No))
	{
		TRACE_END_REGION_WITH_ID(Region);
	}
}

void FADSKInstance::FImpl::EnterInitialConfiguration()
{
	check(!Definition->GetStates().IsEmpty());
	EnterState(0);
	EnterEntryChain(true);
	RefreshActiveTags();
	StartLeafProcedure();
}

void FADSKInstance::FImpl::EnterEntryChain(bool bJustEntered)
{
	bool bDeep = false;
	for (int32 Child = ActivePath.IsEmpty() ? INDEX_NONE : EntryChild(ActivePath.Last(), bJustEntered, bDeep); Child != INDEX_NONE;
		Child = EntryChild(Child, true, bDeep))
	{
		EnterState(Child);
	}
}

int32 FADSKInstance::FImpl::EntryChild(int32 State, bool bJustEntered, bool& bInOutDeep)
{
	const TConstArrayView<FADSKStateDefinition> States = Definition->GetStates();
	const FADSKStateDefinition& Parent = States[State];
	// A state that stayed active while its children changed (a goto to it from inside) is not re-entered: no history.
	if (bJustEntered && LastChild[State] != INDEX_NONE && (bInOutDeep || Parent.History != EADSKStateHistory::None))
	{
		bInOutDeep = bInOutDeep || Parent.History == EADSKStateHistory::Deep;
		Record({.Kind = EADSKRecordKind::StateSelected, .Reason = EADSKRecordReason::History, .State = LastChild[State]});
		return LastChild[State];
	}
	if (Parent.Select == EADSKStateSelect::Initial)
	{
		return Parent.Initial;
	}
	// Scores read the slots as they are now: after the Binds of the transition that enters.
	TArray<float, TInlineAllocator<FADSKCompiler::MaxStates>> Scores;
	float Total = 0.f;
	int32 Best = INDEX_NONE;
	for (int32 Offset = 0; Offset < Parent.ChildCount; ++Offset)
	{
		const FADSKStateDefinition& Child = States[Definition->GetStateChildren()[Parent.FirstChild + Offset]];
		float Score = 0.f;
		if (Child.bScored)
		{
			float Value = 1.f;
			if (Child.ScoreSlot != INDEX_NONE)
			{
				const FADSKValue& Slot = Slots[Child.ScoreSlot];
				Value = Slot.Type == EADSKValueType::Int ? static_cast<float>(Slot.Int) : Slot.Float;
			}
			Score = Child.ScoreWeight * Value + Child.ScoreBias;
			Score = FMath::IsFinite(Score) && Score > 0.f ? Score : 0.f;
		}
		Scores.Add(Score);
		Total += Score;
		if (Score > 0.f && (Best == INDEX_NONE || Score > Scores[Best]))
		{
			Best = Offset;
		}
	}
	int32 Chosen = Best;
	// Scores past the float range sum to infinity, where a draw cannot pick; the best one takes it then.
	if (Parent.Select == EADSKStateSelect::Weighted && Best != INDEX_NONE && FMath::IsFinite(Total))
	{
		// Rounding can leave the draw past the last cumulative score; the last scoring child takes it then.
		float Draw = Random.FRand() * Total;
		for (int32 Offset = 0; Offset < Scores.Num(); ++Offset)
		{
			if (Scores[Offset] <= 0.f) continue;
			Chosen = Offset;
			Draw -= Scores[Offset];
			if (Draw < 0.f) break;
		}
	}
	if (Chosen == INDEX_NONE)
	{
		Record({.Kind = EADSKRecordKind::StateSelected, .Reason = EADSKRecordReason::NoScore, .State = Parent.Initial});
		return Parent.Initial;
	}
	const int32 ChildState = Definition->GetStateChildren()[Parent.FirstChild + Chosen];
	Record({.Kind = EADSKRecordKind::StateSelected, .State = ChildState, .bHasValue = true, .Value = FADSKValue::MakeFloat(Scores[Chosen])});
	return ChildState;
}

void FADSKInstance::FImpl::ExitAllStates()
{
	LastExited = INDEX_NONE;
	while (!ActivePath.IsEmpty())
	{
		ExitTopState();
	}
	RefreshActiveTags();
}

void FADSKInstance::FImpl::RefreshActiveTags()
{
	const TConstArrayView<FName> AllTags = Definition->GetStateTags();
	if (AllTags.IsEmpty() && ActiveTags.IsEmpty()) return;
	TArray<FName, TInlineAllocator<8>> Union;
	for (const int32 State : ActivePath)
	{
		const FADSKStateDefinition& StateDefinition = Definition->GetStates()[State];
		for (const FName Tag : AllTags.Slice(StateDefinition.FirstTag, StateDefinition.TagCount)) Union.AddUnique(Tag);
	}
	if (Union.Num() == ActiveTags.Num() && Algo::Compare(Union, ActiveTags)) return;
	ActiveTags.Reset();
	ActiveTags.Append(Union);
	++ActiveTagsSerial;
}

int32 FADSKInstance::FImpl::FindActiveStatePosition(int32 State) const
{
	return ActivePath.Find(State);
}

bool FADSKInstance::FImpl::IsStateTokenActive(int32 State, uint64 Token) const
{
	const int32 Position = FindActiveStatePosition(State);
	return Position != INDEX_NONE && ActiveTokens[Position] == Token;
}

bool FADSKInstance::FImpl::IsAncestorOrSelf(int32 Ancestor, int32 State) const
{
	while (State != INDEX_NONE)
	{
		if (State == Ancestor)
		{
			return true;
		}
		State = Definition->GetStates()[State].Parent;
	}
	return false;
}

int32 FADSKInstance::FImpl::LeastCommonAncestor(int32 A, int32 B) const
{
	const TConstArrayView<FADSKStateDefinition> States = Definition->GetStates();
	while (States[A].Depth > States[B].Depth)
	{
		A = States[A].Parent;
	}
	while (States[B].Depth > States[A].Depth)
	{
		B = States[B].Parent;
	}
	while (A != B)
	{
		A = States[A].Parent;
		B = States[B].Parent;
	}
	return A;
}

int32 FADSKInstance::FImpl::FindTransition(int32 Event)
{
	const TConstArrayView<FADSKStateDefinition> States = Definition->GetStates();
	const TConstArrayView<FADSKTransitionDefinition> Transitions = Definition->GetTransitions();
	for (int32 Position = ActivePath.Num() - 1; Position >= 0; --Position)
	{
		const FADSKStateDefinition& State = States[ActivePath[Position]];
		for (int32 Offset = 0; Offset < State.TransitionCount; ++Offset)
		{
			const int32 Index = State.FirstTransition + Offset;
			const FADSKTransitionDefinition& Transition = Transitions[Index];
			if (Transition.Event != Event) continue;
			if (!ADSK::EvaluateTransitionCondition(*Definition, Transition, Slots)) continue;
			// Drawn only for a handler whose condition passed, so a file without chance never touches the stream.
			const float Chance = Transition.ChanceSlot != INDEX_NONE ? FMath::Clamp(Slots[Transition.ChanceSlot].Float, 0.f, 1.f) : Transition.Chance;
			if (Chance < 1.f && Random.FRand() >= Chance) continue;
			return Index;
		}
	}
	return INDEX_NONE;
}

void FADSKInstance::FImpl::StartLeafProcedure()
{
	if (ActivePath.IsEmpty())
	{
		return;
	}
	const int32 State = ActivePath.Last();
	const int32 RootNode = Definition->GetStates()[State].RootNode;
	if (RootNode != INDEX_NONE)
	{
		Procedure->Start(RootNode, State, ActiveTokens.Last());
	}
}

void FADSKInstance::FImpl::PerformTransition(const FADSKTransitionDefinition& Transition)
{
	if (Transition.Target == INDEX_NONE)
	{
		return;
	}
	const TConstArrayView<FADSKStateDefinition> States = Definition->GetStates();
	const int32 Source = Transition.Source;
	const int32 Target = Transition.Target;
	const int32 Domain = IsAncestorOrSelf(Source, Target) ? States[Source].Parent : LeastCommonAncestor(Source, Target);

	Procedure->Cancel();
	LastExited = INDEX_NONE;
	while (!ActivePath.IsEmpty() && ActivePath.Last() != Domain)
	{
		ExitTopState();
	}

	if (Target != Domain)
	{
		TArray<int32, TInlineAllocator<8>> ToEnter;
		for (int32 State = Target; State != Domain; State = States[State].Parent)
		{
			ToEnter.Add(State);
		}
		Algo::Reverse(ToEnter);
		for (const int32 State : ToEnter)
		{
			EnterState(State);
		}
	}

	EnterEntryChain(Target != Domain);
	bHold = false;
	RefreshActiveTags();
	StartLeafProcedure();
}

void FADSKInstance::FImpl::QueueCompletion(EADSKNodeStatus Status, int32 InOwnerState, uint64 InOwnerToken)
{
	check(Status != EADSKNodeStatus::Running);
	check(!Completion.IsSet());
	FADSKTreeCompletion NewCompletion;
	NewCompletion.Event = Status == EADSKNodeStatus::Succeeded ? ADSK::TreeSuccessEvent : ADSK::TreeFailureEvent;
	NewCompletion.OwnerState = InOwnerState;
	NewCompletion.OwnerToken = InOwnerToken;
	NewCompletion.EnqueueSequence = NextEnqueueSequence++;
	NewCompletion.EligibleTick = TickCount + 1;
	Completion = NewCompletion;
}

void FADSKInstance::FImpl::DispatchEvents()
{
	for (int32 Dispatch = 0; Dispatch < ADSK::EventDispatchBudget && !bStopRequested; ++Dispatch)
	{
		int32 BestExternal = INDEX_NONE;
		if (!SelectNextEvent(BestExternal))
		{
			break;
		}

		int32 Event = INDEX_NONE;
		const FADSKValue* Parameters = nullptr;
		bool bWasCompletion = BestExternal == INDEX_NONE;
		if (bWasCompletion)
		{
			const FADSKTreeCompletion Removed = Completion.GetValue();
			Completion.Reset();
			Event = Removed.Event;
			if (!IsStateTokenActive(Removed.OwnerState, Removed.OwnerToken))
			{
				Record({.Kind = EADSKRecordKind::CompletionStale, .Reason = EADSKRecordReason::StaleResult, .State = Removed.OwnerState, .Event = Event});
				continue;
			}
		}
		else
		{
			Event = TakeExternalEvent(BestExternal);
			Parameters = EventParamValues.GetData();
		}
		HandleEvent(Event, Parameters, bWasCompletion);
	}
}

bool FADSKInstance::FImpl::SelectNextEvent(int32& OutExternal) const
{
	// OutExternal is the queue index of the winner, or INDEX_NONE when the ready completion wins.
	// "Found" is its own flag: every int32 is a legal priority, MIN_int32 included, so no priority
	// value can double as the empty marker.
	const TConstArrayView<FADSKEventDefinition> EventDefinitions = Definition->GetEvents();
	bool bFound = false;
	int32 BestPriority = 0;
	uint64 BestSequence = 0;
	OutExternal = INDEX_NONE;
	const auto Beats = [&](int32 Priority, uint64 Sequence)
	{
		return !bFound || Priority > BestPriority || (Priority == BestPriority && Sequence < BestSequence);
	};
	for (int32 Index = 0; Index < EventCount; ++Index)
	{
		const FADSKQueuedEvent& Candidate = Events[Index];
		if (Candidate.EligibleTick > TickCount) continue;
		const int32 Priority = EventDefinitions[Candidate.Event].Priority;
		if (Beats(Priority, Candidate.EnqueueSequence))
		{
			OutExternal = Index;
			BestPriority = Priority;
			BestSequence = Candidate.EnqueueSequence;
			bFound = true;
		}
	}
	const bool bCompletionReady = Completion.IsSet() && Completion->EligibleTick <= TickCount;
	if (bCompletionReady && Beats(EventDefinitions[Completion->Event].Priority, Completion->EnqueueSequence))
	{
		OutExternal = INDEX_NONE;
		bFound = true;
	}
	return bFound;
}

int32 FADSKInstance::FImpl::TakeExternalEvent(int32 Index)
{
	// Closes the gap so the queue stays a prefix.
	const int32 Event = Events[Index].Event;
	for (int32 Param = 0; Param < Definition->GetEvents()[Event].ParamCount; ++Param)
	{
		EventParamValues[Param] = QueuedParamsOf(Index)[Param];
	}
	for (int32 Move = Index; Move + 1 < EventCount; ++Move)
	{
		Events[Move].Event = Events[Move + 1].Event;
		Events[Move].EnqueueSequence = Events[Move + 1].EnqueueSequence;
		Events[Move].EligibleTick = Events[Move + 1].EligibleTick;
		const int32 Count = Definition->GetEvents()[Events[Move].Event].ParamCount;
		for (int32 Param = 0; Param < Count; ++Param)
		{
			QueuedParamsOf(Move)[Param] = QueuedParamsOf(Move + 1)[Param];
		}
	}
	--EventCount;
	return Event;
}

void FADSKInstance::FImpl::HandleEvent(int32 Event, const FADSKValue* Parameters, bool bWasCompletion)
{
	INC_DWORD_STAT(STAT_ADSKEventsDispatched);
	const int32 TransitionIndex = FindTransition(Event);
	if (TransitionIndex == INDEX_NONE)
	{
		Record({.Kind = EADSKRecordKind::EventUnhandled, .Reason = EADSKRecordReason::NoHandler, .Event = Event});
		if (bWasCompletion && !bHold)
		{
			bHold = true;
			Record({.Kind = EADSKRecordKind::Hold, .Reason = EADSKRecordReason::Hold, .State = ActivePath.IsEmpty() ? INDEX_NONE : ActivePath.Last(), .Event = Event});
			if (ADSK_TRACE_ENABLED())
			{
				TRACE_BOOKMARK(TEXT("AdamskAI hold: %s in %s"), *TraceName(), ActivePath.IsEmpty() ? TEXT("") : *Definition->GetStates()[ActivePath.Last()].Id);
			}
		}
		return;
	}

	const FADSKTransitionDefinition& Transition = Definition->GetTransitions()[TransitionIndex];
	Record({.Kind = EADSKRecordKind::EventConsumed, .State = Transition.Source, .Event = Event});
	// A handler with only `<When>`/`<Any>` terms records its first term's slot.
	const int32 ConditionSlot = Transition.Condition.IsSet() || Transition.TermCount == 0 ? Transition.Condition.Slot
		: Definition->GetConditionTerms()[Transition.FirstTerm].Slot;
	Record({.Kind = EADSKRecordKind::TransitionTaken, .State = Transition.Source, .Event = Event, .Slot = ConditionSlot, .Transition = TransitionIndex,
		.bHasValue = ConditionSlot != INDEX_NONE, .Value = ConditionSlot != INDEX_NONE ? Slots[ConditionSlot] : FADSKValue()});
	for (int32 Offset = 0; Offset < Transition.BindCount; ++Offset)
	{
		const FADSKBindDefinition& Bind = Definition->GetBinds()[Transition.FirstBind + Offset];
		check(Parameters && Bind.Param < Definition->GetEvents()[Event].ParamCount);
		if (Slots[Bind.Slot] != Parameters[Bind.Param])
		{
			Slots[Bind.Slot] = Parameters[Bind.Param];
			Record({.Kind = EADSKRecordKind::SlotWritten, .Event = Event, .Slot = Bind.Slot, .Transition = TransitionIndex, .bHasValue = true, .Value = Slots[Bind.Slot]});
		}
	}
	PerformTransition(Transition);
}
