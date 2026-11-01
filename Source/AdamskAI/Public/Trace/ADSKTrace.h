// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKNode.h"
#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"

enum class EADSKRecordKind : uint8
{
	InstanceStarted,
	StartFailed,
	InstanceStopped,
	InputSet,
	EventEnqueued,
	InputRejected,
	EventConsumed,
	EventUnhandled,
	StateEntered,
	StateExited,
	GuardValue,
	NodeStarted,
	NodeEnded,
	ResultAccepted,
	ResultStale,
	ResultDuplicate,
	CompletionStale,
	Hold,
	BudgetDeferred,
	/** Replaces records the ring had to overwrite; carries the exact lost count. */
	Gap,
	/** The transition an event took; Slot and Value are its condition's slot as read, when it has one. */
	TransitionTaken,
	/** A Bind or an Out port changed a slot; unchanged writes are not recorded. */
	SlotWritten,
	/** A `select` pick and its score; reason no-score (fell back to `initial`) or history (no value). */
	StateSelected,
};

enum class EADSKRecordReason : uint8
{
	None,
	Started,
	Stopped,
	QueueFull,
	BudgetDeferred,
	NoHandler,
	Hold,
	DuplicateResult,
	StaleResult,
	Cancelled,
	PartialEntry,
	Succeeded,
	Failed,
	NoScore,
	/** InstanceStarted from a snapshot. */
	Restored,
	/** StateSelected: the child a history resumed. */
	History,
};

ADAMSKAI_API const TCHAR* LexToString(EADSKRecordKind Kind);
/** Kebab-case reason, e.g. `queue-full`. */
ADAMSKAI_API const TCHAR* LexToString(EADSKRecordReason Reason);

struct FADSKRecord
{
	/** Assigned by the recorder; anything supplied by the caller is ignored. */
	uint64 Sequence = 0;
	uint64 Tick = 0;
	EADSKRecordKind Kind = EADSKRecordKind::InstanceStarted;
	EADSKRecordReason Reason = EADSKRecordReason::None;
	int32 State = INDEX_NONE;
	int32 Node = INDEX_NONE;
	int32 Event = INDEX_NONE;
	int32 Slot = INDEX_NONE;
	/** Index into the definition's transitions. */
	int32 Transition = INDEX_NONE;
	FADSKActivationHandle Handle;
	EADSKNodeStatus Status = EADSKNodeStatus::Running;
	bool bHasValue = false;
	FADSKValue Value;
	/** Records this Gap stands for; zero for every other kind. */
	uint64 LostCount = 0;
};

/** Preallocated ring; when full, the oldest records coalesce into one counted Gap. Decisions never read it. */
class ADAMSKAI_API FADSKRecorder
{
public:
	/** Capacity below two cannot hold a Gap plus a record; returns null. */
	static TSharedPtr<FADSKRecorder> Create(int32 Capacity);

	FADSKRecorder(const FADSKRecorder&) = delete;
	FADSKRecorder& operator=(const FADSKRecorder&) = delete;

	/** Never allocates or calls back. */
	void Record(const FADSKRecord& Record);

	/** Oldest first; may allocate. */
	TArray<FADSKRecord> Snapshot() const;

	/** The newest MaxRecords records, oldest first; may allocate. */
	TArray<FADSKRecord> Tail(int32 MaxRecords) const;

	/** Clears occupancy and counters; capacity is kept. */
	void Reset();

	int32 GetCapacity() const { return Capacity; }
	SIZE_T GetAllocatedBytes() const { return sizeof(*this) + Storage.GetAllocatedSize(); }
	int32 Num() const { return Count; }
	/** Every record ever accepted since construction or Reset, including overwritten ones. */
	uint64 GetTotalRecorded() const { return NextSequence - 1; }
	/** Records currently represented by the Gap. */
	uint64 GetTotalLost() const;

private:
	explicit FADSKRecorder(int32 InCapacity);

	FADSKRecord& At(int32 LogicalIndex);
	const FADSKRecord& At(int32 LogicalIndex) const;

	TArray<FADSKRecord> Storage;
	int32 Capacity;
	int32 Head = 0;
	int32 Count = 0;
	uint64 NextSequence = 1;
};

namespace ADSK
{
	/** ShowValue formats the recorded value; a host passes one that can name what an Object value resolves to. */
	ADAMSKAI_API FString FormatRecord(const FADSKDefinition& Definition, const FADSKRecord& Record,
		TFunctionRef<FString(const FADSKValue&)> ShowValue);

	/** FormatRecord with values shown by ADSK::FormatValue. */
	ADAMSKAI_API FString FormatRecord(const FADSKDefinition& Definition, const FADSKRecord& Record);
}
