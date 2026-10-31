// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class FADSKNodeRegistry;

class ADAMSKAIHOST_API IADSKNodeProvider : public IModularFeature
{
public:
	static FName GetFeatureName() { return TEXT("ADSKNodeProvider"); }
	virtual FString GetProviderName() const = 0;
	virtual void RegisterNodes(FADSKNodeRegistry& Registry, TArray<FString>& OutErrors) = 0;
};
