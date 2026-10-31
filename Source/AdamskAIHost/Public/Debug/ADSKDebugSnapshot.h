// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

// Debug and tooling surface: declared and defined only outside Shipping, like the console commands that use it.
#if !UE_BUILD_SHIPPING

class AActor;
class UADSKBrainComponent;
class UWorld;

enum class EADSKDebugDetail : uint8
{
	/** Names, location, state path and running nodes: what a world label shows, cheap enough for every frame. */
	Labels,
	Full,
};

enum class EADSKDebugExecState : uint8
{
	Stopped,
	Running,
	Holding,
};

struct FADSKDebugNamedValue
{
	FString Name;
	FString Type;
	FString Value;
};

struct FADSKDebugEventSchema
{
	FString Name;
	int32 Priority = 0;
	/** Ordered parameters; Value is always empty. */
	TArray<FADSKDebugNamedValue> Params;
};

/** One live async operation, copied by value. MoveRequest is empty when the operation holds no move tag. */
struct FADSKDebugAsyncOp
{
	FName Kind;
	FName NodeId;
	/** Activation handle as `<generation>.<activation>`. */
	FString Handle;
	float AgeSeconds = 0.f;
	FString MoveRequest;
};

/** One brain, copied by value on the game thread. Retains no UObject, definition or recorder pointer. */
struct FADSKBrainDebugRow
{
	FString BrainPath, OwnerName, PawnName;
	bool bHasLabelLocation = false;
	FVector LabelLocation = FVector::ZeroVector;
	FString SourcePath, CompositeHash, Diagnostics, LastError;
	bool bHostRunning = false;
	EADSKDebugExecState ExecState = EADSKDebugExecState::Stopped;
	uint32 Generation = 0;
	uint64 TickCount = 0;
	TArray<FString> ActiveStatePath;
	/** The active states' tags; printed as a `tags=` line when there are any. */
	TArray<FName> ActiveTags;
	FName ActiveNodeId;
	/** The deepest node of each running SimpleParallel's background child, in start order. */
	TArray<FName> BackgroundNodeIds;
	TArray<FADSKDebugNamedValue> CachedInputs, Inputs, Locals;
	TArray<FString> QueuedEvents;
	/** Every table entry, including accepted results waiting for the next tick. */
	TArray<FADSKDebugAsyncOp> PendingOps;
	uint32 StaleAsyncCompletions = 0;
	TArray<FADSKDebugEventSchema> Events;
	TArray<FString> RecentRecords;
};

/** Renderer-free observation of every brain in one world, shared by text and visual consumers. */
struct ADAMSKAIHOST_API FADSKDebugSnapshot
{
	static constexpr int32 RecordTail = 16;

	TArray<FADSKBrainDebugRow> Rows;

	/** Game thread only. Null or unsupported worlds yield an empty snapshot. */
	static FADSKDebugSnapshot Collect(const UWorld* World, EADSKDebugDetail Detail = EADSKDebugDetail::Full);
	/** One brain, registered or not; what the Gameplay Debugger and Visual Logger show. Game thread only. */
	static FADSKBrainDebugRow Describe(const UADSKBrainComponent& Brain);
	/** The brain an actor shows: its own, or for a pawn its controller's; null when neither has one. */
	static const UADSKBrainComponent* FindBrainFor(const AActor* Actor);
	/** The Gameplay Debugger category's text: the brain's row with color tags, or a grey line without a brain. */
	static TArray<FString> GameplayDebuggerLines(const AActor* DebugActor);
	/** The legacy line, then two-space-indented continuation lines: one row of ToLines. */
	static TArray<FString> FormatRow(const FADSKBrainDebugRow& Row);
	/** The `adsk.Status` per-brain line, byte-identical to the former command format. */
	static FString FormatLegacyLine(const FADSKBrainDebugRow& Row);
	/** `<kind>@<node>(<age %.1f>s)[,...]` or `none`; the text `adsk.Status` and the panel share. */
	static FString FormatOps(const FADSKBrainDebugRow& Row);
	TArray<FString> ToLines() const;
	FString ToText() const;
};

#endif // !UE_BUILD_SHIPPING
