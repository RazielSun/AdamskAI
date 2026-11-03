// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Nodes/ADSKEnvQueryContexts.h"
#include "EnvQueryContexts.generated.h"

UCLASS(NotBlueprintable, Transient)
class UADSKTestEnvQueryContext_Point : public UADSKEnvQueryContext_Slot
{
	GENERATED_BODY()
public:
	UADSKTestEnvQueryContext_Point() { SlotName = TEXT("Point"); }
};

UCLASS(NotBlueprintable, Transient)
class UADSKTestEnvQueryContext_Flag : public UADSKEnvQueryContext_Slot
{
	GENERATED_BODY()
public:
	UADSKTestEnvQueryContext_Flag() { SlotName = TEXT("Flag"); }
};
