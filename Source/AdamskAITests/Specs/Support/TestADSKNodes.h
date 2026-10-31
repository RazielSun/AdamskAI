// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKNodeRegistry.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagContainer.h"

#include "TestADSKNodes.generated.h"

USTRUCT()
struct FADSKTestMoveConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float AcceptanceRadius = 50.f;
};

/** Configuration with an object reference, used to prove the registry rejects it. */
USTRUCT()
struct FADSKTestObjectConfig
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UObject> Target = nullptr;
};

/** Wraps an object reference one struct deeper than the field list reaches. */
USTRUCT()
struct FADSKTestNestedObjectConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float Radius = 1.f;

	UPROPERTY()
	TArray<FADSKTestObjectConfig> Targets;
};

USTRUCT()
struct FADSKTestValueMemory
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Counter = 0;
};

/** Config with unsupported field kinds next to the three supported ones. */
USTRUCT()
struct FADSKTestFieldConfig
{
	GENERATED_BODY()

	UPROPERTY()
	bool bFlag = false;

	UPROPERTY()
	float Scale = 1.f;

	UPROPERTY()
	FVector Offset = FVector::ZeroVector;

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	FRotator Spin = FRotator::ZeroRotator;

	UPROPERTY()
	int64 Wide = 0;
};

USTRUCT()
struct FADSKTestIntConfig
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Amount = 3;
};

USTRUCT()
struct FADSKTestPathConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Query;

	UPROPERTY()
	float Scale = 1.f;
};

USTRUCT()
struct FADSKTestTextConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FName Section = TEXT("Start");

	UPROPERTY()
	FString Note;
};

USTRUCT()
struct FADSKTestClassTagConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftClassPath Spawn;

	UPROPERTY()
	FGameplayTag Tag;
};

/** A Choice field's enum: an enum class, so the field is an FEnumProperty. */
UENUM()
enum class EADSKTestMood : uint8
{
	Calm,
	Angry,
};

USTRUCT()
struct FADSKTestChoiceConfig
{
	GENERATED_BODY()

	UPROPERTY()
	EADSKTestMood Mood = EADSKTestMood::Calm;

	UPROPERTY()
	TArray<FADSKNamedParam> Params;
};

/** Callbacks capture it as a shared ref, so it outlives every registry and definition built from it. */
struct FADSKTestLeafLog
{
	TArray<FString> Lines;
	TArray<FADSKActivationHandle> Handles;
	TMap<FName, EADSKNodeStatus> ScriptedStarts;
	TMap<FName, EADSKNodeStatus> ScriptedUpdates;
	/** AcquireResource marks itself partially entered only when its authored id is listed. */
	TSet<FName> PartialEntryIds;
	int32 Starts = 0;
	int32 Updates = 0;
	int32 Ends = 0;

	bool IsBalanced() const { return Starts == Ends; }
};

namespace FADSKTestNodes
{
	/** Reverse order exists so schema determinism is checked against genuinely different registration sequences. */
	void RegisterTestNodes(FADSKNodeRegistry& Registry, const TSharedRef<FADSKTestLeafLog>& Log, bool bReverseOrder = false);

	TSharedRef<const FADSKNodeRegistry> MakeFrozenRegistry(const TSharedRef<FADSKTestLeafLog>& Log, bool bReverseOrder = false);
}
