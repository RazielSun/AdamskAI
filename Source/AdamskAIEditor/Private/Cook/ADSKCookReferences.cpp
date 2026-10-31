// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Cook/ADSKCookReferences.h"

#include "Catalogue/ADSKRegistryAssembly.h"
#include "GameDelegates.h"
#include "Program/ADSKDefinition.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Sources/ADSKValidation.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdamskAICook, Log, All);

void FADSKCookReferences::Start()
{
	Handle = FGameDelegates::Get().GetModifyCookDelegate().AddRaw(this, &FADSKCookReferences::OnModifyCook);
}

void FADSKCookReferences::Stop()
{
	FGameDelegates::Get().GetModifyCookDelegate().Remove(Handle);
	Handle.Reset();
}

TArray<FName> FADSKCookReferences::CollectPackages(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry, TArray<FString>& OutSkipped)
{
	TSet<FName> Packages;
	for (const FADSKSourceReadResult& Read : FADSKValidation::ValidateFiles(Root, Registry))
	{
		if (!Read.Result.Definition)
		{
			OutSkipped.Add(Read.Result.Diagnostics.IsEmpty() ? Read.ReportCode : Read.Result.Diagnostics[0].Source);
			continue;
		}
		for (const FADSKObjectPathField& Field : ADSK::GetObjectPathFields(*Read.Result.Definition))
		{
			if (!Field.Path.IsNull() && !Field.Path.GetLongPackageName().StartsWith(TEXT("/Script/"))) Packages.Add(Field.Path.GetLongPackageFName());
		}
	}
	TArray<FName> Sorted = Packages.Array();
	Sorted.Sort(FNameLexicalLess());
	return Sorted;
}

void FADSKCookReferences::OnModifyCook(TConstArrayView<const ITargetPlatform*>, TArray<FName>& PackagesToCook, TArray<FName>&)
{
	TArray<FString> Errors;
	const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::TryGet(Errors);
	if (!Registry)
	{
		UE_LOG(LogAdamskAICook, Error, TEXT("behavior assets not added to the cook: catalogue failed: %s"), *FString::Join(Errors, TEXT("; ")));
		return;
	}
	TArray<FString> Skipped;
	TArray<FName> Packages;
	for (const FString& Root : FADSKBehaviorRoots::GetAllDirectories())
	{
		for (const FName Package : CollectPackages(Root, Registry.ToSharedRef(), Skipped)) Packages.AddUnique(Package);
	}
	for (const FString& Source : Skipped)
	{
		UE_LOG(LogAdamskAICook, Warning, TEXT("%s does not compile; the assets it names are not added to the cook"), *Source);
	}
	for (const FName Package : Packages) PackagesToCook.AddUnique(Package);
	UE_LOG(LogAdamskAICook, Display, TEXT("added %d package(s) named by behavior files"), Packages.Num());
}
