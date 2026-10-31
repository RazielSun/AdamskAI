// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKBrainComponent.h"

#include "AIController.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKSourcePath.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "ADSKHostModule.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "Navigation/PathFollowingComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "VisualLogger/VisualLogger.h"
#include "GameplayTagsManager.h"
#include "Trace/ADSKStats.h"

DECLARE_CYCLE_STAT(TEXT("Brain tick"), STAT_ADSKBrainTick, STATGROUP_AdamskAI);
DECLARE_DWORD_COUNTER_STAT(TEXT("Brains ticked"), STAT_ADSKBrainsTicked, STATGROUP_AdamskAI);

UADSKBrainComponent::UADSKBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UADSKBrainComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bManagedTick && bAutoTick)
	{
		UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: bManagedTick and bAutoTick are both set; the managed tick wins"), *GetPathName());
	}
	// A copy of a replicated actor on a client never runs: the server's brain is the one that acts.
	const bool bAuthority = HasOwnerAuthority();
	bTicksItself = bAutoTick && !bManagedTick && bAuthority;
	SetComponentTickEnabled(bTicksItself);
	UADSKWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UADSKWorldSubsystem>() : nullptr;
	if (bManagedTick && bAuthority && Subsystem) Subsystem->AddManagedBrain(this);
}

bool UADSKBrainComponent::HasOwnerAuthority() const
{
	// A brain without an owner actor (tests, tools) is not networked.
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

bool UADSKBrainComponent::RefuseWithoutAuthority()
{
	if (HasOwnerAuthority()) return false;
	LastError = TEXT("not-authority");
	if (!bWarnedAuthority)
	{
		bWarnedAuthority = true;
		UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: refused on a copy without authority; brains run on the server (or standalone)"), *GetPathName());
	}
	return true;
}

void UADSKBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// After an interval the engine's delta can miss time (its first interval tick passes one frame); the world clock
	// does not, scaled as the engine scales this actor's ticks.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float Delta = LastAutoTickTime >= 0.0 && World ? static_cast<float>(Now - LastAutoTickTime) * (GetOwner() ? GetOwner()->CustomTimeDilation : 1.f) : DeltaTime;
	LastAutoTickTime = Now;
	TickBrain(Delta);
}

bool UADSKBrainComponent::InitializeFromSource(FStringView InSourcePath)
{
	// Every failure below leaves the current instance, its registration and its inputs as they were.
	if (RefuseWithoutAuthority()) return false;
	FString Normalized;
	if (!FADSKSourcePath::Normalize(InSourcePath, Normalized))
	{
		LastError = TEXT("source-read");
		return false;
	}
	UWorld* World = GetWorld();
	UADSKWorldSubsystem* Subsystem = World ? World->GetSubsystem<UADSKWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		LastError = TEXT("world-subsystem");
		return false;
	}
	FString Error;
	TSharedPtr<const FADSKDefinition> Definition = Subsystem->Acquire(Normalized, Error);
	if (!Definition)
	{
		LastError = MoveTemp(Error);
		return false;
	}
	if (!RunBindingValidators(*Definition, Error))
	{
		LastError = MoveTemp(Error);
		return false;
	}
	InitializeDefinition(Definition.ToSharedRef(), Subsystem->CreateBrainRecorder());
	SourcePath = MoveTemp(Normalized);
	WorldSubsystem = Subsystem;
	Subsystem->RegisterBrain(SourcePath, this);
	bRegistered = true;
	LastError.Reset();
	return true;
}

void UADSKBrainComponent::InitializeDefinition(const TSharedRef<const FADSKDefinition>& Definition, const TSharedPtr<FADSKRecorder>& InRecorder)
{
	if (RefuseWithoutAuthority()) return;
	StopLogic();
	// The new definition belongs to no source file until InitializeFromSource registers it, so a later
	// reload of the previous file cannot replace a definition supplied here directly.
	UnregisterFromSource();
	SourcePath.Reset();
	// A new definition is a new life: inputs written for the previous one are never replayed into it.
	InputCache.Reset();
	UnbindMoveCompleted();
	// Cleanup ends a life, not the component: AAIController cleans its brain on every unpossess, and a
	// controller that possesses again must still be cleaned at EndPlay.
	if (State == EADSKBrainState::CleanedUp) State = EADSKBrainState::Stopped;
	Recorder = InRecorder;
	ReplaceInstance(Definition);
	BindMoveCompleted();
}

FADSKInputId UADSKBrainComponent::FindInput(FStringView Name) const
{
	if (!Instance) return FADSKInputId();
	const FADSKInputId Input = Instance->FindInput(Name);
#if !UE_BUILD_SHIPPING
	if (!Input.IsValid()) WarnUnknownName(TEXT("Input"), Name);
#endif
	return Input;
}

FADSKEventId UADSKBrainComponent::FindEvent(FStringView Name) const
{
	if (!Instance) return FADSKEventId();
	const FADSKEventId Event = Instance->FindEvent(Name);
#if !UE_BUILD_SHIPPING
	if (!Event.IsValid()) WarnUnknownName(TEXT("event"), Name);
#endif
	return Event;
}

EADSKInputResult UADSKBrainComponent::SetInput(FStringView Name, const FADSKValue& Value)
{
	if (!Instance) return EADSKInputResult::NotRunning;
	const FADSKInputId Input = Instance->FindInput(Name);
	if (Input.IsValid()) return SetInput(Input, Value);
	// An unknown name still goes through the instance, which records the rejection.
	const EADSKInputResult Result = Instance->SetInput(Name, Value);
#if !UE_BUILD_SHIPPING
	if (Result == EADSKInputResult::UnknownName) WarnUnknownName(TEXT("Input"), Name);
#endif
	return Result;
}

#if !UE_BUILD_SHIPPING
void UADSKBrainComponent::WarnUnknownName(const TCHAR* Kind, FStringView Name) const
{
	bool bAlready = false;
	WarnedUnknownNames.Add(FString::Printf(TEXT("%s %.*s"), Kind, Name.Len(), Name.GetData()), &bAlready);
	if (bAlready) return;
	UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: %s has no %s named '%.*s'; the write is ignored"),
		*GetPathName(), *SourcePath, Kind, Name.Len(), Name.GetData());
}
#endif

EADSKInputResult UADSKBrainComponent::SetInput(FADSKInputId Input, const FADSKValue& Value)
{
	if (!Instance) return EADSKInputResult::NotRunning;
	// FindInput warned once already; a host writing it every tick would otherwise fill the trace with rejections
	if (!Input.IsValid()) return EADSKInputResult::UnknownName;
	const EADSKInputResult Result = Instance->SetInput(Input, Value);
	if (Result == EADSKInputResult::Accepted) InputCache.Record(*Instance->GetDefinition(), Input.Slot, Value);
	return Result;
}

EADSKInputResult UADSKBrainComponent::SetEnumInput(FStringView Name, FStringView ValueName)
{
	if (!Instance) return EADSKInputResult::NotRunning;
	const EADSKInputResult Result = Instance->SetEnumInput(Name, ValueName);
#if !UE_BUILD_SHIPPING
	if (Result == EADSKInputResult::UnknownName && !Instance->FindInput(Name).IsValid()) WarnUnknownName(TEXT("Input"), Name);
#endif
	if (Result == EADSKInputResult::Accepted)
	{
		const FADSKDefinition& Definition = *Instance->GetDefinition();
		const int32 Slot = Definition.FindSlot(Name);
		check(Definition.GetSlots().IsValidIndex(Slot));
		const TOptional<FADSKValue> Resolved = Definition.MakeEnumValue(Definition.GetEnums()[Definition.GetSlots()[Slot].Enum].Name, ValueName);
		check(Resolved.IsSet());
		InputCache.Record(Definition, Slot, Resolved.GetValue());
	}
	return Result;
}

EADSKInputResult UADSKBrainComponent::EnqueueEvent(FStringView Name, TConstArrayView<FADSKNamedValue> Parameters)
{
	const EADSKInputResult Result = Instance ? Instance->EnqueueEvent(Name, Parameters) : EADSKInputResult::NotRunning;
	if (Result != EADSKInputResult::Accepted)
	{
		UE_LOG(LogAdamskAIHost, Verbose, TEXT("%s: event %.*s refused: %s"), *GetPathName(), Name.Len(), Name.GetData(), LexToString(Result));
	}
#if !UE_BUILD_SHIPPING
	if (Result == EADSKInputResult::UnknownName) WarnUnknownName(TEXT("event"), Name);
#endif
	return Result;
}

EADSKInputResult UADSKBrainComponent::EnqueueEvent(FADSKEventId Event, TConstArrayView<FADSKNamedValue> Parameters)
{
	// as SetInput: FindEvent warned once already
	if (Instance && !Event.IsValid()) return EADSKInputResult::UnknownName;
	const EADSKInputResult Result = Instance ? Instance->EnqueueEvent(Event, Parameters) : EADSKInputResult::NotRunning;
	if (Result != EADSKInputResult::Accepted)
	{
		UE_LOG(LogAdamskAIHost, Verbose, TEXT("%s: event #%d refused: %s"), *GetPathName(), Event.Event, LexToString(Result));
	}
	return Result;
}

int32 UADSKBrainComponent::GetRandomSeed() const
{
	return Instance ? Instance->GetRandomSeed() : 0;
}

bool UADSKBrainComponent::ValidateBindings(const FADSKDefinition& Definition, FString& OutError)
{
	return InputCache.Validate(Definition, OutError) && RunBindingValidators(Definition, OutError);
}

bool UADSKBrainComponent::AddBindingValidator(UObject* Owner, FADSKBindingValidator Validator, FString& OutError)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Owner || !Validator)
	{
		OutError = TEXT("invalid binding validator");
		return false;
	}
	BindingValidators.RemoveAll([](const FBindingValidatorEntry& Entry) { return !Entry.Owner.IsValid(); });
	if (Instance && !Validator(*Instance->GetDefinition(), OutError)) { return false; }
	BindingValidators.RemoveAll([Owner](const FBindingValidatorEntry& Entry) { return Entry.Owner.Get() == Owner; });
	BindingValidators.Add({Owner, MoveTemp(Validator)});
	OutError.Reset();
	return true;
}

void UADSKBrainComponent::RemoveBindingValidators(UObject* Owner)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Owner) { return; }
	BindingValidators.RemoveAll([Owner](const FBindingValidatorEntry& Entry) { return !Entry.Owner.IsValid() || Entry.Owner.Get() == Owner; });
}

bool UADSKBrainComponent::RunBindingValidators(const FADSKDefinition& Definition, FString& OutError)
{
	ADSK_CHECK_GAME_THREAD();
	// State tags reach clients as gameplay tags, so a typo is refused here rather than silently dropped in play.
	for (const FName Tag : Definition.GetStateTags())
	{
		if (!UGameplayTagsManager::Get().RequestGameplayTag(Tag, false).IsValid())
		{
			OutError = FString::Printf(TEXT("unknown-tag: %s is not a registered gameplay tag"), *Tag.ToString());
			return false;
		}
	}
	BindingValidators.RemoveAll([](const FBindingValidatorEntry& Entry) { return !Entry.Owner.IsValid(); });
	// A copy: a validator may add or remove validators while it runs.
	const TArray<FBindingValidatorEntry> Validators = BindingValidators;
	for (const FBindingValidatorEntry& Entry : Validators)
	{
		if (!Entry.Validator(Definition, OutError)) { return false; }
	}
	OutError.Reset();
	return true;
}

bool UADSKBrainComponent::ReplaceDefinition(const TSharedRef<const FADSKDefinition>& Definition, bool bRestart, FString& OutError)
{
	FADSKPreparedReplacement Prepared;
	if (!PrepareReplacement(Definition, Prepared, OutError)) return false;
	ApplyReplacement(MoveTemp(Prepared), bRestart);
	return true;
}

bool UADSKBrainComponent::PrepareReplacement(const TSharedRef<const FADSKDefinition>& Definition, FADSKPreparedReplacement& OutPrepared, FString& OutError)
{
	if (!ValidateBindings(*Definition, OutError) || !InputCache.Stage(*Definition, OutPrepared.Inputs, OutError)) return false;
	OutPrepared.Definition = Definition;
	return true;
}

void UADSKBrainComponent::ApplyReplacement(FADSKPreparedReplacement&& Prepared, bool bRestart)
{
	check(Prepared.Definition.IsValid());
	StopLogic(TEXT("definition replaced"));
	ReplaceInstance(Prepared.Definition.ToSharedRef());
	for (const TPair<FString, FADSKValue>& Input : Prepared.Inputs) Instance->SetInput(Input.Key, Input.Value);
	InputCache.Adopt(MoveTemp(Prepared.Inputs));
	OnDefinitionReplaced.Broadcast();
	if (!bRestart) return;
	if (Prepared.Snapshot.IsSet()) StartLogicFrom(Prepared.Snapshot.GetValue());
	else StartLogic();
}

void UADSKBrainComponent::StartLogicFrom(const FADSKSnapshot& Snapshot)
{
	PendingRestore = Snapshot;
	StartLogic();
}

bool UADSKBrainComponent::CaptureSnapshot(FADSKSnapshot& Out) const
{
	if (Instance) return Instance->CaptureSnapshot(Out);
	Out = FADSKSnapshot();
	return false;
}

void UADSKBrainComponent::StartLogic()
{
	if (!Instance || IsRunning() || RefuseWithoutAuthority()) { PendingRestore.Reset(); return; }
	// A stop still being finalized must drain its async entries before anything new can create one.
	if (bAsyncDrainPending) { bRestartRequested = true; return; }
	// Zero is "fresh": brains that share a definition must not make the same choices in lockstep.
	Instance->SetRandomSeed(RandomSeed != 0 ? RandomSeed : FMath::Rand());
	const TOptional<FADSKSnapshot> Restore = MoveTemp(PendingRestore);
	PendingRestore.Reset();
	// A required Input a sensor writes must hold its first reading at the start.
	SampleSensors();
	EnterInstanceCall();
	const FADSKStartReport Report = Restore.IsSet() ? Instance->Start(Restore.GetValue()) : Instance->Start();
	LeaveInstanceCall();
	if (!Report.Dropped.IsEmpty())
	{
		UE_LOG(LogAdamskAIHost, Log, TEXT("%s restored without %s"), *GetPathName(), *FString::Join(Report.Dropped, TEXT(", ")));
	}
	if (Report.IsSuccess()) { LastError.Reset(); }
	else if (Report.Result == EADSKStartResult::MissingRequiredInput)
	{
		LastError = FString::Printf(TEXT("required input missing: %s"), *FString::Join(Report.MissingInputs, TEXT(", ")));
		UE_LOG(LogAdamskAIHost, Warning, TEXT("%s did not start: %s"), *GetPathName(), *LastError);
	}
	// A resource lock taken before this start (or held across a stop, restart or reload) still pauses the run;
	// the base only pauses on the unlocked-to-locked edge, which has already passed.
	if (State == EADSKBrainState::Running && IsResourceLocked()) State = EADSKBrainState::Paused;
	SleptTime = 0.f;
	UpdateSleep();
#if ENABLE_VISUAL_LOG
	VisLogTransition();
#endif
}

void UADSKBrainComponent::StopLogic(const FString&)
{
	StopLogicImpl(nullptr);
}

bool UADSKBrainComponent::StopLogicAndCaptureSnapshot(FADSKSnapshot& Out)
{
	Out = FADSKSnapshot();
	bool bCaptured = false;
	StopLogicImpl(&Out, &bCaptured);
	return bCaptured;
}

void UADSKBrainComponent::StopLogicImpl(FADSKSnapshot* OutSnapshot, bool* bOutCaptured)
{
	const bool bHadRunningInstance = Instance && Instance->IsRunning();
	if (State != EADSKBrainState::CleanedUp) State = EADSKBrainState::Stopped;
	bAsyncDrainPending = bHadRunningInstance || !AsyncOps.GetOps().IsEmpty();
	if (!bAsyncDrainPending) { return; }
	bStoppedNotificationPending |= bHadRunningInstance;
	EnterInstanceCall();
	if (bHadRunningInstance && OutSnapshot)
	{
		const bool bCaptured = Instance->StopAndCaptureSnapshot(*OutSnapshot);
		if (bOutCaptured) { *bOutCaptured = bCaptured; }
	}
	else if (bHadRunningInstance) { Instance->Stop(); }
	LeaveInstanceCall();
#if ENABLE_VISUAL_LOG
	VisLogTransition();
#endif
}

void UADSKBrainComponent::RestartLogic()
{
	StopLogic(TEXT("restart"));
	if (InstanceCallDepth > 0)
	{
		bRestartRequested = true;
		return;
	}
	StartLogic();
}

void UADSKBrainComponent::PauseLogic(const FString& Reason)
{
	Super::PauseLogic(Reason);
	if (State == EADSKBrainState::Running) State = EADSKBrainState::Paused;
}

EAILogicResuming::Type UADSKBrainComponent::ResumeLogic(const FString& Reason)
{
	// The base restarts instead when a resource lock asked for it; RestartLogic clears the pause itself.
	const EAILogicResuming::Type Result = Super::ResumeLogic(Reason);
	if (State == EADSKBrainState::Paused) State = EADSKBrainState::Running;
	WakeUp();
	return Result;
}

void UADSKBrainComponent::TickBrain(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKBrainComponent::TickBrain);
	SCOPE_CYCLE_COUNTER(STAT_ADSKBrainTick);
	CSV_SCOPED_TIMING_STAT(AdamskAI, BrainTick);
	// Asleep, the time is kept for the tick that wakes it, so every timer sees the same total as at full rate.
	SleptTime += DeltaTime;
	if (IsAsleep(SleptTime)) { return; }
	DeltaTime = SleptTime;
	SleptTime = 0.f;
	INC_DWORD_STAT(STAT_ADSKBrainsTicked);
	if (!BeginBrainTick()) { return; }
	FADSKInstance* CurrentInstance = Instance.Get();
	CurrentInstance->Tick(DeltaTime);
	EndBrainTick();
}

bool UADSKBrainComponent::BeginBrainTick()
{
	if (State != EADSKBrainState::Running || !Instance) { return false; }

#if !UE_BUILD_SHIPPING
	if (bAutoTick)
	{
		if (LastBrainTickFrame == GFrameCounter && LastDoubleTickWarningFrame != GFrameCounter)
		{
			LastDoubleTickWarningFrame = GFrameCounter;
			UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI brain auto-ticked more than once in frame %llu"), GFrameCounter);
		}
		LastBrainTickFrame = GFrameCounter;
	}
#endif

	OnPreTick.Broadcast();
	if (State != EADSKBrainState::Running || !Instance) { return false; }
	SampleSensors();
	EnterInstanceCall();
	return true;
}

void UADSKBrainComponent::SampleSensors()
{
	const FADSKDefinition& Definition = *Instance->GetDefinition();
	if (Definition.GetSensors().IsEmpty()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKBrainComponent::SampleSensors);
	const TConstArrayView<FADSKSlotDefinition> Slots = Definition.GetSlots();
	SensorSlots.SetNum(Slots.Num());
	for (const int32 NodeIndex : Definition.GetSensors())
	{
		const FADSKNodeDefinition& Node = Definition.GetNodes()[NodeIndex];
		const TConstArrayView<int32> PortSlots = Definition.GetPortBindings().Slice(Node.FirstPortBinding, Node.PortCount);
		for (const int32 Slot : PortSlots)
		{
			const FADSKValue* Latest = Slots[Slot].bIsInput ? InputCache.FindLatest(Definition, Slot) : nullptr;
			if (Latest) { SensorSlots[Slot] = *Latest; }
			else if (!Instance->GetValue(Slots[Slot].Name, SensorSlots[Slot])) { SensorSlots[Slot] = Slots[Slot].Default; }
		}
		FADSKNodeContextParams Params;
		Params.ConfigStruct = Node.Descriptor->ConfigStruct;
		Params.Config = Node.Config.GetMemory();
		Params.Ports = Node.Descriptor->Ports;
		Params.PortSlots = PortSlots;
		Params.Slots = SensorSlots;
		Params.NodeId = Node.Id;
		Params.Host = this;
		Params.Node = NodeIndex;
		FADSKNodeContext Context(Params);
		Node.Descriptor->OnSample(Context);
		for (int32 Port = 0; Port < Node.PortCount; ++Port)
		{
			if (Node.Descriptor->Ports[Port].Direction == EADSKPortDirection::Out)
			{
				SetInput(FADSKInputId{Definition.GetSerial(), PortSlots[Port]}, SensorSlots[PortSlots[Port]]);
			}
		}
	}
}

void UADSKBrainComponent::EndBrainTick()
{
	LeaveInstanceCall();

	// Hold is legal but almost always an unhandled completion the author did not mean; say so once per episode.
	const bool bHolding = Instance && Instance->IsRunning() && Instance->IsHolding();
	if (bHolding && !bWarnedHold)
	{
		const TArray<FString> Path = Instance->GetActiveStatePath();
		UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: the tree of state '%s' finished with no handler for it; holding until an event moves the brain"),
			*GetPathName(), Path.IsEmpty() ? TEXT("") : *Path.Last());
	}
	bWarnedHold = bHolding;
	UpdateSleep();
#if ENABLE_VISUAL_LOG
	VisLogTransition();
#endif
}

bool UADSKBrainComponent::IsAsleep(float Elapsed) const
{
	return SleepFor > 0.f && Elapsed + KINDA_SMALL_NUMBER < SleepFor && !OnPreTick.IsBound();
}

void UADSKBrainComponent::UpdateSleep()
{
	SleepFor = 0.f;
	if (GetDefault<UADSKSettings>()->bSleepWhenIdle && State == EADSKBrainState::Running && Instance && InstanceCallDepth == 0
		&& !OnPreTick.IsBound() && Instance->GetDefinition()->GetSensors().IsEmpty())
	{
		SleepFor = Instance->GetNextWake();
	}
	ApplyAutoTickRate();
}

void UADSKBrainComponent::WakeUp()
{
	if (SleepFor <= 0.f) { return; }
	SleepFor = 0.f;
	ApplyAutoTickRate();
}

void UADSKBrainComponent::ApplyAutoTickRate()
{
	if (!bTicksItself) { return; }
	if (SleepFor >= ADSK::NeverWake) { SetComponentTickEnabled(false); return; }
	SetComponentTickIntervalAndCooldown(SleepFor);
	SetComponentTickEnabled(true);
}

#if ENABLE_VISUAL_LOG
void UADSKBrainComponent::VisLogTransition()
{
	if (!FVisualLogger::IsRecording())
	{
		return;
	}
	const FString Path = Instance && Instance->IsRunning() ? FString::Join(Instance->GetActiveStatePath(), TEXT("/")) : FString();
	if (Path == VisLogStatePath)
	{
		return;
	}
	// Controller brains log on the pawn too, so the transition shows on the actor that moves.
	UE_VLOG(GetOwner(), LogAdamskAIHost, Log, TEXT("%s -> %s"), VisLogStatePath.IsEmpty() ? TEXT("(stopped)") : *VisLogStatePath, Path.IsEmpty() ? TEXT("(stopped)") : *Path);
	VisLogStatePath = Path;
}

void UADSKBrainComponent::DescribeSelfToVisLog(FVisualLogEntry* Snapshot) const
{
	Super::DescribeSelfToVisLog(Snapshot);
#if !UE_BUILD_SHIPPING
	FVisualLogStatusCategory Category(TEXT("AdamskAI"));
	for (const FString& Line : FADSKDebugSnapshot::FormatRow(FADSKDebugSnapshot::Describe(*this)))
	{
		Category.Add(TEXT(""), Line.TrimStart());
	}
	Snapshot->Status.Add(Category);
#endif
}
#endif

FString UADSKBrainComponent::GetDebugInfoString() const
{
	if (!Instance)
	{
		return TEXT("AdamskAI: no definition");
	}
	TArray<FName> Nodes;
	TArray<FADSKActivationHandle> Handles;
	Instance->GetActiveNodes(Nodes, Handles);
	return FString::Printf(TEXT("AdamskAI %s: %s [%s]"), *SourcePath, *FString::Join(Instance->GetActiveStatePath(), TEXT("/")),
		*FString::JoinBy(Nodes, TEXT(" + "), [](FName Node) { return Node.ToString(); }));
}

void UADSKBrainComponent::RetireInstance()
{
	// The next instance starts above every generation this one issued handles in, even after Cleanup.
	if (Instance) GenerationFloor = FMath::Max(GenerationFloor, Instance->GetGeneration());
	if (InstanceCallDepth > 0 && Instance)
	{
		RetiredInstances.Add(MoveTemp(Instance));
	}
	Instance.Reset();
}

void UADSKBrainComponent::ReplaceInstance(const TSharedRef<const FADSKDefinition>& Definition)
{
	// A stopped predecessor's generation is above every generation in which it issued handles; the
	// successor starts above it, so a late result for any earlier instance of this component cannot match a new leaf.
	RetireInstance();
	Instance = MakeUnique<FADSKInstance>(Definition, Recorder, this, GenerationFloor);
	++InstanceSerial;
	// Any accepted write wakes a sleeping brain, made through this component or on the instance directly.
	Instance->SetQueueListener([this] { WakeUp(); });
}

void UADSKBrainComponent::BindMoveCompleted()
{
	AAIController* Controller = GetAIController();
	if (!Controller || bMoveBound) return;
	Controller->ReceiveMoveCompleted.AddDynamic(this, &UADSKBrainComponent::OnMoveCompleted);
	bMoveBound = true;
}

void UADSKBrainComponent::UnbindMoveCompleted()
{
	if (!bMoveBound) return;
	if (AAIController* Controller = GetAIController()) Controller->ReceiveMoveCompleted.RemoveDynamic(this, &UADSKBrainComponent::OnMoveCompleted);
	bMoveBound = false;
}

void UADSKBrainComponent::UnregisterFromSource()
{
	if (!bRegistered) return;
	if (UADSKWorldSubsystem* Subsystem = WorldSubsystem.Get()) Subsystem->UnregisterBrain(SourcePath, this);
	bRegistered = false;
}

void UADSKBrainComponent::LeaveInstanceCall()
{
	if (--InstanceCallDepth > 0) { return; }
	while (bAsyncDrainPending)
	{
		bAsyncDrainPending = false;
		// Cancellation callbacks are external code: they defer restarts and retirement like node callbacks.
		++InstanceCallDepth;
		FinalizeStoppedAsync();
		--InstanceCallDepth;
	}
	RetiredInstances.Reset();
	// The instance may have started, or stopped itself, inside the call; a paused run that still runs stays paused.
	if (State != EADSKBrainState::CleanedUp)
	{
		const bool bInstanceRunning = Instance && Instance->IsRunning();
		State = !bInstanceRunning ? EADSKBrainState::Stopped : State == EADSKBrainState::Paused ? EADSKBrainState::Paused : EADSKBrainState::Running;
	}
	BroadcastTagsIfChanged();
	if (bStoppedNotificationPending)
	{
		bStoppedNotificationPending = false;
		OnStopped.Broadcast();
	}
	if (bRestartRequested)
	{
		bRestartRequested = false;
		StartLogic();
	}
}

void UADSKBrainComponent::BroadcastTagsIfChanged()
{
	const FADSKInstance* Current = Instance.Get();
	const uint32 Generation = Current ? Current->GetGeneration() : 0;
	const uint64 Serial = Current ? Current->GetActiveTagsSerial() : 0;
	if (Current == TagsInstance && Generation == TagsGeneration && Serial == TagsSerial) return;
	TagsInstance = Current;
	TagsGeneration = Generation;
	TagsSerial = Serial;
	FGameplayTagContainer Tags;
	if (Current)
	{
		for (const FName Name : Current->GetActiveTags())
		{
			const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(Name, false);
			if (Tag.IsValid()) Tags.AddTag(Tag);
		}
	}
	if (Tags == ActiveTags) return;
	ActiveTags = MoveTemp(Tags);
	OnActiveTagsChanged.Broadcast(ActiveTags);
}

void UADSKBrainComponent::FinalizeStoppedAsync()
{
	const TConstArrayView<FADSKAsyncOp> Ops = AsyncOps.GetOps();
	if (Ops.IsEmpty()) { return; }
	FString Leaked;
	for (const FADSKAsyncOp& Op : Ops)
	{
		Leaked += FString::Printf(TEXT("%s%s@%s"), Leaked.IsEmpty() ? TEXT("") : TEXT(", "), *Op.Kind.ToString(), *Op.NodeId.ToString());
	}
	ensureAlwaysMsgf(false, TEXT("AdamskAI stopped with %d unfinished async op(s): %s"), Ops.Num(), *Leaked);
	AsyncOps.CancelAll();
}

bool UADSKBrainComponent::BindMove(FADSKActivationHandle Handle, FAIRequestID RequestId)
{
	ADSK_CHECK_GAME_THREAD();
	FADSKAsyncOp* Op = AsyncOps.Find(Handle);
	if (!Op || Op->Result.IsSet() || !RequestId.IsValid()) { return false; }
	for (const FADSKAsyncOp& Other : AsyncOps.GetOps())
	{
		if (Other.MoveRequest.IsValid()) { return false; }
	}
	Op->MoveRequest = RequestId;
	return true;
}

void UADSKBrainComponent::UnbindMove(FADSKActivationHandle Handle)
{
	ADSK_CHECK_GAME_THREAD();
	if (FADSKAsyncOp* Op = AsyncOps.Find(Handle)) { Op->MoveRequest = FAIRequestID::InvalidRequest; }
}

EADSKAsyncCompletion UADSKBrainComponent::CompleteAsync(FADSKActivationHandle Handle, EADSKNodeStatus Status, TOptional<FADSKValue> Payload)
{
	ADSK_CHECK_GAME_THREAD();
	if (!ensureMsgf(Status != EADSKNodeStatus::Running, TEXT("CompleteAsync cannot complete with Running"))) { return EADSKAsyncCompletion::Stale; }
	FADSKAsyncOp* Op = AsyncOps.Find(Handle);
	if (Op && !Op->Result.IsSet())
	{
		if (!Instance) { return EADSKAsyncCompletion::NotRunning; }
		switch (Instance->PostActionResult(Handle, Status))
		{
		case EADSKInputResult::Accepted:
			Op->Result = Status;
			Op->Payload = MoveTemp(Payload);
			return EADSKAsyncCompletion::Accepted;
		case EADSKInputResult::QueueFull:
			UE_LOG(LogAdamskAIHost, Warning, TEXT("async %s@%s completion deferred: result queue full"), *Op->Kind.ToString(), *Op->NodeId.ToString());
			return EADSKAsyncCompletion::QueueFull;
		default:
			return EADSKAsyncCompletion::NotRunning;
		}
	}
	++StaleAsyncCompletions;
	UE_LOG(LogAdamskAIHost, Verbose, TEXT("stale async completion for activation %u.%u"), Handle.Generation, Handle.ActivationId);
	// Still post it, so the core records ResultStale or ResultDuplicate on the next tick.
	if (Instance && Instance->IsRunning()) { Instance->PostActionResult(Handle, Status); }
	return EADSKAsyncCompletion::Stale;
}

AAIController* UADSKBrainComponent::GetAIController() const
{
	return Cast<AAIController>(GetOwner());
}

AActor* UADSKBrainComponent::GetHostActor() const
{
	if (AAIController* Controller = GetAIController()) { return Controller->GetPawn(); }
	return GetOwner();
}

FAIRequestID UADSKBrainComponent::GetActiveMoveRequest() const
{
	for (const FADSKAsyncOp& Op : AsyncOps.GetOps())
	{
		if (Op.MoveRequest.IsValid()) { return Op.MoveRequest; }
	}
	return FAIRequestID::InvalidRequest;
}

void UADSKBrainComponent::OnMoveCompleted(FAIRequestID RequestId, EPathFollowingResult::Type Result)
{
	FADSKAsyncOp* Op = AsyncOps.FindByMove(RequestId);
	if (!Op) { return; }
	const FADSKActivationHandle Handle = Op->Handle;
	// The tag clears now, not on the next tick: a finished navigation cannot be aborted by the later cancel.
	Op->MoveRequest = FAIRequestID::InvalidRequest;
	CompleteAsync(Handle, Result == EPathFollowingResult::Success ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed);
}

void UADSKBrainComponent::Cleanup()
{
	if (State == EADSKBrainState::CleanedUp) return;
	State = EADSKBrainState::CleanedUp;
	StopLogic();
	UnregisterFromSource();
	UnbindMoveCompleted();
	RetireInstance();
	WorldSubsystem.Reset();
	Super::Cleanup();
}

void UADSKBrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UADSKWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UADSKWorldSubsystem>() : nullptr) Subsystem->RemoveManagedBrain(this);
	Cleanup();
	Super::EndPlay(EndPlayReason);
}
