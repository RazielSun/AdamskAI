// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Runtime/ADSKParallelTick.h"

#include "Async/ParallelFor.h"
#include "Program/ADSKDefinition.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ADSKParallelTickPrivate
{
	/** The instance this worker ticks; null on every other thread and outside the pass. */
	thread_local const void* WorkerOwner = nullptr;

	struct FWorkerScope
	{
		explicit FWorkerScope(const void* Owner) : Saved(WorkerOwner) { WorkerOwner = IsInGameThread() ? nullptr : Owner; }
		~FWorkerScope() { WorkerOwner = Saved; }
		const void* Saved;
	};
}

bool ADSK::IsParallelOwner(const void* Owner)
{
	return Owner && ADSKParallelTickPrivate::WorkerOwner == Owner;
}

bool ADSK::IsInParallelTick()
{
	return ADSKParallelTickPrivate::WorkerOwner != nullptr;
}

void FADSKParallelTick::Tick(TConstArrayView<FADSKInstance*> Instances, TConstArrayView<float> Deltas, TArrayView<EADSKTickResult> OutResults)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FADSKParallelTick::Tick);
	ADSK_CHECK_GAME_THREAD();
	check(Instances.Num() == Deltas.Num() && Instances.Num() == OutResults.Num());
	TArray<int32, TInlineAllocator<64>> Parallel;
	TArray<int32, TInlineAllocator<64>> Serial;
	TSet<const FADSKRecorder*, DefaultKeyFuncs<const FADSKRecorder*>, TInlineSetAllocator<64>> Recorders;
	for (int32 Index = 0; Index < Instances.Num(); ++Index)
	{
		// Two instances writing one recorder from two workers would race on its ring; the second ticks serially.
		const FADSKRecorder* Recorder = Instances[Index]->GetRecorder().Get();
		bool bShared = false;
		if (Recorder) Recorders.Add(Recorder, &bShared);
		(Instances[Index]->GetDefinition()->IsThreadSafe() && !bShared ? Parallel : Serial).Add(Index);
	}
	ParallelFor(TEXT("AdamskAI.ParallelTick"), Parallel.Num(), FADSKParallelTick::MinBatchSize, [&](int32 Item)
	{
		const int32 Index = Parallel[Item];
		const ADSKParallelTickPrivate::FWorkerScope Scope(Instances[Index]);
		OutResults[Index] = Instances[Index]->Tick(Deltas[Index]);
	});
	for (const int32 Index : Serial)
	{
		OutResults[Index] = Instances[Index]->Tick(Deltas[Index]);
	}
}
