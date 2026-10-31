// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

// Debug and tooling surface: declared and defined only outside Shipping, like the console commands that use it.
#if !UE_BUILD_SHIPPING

class UADSKBrainComponent;

struct ADAMSKAIHOST_API FADSKTraceExportResult
{
	FString Path;
	FString Error;
	bool IsSuccess() const { return Error.IsEmpty() && !Path.IsEmpty(); }
};

/** Writes one brain recorder as a standalone, atomically-published JSONL file. */
struct ADAMSKAIHOST_API FADSKTraceExport
{
	static FADSKTraceExportResult Write(const UADSKBrainComponent& Brain, const FString& WorldName, const FString& Root = FString());
};

#endif // !UE_BUILD_SHIPPING
