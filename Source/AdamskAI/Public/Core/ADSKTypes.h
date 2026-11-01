// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
class FADSKDefinition;

/** Every public AdamskAI entry point is game-thread only. */
#define ADSK_CHECK_GAME_THREAD() checkf(IsInGameThread(), TEXT("AdamskAI is game-thread only"))
/** The game thread, or the worker FADSKParallelTick lent this instance to for its tick. */
#define ADSK_CHECK_OWNER(Owner) checkf(IsInGameThread() || ADSK::IsParallelOwner(Owner), TEXT("AdamskAI is game-thread only outside its parallel tick"))

enum class EADSKValueType : uint8
{
	Bool,
	Float,
	Vector,
	Int,
	Enum,
	/** An opaque id the host resolves to an object; 0 is `none`. The core only stores and compares it. */
	Object,
};

/** Only the member matching Type is meaningful and compared; NaN equals NaN so a NaN slot never looks changed. */
struct ADAMSKAI_API FADSKValue
{
	EADSKValueType Type = EADSKValueType::Bool;
	bool bBool = false;
	float Float = 0.f;
	FVector Vector = FVector::ZeroVector;
	int32 Int = 0;
	/** Definition-relative enum type index; Int is the value index. */
	int16 Enum = INDEX_NONE;
	uint64 ObjectId = 0;

	static FADSKValue MakeBool(bool bInValue);
	static FADSKValue MakeFloat(float InValue);
	static FADSKValue MakeVector(const FVector& InValue);
	static FADSKValue MakeInt(int32 InValue);
	static FADSKValue MakeEnum(int16 InEnum, int32 InValue);
	static FADSKValue MakeObject(uint64 InId);

	static FADSKValue MakeZero(EADSKValueType InType);

	/** False for a Float or Vector holding a NaN or an infinity; the host boundary refuses such values. */
	bool IsFinite() const;
	bool operator==(const FADSKValue& Other) const;
	bool operator!=(const FADSKValue& Other) const { return !(*this == Other); }
};

/** Identifies one node activation; stale results are recognised by comparing handles. */
struct ADAMSKAI_API FADSKActivationHandle
{
	uint32 Generation = 0;
	uint32 ActivationId = 0;

	/** Activation ids start at one, so a default handle names nothing. */
	bool IsValid() const { return ActivationId != 0; }
	bool operator==(const FADSKActivationHandle& Other) const
	{
		return Generation == Other.Generation && ActivationId == Other.ActivationId;
	}
	bool operator!=(const FADSKActivationHandle& Other) const { return !(*this == Other); }
};

namespace ADSK
{
	/** True on the worker that ticks Owner inside FADSKParallelTick. */
	ADAMSKAI_API bool IsParallelOwner(const void* Owner);
	/** True on a worker while it ticks any instance inside FADSKParallelTick. */
	ADAMSKAI_API bool IsInParallelTick();

	/** Child bound meaning "no upper limit" in a node descriptor. */
	constexpr int32 UnboundedChildren = MAX_int32;

	/** A reactive selector remembers one condition bit per option. */
	constexpr int32 MaxReactiveChildren = 64;

	/** Exactly `true` or `false`. */
	ADAMSKAI_API bool TryParseBool(FStringView Text, bool& OutValue);

	/** `-?digits(.digits)?`; no whitespace, plus sign, exponent, NaN or infinity. Result must fit a float. */
	ADAMSKAI_API bool TryParseFloat(FStringView Text, float& OutValue);

	/** `-?[0-9]+` inside the int32 range; no whitespace, plus sign, decimal point, exponent or hex. */
	ADAMSKAI_API bool TryParseInt(FStringView Text, int32& OutValue);

	/** `X,Y,Z`, each component a strict float literal; no whitespace around the commas. */
	ADAMSKAI_API bool TryParseVector(FStringView Text, FVector& OutValue);

	/** The only Object literal is `none`. */
	ADAMSKAI_API bool TryParseValue(EADSKValueType Type, FStringView Text, FADSKValue& OutValue);
	/** Letter followed by letters, digits or dashes. */
	ADAMSKAI_API bool IsIdentifier(FStringView Text);
	/** Definition-aware value text; enum values use `Type:Value`, invalid indices use `Enum#e:v`. */
	ADAMSKAI_API FString FormatValue(const FADSKDefinition& Definition, const FADSKValue& Value);
}

/** Exact-case map keys: XML names and behavior paths are exact-case, plain FString keys are not. */
template <typename ValueType>
struct TADSKExactKeyFuncs : BaseKeyFuncs<TPair<FString, ValueType>, FString, false>
{
	static const FString& GetSetKey(const TPair<FString, ValueType>& Element) { return Element.Key; }
	static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

/** Set key functions for exact-case strings; see TADSKExactKeyFuncs. */
struct FADSKExactSetKeyFuncs : BaseKeyFuncs<FString, FString, false>
{
	static const FString& GetSetKey(const FString& Element) { return Element; }
	static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

/** A set of exact-case strings. */
using FADSKExactStringSet = TSet<FString, FADSKExactSetKeyFuncs>;
/** A map keyed by exact-case strings. */
template <typename ValueType>
using TADSKExactMap = TMap<FString, ValueType, FDefaultSetAllocator, TADSKExactKeyFuncs<ValueType>>;

ADAMSKAI_API const TCHAR* LexToString(EADSKValueType Type);
ADAMSKAI_API FString LexToString(const FADSKValue& Value);
