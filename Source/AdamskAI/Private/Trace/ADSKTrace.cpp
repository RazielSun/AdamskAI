// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Trace/ADSKTrace.h"

#include "Program/ADSKDefinition.h"
#include "Trace/ADSKStats.h"

const TCHAR* LexToString(EADSKRecordKind Kind)
{
	switch (Kind)
	{
	case EADSKRecordKind::InstanceStarted:
		return TEXT("instance-started");
	case EADSKRecordKind::StartFailed:
		return TEXT("start-failed");
	case EADSKRecordKind::InstanceStopped:
		return TEXT("instance-stopped");
	case EADSKRecordKind::InputSet:
		return TEXT("input-set");
	case EADSKRecordKind::EventEnqueued:
		return TEXT("event-enqueued");
	case EADSKRecordKind::InputRejected:
		return TEXT("input-rejected");
	case EADSKRecordKind::EventConsumed:
		return TEXT("event-consumed");
	case EADSKRecordKind::EventUnhandled:
		return TEXT("event-unhandled");
	case EADSKRecordKind::StateEntered:
		return TEXT("state-entered");
	case EADSKRecordKind::StateExited:
		return TEXT("state-exited");
	case EADSKRecordKind::GuardValue:
		return TEXT("guard-value");
	case EADSKRecordKind::NodeStarted:
		return TEXT("node-started");
	case EADSKRecordKind::NodeEnded:
		return TEXT("node-ended");
	case EADSKRecordKind::ResultAccepted:
		return TEXT("result-accepted");
	case EADSKRecordKind::ResultStale:
		return TEXT("result-stale");
	case EADSKRecordKind::ResultDuplicate:
		return TEXT("result-duplicate");
	case EADSKRecordKind::CompletionStale:
		return TEXT("completion-stale");
	case EADSKRecordKind::Hold:
		return TEXT("hold");
	case EADSKRecordKind::BudgetDeferred:
		return TEXT("budget-deferred");
	case EADSKRecordKind::Gap:
		return TEXT("gap");
	case EADSKRecordKind::TransitionTaken:
		return TEXT("transition-taken");
	case EADSKRecordKind::SlotWritten:
		return TEXT("slot-written");
	case EADSKRecordKind::StateSelected:
		return TEXT("state-selected");
	}
	return TEXT("unknown");
}

const TCHAR* LexToString(EADSKRecordReason Reason)
{
	switch (Reason)
	{
	case EADSKRecordReason::None:
		return TEXT("none");
	case EADSKRecordReason::Started:
		return TEXT("started");
	case EADSKRecordReason::Stopped:
		return TEXT("stopped");
	case EADSKRecordReason::QueueFull:
		return TEXT("queue-full");
	case EADSKRecordReason::BudgetDeferred:
		return TEXT("budget-deferred");
	case EADSKRecordReason::NoHandler:
		return TEXT("no-handler");
	case EADSKRecordReason::Hold:
		return TEXT("hold");
	case EADSKRecordReason::DuplicateResult:
		return TEXT("duplicate-result");
	case EADSKRecordReason::StaleResult:
		return TEXT("stale-result");
	case EADSKRecordReason::Cancelled:
		return TEXT("cancelled");
	case EADSKRecordReason::PartialEntry:
		return TEXT("partial-entry");
	case EADSKRecordReason::Succeeded:
		return TEXT("succeeded");
	case EADSKRecordReason::Failed:
		return TEXT("failed");
	case EADSKRecordReason::NoScore:
		return TEXT("no-score");
	case EADSKRecordReason::Restored:
		return TEXT("restored");
	case EADSKRecordReason::History:
		return TEXT("history");
	}
	return TEXT("unknown");
}

TSharedPtr<FADSKRecorder> FADSKRecorder::Create(int32 Capacity)
{
	ADSK_CHECK_GAME_THREAD();
	if (Capacity < 2)
	{
		return nullptr;
	}
	LLM_SCOPE_BYTAG(AdamskAI);
	return MakeShareable(new FADSKRecorder(Capacity));
}

FADSKRecorder::FADSKRecorder(int32 InCapacity)
	: Capacity(InCapacity)
{
	Storage.SetNum(InCapacity);
}

FADSKRecord& FADSKRecorder::At(int32 LogicalIndex)
{
	return Storage[(Head + LogicalIndex) % Capacity];
}

const FADSKRecord& FADSKRecorder::At(int32 LogicalIndex) const
{
	return Storage[(Head + LogicalIndex) % Capacity];
}

void FADSKRecorder::Record(const FADSKRecord& Incoming)
{
	// FADSKParallelTick gives each recorder one worker at most, so the worker ticking its instance may write it.
	checkf(IsInGameThread() || ADSK::IsInParallelTick(), TEXT("AdamskAI is game-thread only outside its parallel tick"));
	if (Count == Capacity)
	{
		// Fold the oldest record and its successor into one Gap stored where the successor was.
		FADSKRecord Gap = At(0);
		if (Gap.Kind == EADSKRecordKind::Gap)
		{
			Gap.LostCount += 1;
		}
		else
		{
			Gap.Kind = EADSKRecordKind::Gap;
			Gap.LostCount = 2;
			Gap.Reason = EADSKRecordReason::None;
			Gap.bHasValue = false;
		}
		Head = (Head + 1) % Capacity;
		At(0) = Gap;
		--Count;
	}
	FADSKRecord& Slot = At(Count);
	Slot = Incoming;
	Slot.Sequence = NextSequence++;
	++Count;
}

TArray<FADSKRecord> FADSKRecorder::Snapshot() const
{
	ADSK_CHECK_GAME_THREAD();
	TArray<FADSKRecord> Result;
	Result.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Result.Add(At(Index));
	}
	return Result;
}

TArray<FADSKRecord> FADSKRecorder::Tail(int32 MaxRecords) const
{
	ADSK_CHECK_GAME_THREAD();
	TArray<FADSKRecord> Result;
	const int32 First = FMath::Max(0, Count - FMath::Max(0, MaxRecords));
	Result.Reserve(Count - First);
	for (int32 Index = First; Index < Count; ++Index)
	{
		Result.Add(At(Index));
	}
	return Result;
}

void FADSKRecorder::Reset()
{
	ADSK_CHECK_GAME_THREAD();
	Head = 0;
	Count = 0;
	NextSequence = 1;
}

uint64 FADSKRecorder::GetTotalLost() const
{
	return Count > 0 && At(0).Kind == EADSKRecordKind::Gap ? At(0).LostCount : 0;
}

FString ADSK::FormatRecord(const FADSKDefinition& Definition, const FADSKRecord& Record,
	TFunctionRef<FString(const FADSKValue&)> ShowValue)
{
	FString Line = FString::Printf(TEXT("#%llu t=%llu %s"), Record.Sequence, Record.Tick, LexToString(Record.Kind));
	if (Record.Reason != EADSKRecordReason::None)
	{
		Line += FString::Printf(TEXT(" reason=%s"), LexToString(Record.Reason));
	}
	if (Record.State != INDEX_NONE)
	{
		Line += FString::Printf(TEXT(" state=%s"), Definition.GetStates().IsValidIndex(Record.State)
			? *Definition.GetStates()[Record.State].Id : *FString::FromInt(Record.State));
	}
	if (Record.Node != INDEX_NONE)
	{
		Line += FString::Printf(TEXT(" node=%s"), Definition.GetNodes().IsValidIndex(Record.Node)
			? *Definition.GetNodes()[Record.Node].AuthoredId : *FString::FromInt(Record.Node));
	}
	if (Record.Handle.IsValid())
	{
		Line += FString::Printf(TEXT(" handle=%u.%u"), Record.Handle.Generation, Record.Handle.ActivationId);
	}
	if (Record.Event != INDEX_NONE)
	{
		Line += FString::Printf(TEXT(" event=%s"), Definition.GetEvents().IsValidIndex(Record.Event)
			? *Definition.GetEvents()[Record.Event].Name : *FString::FromInt(Record.Event));
	}
	if (Record.Transition != INDEX_NONE)
	{
		Line += FString::Printf(TEXT(" transition=%d"), Record.Transition);
		if (Definition.GetTransitions().IsValidIndex(Record.Transition) && Definition.GetStates().IsValidIndex(Definition.GetTransitions()[Record.Transition].Target))
		{
			Line += FString::Printf(TEXT(" target=%s"), *Definition.GetStates()[Definition.GetTransitions()[Record.Transition].Target].Id);
		}
	}
	if (Record.Slot != INDEX_NONE)
	{
		Line += FString::Printf(TEXT(" slot=%s"), Definition.GetSlots().IsValidIndex(Record.Slot)
			? *Definition.GetSlots()[Record.Slot].Name : *FString::FromInt(Record.Slot));
	}
	if (Record.bHasValue)
	{
		Line += FString::Printf(TEXT(" value=%s"), *ShowValue(Record.Value));
	}
	if (Record.LostCount != 0)
	{
		Line += FString::Printf(TEXT(" lost=%llu"), Record.LostCount);
	}
	return Line;
}

FString ADSK::FormatRecord(const FADSKDefinition& Definition, const FADSKRecord& Record)
{
	return FormatRecord(Definition, Record, [&Definition](const FADSKValue& Value) { return ADSK::FormatValue(Definition, Value); });
}
