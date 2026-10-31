// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UADSKBrainComponent;

/** The brains each behavior file drives, by normalized path: the ones a reload of that file replaces. */
class ADAMSKAIHOST_API FADSKBrainRegistry
{
public:
	void Register(const FString& Path, UADSKBrainComponent* Brain);
	void Unregister(const FString& Path, UADSKBrainComponent* Brain);

	/** The live brains of Path, in registration order; forgets the destroyed ones. */
	TArray<UADSKBrainComponent*> GetLive(const FString& Path);
	TArray<TWeakObjectPtr<UADSKBrainComponent>> Get(const FString& Path) const;
	TArray<UADSKBrainComponent*> GetAll() const;

private:
	TMap<FString, TArray<TWeakObjectPtr<UADSKBrainComponent>>> BySource;
};
