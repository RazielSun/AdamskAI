// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "StateTreeSchema.h"
#include "StateTreeTaskBase.h"
#include "ADSKBenchTypes.generated.h"

/** Which implementation of the bench behavior counted a branch entry. */
enum class EADSKBenchSystem : uint8 { AdamskAI, BehaviorTree, StateTree, Count };

namespace ADSKBench
{
	/** Branch entries counted by each system's native Count task since the last reset; game thread only. */
	extern int64 Entries[static_cast<int32>(EADSKBenchSystem::Count)];

	inline void Count(EADSKBenchSystem System) { ++Entries[static_cast<int32>(System)]; }
}

/** The Behavior Tree's Count task: counts a branch entry and succeeds at once. */
UCLASS(HideDropdown)
class UADSKBenchBTCountTask : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UADSKBenchBTCountTask();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

USTRUCT()
struct FADSKBenchSTCountTaskInstanceData
{
	GENERATED_BODY()
};

/** The StateTree's Count task: counts a branch entry on enter, never ticks and never completes the state. */
USTRUCT(meta = (Hidden))
struct FADSKBenchSTCountTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FADSKBenchSTCountTaskInstanceData;

	FADSKBenchSTCountTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/** A schema with no context requirements, for a StateTree ticked without a component or a world. */
UCLASS(HideDropdown)
class UADSKBenchSchema : public UStateTreeSchema
{
	GENERATED_BODY()
public:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override { return true; }
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override { return true; }
	virtual bool IsScheduledTickAllowed() const override { return true; }
};
