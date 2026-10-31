// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKInputCache.h"

#include "Program/ADSKDefinition.h"

void FADSKInputCache::Record(const FADSKDefinition& Definition, int32 Slot, const FADSKValue& Value)
{
	if (LastSerial != Definition.GetSerial())
	{
		LastSerial = Definition.GetSerial();
		LastBySlot.Reset();
		LastBySlot.SetNum(Definition.GetSlots().Num());
	}
	TOptional<FADSKValue>& Last = LastBySlot[Slot];
	if (Last.IsSet() && Last.GetValue() == Value)
	{
		return;
	}
	Last = Value;
	// The definition owns the name, so a changed write finds its entry without building a key.
	const FString& Key = Definition.GetSlots()[Slot].Name;
	if (Value.Type == EADSKValueType::Enum)
	{
		FString TypeName, ValueName;
		// verify, not check: the call fills the names and must also run in Shipping and Test.
		verify(Definition.ResolveEnumValue(Value, TypeName, ValueName));
		EnumNames.FindOrAdd(Key) = TPair<FString, FString>(MoveTemp(TypeName), MoveTemp(ValueName));
	}
	else if (!EnumNames.IsEmpty()) EnumNames.Remove(Key);
	Values.FindOrAdd(Key) = Value;
}

bool FADSKInputCache::Validate(const FADSKDefinition& Definition, FString& OutError) const
{
	for (const TPair<FString, FADSKValue>& Input : Values)
	{
		if (EnumNames.Contains(Input.Key)) continue;
		const int32 Index = Definition.FindSlot(Input.Key);
		if (Index == INDEX_NONE || !Definition.GetSlots()[Index].bIsInput || Definition.GetSlots()[Index].Type != Input.Value.Type)
		{
			OutError = FString::Printf(TEXT("input binding changed: %s"), *Input.Key);
			return false;
		}
	}
	for (const TPair<FString, TPair<FString, FString>>& Input : EnumNames)
	{
		const int32 Index = Definition.FindSlot(Input.Key);
		const FADSKSlotDefinition* Slot = Index == INDEX_NONE ? nullptr : &Definition.GetSlots()[Index];
		const bool bSameEnum = Slot && Slot->bIsInput && Slot->Type == EADSKValueType::Enum && Definition.GetEnums().IsValidIndex(Slot->Enum)
			&& Definition.GetEnums()[Slot->Enum].Name.Equals(Input.Value.Key, ESearchCase::CaseSensitive);
		if (!bSameEnum || !Definition.MakeEnumValue(Input.Value.Key, Input.Value.Value).IsSet())
		{
			OutError = FString::Printf(TEXT("input binding changed: %s"), *Input.Key);
			return false;
		}
	}
	for (const FADSKSlotDefinition& Slot : Definition.GetSlots())
	{
		if (Slot.bIsInput && Slot.bRequired && !Values.Contains(Slot.Name) && !EnumNames.Contains(Slot.Name))
		{
			OutError = FString::Printf(TEXT("required input missing: %s"), *Slot.Name);
			return false;
		}
	}
	return true;
}

bool FADSKInputCache::Stage(const FADSKDefinition& Definition, FADSKInputValues& OutValues, FString& OutError) const
{
	OutValues = Values;
	for (const TPair<FString, TPair<FString, FString>>& Input : EnumNames)
	{
		const TOptional<FADSKValue> Value = Definition.MakeEnumValue(Input.Value.Key, Input.Value.Value);
		if (!Value.IsSet()) { OutError = FString::Printf(TEXT("input binding changed: %s"), *Input.Key); return false; }
		OutValues.Add(Input.Key, Value.GetValue());
	}
	return true;
}

void FADSKInputCache::Adopt(FADSKInputValues&& Staged)
{
	Values = MoveTemp(Staged);
	// The staged values are what the replacement instance holds now; repeats are judged against them afresh.
	LastSerial = 0;
	LastBySlot.Reset();
}

const FADSKValue* FADSKInputCache::FindLatest(const FADSKDefinition& Definition, int32 Slot) const
{
	if (LastSerial == Definition.GetSerial() && LastBySlot.IsValidIndex(Slot) && LastBySlot[Slot].IsSet())
	{
		return &LastBySlot[Slot].GetValue();
	}
	return Values.Find(Definition.GetSlots()[Slot].Name);
}

void FADSKInputCache::Reset()
{
	Values.Reset();
	EnumNames.Reset();
	LastSerial = 0;
	LastBySlot.Reset();
}
