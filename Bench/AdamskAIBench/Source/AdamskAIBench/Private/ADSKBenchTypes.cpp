// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKBenchTypes.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, AdamskAIBench)

int64 ADSKBench::Entries[static_cast<int32>(EADSKBenchSystem::Count)] = {};

UADSKBenchBTCountTask::UADSKBenchBTCountTask()
{
	NodeName = TEXT("Count");
}

EBTNodeResult::Type UADSKBenchBTCountTask::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ADSKBench::Count(EADSKBenchSystem::BehaviorTree);
	return EBTNodeResult::Succeeded;
}

FADSKBenchSTCountTask::FADSKBenchSTCountTask()
{
	bShouldCallTick = false;
	bConsideredForCompletion = false;
}

EStateTreeRunStatus FADSKBenchSTCountTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ADSKBench::Count(EADSKBenchSystem::StateTree);
	return EStateTreeRunStatus::Running;
}
