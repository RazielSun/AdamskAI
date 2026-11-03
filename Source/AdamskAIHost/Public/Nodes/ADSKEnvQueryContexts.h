// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"

#include "ADSKEnvQueryContexts.generated.h"

/**
 * An EQS context read from a slot of the querier's brain (its own, or its controller's for a pawn) when the query
 * runs: an Object slot gives its actor, a Vector slot its point. Empty while the slot holds `none` or the brain is
 * stopped; a querier without a brain, or a slot missing or of another type, also warns once per context class.
 * A project subclasses it in C++ and sets SlotName in the constructor.
 */
UCLASS(Abstract)
class ADAMSKAIHOST_API UADSKEnvQueryContext_Slot : public UEnvQueryContext
{
	GENERATED_BODY()
public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;

protected:
	FString SlotName;

private:
	mutable bool bWarned = false;
};

/** The querier brain's `Target` slot: the actor a perception binding or the file selected. */
UCLASS(meta = (DisplayName = "AdamskAI Target"))
class ADAMSKAIHOST_API UADSKEnvQueryContext_Target : public UADSKEnvQueryContext_Slot
{
	GENERATED_BODY()
public:
	UADSKEnvQueryContext_Target() { SlotName = TEXT("Target"); }
};
