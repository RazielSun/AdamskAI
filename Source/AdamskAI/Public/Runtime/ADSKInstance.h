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
	/**
	 * A start from a snapshot: what the definition could not take (`state x`, `local y`, `node-state z`, `history w`),
	 * in snapshot order, after `agent v` when the snapshot names another agent id (restored all the same).
	 */
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
	 * Node callbacks may call SetInput, EnqueueEvent, PostActionResult and Stop on their own instance.
	 * Stop from a callback is deferred: nothing new starts, and the outermost Start, Tick or Stop
	 * performs it before returning. Start and Tick from a callback return Reentrant. Destroying the
	 * instance from its own callback is a fatal error; owners defer it (UADSKBrainComponent does).
	 */
	FADSKStartReport Start();
	/**
	 * Start, but on a snapshot instead of the initial configuration: it enters the deepest state of the snapshot's
	 * path this definition still has, then that state's entry children, and takes every Local, node state and change
	 * baseline whose name and type still match; the leaf state's tree starts from its root. The random stream and
	 * the clock continue. Inputs are this instance's, as for Start. What it leaves out is in the report's Dropped.
	 */
	FADSKStartReport Start(const FADSKSnapshot& Snapshot);
	EADSKTickResult Tick(float Delta);
	void Stop();
	/**
	 * False, with Out reset, when the instance is not running. Taken while the procedure still runs: what its nodes would
	 * write when cancelled (a Cooldown's ready time) is not in it. To stop anyway, StopAndCaptureSnapshot includes that.
	 */
	bool CaptureSnapshot(FADSKSnapshot& Out) const;
	/**
	 * Stop, capturing the snapshot after the running procedure is cancelled and before the states are left. False, Out
	 * reset, when it is not running or when called from a callback (the stop is then only deferred, as Stop's).
	 */
	bool StopAndCaptureSnapshot(FADSKSnapshot& Out);

	/**
	 * Every Start reseeds the node random stream with it, so a run is a pure function of seed, inputs,
	 * events and deltas. Takes effect at the next Start. A Start from a snapshot continues the snapshot's stream instead,
	 * so after one this seed no longer reproduces the draws.
	 */
	void SetRandomSeed(int32 Seed);
	int32 GetRandomSeed() const;

	bool IsRunning() const;
	bool IsHolding() const;
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
	/**
	 * The deepest active node of every procedure lane: the main path first, then the background child of each
	 * running SimpleParallel in start order. OutLeafHandles is parallel to it, invalid where that node is no leaf.
	 */
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
