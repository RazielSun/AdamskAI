// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKNodeRegistry;
class ITargetPlatform;

/** Behavior files are plain text, so the cooker never sees the assets they name; this adds those packages to every cook. */
class ADAMSKAIEDITOR_API FADSKCookReferences
{
public:
	void Start();
	void Stop();

	/** Sorted; behavior files that do not compile go to OutSkipped. */
	static TArray<FName> CollectPackages(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry, TArray<FString>& OutSkipped);

private:
	void OnModifyCook(TConstArrayView<const ITargetPlatform*> Platforms, TArray<FName>& PackagesToCook, TArray<FName>& PackagesToNeverCook);

	FDelegateHandle Handle;
};
