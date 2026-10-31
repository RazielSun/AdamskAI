// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKDefinition;

namespace ADSKTest
{
	/**
	 * Deterministic, for comparing compiled programs across compiler changes. Fields introduced after the
	 * legacy format are omitted while they hold their compatibility defaults; source lines are not emitted.
	 */
	ADAMSKAI_API FString DumpNormalizedTables(const FADSKDefinition& Definition);
}
