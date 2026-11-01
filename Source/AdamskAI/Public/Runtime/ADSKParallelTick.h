// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Runtime/ADSKInstance.h"

/**
 * Thread-safe definitions tick on workers, the rest after on the game thread (as does one sharing an earlier
 * instance's recorder); same outcome as ticking in order. Nothing may call into these instances meanwhile.
 */
struct ADAMSKAI_API FADSKParallelTick
{
	/** Instances per worker task: one instance tick is far cheaper than scheduling a task for it alone. */
	static constexpr int32 MinBatchSize = 16;

	/** One Delta per instance; OutResults receives each instance's Tick result. Game thread only. */
	static void Tick(TConstArrayView<FADSKInstance*> Instances, TConstArrayView<float> Deltas, TArrayView<EADSKTickResult> OutResults);
};
