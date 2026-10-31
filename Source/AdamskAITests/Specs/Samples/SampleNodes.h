// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Catalogue/ADSKNodeProvider.h"
#include "CoreMinimal.h"
#include "SampleNodes.generated.h"

/** SampleTurnToward's configuration: the `yawOffset` attribute, degrees added to the direction of the point. */
USTRUCT()
struct FADSKSampleTurnConfig
{
	GENERATED_BODY()

	UPROPERTY()
	float YawOffset = 0.f;
};

/** SampleSendTo's configuration: the `event` attribute, the event it queues in the receiver. */
USTRUCT()
struct FADSKSampleSendToConfig
{
	GENERATED_BODY()

	UPROPERTY()
	FName Event;
};

/**
 * The smallest useful node provider, compiled with the tests so it keeps building: a host leaf,
 * `<SampleTurnToward id="..." in-point="Slot" yawOffset="0"/>`, that turns the brain's host actor toward a Vector
 * and succeeds, and `<SampleSendTo id="..." event="..." in-target="Slot"><Param name="..." from="Slot"/></SampleSendTo>`,
 * which sends an event with parameters to another actor's brain (docs/recipes/talk-between-agents.md). A project's provider looks the same and is registered in its game module's StartupModule
 * (docs/authoring-nodes.md, docs/recipes/add-a-project-node.md).
 */
class FADSKSampleNodeProvider final : public IADSKNodeProvider
{
public:
	virtual FString GetProviderName() const override { return TEXT("AdamskAISample"); }
	virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) override;
};
