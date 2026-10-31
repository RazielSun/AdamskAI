// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Objects/ADSKObjectHandles.h"

#include "UObject/Object.h"
#include "UObject/ObjectKey.h"

namespace ADSKObjectHandlesPrivate
{
	struct FEntry
	{
		TWeakObjectPtr<UObject> Object;
		FObjectKey Key;
	};

	struct FTable
	{
		TMap<FObjectKey, uint64> Ids;
		TMap<uint64, FEntry> Entries;
		uint64 NextId = 1;
		int32 PruneAt = 256;

		/** Amortized: the table only grows past twice what is still alive. */
		void PruneIfFull()
		{
			if (Entries.Num() < PruneAt)
			{
				return;
			}
			for (auto It = Entries.CreateIterator(); It; ++It)
			{
				if (!It.Value().Object.IsValid())
				{
					Ids.Remove(It.Value().Key);
					It.RemoveCurrent();
				}
			}
			PruneAt = FMath::Max(256, Entries.Num() * 2);
		}
	};

	FTable& Table()
	{
		static FTable Instance;
		return Instance;
	}
}

using namespace ADSKObjectHandlesPrivate;

FADSKValue FADSKObjectHandles::Make(const UObject* Object)
{
	ADSK_CHECK_GAME_THREAD();
	if (!Object)
	{
		return FADSKValue::MakeObject(0);
	}
	FTable& Handles = Table();
	// FObjectKey carries the serial number, so a new object in a recycled slot gets a new id.
	const FObjectKey Key(Object);
	if (const uint64* Existing = Handles.Ids.Find(Key))
	{
		return FADSKValue::MakeObject(*Existing);
	}
	Handles.PruneIfFull();
	const uint64 Id = Handles.NextId++;
	Handles.Ids.Add(Key, Id);
	Handles.Entries.Add(Id, {const_cast<UObject*>(Object), Key});
	return FADSKValue::MakeObject(Id);
}

UObject* FADSKObjectHandles::Resolve(uint64 Id)
{
	ADSK_CHECK_GAME_THREAD();
	const FEntry* Entry = Id != 0 ? Table().Entries.Find(Id) : nullptr;
	return Entry ? Entry->Object.Get() : nullptr;
}

UObject* FADSKObjectHandles::Resolve(const FADSKValue& Value)
{
	return Value.Type == EADSKValueType::Object ? Resolve(Value.ObjectId) : nullptr;
}

FString FADSKObjectHandles::Describe(const FADSKValue& Value)
{
	if (Value.Type != EADSKValueType::Object || Value.ObjectId == 0)
	{
		return LexToString(Value);
	}
	const UObject* Object = Resolve(Value);
	return FString::Printf(TEXT("%s (%s)"), *LexToString(Value), Object ? *Object->GetName() : TEXT("destroyed"));
}

int32 FADSKObjectHandles::GetTrackedCountForTest()
{
	return Table().Entries.Num();
}
