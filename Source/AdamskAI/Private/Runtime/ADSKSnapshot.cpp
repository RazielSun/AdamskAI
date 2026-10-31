// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKSnapshot.h"

#include "Runtime/ADSKInstanceInternal.h"
#include "Algo/Reverse.h"

namespace ADSKSnapshotPrivate
{
	FADSKSnapshotValue Save(const FADSKDefinition& Definition, const FString& Name, const FADSKValue& Value)
	{
		FADSKSnapshotValue Saved{Name, Value};
		if (Value.Type == EADSKValueType::Enum)
		{
			Definition.ResolveEnumValue(Value, Saved.EnumType, Saved.EnumValue);
		}
		return Saved;
	}

	/** The saved value as this definition's slot type; an enum by its type and value names, never its index. */
	bool Load(const FADSKDefinition& Definition, const FADSKSnapshotValue& Saved, const FADSKSlotDefinition& Slot, FADSKValue& Out)
	{
		if (Saved.Value.Type != Slot.Type)
		{
			return false;
		}
		if (Slot.Type != EADSKValueType::Enum)
		{
			Out = Saved.Value;
			return true;
		}
		if (!Definition.GetEnums().IsValidIndex(Slot.Enum) || !Definition.GetEnums()[Slot.Enum].Name.Equals(Saved.EnumType, ESearchCase::CaseSensitive))
		{
			return false;
		}
		const TOptional<FADSKValue> Value = Definition.MakeEnumValue(Saved.EnumType, Saved.EnumValue);
		if (!Value.IsSet())
		{
			return false;
		}
		Out = Value.GetValue();
		return true;
	}
}

using namespace ADSKSnapshotPrivate;

bool FADSKInstance::CaptureSnapshot(FADSKSnapshot& Out) const
{
	ADSK_CHECK_GAME_THREAD();
	Out = FADSKSnapshot();
	if (!Impl->bRunning)
	{
		return false;
	}
	const FADSKDefinition& Definition = *Impl->Definition;
	Out.AgentId = Definition.GetAgentId();
	Out.StatePath = GetActiveStatePath();
	const TConstArrayView<FADSKSlotDefinition> Slots = Definition.GetSlots();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (!Slots[Slot].bIsInput)
		{
			Out.Locals.Add(Save(Definition, Slots[Slot].Name, Impl->Slots[Slot]));
		}
	}
	for (int32 Index = 0; Index < Impl->ChangeEvents.Num(); ++Index)
	{
		const FADSKEventDefinition& Event = Definition.GetEvents()[Impl->ChangeEvents[Index]];
		FADSKSnapshotBaseline& Baseline = Out.ChangeBaselines.AddDefaulted_GetRef();
		static_cast<FADSKSnapshotValue&>(Baseline) = Save(Definition, Event.Name, Impl->ChangeSeen[Index]);
		Baseline.Slot = Slots[Event.ChangeSlot].Name;
		Baseline.bSlotIsInput = Slots[Event.ChangeSlot].bIsInput;
	}
	for (int32 Node = 0; Node < Impl->NodeStates.Num(); ++Node)
	{
		if (Impl->NodeStates[Node].IsValid())
		{
			Out.NodeStates.Add({Definition.GetNodes()[Node].AuthoredId, Impl->NodeStates[Node]});
		}
	}
	for (int32 State = 0; State < Impl->LastChild.Num(); ++State)
	{
		if (Impl->LastChild[State] != INDEX_NONE)
		{
			Out.History.Add({Definition.GetStates()[State].Id, Definition.GetStates()[Impl->LastChild[State]].Id});
		}
	}
	Out.Clock = Impl->Clock;
	Out.RandomState = Impl->Random.GetCurrentSeed();
	Out.bHolding = Impl->bHold;
	return true;
}

void FADSKInstance::FImpl::Restore(const FADSKSnapshot& Snapshot, TArray<FString>& OutDropped)
{
	// Everything is still matched by name: a reload that renamed the agent keeps its state, and the host learns of it.
	// A snapshot built by hand may leave the id empty.
	if (!Snapshot.AgentId.IsEmpty() && !Snapshot.AgentId.Equals(Definition->GetAgentId(), ESearchCase::CaseSensitive))
	{
		OutDropped.Add(TEXT("agent ") + Snapshot.AgentId);
	}
	const TConstArrayView<FADSKSlotDefinition> SlotDefinitions = Definition->GetSlots();
	for (const FADSKSnapshotValue& Saved : Snapshot.Locals)
	{
		const int32 Slot = Definition->FindSlot(Saved.Name);
		FADSKValue Value;
		if (Slot == INDEX_NONE || SlotDefinitions[Slot].bIsInput || !Load(*Definition, Saved, SlotDefinitions[Slot], Value))
		{
			OutDropped.Add(TEXT("local ") + Saved.Name);
			continue;
		}
		if (Slots[Slot] != Value)
		{
			Slots[Slot] = Value;
			Record({.Kind = EADSKRecordKind::SlotWritten, .Slot = Slot, .bHasValue = true, .Value = Value});
		}
	}
	// A baseline that cannot be read becomes the slot's current value, as at a plain Start: no change is raised for it.
	// So does one whose event now watches another slot, or the same name turned from Local to Input or back.
	for (int32 Index = 0; Index < ChangeEvents.Num(); ++Index)
	{
		const FADSKEventDefinition& Event = Definition->GetEvents()[ChangeEvents[Index]];
		const FADSKSlotDefinition& Watched = SlotDefinitions[Event.ChangeSlot];
		ChangeSeen[Index] = Slots[Event.ChangeSlot];
		const FADSKSnapshotBaseline* Saved = Snapshot.ChangeBaselines.FindByPredicate([&Event](const FADSKSnapshotBaseline& Each)
		{
			return Each.Name.Equals(Event.Name, ESearchCase::CaseSensitive);
		});
		FADSKValue Value;
		if (Saved && Saved->Slot.Equals(Watched.Name, ESearchCase::CaseSensitive) && Saved->bSlotIsInput == Watched.bIsInput
			&& Load(*Definition, *Saved, Watched, Value))
		{
			ChangeSeen[Index] = Value;
		}
	}
	for (const FADSKSnapshotNodeState& Saved : Snapshot.NodeStates)
	{
		const int32 Node = Definition->FindNode(FName(*Saved.Node));
		if (Node == INDEX_NONE || !NodeStates[Node].IsValid() || NodeStates[Node].GetScriptStruct() != Saved.State.GetScriptStruct())
		{
			OutDropped.Add(TEXT("node-state ") + Saved.Node);
			continue;
		}
		NodeStates[Node] = Saved.State;
	}
	// Kept only while the child is still a child of that state.
	for (const FADSKSnapshotHistory& Saved : Snapshot.History)
	{
		const int32 State = Definition->FindState(Saved.State);
		const int32 Child = Definition->FindState(Saved.Child);
		if (State == INDEX_NONE || Child == INDEX_NONE || Definition->GetStates()[Child].Parent != State)
		{
			OutDropped.Add(TEXT("history ") + Saved.State);
			continue;
		}
		LastChild[State] = Child;
	}
	Clock = Snapshot.Clock;
	Random.Initialize(Snapshot.RandomState);

	int32 Resume = INDEX_NONE;
	for (int32 Index = Snapshot.StatePath.Num() - 1; Index >= 0 && Resume == INDEX_NONE; --Index)
	{
		Resume = Definition->FindState(Snapshot.StatePath[Index]);
	}
	const bool bSameLeaf = !Snapshot.StatePath.IsEmpty() && Resume != INDEX_NONE && Definition->GetStates()[Resume].Id == Snapshot.StatePath.Last();
	if (!Snapshot.StatePath.IsEmpty() && !bSameLeaf)
	{
		OutDropped.Add(TEXT("state ") + Snapshot.StatePath.Last());
	}
	if (Resume == INDEX_NONE)
	{
		EnterInitialConfiguration();
		return;
	}
	TArray<int32, TInlineAllocator<8>> Chain;
	for (int32 State = Resume; State != INDEX_NONE; State = Definition->GetStates()[State].Parent)
	{
		Chain.Add(State);
	}
	Algo::Reverse(Chain);
	for (const int32 State : Chain)
	{
		EnterState(State);
	}
	EnterEntryChain(true);
	RefreshActiveTags();
	// A held brain back in the same leaf stays held: running its finished tree again would repeat what it did.
	if (Snapshot.bHolding && bSameLeaf && ActivePath.Last() == Resume)
	{
		bHold = true;
		Record({.Kind = EADSKRecordKind::Hold, .Reason = EADSKRecordReason::Restored, .State = Resume});
		return;
	}
	StartLeafProcedure();
}
