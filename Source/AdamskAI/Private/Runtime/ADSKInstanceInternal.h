// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKProcedure.h"
#include "Trace/ADSKTrace.h"
#include "Containers/BitArray.h"

struct FADSKQueuedEvent
{
	int32 Event = INDEX_NONE;
	uint64 EnqueueSequence = 0;
	uint64 EligibleTick = 0;
};

struct FADSKQueuedResult
{
	FADSKActivationHandle Handle;
	EADSKNodeStatus Status = EADSKNodeStatus::Failed;
};

struct FADSKTreeCompletion
{
	int32 Event = INDEX_NONE;
	int32 OwnerState = INDEX_NONE;
	uint64 OwnerToken = 0;
	uint64 EnqueueSequence = 0;
	uint64 EligibleTick = 0;
};

struct FADSKInstance::FImpl final : IADSKSlotWriteSink
{
	FImpl(const TSharedRef<const FADSKDefinition>& InDefinition, const TSharedPtr<FADSKRecorder>& InRecorder,
		TWeakObjectPtr<UObject> InHost, uint32 InInitialGeneration);

	/** Stamps the current tick; a no-op without a recorder. Inline, so callers skip building the record without one. */
	void Record(const FADSKRecord& Entry) { if (Recorder) { RecordStamped(Entry); } }
	void RecordStamped(const FADSKRecord& Entry);
	SIZE_T AllocatedBytes() const;
	/** The host's name, or the agent id without a host; for Insights. Taken on the game thread, read on any. */
	const FString& TraceName() const { return HostName; }
	/** Game thread only: the parallel tick may trace on a worker, where the host must not be touched. */
	void RefreshTraceName();
	virtual void SlotWritten(int32 Node, int32 Slot, FADSKActivationHandle Handle) override;
	void ApplyPendingInputs();
	void PersistPendingInputs();
	void BeginGeneration(EADSKRecordReason Reason);
	/** After BeginGeneration: takes what the definition still matches from Snapshot, then enters its states. */
	void Restore(const FADSKSnapshot& Snapshot, TArray<FString>& OutDropped);
	EADSKInputResult StageEventParameters(int32 Event, TConstArrayView<FADSKNamedValue> Parameters);
	EADSKInputResult EnqueueInternal(int32 Event, uint64 EligibleTick);
	void RaiseChangeEvents();
	void ApplyResults();
	void DispatchEvents();
	bool SelectNextEvent(int32& OutExternal) const;
	int32 TakeExternalEvent(int32 Index);
	void HandleEvent(int32 Event, const FADSKValue* Parameters, bool bWasCompletion);
	void EnterInitialConfiguration();
	/** The child State enters: history's (only on entry), its `select` pick or initial. INDEX_NONE for a leaf. */
	int32 EntryChild(int32 State, bool bJustEntered, bool& bInOutDeep);
	/** Enters the chain of entry children below the deepest active state; bJustEntered: that state was entered now. */
	void EnterEntryChain(bool bJustEntered);
	void EnterState(int32 State);
	void ExitAllStates();
	/** Recomputes ActiveTags from ActivePath; moves ActiveTagsSerial only when the set changed. */
	void RefreshActiveTags();
	/** Pops the deepest active state, recording the exit and closing its Insights region. */
	void ExitTopState();
	void PerformTransition(const FADSKTransitionDefinition& Transition);
	void StartLeafProcedure();
	bool IsStateTokenActive(int32 State, uint64 Token) const;
	/** The handler an event takes, innermost state first; draws for a `chance` handler whose condition passed. */
	int32 FindTransition(int32 Event);
	int32 FindActiveStatePosition(int32 State) const;
	bool IsAncestorOrSelf(int32 Ancestor, int32 State) const;
	int32 LeastCommonAncestor(int32 A, int32 B) const;
	void QueueCompletion(EADSKNodeStatus Status, int32 OwnerState, uint64 OwnerToken);

	TSharedRef<const FADSKDefinition> Definition;
	TSharedPtr<FADSKRecorder> Recorder;
	TWeakObjectPtr<UObject> Host;
	FString HostName;
	TArray<FADSKValue> Slots;
	TArray<FADSKValue> InputValues;
	TBitArray<> HasInputValue;
	TArray<TOptional<FADSKValue>> PendingInputs;
	/** Sixty-four constructed queue slots; EventCount is the occupied prefix. */
	TArray<FADSKQueuedEvent> Events;
	TArray<FADSKValue> QueuedParams;
	FADSKValue* QueuedParamsOf(int32 Slot) { return QueuedParams.GetData() + Slot * MaxEventParams; }
	/** One slice per node is enough: a node is active at most once at a time (acyclic trees, disjoint lanes). */
	TArray<uint8> MemoryArena;
	uint8* MemoryBase = nullptr;
	TArray<int32> MemoryOffsets;
	uint8* NodeMemory(int32 Node) { return MemoryBase + MemoryOffsets[Node]; }
	TArray<FADSKValue> EventParamValues;
	TArray<uint8> EventParamSeen;
	/** Change events, and per event the slot value it last saw: the Start value, then the value it fired for. */
	TArray<int32> ChangeEvents;
	TArray<FADSKValue> ChangeSeen;
	TArray<FADSKQueuedResult> Results;
	/** The results ApplyResults is classifying; posts made meanwhile land in the emptied Results for the next tick. */
	TArray<FADSKQueuedResult> ResultsInFlight;
	TOptional<FADSKTreeCompletion> Completion;
	TArray<int32, TInlineAllocator<8>> ActivePath;
	TArray<uint64, TInlineAllocator<8>> ActiveTokens;
	/** Per active state, its Insights region; 0 when AdamskAIChannel was off as it was entered. */
	TArray<uint64, TInlineAllocator<8>> ActiveRegions;
	/** Per state, the child active when last left (history resumes it); INDEX_NONE before. Reset by Start. */
	TArray<int32> LastChild;
	/** The state ExitTopState popped last, so the next pop knows which child its state is leaving. */
	int32 LastExited = INDEX_NONE;
	/** The union of the tags of the states on ActivePath, in path order, without repeats. */
	TArray<FName> ActiveTags;
	uint64 ActiveTagsSerial = 0;
	TUniquePtr<FADSKProcedure> Procedure;
	/** Seconds of tick delta since Start; FADSKNodeContext::GetTime. */
	double Clock = 0.0;
	/** Per node, its descriptor's StateStruct (invalid for nodes without one); reset by every Start. */
	TArray<FInstancedStruct> NodeStates;
	/** Reseeded from RandomSeed by every Start; nodes draw from it through their context. */
	FRandomStream Random;
	int32 RandomSeed = 0;
	FADSKActivationHandle LastAcceptedHandle;
	uint64 NextEnqueueSequence = 1;
	uint64 NextStateToken = 1;
	uint32 Generation;
	uint64 TickCount = 0;
	int32 EventCount = 0;
	int32 MaxEventParams = 0;
	bool bRunning = false;
	bool bHold = false;
	/** Node callbacks currently on the stack. While non-zero Stop is deferred and Tick/Start are rejected. */
	int32 CallbackDepth = 0;
	/** Stop was called from a callback; the outermost Tick, Start or Stop performs it. */
	bool bStopRequested = false;
	/** A node, Set or Bind wrote a slot this tick, so the next one has work. Inputs do not count. */
	bool bSlotWrittenThisTick = false;
	/** A SetInput is waiting for the next tick's ApplyPendingInputs. */
	bool bHasPendingInputs = false;
	TFunction<void()> QueueListener;
	void NotifyQueued() { if (QueueListener) QueueListener(); }
};

struct FADSKCallbackScope
{
	explicit FADSKCallbackScope(FADSKInstance::FImpl& InOwner) : Owner(InOwner) { ++Owner.CallbackDepth; }
	~FADSKCallbackScope() { --Owner.CallbackDepth; }
	FADSKCallbackScope(const FADSKCallbackScope&) = delete;
	FADSKCallbackScope& operator=(const FADSKCallbackScope&) = delete;

	FADSKInstance::FImpl& Owner;
};
