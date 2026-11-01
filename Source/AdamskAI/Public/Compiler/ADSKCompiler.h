// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Program/ADSKDiagnostic.h"
#include "CoreMinimal.h"

class FADSKNodeRegistry;

struct ADAMSKAI_API FADSKCompiler
{
	static constexpr int32 MaxSourceBytes = 256 * 1024;
	static constexpr int32 MaxElements = 1024;
	static constexpr int32 MaxTreeDepth = 16;
	static constexpr int32 MaxStateDepth = 8;
	static constexpr int32 MaxStates = 64;
	/** Bounds `Run` expansion: final node-table size and cumulative generated node-id characters. */
	static constexpr int32 MaxExpandedNodes = 4096;
	static constexpr int32 MaxGeneratedIdChars = 1024 * 1024;

	/** Any thread. The registry must be frozen; an unfrozen one ensures and yields `missing-required`. */
	static FADSKCompileResult Compile(
		const FString& SourceName, const FString& Text, const TSharedRef<const FADSKNodeRegistry>& Registry);
};
