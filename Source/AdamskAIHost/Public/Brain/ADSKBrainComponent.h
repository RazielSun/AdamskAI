// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Brain/ADSKAsyncOps.h"
#include "Brain/ADSKInputCache.h"
#include "Runtime/ADSKInstance.h"
#include "BrainComponent.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"
#include "Navigation/PathFollowingComponent.h"
#include "ADSKBrainComponent.generated.h"

class AAIController;
class AActor;
class UADSKWorldSubsystem;

DECLARE_MULTICAST_DELEGATE(FOnADSKDefinitionReplaced);
DECLARE_MULTICAST_DELEGATE(FOnADSKBrainPreTick);
DECLARE_MULTICAST_DELEGATE(FOnADSKBrainStopped);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnADSKActiveTagsChanged, const FGameplayTagContainer&);
using FADSKBindingValidator = TFunction<bool(const FADSKDefinition&, FString&)>;

/** Where a brain is in its life. Paused is a running brain whose ticks are skipped. */
enum class EADSKBrainState : uint8
{
	Stopped,
	Running,
	Paused,
	/** Cleaned up and not initialized since: the owner ended this life, so nothing restarts it. */
	CleanedUp,
};

/** A replacement already validated against one brain; applying it cannot fail. */
struct FADSKPreparedReplacement
{
	TSharedPtr<const FADSKDefinition> Definition;
	FADSKInputValues Inputs;
	/** Set: a restart starts from it instead of the initial configuration (a live reload keeping state). */
	TOptional<FADSKSnapshot> Snapshot;
};

UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class ADAMSKAIHOST_API UADSKBrainComponent : public UBrainComponent
{
	GENERATED_BODY()
public:
	UADSKBrainComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	bool InitializeFromSource(FStringView InSourcePath);
	void InitializeDefinition(const TSharedRef<const FADSKDefinition>& Definition, const TSharedPtr<FADSKRecorder>& Recorder = nullptr);
	/**
	 * Resolve once per definition (again on OnDefinitionReplaced) and write by id every tick. A name the file lacks
	 * warns once (not in Shipping) and gives an invalid id, which SetInput and EnqueueEvent refuse with UnknownName
	 * and no trace record.
	 */
	FADSKInputId FindInput(FStringView Name) const;
	FADSKEventId FindEvent(FStringView Name) const;
	EADSKInputResult SetInput(FStringView Name, const FADSKValue& Value);
	EADSKInputResult SetInput(FADSKInputId Input, const FADSKValue& Value);
	EADSKInputResult SetEnumInput(FStringView Name, FStringView ValueName);
	/** Queued for the next brain tick. Unlike inputs, events are never cached: refused with NotRunning when not running. */
	EADSKInputResult EnqueueEvent(FStringView Name, TConstArrayView<FADSKNamedValue> Parameters = {});
	EADSKInputResult EnqueueEvent(FADSKEventId Event, TConstArrayView<FADSKNamedValue> Parameters = {});
	/** The seed of the current or last run; meaningful once StartLogic has run. */
	int32 GetRandomSeed() const;
	bool ValidateBindings(const FADSKDefinition& Definition, FString& OutError);
	bool AddBindingValidator(UObject* Owner, FADSKBindingValidator Validator, FString& OutError);
	void RemoveBindingValidators(UObject* Owner);
	bool RunBindingValidators(const FADSKDefinition& Definition, FString& OutError);
	bool ReplaceDefinition(const TSharedRef<const FADSKDefinition>& Definition, bool bRestart, FString& OutError);
	/** Validates the bindings and stages the cached inputs; no side effects, so a reload can reject all brains at once. */
	bool PrepareReplacement(const TSharedRef<const FADSKDefinition>& Definition, FADSKPreparedReplacement& OutPrepared, FString& OutError);
	/** Stops, swaps in the prepared definition and restarts when asked. Never revalidates. */
	void ApplyReplacement(FADSKPreparedReplacement&& Prepared, bool bRestart);
	bool IsCleanedUp() const { return State == EADSKBrainState::CleanedUp; }
	EADSKBrainState GetState() const { return State; }
	virtual void StartLogic() override;
	/** StartLogic on a snapshot (FADSKInstance::Start(const FADSKSnapshot&)); what it drops is logged. */
	void StartLogicFrom(const FADSKSnapshot& Snapshot);
	/** False when no instance runs; see FADSKSnapshot for what it holds, and FADSKInstance::CaptureSnapshot for what it misses. */
	bool CaptureSnapshot(FADSKSnapshot& Out) const;
	virtual void StopLogic(const FString& Reason = TEXT("")) override;
	/** StopLogic with FADSKInstance::StopAndCaptureSnapshot: the snapshot holds what the cancelled nodes wrote. */
	bool StopLogicAndCaptureSnapshot(FADSKSnapshot& Out);
	/** Stop, then Start with the cached inputs; clears a pause. From a node callback it runs once the brain call returns. */
	virtual void RestartLogic() override;
	/** TickBrain does nothing while paused; no delta accumulates. */
	virtual void PauseLogic(const FString& Reason) override;
	virtual EAILogicResuming::Type ResumeLogic(const FString& Reason) override;
	/** Started and not stopped since; a paused brain is still running. */
	virtual bool IsRunning() const override { return State == EADSKBrainState::Running || State == EADSKBrainState::Paused; }
	virtual FString GetDebugInfoString() const override;
#if ENABLE_VISUAL_LOG
	virtual void DescribeSelfToVisLog(FVisualLogEntry* Snapshot) const override;
#endif
	virtual bool IsPaused() const override { return State == EADSKBrainState::Paused; }
	void TickBrain(float DeltaTime);
	/**
	 * TickBrain split for batched ticking (FADSKParallelTick): the caller ticks GetInstance() only when BeginBrainTick
	 * returns true, then calls EndBrainTick. Game thread, in this order, nothing else in between.
	 */
	bool BeginBrainTick();
	void EndBrainTick();
	const FADSKInstance* GetInstance() const { return Instance.Get(); }
	FADSKInstance* GetInstance() { return Instance.Get(); }
	const TSharedPtr<FADSKRecorder>& GetRecorder() const { return Recorder; }
	const FString& GetSourcePath() const { return SourcePath; }
	const FADSKInputValues& GetCachedInputs() const { return InputCache.GetValues(); }
	const FADSKEnumInputValues& GetCachedEnumInputs() const { return InputCache.GetEnumNames(); }
	const FString& GetLastError() const { return LastError; }
	/** IsRunning, under a name that does not collide with the engine's. */
	bool IsADSKRunning() const { return IsRunning(); }
	const TSharedRef<const FADSKDefinition>& GetDefinition() const { check(Instance); return Instance->GetDefinition(); }
	AAIController* GetAIController() const;
	/** Controller brains host their pawn (or null when unpossessed); all others host their owner actor. */
	AActor* GetHostActor() const;
	/** The one move request the async table currently tags; invalid when none. */
	FAIRequestID GetActiveMoveRequest() const;
	FADSKAsyncOps& GetAsyncOps() { return AsyncOps; }
	const FADSKAsyncOps& GetAsyncOps() const { return AsyncOps; }
	/** Tags an unfinished entry with the one move request this brain may track; false when it cannot. */
	bool BindMove(FADSKActivationHandle Handle, FAIRequestID RequestId);
	/** Drops the entry's move tag, so the completion of the request it named no longer finishes the entry. */
	void UnbindMove(FADSKActivationHandle Handle);
	/** An accepted result is applied on the next TickBrain, never inline. Status must not be Running. */
	EADSKAsyncCompletion CompleteAsync(FADSKActivationHandle Handle, EADSKNodeStatus Status, TOptional<FADSKValue> Payload = {});
	uint32 GetStaleAsyncCompletions() const { return StaleAsyncCompletions; }
#if WITH_AUTOMATION_TESTS
	void CompleteMoveForTest(FAIRequestID RequestId, EPathFollowingResult::Type Result) { OnMoveCompleted(RequestId, Result); }
#endif
	FOnADSKDefinitionReplaced OnDefinitionReplaced;
	/** Immediately before a live instance ticks, for host fact publication. */
	FOnADSKBrainPreTick OnPreTick;
	/** After a running instance stops, its nodes end and async operations drain. */
	FOnADSKBrainStopped OnStopped;
	/**
	 * The gameplay tags of the active states (XML `<State tags="...">`), for a replicated property that drives
	 * animation, VFX or UI on clients. Empty while stopped.
	 */
	const FGameplayTagContainer& GetActiveTags() const { return ActiveTags; }
	/** Once per change of GetActiveTags, at the end of the brain call that changed it; never from inside a node. */
	FOnADSKActiveTagsChanged OnActiveTagsChanged;
	UPROPERTY(EditAnywhere, Category = "AdamskAI")
	bool bAutoTick = false;
	/** The world subsystem ticks this brain at a distance-based rate or ManagedTickInterval. Overrides bAutoTick; nothing else may call TickBrain. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI")
	bool bManagedTick = false;
	/** With bManagedTick: a fixed interval in seconds instead of the distance rate; 0 uses the distance rate. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI", meta = (ClampMin = "0"))
	float ManagedTickInterval = 0.f;
	/** Seed for random nodes. 0 draws a fresh seed each StartLogic; any other value replays the same choices for the same inputs and events. */
	UPROPERTY(EditAnywhere, Category = "AdamskAI")
	int32 RandomSeed = 0;
	virtual void Cleanup() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION()
	void OnMoveCompleted(FAIRequestID RequestId, EPathFollowingResult::Type Result);
	/**
	 * Releases Instance. Node callbacks may reach Cleanup or a re-initialization while the instance is
	 * executing; it is then kept in RetiredInstances until the outermost brain call returns.
	 */
	void RetireInstance();
	/** Replaces the instance with a fresh one of Definition, its generations above the old instance's. */
	void ReplaceInstance(const TSharedRef<const FADSKDefinition>& Definition);
	void StopLogicImpl(FADSKSnapshot* OutSnapshot, bool* bOutCaptured = nullptr);
	void BindMoveCompleted();
	void UnbindMoveCompleted();
	void UnregisterFromSource();
	/** Brackets an Instance call that can run node callbacks. */
	void EnterInstanceCall() { ++InstanceCallDepth; }
	void LeaveInstanceCall();
	/** Cancels the entries a completed stop left behind; they are leaks, since nodes remove their own in OnEnd. */
	void FinalizeStoppedAsync();
	/** Writes every `<Sensors>` entry's out ports to their Inputs; in ports read the latest host writes. After OnPreTick. */
	void SampleSensors();
	/** Slot values a sensor reads and writes; only its own ports' slots are filled. */
	TArray<FADSKValue> SensorSlots;
	struct FBindingValidatorEntry
	{
		TWeakObjectPtr<UObject> Owner;
		FADSKBindingValidator Validator;
	};
	/** Declared before the instances so it is destroyed after them: a running instance's destructor ends its nodes, and their OnEnd releases ops here. */
	FADSKAsyncOps AsyncOps;
	TUniquePtr<FADSKInstance> Instance;
	TArray<TUniquePtr<FADSKInstance>> RetiredInstances;
	TSharedPtr<FADSKRecorder> Recorder;
	TWeakObjectPtr<UADSKWorldSubsystem> WorldSubsystem;
	FString SourcePath;
	FString LastError;
	FADSKInputCache InputCache;
	TArray<FBindingValidatorEntry> BindingValidators;
	uint32 StaleAsyncCompletions = 0;
	int32 InstanceCallDepth = 0;
	EADSKBrainState State = EADSKBrainState::Stopped;
	bool bRestartRequested = false;
	/** The snapshot the next StartLogic starts from; kept across a start deferred until a stop drains. */
	TOptional<FADSKSnapshot> PendingRestore;
	/** A stop was requested and its instance has not yet finished it; drained by the outermost LeaveInstanceCall. */
	bool bAsyncDrainPending = false;
	bool bStoppedNotificationPending = false;
	bool bMoveBound = false;
	bool bWarnedHold = false;
	bool bWarnedAuthority = false;
	/** Rebuilds ActiveTags when the instance's tag serial moved and broadcasts when the container changed. */
	void BroadcastTagsIfChanged();
	FGameplayTagContainer ActiveTags;
	const FADSKInstance* TagsInstance = nullptr;
	uint32 TagsGeneration = 0;
	uint64 TagsSerial = 0;
	/** True without an owner actor or when the owner has authority (server, standalone, a client-local actor). */
	bool HasOwnerAuthority() const;
	/** On a copy without authority: sets LastError to not-authority, warns once, returns true. */
	bool RefuseWithoutAuthority();
#if ENABLE_VISUAL_LOG
	FString VisLogStatePath;
	void VisLogTransition();
#endif
	bool bRegistered = false;
#if !UE_BUILD_SHIPPING
	/** A misspelled Input or event is otherwise only a trace record: warn once per name and kind. */
	void WarnUnknownName(const TCHAR* Kind, FStringView Name) const;
	mutable TSet<FString> WarnedUnknownNames;
	uint64 LastBrainTickFrame = MAX_uint64;
	uint64 LastDoubleTickWarningFrame = MAX_uint64;
#endif
};
