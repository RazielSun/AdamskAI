// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Settings/ADSKSettings.h"

#include "ADSKHostModule.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

FString UADSKSettings::GetBehaviorRoot()
{
	static const FString Default = TEXT("AI");
	FString Root = GetDefault<UADSKSettings>()->BehaviorRoot;
	Root.ReplaceInline(TEXT("\\"), TEXT("/"));
	TArray<FString> Parts;
	Root.ParseIntoArray(Parts, TEXT("/"), true);
	const bool bAbsolute = Root.StartsWith(TEXT("/")) || Root.Contains(TEXT(":"));
	const bool bDotted = Parts.ContainsByPredicate([](const FString& Part) { return Part == TEXT(".") || Part == TEXT(".."); });
	if (Parts.IsEmpty() || bAbsolute || bDotted)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogAdamskAIHost, Warning, TEXT("AdamskAI BehaviorRoot '%s' is not a Content-relative directory; using '%s'"), *Root, *Default);
		}
		return Default;
	}
	return FString::Join(Parts, TEXT("/"));
}

FString UADSKSettings::GetBehaviorRootDir()
{
	return FPaths::ProjectContentDir() / GetBehaviorRoot();
}

int32 UADSKSettings::GetRecorderCapacity()
{
#if UE_BUILD_SHIPPING
	return 0;
#else
	return FMath::Max(0, GetDefault<UADSKSettings>()->RecorderCapacity);
#endif
}

float UADSKSettings::GetTickInterval(double Distance)
{
	float Interval = 0.f;
	float Reached = -1.f;
	for (const FADSKTickLODLevel& Level : GetDefault<UADSKSettings>()->TickLOD)
	{
		if (Distance >= Level.Distance && Level.Distance > Reached)
		{
			Reached = Level.Distance;
			Interval = FMath::Max(0.f, Level.Interval);
		}
	}
	return Interval;
}
