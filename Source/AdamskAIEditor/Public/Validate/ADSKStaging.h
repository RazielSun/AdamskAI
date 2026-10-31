// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

namespace ADSKStaging
{
	/** Entries are raw DirectoriesToAlwaysStageAs(Non)UFS values such as `(Path="AI")`; staging an ancestor of Root counts. */
	ADAMSKAIEDITOR_API bool IsStagedBy(const FString& Root, const TArray<FString>& StageEntries);
}
