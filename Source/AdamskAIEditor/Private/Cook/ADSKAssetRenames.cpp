// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Cook/ADSKAssetRenames.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Sources/ADSKBehaviorRoots.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdamskAIRenames, Log, All);

void FADSKAssetRenames::Start()
{
	if (IAssetRegistry* Registry = IAssetRegistry::Get())
	{
		Handle = Registry->OnAssetRenamed().AddRaw(this, &FADSKAssetRenames::OnAssetRenamed);
	}
}

void FADSKAssetRenames::Stop()
{
	if (IAssetRegistry* Registry = IAssetRegistry::Get(); Registry && Handle.IsValid())
	{
		Registry->OnAssetRenamed().Remove(Handle);
	}
	Handle.Reset();
}

int32 FADSKAssetRenames::RewriteText(FString& Text, const FString& OldPath, const FString& NewPath)
{
	if (OldPath.IsEmpty() || OldPath == NewPath)
	{
		return 0;
	}
	int32 Count = 0;
	// Whole quoted values only, so `/Game/Q.Q` never rewrites inside `/Game/Q.Q2` or a longer path.
	for (const TCHAR* Suffix : {TEXT("_C"), TEXT("")})
	{
		for (const TCHAR* Quote : {TEXT("\""), TEXT("'")})
		{
			const FString From = Quote + OldPath + Suffix + Quote;
			const FString To = Quote + NewPath + Suffix + Quote;
			Count += Text.ReplaceInline(*From, *To, ESearchCase::CaseSensitive);
		}
	}
	return Count;
}

TArray<FString> FADSKAssetRenames::RewriteFiles(TConstArrayView<FString> Roots, const FString& OldPath, const FString& NewPath)
{
	TArray<FString> Changed;
	for (const FString& Root : Roots)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.xml"), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File) || RewriteText(Text, OldPath, NewPath) == 0)
			{
				continue;
			}
			if (FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				Changed.Add(File);
			}
			else
			{
				UE_LOG(LogAdamskAIRenames, Warning, TEXT("%s names the renamed '%s' but could not be rewritten"), *File, *OldPath);
			}
		}
	}
	return Changed;
}

TArray<FString> FADSKAssetRenames::HandleRename(const FAssetData& Asset, const FString& OldObjectPath)
{
	const FString NewObjectPath = Asset.GetSoftObjectPath().ToString();
	const TArray<FString> Changed = RewriteFiles(FADSKBehaviorRoots::GetAllDirectories(), OldObjectPath, NewObjectPath);
	for (const FString& File : Changed)
	{
		UE_LOG(LogAdamskAIRenames, Display, TEXT("%s: '%s' is now '%s'"), *File, *OldObjectPath, *NewObjectPath);
	}
	return Changed;
}

void FADSKAssetRenames::OnAssetRenamed(const FAssetData& Asset, const FString& OldObjectPath)
{
	HandleRename(Asset, OldObjectPath);
}
