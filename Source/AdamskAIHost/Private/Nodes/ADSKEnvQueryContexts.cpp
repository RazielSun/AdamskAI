// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Nodes/ADSKEnvQueryContexts.h"

#include "ADSKHostModule.h"
#include "Brain/ADSKBrainComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "GameFramework/Actor.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Objects/ADSKObjectHandles.h"
#include "Runtime/ADSKInstance.h"

void UADSKEnvQueryContext_Slot::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AActor* Querier = Cast<AActor>(QueryInstance.Owner.Get());
	const UADSKBrainComponent* Brain = UADSKGroupSubsystem::FindBrain(Querier);
	const FADSKInstance* Instance = Brain ? Brain->GetInstance() : nullptr;
	if (Brain && !Instance) return;
	FADSKValue Value;
	const bool bRead = Instance && Instance->GetValue(SlotName, Value);
	if (bRead && Value.Type == EADSKValueType::Object)
	{
		if (const AActor* Actor = FADSKObjectHandles::Resolve<AActor>(Value)) UEnvQueryItemType_Actor::SetContextHelper(ContextData, Actor);
		return;
	}
	if (bRead && Value.Type == EADSKValueType::Vector) { UEnvQueryItemType_Point::SetContextHelper(ContextData, Value.Vector); return; }
	if (!bWarned)
	{
		bWarned = true;
		UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI EQS context %s is empty for %s: %s"), *GetClass()->GetName(), *GetNameSafe(Querier),
			Brain ? *FString::Printf(TEXT("no Object or Vector slot '%s'"), *SlotName) : TEXT("no AdamskAI brain"));
	}
}
