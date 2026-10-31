// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKInstance.h"

#include "Runtime/ADSKInstanceInternal.h"
#include "Trace/ADSKInsights.h"
#include "Trace/ADSKStats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ADSKInstancePrivate
{
	bool SameName(FStringView A, const FString& B)
	{
		return A.Equals(B, ESearchCase::CaseSensitive);
	}
}

using namespace ADSKInstancePrivate;

FADSKInstance::FImpl::FImpl(
	const TSharedRef<const FADSKDefinition>& InDefinition, const TSharedPtr<FADSKRecorder>& InRecorder, TWeakObjectPtr<UObject> InHost, uint32 InInitialGeneration)
	: Definition(InDefinition)
	, Recorder(InRecorder)
	, Host(InHost)
	, Generation(InInitialGeneration)
{
	ADSK_CHECK_GAME_THREAD();
	LLM_SCOPE_BYTAG(AdamskAI);
	RefreshTraceName();
	const TConstArrayView<FADSKSlotDefinition> Definitions = Definition->GetSlots();
	Slots.Reserve(Definitions.Num());
	InputValues.Reserve(Definitions.Num());
	PendingInputs.SetNum(Definitions.Num());
	HasInputValue.Init(false, Definitions.Num());
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		Slots.Add(Definitions[Index].Default);
		InputValues.Add(Definitions[Index].Default);
		if (Definitions[Index].bIsInput && !Definitions[Index].bRequired)
		{
			HasInputValue[Index] = true;
		}
	}
	for (int32 Event = 0; Event < Definition->GetEvents().Num(); ++Event)
	{
		MaxEventParams = FMath::Max(MaxEventParams, Definition->GetEvents()[Event].ParamCount);
		if (Definition->GetEvents()[Event].ChangeSlot != INDEX_NONE)
		{
			ChangeEvents.Add(Event);
		}
	}
	ChangeSeen.SetNum(ChangeEvents.Num());
	Events.SetNum(ADSK::ExternalEventCapacity);
	QueuedParams.SetNum(ADSK::ExternalEventCapacity * MaxEventParams);
	// One aligned slice per node with activation memory; the arena never reallocates, so the base stays put.
	int32 ArenaSize = 0;
	int32 ArenaAlignment = 1;
	MemoryOffsets.Init(INDEX_NONE, Definition->GetNodes().Num());
	for (int32 Node = 0; Node < Definition->GetNodes().Num(); ++Node)
	{
		if (const UScriptStruct* Memory = Definition->GetNodes()[Node].Descriptor->MemoryStruct)
		{
			const int32 Alignment = FMath::Max(1, Memory->GetMinAlignment());
			ArenaAlignment = FMath::Max(ArenaAlignment, Alignment);
			ArenaSize = Align(ArenaSize, Alignment);
			MemoryOffsets[Node] = ArenaSize;
			ArenaSize += Memory->GetStructureSize();
		}
	}
	MemoryArena.SetNumZeroed(ArenaSize + ArenaAlignment);
	MemoryBase = Align(MemoryArena.GetData(), ArenaAlignment);
	EventParamValues.SetNum(MaxEventParams);
	EventParamSeen.SetNumZeroed(MaxEventParams);
	// One slot beyond capacity is reserved for the running leaf's own result.
	Results.Reserve(ADSK::ActionResultCapacity + 1);
	ResultsInFlight.Reserve(ADSK::ActionResultCapacity + 1);
	ActivePath.Reserve(FADSKCompiler::MaxStateDepth);
	ActiveTokens.Reserve(FADSKCompiler::MaxStateDepth);
	ActiveRegions.Reserve(FADSKCompiler::MaxStateDepth);
	NodeStates.SetNum(Definition->GetNodes().Num());
	LastChild.Init(INDEX_NONE, Definition->GetStates().Num());
	Procedure = MakeUnique<FADSKProcedure>(*this);
}

void FADSKInstance::FImpl::RecordStamped(const FADSKRecord& Entry)
{
	INC_DWORD_STAT(STAT_ADSKRecordsWritten);
	FADSKRecord Stamped = Entry;
	Stamped.Tick = TickCount;
	Recorder->Record(Stamped);
}

void FADSKInstance::FImpl::RefreshTraceName()
{
	ADSK_CHECK_GAME_THREAD();
	const UObject* Object = Host.Get();
	HostName = Object ? Object->GetName() : Definition->GetAgentId();
}

void FADSKInstance::FImpl::SlotWritten(int32 Node, int32 Slot, FADSKActivationHandle Handle)
{
	bSlotWrittenThisTick = true;
	Record({.Kind = EADSKRecordKind::SlotWritten, .Node = Node, .Slot = Slot, .Handle = Handle, .bHasValue = true, .Value = Slots[Slot]});
}

void FADSKInstance::FImpl::ApplyPendingInputs()
{
	if (!bHasPendingInputs)
	{
		return;
	}
	bHasPendingInputs = false;
	const TConstArrayView<FADSKSlotDefinition> Definitions = Definition->GetSlots();
	for (int32 Slot = 0; Slot < PendingInputs.Num(); ++Slot)
	{
		if (!PendingInputs[Slot].IsSet())
		{
			continue;
		}
		check(Definitions[Slot].bIsInput);
		// Hosts republish their facts every frame; only a value that changes is a decision input worth a record.
		const bool bChanged = !HasInputValue[Slot] || !(InputValues[Slot] == PendingInputs[Slot].GetValue()) || !(Slots[Slot] == InputValues[Slot]);
		InputValues[Slot] = PendingInputs[Slot].GetValue();
		HasInputValue[Slot] = true;
		Slots[Slot] = InputValues[Slot];
		PendingInputs[Slot].Reset();
		if (bChanged)
		{
			Record({.Kind = EADSKRecordKind::InputSet, .Slot = Slot, .bHasValue = true, .Value = Slots[Slot]});
		}
	}
}

FADSKInstance::FADSKInstance(
	const TSharedRef<const FADSKDefinition>& InDefinition, const TSharedPtr<FADSKRecorder>& InRecorder, TWeakObjectPtr<UObject> Host,
	uint32 InitialGeneration)
	: Impl(MakeUnique<FImpl>(InDefinition, InRecorder, Host, InitialGeneration))
{
	ADSK_CHECK_GAME_THREAD();
}

FADSKInstance::~FADSKInstance()
{
	ADSK_CHECK_GAME_THREAD();
	checkf(!Impl || Impl->CallbackDepth == 0, TEXT("FADSKInstance destroyed from inside its own node callback; the owner must defer destruction"));
	Stop();
}

FADSKInputId FADSKInstance::FindInput(FStringView Name) const
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Slot = Impl->Definition->FindSlot(Name);
	if (Slot == INDEX_NONE || !Impl->Definition->GetSlots()[Slot].bIsInput)
	{
		return {};
	}
	return {Impl->Definition->GetSerial(), Slot};
}

FADSKEventId FADSKInstance::FindEvent(FStringView Name) const
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Event = Impl->Definition->FindEvent(Name);
	if (Event == INDEX_NONE || Impl->Definition->GetEvents()[Event].IsRaisedByCore())
	{
		return {};
	}
	return {Impl->Definition->GetSerial(), Event};
}

EADSKInputResult FADSKInstance::SetInput(FStringView Name, const FADSKValue& Value)
{
	ADSK_CHECK_GAME_THREAD();
	const FADSKInputId Input = FindInput(Name);
	if (!Input.IsValid())
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::UnknownName;
	}
	return SetInput(Input, Value);
}

EADSKInputResult FADSKInstance::SetInput(FADSKInputId Input, const FADSKValue& Value)
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Slot = Input.Slot;
	if (Input.Definition != Impl->Definition->GetSerial() || !Impl->Definition->GetSlots().IsValidIndex(Slot)
		|| !Impl->Definition->GetSlots()[Slot].bIsInput)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::UnknownName;
	}
	if (Impl->Definition->GetSlots()[Slot].Type != Value.Type || !Value.IsFinite())
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected, .Slot = Slot});
		return EADSKInputResult::TypeMismatch;
	}
	if (Value.Type == EADSKValueType::Enum)
	{
		const FADSKSlotDefinition& SlotDefinition = Impl->Definition->GetSlots()[Slot];
		const TConstArrayView<FADSKEnumDefinition> Enums = Impl->Definition->GetEnums();
		if (Value.Enum != SlotDefinition.Enum || !Enums.IsValidIndex(Value.Enum) || Value.Int < 0 || Value.Int >= Enums[Value.Enum].ValueCount)
		{
			Impl->Record({.Kind = EADSKRecordKind::InputRejected, .Slot = Slot});
			return EADSKInputResult::TypeMismatch;
		}
	}
	Impl->PendingInputs[Slot] = Value;
	Impl->bHasPendingInputs = true;
	Impl->NotifyQueued();
	return EADSKInputResult::Accepted;
}

EADSKInputResult FADSKInstance::SetEnumInput(FStringView Name, FStringView ValueName)
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Slot = Impl->Definition->FindSlot(Name);
	if (Slot == INDEX_NONE || !Impl->Definition->GetSlots()[Slot].bIsInput) return EADSKInputResult::UnknownName;
	const FADSKSlotDefinition& SlotDefinition = Impl->Definition->GetSlots()[Slot];
	if (SlotDefinition.Type != EADSKValueType::Enum || !Impl->Definition->GetEnums().IsValidIndex(SlotDefinition.Enum)) return EADSKInputResult::TypeMismatch;
	const FADSKEnumDefinition& Enum = Impl->Definition->GetEnums()[SlotDefinition.Enum];
	for (int32 Index = 0; Index < Enum.ValueCount; ++Index)
	{
		if (ValueName.Equals(Impl->Definition->GetEnumValueNames()[Enum.FirstValue + Index], ESearchCase::CaseSensitive)) return SetInput(Name, FADSKValue::MakeEnum(SlotDefinition.Enum, Index));
	}
	return EADSKInputResult::TypeMismatch;
}

EADSKInputResult FADSKInstance::EnqueueEvent(FStringView Name, TConstArrayView<FADSKNamedValue> Parameters)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Impl->bRunning)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::NotRunning;
	}
	const int32 Event = Impl->Definition->FindEvent(Name);
	if (Event == INDEX_NONE)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::UnknownName;
	}
	return EnqueueEvent(FADSKEventId{Impl->Definition->GetSerial(), Event}, Parameters);
}

EADSKInputResult FADSKInstance::EnqueueEvent(FADSKEventId Event, TConstArrayView<FADSKNamedValue> Parameters)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Impl->bRunning)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::NotRunning;
	}
	if (Event.Definition != Impl->Definition->GetSerial() || !Impl->Definition->GetEvents().IsValidIndex(Event.Event))
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::UnknownName;
	}
	if (Impl->Definition->GetEvents()[Event.Event].IsRaisedByCore())
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected, .Event = Event.Event});
		return EADSKInputResult::Reserved;
	}
	const EADSKInputResult Staged = Impl->StageEventParameters(Event.Event, Parameters);
	if (Staged != EADSKInputResult::Accepted)
	{
		return Staged;
	}
	const EADSKInputResult Result = Impl->EnqueueInternal(Event.Event, 0);
	if (Result == EADSKInputResult::Accepted) Impl->NotifyQueued();
	return Result;
}

EADSKInputResult FADSKInstance::FImpl::EnqueueInternal(int32 Event, uint64 EligibleTick)
{
	if (EventCount == ADSK::ExternalEventCapacity)
	{
		Record({.Kind = EADSKRecordKind::InputRejected, .Reason = EADSKRecordReason::QueueFull, .Event = Event});
		return EADSKInputResult::QueueFull;
	}
	FADSKValue* Params = QueuedParamsOf(EventCount);
	FADSKQueuedEvent& Queued = Events[EventCount++];
	const FADSKEventDefinition& EventDefinition = Definition->GetEvents()[Event];
	Queued.Event = Event;
	for (int32 Param = 0; Param < EventDefinition.ParamCount; ++Param)
	{
		Params[Param] = EventParamValues[Param];
	}
	Queued.EligibleTick = EligibleTick;
	Queued.EnqueueSequence = NextEnqueueSequence++;
	Record({.Kind = EADSKRecordKind::EventEnqueued, .Event = Event});
	return EADSKInputResult::Accepted;
}

EADSKInputResult FADSKInstance::PostActionResult(FADSKActivationHandle Handle, EADSKNodeStatus Status)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Impl->bRunning)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::NotRunning;
	}
	if (!Handle.IsValid() || Status == EADSKNodeStatus::Running)
	{
		Impl->Record({.Kind = EADSKRecordKind::InputRejected});
		return EADSKInputResult::TypeMismatch;
	}
	if (Impl->Results.Num() >= ADSK::ActionResultCapacity)
	{
		// Only running leaves' results can still change anything; stale posts must not crowd them out. Each running
		// leaf (one per lane) gets one slot beyond capacity, so the overflow stays bounded.
		const bool bReservedSlot = Impl->Procedure->IsCurrentRunningLeaf(Handle)
			&& !Impl->Results.ContainsByPredicate([Handle](const FADSKQueuedResult& Queued) { return Queued.Handle == Handle; });
		if (!bReservedSlot)
		{
			Impl->Record({.Kind = EADSKRecordKind::InputRejected, .Reason = EADSKRecordReason::QueueFull});
			return EADSKInputResult::QueueFull;
		}
	}
	Impl->Results.Add({Handle, Status});
	Impl->NotifyQueued();
	return EADSKInputResult::Accepted;
}

FADSKStartReport FADSKInstance::Start()
{
	return StartFrom(nullptr);
}

FADSKStartReport FADSKInstance::Start(const FADSKSnapshot& Snapshot)
{
	return StartFrom(&Snapshot);
}

FADSKStartReport FADSKInstance::StartFrom(const FADSKSnapshot* Snapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FADSKInstance::Start);
	ADSK_CHECK_GAME_THREAD();
	FADSKStartReport Report;
	if (Impl->CallbackDepth > 0)
	{
		Report.Result = EADSKStartResult::Reentrant;
		Impl->Record({.Kind = EADSKRecordKind::StartFailed});
		return Report;
	}
	if (Impl->bRunning)
	{
		Report.Result = EADSKStartResult::AlreadyRunning;
		Impl->Record({.Kind = EADSKRecordKind::StartFailed});
		return Report;
	}

	// A host renamed since the last start shows under its new name.
	Impl->RefreshTraceName();
	// Accepted pre-start writes become persistent even when another required input is missing.
	Impl->PersistPendingInputs();
	const TConstArrayView<FADSKSlotDefinition> Definitions = Impl->Definition->GetSlots();
	for (int32 Slot = 0; Slot < Definitions.Num(); ++Slot)
	{
		if (Definitions[Slot].bIsInput && Definitions[Slot].bRequired && !Impl->HasInputValue[Slot])
		{
			Report.MissingInputs.Add(Definitions[Slot].Name);
		}
	}
	if (!Report.MissingInputs.IsEmpty())
	{
		Report.Result = EADSKStartResult::MissingRequiredInput;
		Impl->Record({.Kind = EADSKRecordKind::StartFailed});
		if (ADSK_TRACE_ENABLED())
		{
			TRACE_BOOKMARK(TEXT("AdamskAI start failed: %s misses %s"), *Impl->TraceName(), *FString::Join(Report.MissingInputs, TEXT(", ")));
		}
		return Report;
	}

	Impl->BeginGeneration(Snapshot ? EADSKRecordReason::Restored : EADSKRecordReason::Started);
	if (Snapshot)
	{
		Impl->Restore(*Snapshot, Report.Dropped);
	}
	else
	{
		Impl->EnterInitialConfiguration();
	}
	if (Impl->bStopRequested)
	{
		Stop();
	}
	return Report;
}

EADSKInputResult FADSKInstance::FImpl::StageEventParameters(int32 Event, TConstArrayView<FADSKNamedValue> Parameters)
{
	const FADSKEventDefinition& EventDefinition = Definition->GetEvents()[Event];
	for (int32 Index = 0; Index < EventDefinition.ParamCount; ++Index)
	{
		EventParamSeen[Index] = 0;
	}
	const TConstArrayView<FADSKEventParamDefinition> Definitions = Definition->GetEventParams();
	for (const FADSKNamedValue& Supplied : Parameters)
	{
		int32 Param = INDEX_NONE;
		for (int32 Index = 0; Index < EventDefinition.ParamCount; ++Index)
		{
			if (SameName(Supplied.Name, Definitions[EventDefinition.FirstParam + Index].Name))
			{
				Param = Index;
				break;
			}
		}
		if (Param == INDEX_NONE || EventParamSeen[Param] != 0)
		{
			Record({.Kind = EADSKRecordKind::InputRejected, .Event = Event});
			return EADSKInputResult::UnexpectedParameter;
		}
		const FADSKEventParamDefinition& ParamDefinition = Definitions[EventDefinition.FirstParam + Param];
		const bool bEnumOk = ParamDefinition.Type != EADSKValueType::Enum
			|| (Supplied.Value.Enum == ParamDefinition.Enum && Definition->GetEnums().IsValidIndex(ParamDefinition.Enum)
				&& Supplied.Value.Int >= 0 && Supplied.Value.Int < Definition->GetEnums()[ParamDefinition.Enum].ValueCount);
		if (ParamDefinition.Type != Supplied.Value.Type || !bEnumOk || !Supplied.Value.IsFinite())
		{
			Record({.Kind = EADSKRecordKind::InputRejected, .Event = Event});
			return EADSKInputResult::TypeMismatch;
		}
		EventParamSeen[Param] = 1;
		EventParamValues[Param] = Supplied.Value;
	}
	for (int32 Param = 0; Param < EventDefinition.ParamCount; ++Param)
	{
		if (EventParamSeen[Param] == 0)
		{
			Record({.Kind = EADSKRecordKind::InputRejected, .Event = Event});
			return EADSKInputResult::MissingParameter;
		}
	}
	return EADSKInputResult::Accepted;
}

void FADSKInstance::FImpl::PersistPendingInputs()
{
	bHasPendingInputs = false;
	for (int32 Slot = 0; Slot < PendingInputs.Num(); ++Slot)
	{
		if (PendingInputs[Slot].IsSet())
		{
			InputValues[Slot] = PendingInputs[Slot].GetValue();
			HasInputValue[Slot] = true;
			PendingInputs[Slot].Reset();
		}
	}
}

void FADSKInstance::FImpl::BeginGeneration(EADSKRecordReason Reason)
{
	const TConstArrayView<FADSKSlotDefinition> Definitions = Definition->GetSlots();
	for (int32 Slot = 0; Slot < Definitions.Num(); ++Slot)
	{
		Slots[Slot] = Definitions[Slot].bIsInput ? InputValues[Slot] : Definitions[Slot].Default;
	}
	EventCount = 0;
	Results.Reset();
	Completion.Reset();
	ActivePath.Reset();
	ActiveTokens.Reset();
	ActiveRegions.Reset();
	for (int32& Child : LastChild) Child = INDEX_NONE;
	LastExited = INDEX_NONE;
	LastAcceptedHandle = {};
	bHold = false;
	bRunning = true;
	Clock = 0.0;
	for (int32 Node = 0; Node < NodeStates.Num(); ++Node)
	{
		if (const UScriptStruct* StateStruct = Definition->GetNodes()[Node].Descriptor->StateStruct)
		{
			NodeStates[Node].InitializeAs(StateStruct);
		}
	}
	Random.Initialize(RandomSeed);
	++Generation;
	bSlotWrittenThisTick = false;
	Procedure->ResetForGeneration();
	Procedure->ResetBudget();
	// Values at Start are the baseline: a change event means a change while running.
	for (int32 Index = 0; Index < ChangeEvents.Num(); ++Index)
	{
		ChangeSeen[Index] = Slots[Definition->GetEvents()[ChangeEvents[Index]].ChangeSlot];
	}
	Record({.Kind = EADSKRecordKind::InstanceStarted, .Reason = Reason});
	for (int32 Slot = 0; Slot < Definitions.Num(); ++Slot)
	{
		if (Definitions[Slot].bIsInput)
		{
			Record({.Kind = EADSKRecordKind::InputSet, .Slot = Slot, .bHasValue = true, .Value = Slots[Slot]});
		}
	}
}

void FADSKInstance::FImpl::RaiseChangeEvents()
{
	for (int32 Index = 0; Index < ChangeEvents.Num(); ++Index)
	{
		const FADSKValue& Current = Slots[Definition->GetEvents()[ChangeEvents[Index]].ChangeSlot];
		// A change a full queue refuses is raised again on the next tick.
		if (Current != ChangeSeen[Index] && EnqueueInternal(ChangeEvents[Index], TickCount) == EADSKInputResult::Accepted)
		{
			ChangeSeen[Index] = Current;
		}
	}
}

void FADSKInstance::FImpl::ApplyResults()
{
	// A callback started by an accepted result may post again (a leaf completing inside its own OnStart).
	// That post must not join this pass: it goes to the emptied queue and is classified next tick.
	Swap(Results, ResultsInFlight);
	TArray<FADSKActivationHandle, TInlineAllocator<ADSK::ActionResultCapacity + 1>> Seen;
	for (const FADSKQueuedResult& Result : ResultsInFlight)
	{
		const bool bDuplicate = Seen.Contains(Result.Handle) || Result.Handle == LastAcceptedHandle;
		Seen.Add(Result.Handle);
		if (bDuplicate)
		{
			Record({.Kind = EADSKRecordKind::ResultDuplicate, .Reason = EADSKRecordReason::DuplicateResult, .Handle = Result.Handle, .Status = Result.Status});
		}
		else if (Procedure->IsCurrentRunningLeaf(Result.Handle))
		{
			LastAcceptedHandle = Result.Handle;
			Record({.Kind = EADSKRecordKind::ResultAccepted, .Handle = Result.Handle, .Status = Result.Status});
			Procedure->ApplyResult(Result.Handle, Result.Status);
		}
		else
		{
			Record({.Kind = EADSKRecordKind::ResultStale, .Reason = EADSKRecordReason::StaleResult, .Handle = Result.Handle, .Status = Result.Status});
		}
	}
	ResultsInFlight.Reset();
}

EADSKTickResult FADSKInstance::Tick(float Delta)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FADSKInstance::Tick);
	ADSK_CHECK_OWNER(this);
	if (Impl->CallbackDepth > 0)
	{
		return EADSKTickResult::Reentrant;
	}
	if (!Impl->bRunning)
	{
		return EADSKTickResult::NotRunning;
	}
	if (!FMath::IsFinite(Delta) || Delta < 0.f)
	{
		return EADSKTickResult::InvalidDelta;
	}
	++Impl->TickCount;
	Impl->Clock += Delta;
	Impl->bSlotWrittenThisTick = false;
	Impl->Procedure->ResetBudget();
	Impl->ApplyPendingInputs();
	Impl->ApplyResults();
	if (!Impl->bStopRequested)
	{
		Impl->RaiseChangeEvents();
		Impl->DispatchEvents();
	}
	if (!Impl->bStopRequested)
	{
		Impl->Procedure->Step(Delta);
	}
	if (Impl->bStopRequested)
	{
		Stop();
	}
	return EADSKTickResult::Advanced;
}

void FADSKInstance::Stop()
{
	StopImpl(nullptr);
}

bool FADSKInstance::StopAndCaptureSnapshot(FADSKSnapshot& Out)
{
	ADSK_CHECK_GAME_THREAD();
	Out = FADSKSnapshot();
	if (!Impl || !Impl->bRunning || Impl->CallbackDepth > 0)
	{
		Stop();
		return false;
	}
	StopImpl(&Out);
	return true;
}

void FADSKInstance::StopImpl(FADSKSnapshot* OutSnapshot)
{
	ADSK_CHECK_OWNER(this);
	if (!Impl || !Impl->bRunning)
	{
		return;
	}
	if (Impl->CallbackDepth > 0)
	{
		Impl->bStopRequested = true;
		return;
	}
	Impl->Procedure->Cancel();
	// Between the cancel and the exits: the snapshot holds what OnEnd wrote (a Cooldown that the cancel started).
	if (OutSnapshot)
	{
		CaptureSnapshot(*OutSnapshot);
	}
	Impl->ExitAllStates();
	++Impl->Generation;
	Impl->EventCount = 0;
	Impl->Results.Reset();
	Impl->Completion.Reset();
	for (TOptional<FADSKValue>& Pending : Impl->PendingInputs)
	{
		Pending.Reset();
	}
	Impl->LastAcceptedHandle = {};
	Impl->bHold = false;
	Impl->bRunning = false;
	// A Stop requested by an OnEnd during this cancellation is this Stop.
	Impl->bStopRequested = false;
	Impl->Record({.Kind = EADSKRecordKind::InstanceStopped, .Reason = EADSKRecordReason::Stopped});
}

void FADSKInstance::SetRandomSeed(int32 Seed)
{
	ADSK_CHECK_GAME_THREAD();
	Impl->RandomSeed = Seed;
}

int32 FADSKInstance::GetRandomSeed() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->RandomSeed;
}

bool FADSKInstance::IsRunning() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->bRunning;
}

bool FADSKInstance::IsHolding() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->bHold;
}

void FADSKInstance::SetQueueListener(TFunction<void()> Listener)
{
	ADSK_CHECK_GAME_THREAD();
	Impl->QueueListener = MoveTemp(Listener);
}

float FADSKInstance::GetNextWake()
{
	ADSK_CHECK_OWNER(this);
	FImpl& Owner = *Impl;
	if (!Owner.bRunning)
	{
		return ADSK::NeverWake;
	}
	if (Owner.CallbackDepth > 0 || Owner.bStopRequested || Owner.bSlotWrittenThisTick || Owner.EventCount > 0 || !Owner.Results.IsEmpty()
		|| Owner.Completion.IsSet() || Owner.bHasPendingInputs)
	{
		return 0.f;
	}
	return Owner.Procedure->GetNextWake();
}

uint32 FADSKInstance::GetGeneration() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->Generation;
}

uint64 FADSKInstance::GetTickCount() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->TickCount;
}

TArray<FString> FADSKInstance::GetActiveStatePath() const
{
	ADSK_CHECK_GAME_THREAD();
	TArray<FString> Result;
	Result.Reserve(Impl->ActivePath.Num());
	for (const int32 State : Impl->ActivePath)
	{
		Result.Add(Impl->Definition->GetStates()[State].Id);
	}
	return Result;
}

bool FADSKInstance::GetValue(FStringView SlotName, FADSKValue& OutValue) const
{
	ADSK_CHECK_GAME_THREAD();
	const int32 Slot = Impl->Definition->FindSlot(SlotName);
	if (!Impl->Slots.IsValidIndex(Slot))
	{
		return false;
	}
	OutValue = Impl->Slots[Slot];
	return true;
}

FADSKActivationHandle FADSKInstance::GetActiveLeafHandle() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->Procedure->GetDeepestLeafHandle();
}

FName FADSKInstance::GetActiveNodeId() const
{
	ADSK_CHECK_GAME_THREAD();
	if (!Impl || !Impl->bRunning || !Impl->Procedure.IsValid())
	{
		return NAME_None;
	}
	const int32 Node = Impl->Procedure->GetDeepestNodeIndex();
	const TConstArrayView<FADSKNodeDefinition> Nodes = Impl->Definition->GetNodes();
	return Nodes.IsValidIndex(Node) ? Nodes[Node].Id : NAME_None;
}

void FADSKInstance::GetActiveNodes(TArray<FName>& OutNodeIds, TArray<FADSKActivationHandle>& OutLeafHandles) const
{
	ADSK_CHECK_GAME_THREAD();
	OutNodeIds.Reset();
	OutLeafHandles.Reset();
	if (!Impl || !Impl->bRunning)
	{
		return;
	}
	TArray<int32> Nodes;
	Impl->Procedure->GetDeepestNodes(Nodes, OutLeafHandles);
	for (const int32 Node : Nodes)
	{
		OutNodeIds.Add(Impl->Definition->GetNodes()[Node].Id);
	}
}

void FADSKInstance::GetQueuedEvents(TArray<FString>& OutEventNames) const
{
	ADSK_CHECK_GAME_THREAD();
	OutEventNames.Reset();
	if (!Impl)
	{
		return;
	}
	struct FPending
	{
		int32 Priority;
		uint64 Sequence;
		int32 Event;
	};
	const TConstArrayView<FADSKEventDefinition> EventDefinitions = Impl->Definition->GetEvents();
	TArray<FPending, TInlineAllocator<ADSK::ExternalEventCapacity>> Pending;
	for (int32 Index = 0; Index < Impl->EventCount; ++Index)
	{
		const FADSKQueuedEvent& Queued = Impl->Events[Index];
		Pending.Add({EventDefinitions[Queued.Event].Priority, Queued.EnqueueSequence, Queued.Event});
	}
	Pending.Sort([](const FPending& A, const FPending& B)
	{
		return A.Priority != B.Priority ? A.Priority > B.Priority : A.Sequence < B.Sequence;
	});
	for (const FPending& Entry : Pending)
	{
		OutEventNames.Add(EventDefinitions[Entry.Event].Name);
	}
}

const TSharedRef<const FADSKDefinition>& FADSKInstance::GetDefinition() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->Definition;
}

const TSharedPtr<FADSKRecorder>& FADSKInstance::GetRecorder() const
{
	ADSK_CHECK_GAME_THREAD();
	return Impl->Recorder;
}

#define ADSK_ENUM_TEXT(Value) case Value: return TEXT(#Value)

const TCHAR* LexToString(EADSKInputResult Result)
{
	switch (Result)
	{
		ADSK_ENUM_TEXT(EADSKInputResult::Accepted);
		ADSK_ENUM_TEXT(EADSKInputResult::UnknownName);
		ADSK_ENUM_TEXT(EADSKInputResult::TypeMismatch);
		ADSK_ENUM_TEXT(EADSKInputResult::MissingParameter);
		ADSK_ENUM_TEXT(EADSKInputResult::UnexpectedParameter);
		ADSK_ENUM_TEXT(EADSKInputResult::Reserved);
		ADSK_ENUM_TEXT(EADSKInputResult::QueueFull);
		ADSK_ENUM_TEXT(EADSKInputResult::NotRunning);
	}
	return TEXT("unknown");
}

const TCHAR* LexToString(EADSKStartResult Result)
{
	switch (Result)
	{
		ADSK_ENUM_TEXT(EADSKStartResult::Started);
		ADSK_ENUM_TEXT(EADSKStartResult::AlreadyRunning);
		ADSK_ENUM_TEXT(EADSKStartResult::MissingRequiredInput);
		ADSK_ENUM_TEXT(EADSKStartResult::Reentrant);
	}
	return TEXT("unknown");
}

const TCHAR* LexToString(EADSKTickResult Result)
{
	switch (Result)
	{
		ADSK_ENUM_TEXT(EADSKTickResult::Advanced);
		ADSK_ENUM_TEXT(EADSKTickResult::NotRunning);
		ADSK_ENUM_TEXT(EADSKTickResult::InvalidDelta);
		ADSK_ENUM_TEXT(EADSKTickResult::Reentrant);
	}
	return TEXT("unknown");
}

#undef ADSK_ENUM_TEXT

int32 FADSKInstance::GetNodeMemoryBytes() const
{
	ADSK_CHECK_OWNER(this);
	return Impl->MemoryArena.Num();
}

TConstArrayView<FName> FADSKInstance::GetActiveTags() const
{
	ADSK_CHECK_OWNER(this);
	return Impl->ActiveTags;
}

uint64 FADSKInstance::GetActiveTagsSerial() const
{
	ADSK_CHECK_OWNER(this);
	return Impl->ActiveTagsSerial;
}

SIZE_T FADSKInstance::GetAllocatedBytes() const
{
	ADSK_CHECK_OWNER(this);
	return sizeof(FADSKInstance) + Impl->AllocatedBytes();
}

SIZE_T FADSKInstance::FImpl::AllocatedBytes() const
{
	const FImpl& I = *this;
	SIZE_T Bytes = sizeof(FImpl)
		+ I.Slots.GetAllocatedSize() + I.InputValues.GetAllocatedSize() + I.HasInputValue.GetAllocatedSize()
		+ I.PendingInputs.GetAllocatedSize() + I.Events.GetAllocatedSize() + I.QueuedParams.GetAllocatedSize()
		+ I.MemoryArena.GetAllocatedSize() + I.MemoryOffsets.GetAllocatedSize() + I.EventParamValues.GetAllocatedSize()
		+ I.EventParamSeen.GetAllocatedSize() + I.ChangeEvents.GetAllocatedSize() + I.ChangeSeen.GetAllocatedSize()
		+ I.Results.GetAllocatedSize() + I.ResultsInFlight.GetAllocatedSize() + I.ActivePath.GetAllocatedSize()
		+ I.ActiveTokens.GetAllocatedSize() + I.ActiveRegions.GetAllocatedSize() + I.NodeStates.GetAllocatedSize() + I.LastChild.GetAllocatedSize();
	for (const FInstancedStruct& State : I.NodeStates)
	{
		if (const UScriptStruct* Type = State.GetScriptStruct()) Bytes += Type->GetStructureSize();
	}
	if (I.Procedure)
	{
		Bytes += sizeof(FADSKProcedure) + I.Procedure->Lanes.GetAllocatedSize();
		for (const TUniquePtr<FADSKLane>& Lane : I.Procedure->Lanes) Bytes += sizeof(FADSKLane) + Lane->Stack.GetAllocatedSize();
		Bytes += I.Procedure->SpareLanes.GetAllocatedSize() + I.Procedure->SpareLanes.Num() * sizeof(FADSKLane);
	}
	return Bytes;
}
