// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Program/ADSKDefinition.h"
#include "Runtime/ADSKSnapshot.h"
#include "Trace/ADSKTrace.h"
#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"

namespace ADSK
{
	constexpr int32 ExternalEventCapacity = 64;
	/** A full result queue still accepts one result per running leaf, so stale posts cannot strand one. */
	constexpr int32 ActionResultCapacity = 64;
	constexpr int32 EventDispatchBudget = 16;
	constexpr int32 ProcedureOperationBudget = 64;
}

enum class EADSKInputResult : uint8
{
	Accepted,
	UnknownName,
	TypeMismatch,
	MissingParameter,
	UnexpectedParameter,
	Reserved,
	QueueFull,
	NotRunning,
};

enum class EADSKStartResult : uint8
{
	Started,
	AlreadyRunning,
	MissingRequiredInput,
	/** Called from inside one of this instance's own node callbacks. */
	Reentrant,
};

enum class EADSKTickResult : uint8
{
	Advanced,
	NotRunning,
	InvalidDelta,
	/** Called from inside one of this instance's own node callbacks. */
	Reentrant,
};

/** Belongs to one definition: after a reload it is refused as UnknownName and must be resolved again. */
struct FADSKInputId
{
	/** FADSKDefinition::GetSerial of the definition it was resolved against. */
	uint64 Definition = 0;
	int32 Slot = INDEX_NONE;

	bool IsValid() const { return Definition != 0; }
};

/** A declared, non-reserved event resolved once by name; scoped to one definition like FADSKInputId. */
struct FADSKEventId
{
	uint64 Definition = 0;
	int32 Event = INDEX_NONE;

	bool IsValid() const { return Definition != 0; }
};

struct FADSKStartReport
{
	EADSKStartResult Result = EADSKStartResult::Started;
	/** Missing required Input names, in definition slot order. */
	TArray<FString> MissingInputs;
	/** What a snapshot start could not take (`state x`, `local y`, `node-state z`, `history w`; `agent v` first). */
	TArray<FString> Dropped;

	bool IsSuccess() const { return Result == EADSKStartResult::Started; }
};

/** Every operation, query and destruction is game-thread only. */
class ADAMSKAI_API FADSKInstance
{
public:
	/** Opaque runtime storage; named publicly only so private executor translation units can share it. */
	struct FImpl;

	explicit FADSKInstance(const TSharedRef<const FADSKDefinition>& InDefinition,
		const TSharedPtr<FADSKRecorder>& InRecorder = nullptr, TWeakObjectPtr<UObject> Host = nullptr,
		uint32 InitialGeneration = 0);
	~FADSKInstance();

	FADSKInstance(const FADSKInstance&) = delete;
	FADSKInstance& operator=(const FADSKInstance&) = delete;
	FADSKInstance(FADSKInstance&&) = delete;
	FADSKInstance& operator=(FADSKInstance&&) = delete;

	/** Invalid when the name is not an Input of this instance's definition. */
	FADSKInputId FindInput(FStringView Name) const;
	/** Invalid when the name is not a declared event, or is a reserved one. */
	FADSKEventId FindEvent(FStringView Name) const;

	/** A NaN or infinite Float or Vector, here and in event parameters, is refused as TypeMismatch. */
	EADSKInputResult SetInput(FStringView Name, const FADSKValue& Value);
	EADSKInputResult SetInput(FADSKInputId Input, const FADSKValue& Value);
	EADSKInputResult SetEnumInput(FStringView Name, FStringView ValueName);
	EADSKInputResult EnqueueEvent(FStringView Name, TConstArrayView<FADSKNamedValue> Parameters = {});
	EADSKInputResult EnqueueEvent(FADSKEventId Event, TConstArrayView<FADSKNamedValue> Parameters = {});
	EADSKInputResult PostActionResult(FADSKActivationHandle Handle, EADSKNodeStatus Status);

	/**
	 * Callbacks may call SetInput, EnqueueEvent, PostActionResult and Stop on their own instance; Stop is deferred to
	 * the outermost call, Start and Tick return Reentrant. Destroying the instance from its own callback is fatal.
	 */
	FADSKStartReport Start();
	/**
	 * Start on a snapshot: enters the deepest snapshot state still defined and takes every Local, node state and
	 * baseline whose name and type match; the leaf tree restarts. Random stream and clock continue.
	 */
	FADSKStartReport Start(const FADSKSnapshot& Snapshot);
	EADSKTickResult Tick(float Delta);
	void Stop();
	/** False, Out reset, when not running. Excludes what nodes write on cancel; StopAndCaptureSnapshot includes it. */
	bool CaptureSnapshot(FADSKSnapshot& Out) const;
	/** Stop, capturing after the procedure is cancelled. False when not running or called from a callback. */
	bool StopAndCaptureSnapshot(FADSKSnapshot& Out);

	/** Applied at the next Start: a run is a pure function of seed, inputs, events and deltas. */
	void SetRandomSeed(int32 Seed);
	int32 GetRandomSeed() const;

	bool IsRunning() const;
	bool IsHolding() const;
	/**
	 * Tick seconds until time alone can change anything: 0 when the next tick has work, ADSK::NeverWake when only
	 * SetInput, EnqueueEvent or PostActionResult can. Skipped time in one Tick runs the same. Not from a callback.
	 */
	float GetNextWake();
	/** After every accepted external input, event or result, so a sleeping host ticks again. */
	void SetQueueListener(TFunction<void()> Listener);
	uint32 GetGeneration() const;
	uint64 GetTickCount() const;
	TArray<FString> GetActiveStatePath() const;
	/** The `tags` of every active state (a state's tags hold while it or a descendant is active); empty when stopped. */
	TConstArrayView<FName> GetActiveTags() const;
	/** Moves each time the active tag set changes; never for a transition that keeps the same set. */
	uint64 GetActiveTagsSerial() const;
	/** Bytes of the arena every node activation's memory is carved from, alignment padding included. */
	int32 GetNodeMemoryBytes() const;
	/** Heap and object bytes this instance owns, node state structs included; the recorder is not (it may be shared). */
	SIZE_T GetAllocatedBytes() const;
	bool GetValue(FStringView SlotName, FADSKValue& OutValue) const;
	FADSKActivationHandle GetActiveLeafHandle() const;
	/** Authored ID of the deepest active procedure node; NAME_None when no node is active. */
	FName GetActiveNodeId() const;
	/** Deepest active node per lane, main path first; OutLeafHandles parallel, invalid where the node is no leaf. */
	void GetActiveNodes(TArray<FName>& OutNodeIds, TArray<FADSKActivationHandle>& OutLeafHandles) const;
	/** Resets Out, then appends pending external event names in dispatch order. */
	void GetQueuedEvents(TArray<FString>& OutEventNames) const;
	const TSharedRef<const FADSKDefinition>& GetDefinition() const;
	const TSharedPtr<FADSKRecorder>& GetRecorder() const;

private:
	FADSKStartReport StartFrom(const FADSKSnapshot* Snapshot);
	void StopImpl(FADSKSnapshot* OutSnapshot);

	TUniquePtr<FImpl> Impl;
};

ADAMSKAI_API const TCHAR* LexToString(EADSKInputResult Result);
ADAMSKAI_API const TCHAR* LexToString(EADSKStartResult Result);
ADAMSKAI_API const TCHAR* LexToString(EADSKTickResult Result);
