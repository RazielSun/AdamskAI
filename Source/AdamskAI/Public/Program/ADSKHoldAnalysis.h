// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKDefinition;

/** A leaf state whose tree can end with an outcome that nothing on its state path handles for certain. */
struct FADSKHoldRisk
{
	int32 State = INDEX_NONE;
	int32 Line = 0;
	/** tree.success, or tree.failure when false. */
	bool bSuccess = true;
	/** Handlers exist but every one has a condition, so the completion holds when none passes. */
	bool bConditionalOnly = false;
};

namespace ADSK
{
	/**
	 * Conservative: an outcome counts as possible unless the node kinds rule it out. Valid files may hold on
	 * purpose, so callers report these as notes, never as errors.
	 */
	ADAMSKAI_API TArray<FADSKHoldRisk> FindHoldRisks(const FADSKDefinition& Definition);

	ADAMSKAI_API FString DescribeHoldRisk(const FADSKDefinition& Definition, const FADSKHoldRisk& Risk);
}
