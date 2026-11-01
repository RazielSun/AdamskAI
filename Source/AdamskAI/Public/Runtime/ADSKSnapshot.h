// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

/** A slot value by name; an enum also carries its type and value names, so another definition can read it. */
struct FADSKSnapshotValue
{
	FString Name;
	FADSKValue Value;
	FString EnumType;
	FString EnumValue;
};

/** A change event's baseline, with the slot it watched, so it is kept only while the event still watches that slot. */
struct FADSKSnapshotBaseline : FADSKSnapshotValue
{
	FString Slot;
	bool bSlotIsInput = false;
};

/** A state's history: the child that was active when the state was last left, both by id. */
struct FADSKSnapshotHistory
{
	FString State;
	FString Child;
};

/** One node's StateStruct (a cooldown's ready time), by the node's authored id. */
struct FADSKSnapshotNodeState
{
	FString Node;
	FInstancedStruct State;
};

/**
 * An instance's decisions by name: state path, Locals, node state, change baselines, random stream, clock. Not
 * running procedures, queued events or results. Object values are process-local ids.
 */
struct FADSKSnapshot
{
	FString AgentId;
	/** Root first, as FADSKInstance::GetActiveStatePath. */
	TArray<FString> StatePath;
	TArray<FADSKSnapshotValue> Locals;
	/** Per change event, by event name: the value it last raised for. */
	TArray<FADSKSnapshotBaseline> ChangeBaselines;
	TArray<FADSKSnapshotNodeState> NodeStates;
	/** Every state left at least once since the start, for `history` states and what a deep history resumes below them. */
	TArray<FADSKSnapshotHistory> History;
	double Clock = 0.0;
	/** FRandomStream::GetCurrentSeed: the stream continues where it was. */
	int32 RandomState = 0;
	/** The instance held: its leaf tree had finished with no handler. A restore to the same leaf holds again. */
	bool bHolding = false;
};
