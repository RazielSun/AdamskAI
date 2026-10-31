// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Sources/ADSKBehaviorRoots.h"

#include "Misc/Paths.h"
#include "Settings/ADSKSettings.h"

namespace ADSKBehaviorRootsPrivate
{
	/** Mount names are exact-case, which FString map keys are not. */
	TArray<TPair<FString, FString>>& Registered()
	{
		static TArray<TPair<FString, FString>> Mounts;
		return Mounts;
	}

	int32 FindRegistered(const FString& Mount)
	{
		return Registered().IndexOfByPredicate([&Mount](const TPair<FString, FString>& Pair) { return Pair.Key.Equals(Mount, ESearchCase::CaseSensitive); });
	}

	bool FindConfigured(const FString& Mount, FString& OutDirectory)
	{
		for (const TPair<FString, FString>& Pair : GetDefault<UADSKSettings>()->BehaviorMounts)
		{
			if (Pair.Key.Equals(Mount, ESearchCase::CaseSensitive))
			{
				OutDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Pair.Value);
				return true;
			}
		}
		return false;
	}
}

using namespace ADSKBehaviorRootsPrivate;

bool FADSKBehaviorRoots::IsMountName(FStringView Name)
{
	if (Name.IsEmpty() || !FChar::IsAlpha(Name[0]))
	{
		return false;
	}
	for (const TCHAR C : Name)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool FADSKBehaviorRoots::Register(const FString& Mount, const FString& Directory)
{
	check(IsInGameThread());
	FString Existing;
	if (!IsMountName(Mount) || Directory.IsEmpty() || Find(Mount, Existing))
	{
		return false;
	}
	FString Full = FPaths::ConvertRelativePathToFull(Directory);
	FPaths::NormalizeDirectoryName(Full);
	Registered().Add({Mount, MoveTemp(Full)});
	OnChanged().Broadcast();
	return true;
}

bool FADSKBehaviorRoots::Unregister(const FString& Mount)
{
	check(IsInGameThread());
	const int32 Index = FindRegistered(Mount);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Registered().RemoveAt(Index);
	OnChanged().Broadcast();
	return true;
}

bool FADSKBehaviorRoots::Find(const FString& Mount, FString& OutDirectory)
{
	if (const int32 Index = FindRegistered(Mount); Index != INDEX_NONE)
	{
		OutDirectory = Registered()[Index].Value;
		return true;
	}
	return FindConfigured(Mount, OutDirectory);
}

TArray<TPair<FString, FString>> FADSKBehaviorRoots::GetAll()
{
	TArray<TPair<FString, FString>> Out = Registered();
	for (const TPair<FString, FString>& Pair : GetDefault<UADSKSettings>()->BehaviorMounts)
	{
		FString Directory;
		if (IsMountName(Pair.Key) && FindRegistered(Pair.Key) == INDEX_NONE && FindConfigured(Pair.Key, Directory))
		{
			Out.Add({Pair.Key, Directory});
		}
	}
	Out.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0; });
	return Out;
}

TArray<FString> FADSKBehaviorRoots::GetAllDirectories()
{
	TArray<FString> Out = {UADSKSettings::GetBehaviorRootDir()};
	for (const TPair<FString, FString>& Mount : GetAll()) Out.Add(Mount.Value);
	return Out;
}

FSimpleMulticastDelegate& FADSKBehaviorRoots::OnChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}
