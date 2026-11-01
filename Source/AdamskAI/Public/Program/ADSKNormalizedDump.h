// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKDefinition;

namespace ADSKTest
{
	/** Deterministic; fields newer than the legacy format are omitted while at their defaults. No source lines. */
	ADAMSKAI_API FString DumpNormalizedTables(const FADSKDefinition& Definition);
}
