// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"

/**
 * Instances with a thread-safe definition tick concurrently on workers; the rest tick afterwards on the game thread
 * in the given order, as does one sharing its recorder with an instance ticked earlier. The outcome equals ticking
 * them one by one. Nothing may call into these instances while the pass runs.
 */
struct ADAMSKAI_API FADSKParallelTick
{
	/** Instances per worker task: one instance tick is far cheaper than scheduling a task for it alone. */
	static constexpr int32 MinBatchSize = 16;

	/** One Delta per instance; OutResults receives each instance's Tick result. Game thread only. */
	static void Tick(TConstArrayView<FADSKInstance*> Instances, TConstArrayView<float> Deltas, TArrayView<EADSKTickResult> OutResults);
};
