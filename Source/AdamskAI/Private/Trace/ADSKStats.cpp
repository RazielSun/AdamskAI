// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Trace/ADSKStats.h"

DEFINE_STAT(STAT_ADSKEventsDispatched);
DEFINE_STAT(STAT_ADSKNodeStarts);
DEFINE_STAT(STAT_ADSKRecordsWritten);

LLM_DEFINE_TAG(AdamskAI);

CSV_DEFINE_CATEGORY_MODULE(ADAMSKAI_API, AdamskAI, true);
