// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "ADSKGroupSubsystem.generated.h"

class UADSKBrainComponent;
struct FADSKNamedValue;

/** Groups and what members share (sensed targets, radio, attack tokens), so no brain reaches another. Game thread. */
UCLASS()
class ADAMSKAIHOST_API UADSKGroupSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UADSKGroupSubsystem* Get(const UObject* WorldContext);

	/** Moves Member into Group, out of any other; the earliest member still in it is its leader. */
	void Join(AActor& Member, FName Group);
	void Leave(const AActor& Member);
	/** NAME_None when Member is in no group. */
	FName GetGroup(const AActor& Member) const;
	/** Live members in joining order. */
	TArray<AActor*> GetMembers(FName Group) const;
	AActor* GetLeader(FName Group) const;

	/** Member sees Target: it becomes its group's target, sighted now. Nothing for a member of no group. */
	void ReportTarget(const AActor& Member, AActor* Target);
	/** Member's group's target while it lives and was sighted at most MaxAge seconds ago; null otherwise. */
	AActor* GetGroupTarget(const AActor& Member, double MaxAge) const;

	/**
	 * Queues Event in every other member's brain after Delay; dropped if Sender is gone or left by then. Members whose
	 * file lacks Event refuse it. False when Sender is in no group.
	 */
	bool Broadcast(const AActor& Sender, FName Event, float Delay, TConstArrayView<FADSKNamedValue> Parameters = {});
	/** The brain on Actor itself, or on its controller when Actor is a pawn; null when neither has one. */
	static UADSKBrainComponent* FindBrain(const AActor* Actor);

	/**
	 * True when Holder holds one of Pool's tokens on Target (pool size from UADSKSettings::TokenPools, default 1).
	 * Counted: kept until returned as often as taken.
	 */
	bool TakeToken(FName Pool, const AActor* Target, const UObject& Holder);
	void ReturnToken(FName Pool, const AActor* Target, const UObject& Holder);
	/** Live holders of Pool's tokens on Target. */
	int32 CountHolders(FName Pool, const AActor* Target) const;
	static int32 GetPoolSize(FName Pool);

private:
	struct FGroup
	{
		TArray<TWeakObjectPtr<AActor>> Members;
		TWeakObjectPtr<AActor> Target;
		double SightingTime = 0.0;
	};
	using FTokenKey = TPair<FName, TObjectKey<AActor>>;
	struct FTokenHolder
	{
		TWeakObjectPtr<const UObject> Holder;
		/** Takes not yet returned. */
		int32 Takes = 0;
	};

	double Now() const;
	const FGroup* FindGroupOf(const AActor& Member) const;
	void Deliver(TWeakObjectPtr<const AActor> Sender, FName Group, FName Event, TConstArrayView<FADSKNamedValue> Parameters);

	TMap<FName, FGroup> Groups;
	TMap<TObjectKey<AActor>, FName> MemberGroups;
	TMap<FTokenKey, TArray<FTokenHolder>> Tokens;
};
