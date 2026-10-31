// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKManagedTicker.h"

#include "Brain/ADSKBrainComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Program/ADSKDefinition.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKParallelTick.h"
#include "Trace/ADSKStats.h"

DECLARE_CYCLE_STAT(TEXT("Managed pass"), STAT_ADSKManagedPass, STATGROUP_AdamskAI);
#include "Settings/ADSKSettings.h"

void FADSKManagedTicker::Add(UADSKBrainComponent* Brain)
{
	if (!Brain || Managed.ContainsByPredicate([Brain](const FManagedBrain& Entry) { return Entry.Brain == Brain; })) return;
	FManagedBrain& Entry = Managed.AddDefaulted_GetRef();
	Entry.Brain = Brain;
	// Golden-ratio steps spread any number of brains evenly over an interval, in the order they were added.
	Entry.Phase = FMath::Frac(static_cast<float>(Added++) * 0.6180339887f);
}

void FADSKManagedTicker::Remove(UADSKBrainComponent* Brain)
{
	// During a pass entries are addressed by index, so a removal only clears its entry; the next pass compacts.
	if (bInPass)
	{
		for (FManagedBrain& Entry : Managed) if (Entry.Brain == Brain) Entry.Brain.Reset();
		return;
	}
	Managed.RemoveAll([Brain](const FManagedBrain& Entry) { return !Entry.Brain.IsValid() || Entry.Brain == Brain; });
}

void FADSKManagedTicker::Tick(float Delta, TConstArrayView<FVector> Viewers)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FADSKManagedTicker::Tick);
	SCOPE_CYCLE_COUNTER(STAT_ADSKManagedPass);
	CSV_SCOPED_TIMING_STAT(AdamskAI, ManagedPass);
	const double Start = FPlatformTime::Seconds();
	TickPass(Delta, Viewers);
	++Stats.Passes;
	Stats.Seconds += FPlatformTime::Seconds() - Start;
}

void FADSKManagedTicker::TickPass(float Delta, TConstArrayView<FVector> Viewers)
{
	if (!ensureMsgf(!bInPass, TEXT("a managed brain's tick started another managed pass"))) return;
	Managed.RemoveAll([](const FManagedBrain& Entry) { return !Entry.Brain.IsValid(); });
	if (Managed.IsEmpty()) return;
	TGuardValue<bool> InPass(bInPass, true);

	// First decide, calling nothing that could change the set; a brain added by a tick below waits for the next pass.
	DueScratch.Reset();
	// Without LOD levels every distance gives the full rate, so no distance is measured.
	const bool bDistanceRates = !Viewers.IsEmpty() && !GetDefault<UADSKSettings>()->TickLOD.IsEmpty();
	for (int32 Index = 0; Index < Managed.Num(); ++Index)
	{
		FManagedBrain& Entry = Managed[Index];
		const UADSKBrainComponent* Brain = Entry.Brain.Get();
		if (!Brain) continue;
		// A sleeping brain keeps gathering time and costs nothing else until it is due or woken.
		if (Brain->IsAsleep(Entry.Accumulated + Delta)) { Entry.Accumulated += Delta; continue; }
		float Interval = Brain->ManagedTickInterval;
		if (Interval <= 0.f && bDistanceRates)
		{
			const AActor* Host = Brain->GetHostActor();
			double Nearest = TNumericLimits<double>::Max();
			for (const FVector& Viewer : Viewers) Nearest = FMath::Min(Nearest, Host ? FVector::Dist(Viewer, Host->GetActorLocation()) : 0.0);
			Interval = UADSKSettings::GetTickInterval(Nearest);
		}
		// The first tick comes after its phase of an interval; every tick hands over all the time accumulated, so Wait
		// and every timer see the same total as at full rate.
		const float Due = Entry.bTicked ? Interval : Entry.Phase * Interval;
		Entry.Accumulated += Delta;
		if (Entry.Accumulated + KINDA_SMALL_NUMBER < Due) continue;
		DueScratch.Add({Index, Entry.Accumulated});
		Entry.Accumulated = 0.f;
		Entry.bTicked = true;
	}

	// Then tick. A tick may end other brains: their entries are cleared, and Managed only grows during the pass.
	if (!GetDefault<UADSKSettings>()->bParallelManagedTick)
	{
		for (const FManagedDue& Due : DueScratch)
		{
			if (UADSKBrainComponent* Brain = Managed[Due.Index].Brain.Get()) { Brain->TickBrain(Due.Delta); ++Stats.BrainTicks; }
		}
		return;
	}
	// Publication, the instance ticks, then what follows them; only the middle step leaves the game thread.
	TickingScratch.Reset();
	InstanceScratch.Reset();
	DeltaScratch.Reset();
	for (const FManagedDue& Due : DueScratch)
	{
		UADSKBrainComponent* Brain = Managed[Due.Index].Brain.Get();
		if (!Brain || !Brain->BeginBrainTick()) continue;
		TickingScratch.Add(Brain);
		InstanceScratch.Add(Brain->GetInstance());
		DeltaScratch.Add(Due.Delta);
		Stats.ParallelTicks += Brain->GetInstance()->GetDefinition()->IsThreadSafe() ? 1 : 0;
	}
	Stats.BrainTicks += InstanceScratch.Num();
	ResultScratch.SetNumZeroed(InstanceScratch.Num(), EAllowShrinking::No);
	FADSKParallelTick::Tick(InstanceScratch, DeltaScratch, ResultScratch);
	for (UADSKBrainComponent* Brain : TickingScratch) Brain->EndBrainTick();
}
