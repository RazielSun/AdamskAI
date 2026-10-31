// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Trace/Trace.h"

/**
 * `-trace=default,AdamskAI` (or `Trace.Enable AdamskAI`): a timing region per active state, a CPU scope per node
 * callback and a bookmark per hold or failed start. Emitted by the instance rather than the recorder, so a build
 * without recorders can still trace. Off, each costs one channel check.
 */
UE_TRACE_CHANNEL_EXTERN(AdamskAIChannel);

#define ADSK_TRACE_ENABLED() UE_TRACE_CHANNELEXPR_IS_ENABLED(AdamskAIChannel)
