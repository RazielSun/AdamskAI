// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"

class FADSKDefinition;

/** Map key functions for input names, which are exact-case like every XML name; plain FString keys ignore case. */
template <typename ValueType>
struct TADSKExactNameKeyFuncs : BaseKeyFuncs<TPair<FString, ValueType>, FString, false>
{
	static const FString& GetSetKey(const TPair<FString, ValueType>& Element) { return Element.Key; }
	static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

/** Host-written input values by exact input name: what a brain replays into a replacement instance. */
using FADSKInputValues = TMap<FString, FADSKValue, FDefaultSetAllocator, TADSKExactNameKeyFuncs<FADSKValue>>;
/** Enum input values by exact input name as (enum, value) names, which survive a reload that renumbers them. */
using FADSKEnumInputValues = TMap<FString, TPair<FString, FString>, FDefaultSetAllocator, TADSKExactNameKeyFuncs<TPair<FString, FString>>>;

/** Host-written inputs kept by name, so a reload can validate them against a new definition and replay them. */
class ADAMSKAIHOST_API FADSKInputCache
{
public:
	void Record(const FADSKDefinition& Definition, int32 Slot, const FADSKValue& Value);

	/** Every cached input still exists in Definition with its type (and enum value), and every required input is cached. */
	bool Validate(const FADSKDefinition& Definition, FString& OutError) const;

	/** The values to write into an instance of Definition, enum values resolved by name; false when one no longer resolves. */
	bool Stage(const FADSKDefinition& Definition, FADSKInputValues& OutValues, FString& OutError) const;

	void Adopt(FADSKInputValues&& Staged);

	/** The last value written to Slot, an Input of Definition; null when none was. */
	const FADSKValue* FindLatest(const FADSKDefinition& Definition, int32 Slot) const;

	void Reset();

	const FADSKInputValues& GetValues() const { return Values; }
	const FADSKEnumInputValues& GetEnumNames() const { return EnumNames; }

private:
	FADSKInputValues Values;
	FADSKEnumInputValues EnumNames;
	/** Last value per slot for the definition with this serial: hosts republish every frame, so repeats skip name hashing. */
	uint64 LastSerial = 0;
	TArray<TOptional<FADSKValue>> LastBySlot;
};
