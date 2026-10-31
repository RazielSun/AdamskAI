// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FADSKInstance;
class UADSKBrainComponent;
enum class EADSKTickResult : uint8;

/** What the managed passes did since the last reset; for benchmarks and `stat AdamskAI`. */
struct FADSKManagedTickStats
{
	uint64 Passes = 0;
	/** Wall time inside the passes, brain ticks and their host work included. */
	double Seconds = 0.0;
	uint64 BrainTicks = 0;
	/** Of BrainTicks, those that ran on workers (a thread-safe definition under bParallelManagedTick). */
	uint64 ParallelTicks = 0;
};

/** Brains with bManagedTick: each accumulates delta and ticks once its fixed or distance-based interval has passed. */
class ADAMSKAIHOST_API FADSKManagedTicker
{
public:
	void Add(UADSKBrainComponent* Brain);
	void Remove(UADSKBrainComponent* Brain);
	int32 Num() const { return Managed.Num(); }

	/** Without viewers, every brain ticks every frame. */
	void Tick(float Delta, TConstArrayView<FVector> Viewers);

	const FADSKManagedTickStats& GetStats() const { return Stats; }
	void ResetStats() { Stats = FADSKManagedTickStats(); }

private:
	void TickPass(float Delta, TConstArrayView<FVector> Viewers);

	struct FManagedBrain
	{
		TWeakObjectPtr<UADSKBrainComponent> Brain;
		float Accumulated = 0.f;
		/** In [0, 1): the share of an interval before the first tick, so equal rates do not all tick in one frame. */
		float Phase = 0.f;
		bool bTicked = false;
	};
	struct FManagedDue
	{
		int32 Index = INDEX_NONE;
		float Delta = 0.f;
	};

	TArray<FManagedBrain> Managed;
	uint32 Added = 0;
	/** Set while Tick runs: removals then clear entries instead of shifting the indices it holds. */
	bool bInPass = false;
	/** Kept between passes, so a frame allocates nothing once the fleet's size has been reached. */
	TArray<FManagedDue> DueScratch;
	TArray<UADSKBrainComponent*> TickingScratch;
	TArray<FADSKInstance*> InstanceScratch;
	TArray<float> DeltaScratch;
	TArray<EADSKTickResult> ResultScratch;
	FADSKManagedTickStats Stats;
};
