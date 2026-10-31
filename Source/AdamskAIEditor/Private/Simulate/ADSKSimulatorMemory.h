// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

#include "ADSKSimulatorMemory.generated.h"

/** A simulated leaf's activation memory: seconds since it started. */
USTRUCT()
struct FADSKSimLeafMemory
{
	GENERATED_BODY()

	UPROPERTY()
	float Elapsed = 0.f;
};
