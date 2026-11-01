// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Trace/Trace.h"

/** `-trace=default,AdamskAI`: a region per state, a CPU scope per callback, a bookmark per hold. */
UE_TRACE_CHANNEL_EXTERN(AdamskAIChannel);

#define ADSK_TRACE_ENABLED() UE_TRACE_CHANNELEXPR_IS_ENABLED(AdamskAIChannel)
