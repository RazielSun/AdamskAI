// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "ADSKBuiltinNodes.generated.h"

class FADSKNodeRegistry;

/** The `type` of a `<Param>`; Float when omitted. */
UENUM()
enum class EADSKParamType : uint8
{
	Float,
	Int,
	Bool,
};

USTRUCT()
struct ADAMSKAI_API FADSKNamedParam
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	/** Float value; Int and Bool params keep theirs in IntValue and bBoolValue. */
	UPROPERTY()
	float Value = 0.f;

	UPROPERTY()
	EADSKParamType Type = EADSKParamType::Float;

	UPROPERTY()
	int32 IntValue = 0;

	UPROPERTY()
	bool bBoolValue = false;
};

/** Wait and TimeLimit, in tick seconds; `deviation` draws uniformly around `duration` once per start, floored at 0. */
USTRUCT()
struct ADAMSKAI_API FADSKWaitConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Duration = 1.f;

	UPROPERTY()
	float Deviation = 0.f;
};

USTRUCT()
struct ADAMSKAI_API FADSKWaitMemory
{
	GENERATED_BODY()

	UPROPERTY()
	float Elapsed = 0.f;

	UPROPERTY()
	float Target = 0.f;
};

/** Roll writes a uniform Int in [0, count) to its `value` port and succeeds. */
USTRUCT()
struct ADAMSKAI_API FADSKRollConfig
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 2;
};

USTRUCT()
struct ADAMSKAI_API FADSKChanceConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Probability = .5f;
};

/** Order is inline so an activation never allocates; that bounds the children a RandomSelector may have. */
USTRUCT()
struct ADAMSKAI_API FADSKRandomSelectorMemory
{
	GENERATED_BODY()

	static constexpr int32 MaxChildren = 64;

	UPROPERTY()
	uint8 Order[MaxChildren] = {};

	UPROPERTY()
	int32 Tried = 0;
};

/** ReactiveSelector: bit i is whether option i (a Guard) held when it was last checked. */
USTRUCT()
struct ADAMSKAI_API FADSKReactiveSelectorMemory
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 Seen = 0;
};

/** Order is best score first; Cursor is the running option's position in it. */
USTRUCT()
struct ADAMSKAI_API FADSKUtilitySelectorMemory
{
	GENERATED_BODY()

	static constexpr int32 MaxOptions = ADSK::MaxReactiveChildren;

	UPROPERTY()
	uint8 Order[MaxOptions] = {};

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	int32 Cursor = 0;

	UPROPERTY()
	double LastScored = 0.0;
};

/** Repeat: `count` successes in a row; Retry: `count` attempts. 0 means until the child fails (succeeds). */
USTRUCT()
struct ADAMSKAI_API FADSKRepeatConfig
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;
};

USTRUCT()
struct ADAMSKAI_API FADSKRepeatMemory
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Done = 0;
};

/** Parallel: `success` / `failure` are `all`, `any` or a count (above the child count means all). */
USTRUCT()
struct ADAMSKAI_API FADSKParallelConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FString Success = TEXT("all");

	UPROPERTY()
	FString Failure = TEXT("any");
};

/** Which children of a Parallel ended, and which of those succeeded, one bit per child. */
USTRUCT()
struct ADAMSKAI_API FADSKParallelMemory
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Done = 0;

	UPROPERTY()
	uint8 Succeeded = 0;
};

namespace ADSK
{
	/** At most this many children run in one Parallel. */
	inline constexpr int32 MaxParallelChildren = 8;
	/** A Parallel policy's child count: `all` is Children, `any` 1, a count clamped to Children; 0 for bad text. */
	ADAMSKAI_API int32 ParallelThreshold(const FString& Policy, int32 Children);
}

/** Cooldown: `duration` seconds. */
USTRUCT()
struct ADAMSKAI_API FADSKDurationConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Duration = 1.f;
};

/** Cooldown: whether this activation runs its child; the cooldown starts when such a run ends in any way. */
USTRUCT()
struct ADAMSKAI_API FADSKCooldownMemory
{
	GENERATED_BODY()

	UPROPERTY()
	bool bRan = false;
};

/** Cooldown: the instance time from which the child may run again; kept across activations. */
USTRUCT()
struct ADAMSKAI_API FADSKCooldownState
{
	GENERATED_BODY()

	UPROPERTY()
	double ReadyAt = 0.0;
};

USTRUCT()
struct ADAMSKAI_API FADSKTimeLimitMemory
{
	GENERATED_BODY()

	UPROPERTY()
	float Elapsed = 0.f;

	/** The limit drawn at start, as Wait draws its duration. */
	UPROPERTY()
	float Limit = 0.f;
};

/** UtilitySelector: `reevaluate` seconds between rescoring (0 scores once per start) and the `margin` a switch needs. */
USTRUCT()
struct ADAMSKAI_API FADSKUtilityConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Reevaluate = 0.f;

	UPROPERTY()
	float Margin = 0.f;
};

/** Score: an option's utility, `weight * in-value + bias`, around the one child it runs. */
USTRUCT()
struct ADAMSKAI_API FADSKScoreConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Weight = 1.f;

	UPROPERTY()
	float Bias = 0.f;
};

USTRUCT()
struct ADAMSKAI_API FADSKGuardConfig
{
	GENERATED_BODY()

	UPROPERTY()
	bool bInvert = false;
};

struct ADAMSKAI_API FADSKBuiltinNodes
{
	/** Every registration must be accepted. */
	static void RegisterAll(FADSKNodeRegistry& Registry);
};
