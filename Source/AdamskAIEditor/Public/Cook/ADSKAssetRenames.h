// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

class ADAMSKAIEDITOR_API FADSKAssetRenames
{
public:
	void Start();
	void Stop();

	/** Rewrites every quoted attribute value equal to OldPath or OldPath + `_C`; returns how many it rewrote. */
	static int32 RewriteText(FString& Text, const FString& OldPath, const FString& NewPath);
	static TArray<FString> RewriteFiles(TConstArrayView<FString> Roots, const FString& OldPath, const FString& NewPath);
	static TArray<FString> HandleRename(const FAssetData& Asset, const FString& OldObjectPath);

private:
	void OnAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath);

	FDelegateHandle Handle;
};
