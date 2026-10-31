// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Sources/ADSKSourcePath.h"

#include "Misc/Paths.h"
#include "Sources/ADSKBehaviorRoots.h"

bool FADSKSourcePath::Normalize(FStringView Input, FString& OutPath)
{
	OutPath.Reset();
	if (!Input.IsEmpty() && Input[0] == '@')
	{
		int32 Slash = INDEX_NONE;
		const FStringView Rest = Input.RightChop(1);
		for (int32 Index = 0; Index < Rest.Len() && Slash == INDEX_NONE; ++Index) { if (Rest[Index] == '/' || Rest[Index] == '\\') Slash = Index; }
		if (Slash == INDEX_NONE || !FADSKBehaviorRoots::IsMountName(Rest.Left(Slash))) return false;
		FString Relative;
		const bool bOk = Normalize(Rest.RightChop(Slash + 1), Relative);
		if (!Relative.IsEmpty()) OutPath = FString::Printf(TEXT("@%.*s/%s"), Slash, Rest.GetData(), *Relative);
		return bOk;
	}
	if (Input.IsEmpty() || Input[0] == '/' || Input[0] == '\\' || Input.Find(TEXT(":")) != INDEX_NONE) return false;
	FString Value(Input); Value.ReplaceInline(TEXT("\\"), TEXT("/")); TArray<FString> Parts; Value.ParseIntoArray(Parts, TEXT("/"), true);
	if (Parts.IsEmpty()) return false;
	for (const FString& Part : Parts) { if (Part == TEXT(".") || Part == TEXT("..")) return false; }
	OutPath = FString::Join(Parts, TEXT("/"));
	return OutPath.EndsWith(TEXT(".xml"), ESearchCase::CaseSensitive);
}

bool FADSKSourcePath::SplitMount(const FString& Path, FString& OutMount, FString& OutRelative)
{
	int32 Slash = INDEX_NONE;
	if (!Path.StartsWith(TEXT("@")) || !Path.FindChar(TEXT('/'), Slash)) return false;
	OutMount = Path.Mid(1, Slash - 1);
	OutRelative = Path.Mid(Slash + 1);
	return true;
}

bool FADSKSourcePath::Resolve(const FString& Root, FStringView Relative, FString& OutFilename)
{
	FString Normalized; if (!Normalize(Relative, Normalized)) return false;
	FString FullRoot = FPaths::ConvertRelativePathToFull(Root);
	FPaths::NormalizeDirectoryName(FullRoot);
	OutFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FullRoot, Normalized));
	FPaths::NormalizeFilename(OutFilename);
	FullRoot += TEXT("/");
	return OutFilename.StartsWith(FullRoot, ESearchCase::CaseSensitive);
}
