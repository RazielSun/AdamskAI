// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "UObject/SoftObjectPath.h"
#include "ADSKHostNodeTypes.generated.h"

USTRUCT()
struct FADSKMoveToConfig
{
	GENERATED_BODY()
	UPROPERTY() float AcceptanceRadius = 50.f;
	/** Re-path once the goal slot has moved this far from the goal of the running request; 0 never re-paths. */
	UPROPERTY() float RepathDistance = 0.f;
	/** False: a goal the path cannot reach fails the move instead of ending it at the closest reachable point. */
	UPROPERTY() bool bAllowPartialPath = true;
	/** False: the pawn turns to its path even while a focus is set (the StateTree's bAllowStrafe off). */
	UPROPERTY() bool bStrafe = true;
};
USTRUCT()
struct FADSKMoveToMemory
{
	GENERATED_BODY()
	UPROPERTY() FVector Goal = FVector::ZeroVector;
};
/** MoveToActor: path following tracks the actor itself, so there is no repath distance. */
USTRUCT()
struct FADSKMoveToActorConfig
{
	GENERATED_BODY()
	UPROPERTY() float AcceptanceRadius = 50.f;
	/** As FADSKMoveToConfig::bAllowPartialPath. */
	UPROPERTY() bool bAllowPartialPath = true;
	/** As FADSKMoveToConfig::bStrafe. */
	UPROPERTY() bool bStrafe = true;
};
USTRUCT()
struct FADSKMoveToActorMemory
{
	GENERATED_BODY()
	UPROPERTY() uint64 Target = 0;
};
USTRUCT()
struct FADSKLookAtConfig
{
	GENERATED_BODY()
	UPROPERTY() float Duration = .5f;
};
USTRUCT()
struct FADSKLookAtMemory
{
	GENERATED_BODY()
	UPROPERTY() float Elapsed = 0.f;
	UPROPERTY() FVector Point = FVector::ZeroVector;
};
UENUM()
enum class EADSKQueryPick : uint8
{
	Best,
	RandomBest5,
	RandomBest25,
};
USTRUCT()
struct FADSKRunEQSConfig
{
	GENERATED_BODY()
	UPROPERTY() FSoftObjectPath Query;
	/** Kept for v1 files; `pick="RandomBest25"` says the same. Both at once are rejected. */
	UPROPERTY() bool bRandomBest25 = false;
	UPROPERTY() EADSKQueryPick Pick = EADSKQueryPick::Best;
	/** `<Param name="..." value="..." type="..."/>` children: the query's named Float, Int or Bool parameters. */
	UPROPERTY() TArray<FADSKNamedParam> Params;
};
USTRUCT()
struct FADSKRunEQSMemory
{
	GENERATED_BODY()
	UPROPERTY() int32 QueryId = INDEX_NONE;
};
USTRUCT()
struct FADSKLookAtActorMemory
{
	GENERATED_BODY()
	UPROPERTY() float Elapsed = 0.f;
	UPROPERTY() uint64 Target = 0;
};
USTRUCT()
struct FADSKFocusMemory
{
	GENERATED_BODY()
	UPROPERTY() FVector Point = FVector::ZeroVector;
};
USTRUCT()
struct FADSKFocusActorMemory
{
	GENERATED_BODY()
	UPROPERTY() uint64 Target = 0;
};
USTRUCT()
struct FADSKMoveSpeedConfig
{
	GENERATED_BODY()
	UPROPERTY() float Speed = 600.f;
};
USTRUCT()
struct FADSKHasGameplayTagConfig
{
	GENERATED_BODY()
	UPROPERTY() FGameplayTag Tag;
};
USTRUCT()
struct FADSKLineOfSightConfig
{
	GENERATED_BODY()
	/** Half angle in degrees around the host's facing the target must be in; 180 sees all around. */
	UPROPERTY() float Cone = 180.f;
	/** Traces from the eyes to the target's bounds, top to bottom; one clear trace is enough. 1 traces to its center. */
	UPROPERTY() int32 Traces = 1;
};
USTRUCT()
struct FADSKPlayerPawnConfig
{
	GENERATED_BODY()
	/** The local player's index, as UGameplayStatics::GetPlayerPawn takes it. */
	UPROPERTY() int32 Player = 0;
	/** Farther than this (cm) the pawn reads as none; 0 has no limit. */
	UPROPERTY() float Range = 0.f;
};
USTRUCT()
struct FADSKGroupTargetConfig
{
	GENERATED_BODY()
	/** Seconds a sighting stays the group's target. */
	UPROPERTY() float Memory = 10.f;
};
USTRUCT()
struct FADSKMoveSpeedMemory
{
	GENERATED_BODY()
	/** The walk speed the scope replaced; restored at the end unless something else changed it since. */
	UPROPERTY() float Previous = 0.f;
};
USTRUCT()
struct FADSKActConfig
{
	GENERATED_BODY()
	/** The name the performer knows the action by. */
	UPROPERTY() FName Action;
};
USTRUCT()
struct FADSKHoldTokenConfig
{
	GENERATED_BODY()
	/** The token pool; its size per target is UADSKSettings::TokenPools. */
	UPROPERTY() FName Pool = TEXT("Attack");
};
USTRUCT()
struct FADSKHoldTokenMemory
{
	GENERATED_BODY()
	/** The target the token was taken on, returned on it whatever the slot says by then. */
	UPROPERTY() uint64 Target = 0;
};
USTRUCT()
struct FADSKRadioConfig
{
	GENERATED_BODY()
	/** The event every other group member's brain receives; their files declare and handle it. */
	UPROPERTY() FName Event;
	/** Seconds until it arrives; a caller destroyed or out of the group by then is cut off. */
	UPROPERTY() float Delay = 0.f;
};
USTRUCT()
struct FADSKActMemory
{
	GENERATED_BODY()
	/** The target the action was started or last retargeted at. */
	UPROPERTY() uint64 Target = 0;
};
