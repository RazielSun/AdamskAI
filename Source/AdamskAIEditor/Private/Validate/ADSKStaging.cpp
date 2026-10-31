// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Validate/ADSKStaging.h"

#include "Misc/Parse.h"

bool ADSKStaging::IsStagedBy(const FString& Root, const TArray<FString>& StageEntries)
{
	const FString Target = Root.Replace(TEXT("\\"), TEXT("/")) / TEXT("");
	for (const FString& Entry : StageEntries)
	{
		FString Path;
		if (!FParse::Value(*Entry, TEXT("Path="), Path)) continue;
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Path.RemoveFromStart(TEXT("/"))) {}
		while (Path.RemoveFromEnd(TEXT("/"))) {}
		if (Path.IsEmpty() || Target.StartsWith(Path / TEXT(""), ESearchCase::IgnoreCase)) return true;
	}
	return false;
}
