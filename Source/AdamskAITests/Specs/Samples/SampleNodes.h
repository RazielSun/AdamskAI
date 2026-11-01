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

/** The smallest node provider, compiled so it keeps building (docs/authoring-nodes.md). */
class FADSKSampleNodeProvider final : public IADSKNodeProvider
{
public:
	virtual FString GetProviderName() const override { return TEXT("AdamskAISample"); }
	virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) override;
};
