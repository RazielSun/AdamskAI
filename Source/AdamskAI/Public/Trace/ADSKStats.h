// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"

/** `stat AdamskAI`: the core's counters here, the host's cycle stats and counters in its own files. */
DECLARE_STATS_GROUP(TEXT("AdamskAI"), STATGROUP_AdamskAI, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Events dispatched"), STAT_ADSKEventsDispatched, STATGROUP_AdamskAI, ADAMSKAI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Node starts"), STAT_ADSKNodeStarts, STATGROUP_AdamskAI, ADAMSKAI_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Records written"), STAT_ADSKRecordsWritten, STATGROUP_AdamskAI, ADAMSKAI_API);

/** Instances, recorders and definitions, in `stat LLM` and memreport. */
LLM_DECLARE_TAG_API(AdamskAI, ADAMSKAI_API);

/** `csvprofile` timings, for PerfReportTool comparisons of packaged runs. */
CSV_DECLARE_CATEGORY_MODULE_EXTERN(ADAMSKAI_API, AdamskAI);
