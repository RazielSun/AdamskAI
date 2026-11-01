// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Nodes/ADSKBuiltinNodes.h"

class AActor;
class UEnvQuery;
class UWorld;

/** What RunEQS, the perception bridge and the managed tick read from the engine; per world. */
class ADAMSKAIHOST_API IADSKHostServices
{
public:
	virtual ~IADSKHostServices() = default;

	/** The query id, or INDEX_NONE. OnFinished may run before this returns, and never once Querier is destroyed. */
	virtual int32 RunQuery(AActor& Querier, const UEnvQuery& Query, EEnvQueryRunMode::Type Mode, TConstArrayView<FADSKNamedParam> Params,
		TFunction<void(const FEnvQueryResult&)> OnFinished) = 0;
	virtual void AbortQuery(UWorld* World, int32 QueryId) = 0;
	/** The clock perception memory forgets by. */
	virtual double GetTimeSeconds(const UWorld* World) const = 0;
	/** Where tick LOD measures distances from; none means every managed brain ticks at full rate. */
	virtual void GetViewers(const UWorld* World, TArray<FVector>& OutViewers) const = 0;

	static IADSKHostServices& Engine();
	/** World's services: those its subsystem was given, otherwise Engine(). */
	static IADSKHostServices& Get(const UWorld* World);
};
