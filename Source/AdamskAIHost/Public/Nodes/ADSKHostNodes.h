// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKNode.h"
#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTypes.h"

class FADSKNodeRegistry;
class UEnvQuery;
struct FEnvQueryResult;

struct ADAMSKAIHOST_API FADSKHostNodes
{
	/** The host nodes and, through the functions below, the host sensors, `Act` and the group nodes. */
	static void RegisterAll(FADSKNodeRegistry& Registry);
	static void RegisterSensors(FADSKNodeRegistry& Registry);
	/** `Act`, run through IADSKActionPerformer (Nodes/ADSKActionPerformer.h). */
	static void RegisterActions(FADSKNodeRegistry& Registry);
	/** `HoldToken` and `Radio`, through UADSKGroupSubsystem. */
	static void RegisterGroups(FADSKNodeRegistry& Registry);
};

namespace ADSKRunEQS
{
	/** Succeeded with a Vector payload for a successful result holding a valid first item; Failed otherwise. */
	ADAMSKAIHOST_API EADSKNodeStatus MapResult(const FEnvQueryResult& Result, TOptional<FADSKValue>& OutPayload);

	/** The authored params the query does not declare with the same name and type, comma-separated; empty when all match. */
	ADAMSKAIHOST_API FString FindUnknownParams(const UEnvQuery& Query, TConstArrayView<FADSKNamedParam> Params);
}
