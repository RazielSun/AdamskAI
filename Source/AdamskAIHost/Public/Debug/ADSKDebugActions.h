// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

// Debug and tooling surface: declared and defined only outside Shipping, like the console commands that use it.
#if !UE_BUILD_SHIPPING

class FOutputDevice;
class UADSKBrainComponent;
class UADSKWorldSubsystem;
class UWorld;
struct FADSKNamedValue;

/** Shared game-thread debug operations used by console commands and the optional debug panel. */
namespace ADSKDebugActions
{
	ADAMSKAIHOST_API TArray<UADSKWorldSubsystem*> ResolveWorlds(UWorld* ConsoleWorld);
	ADAMSKAIHOST_API FString OwnerName(const UADSKBrainComponent& Brain);
	ADAMSKAIHOST_API FString PawnName(const UADSKBrainComponent& Brain);
	/** Why a client world shows no brains; empty for any other world, or a client world that runs brains of its own. */
	ADAMSKAIHOST_API FString ClientWorldHint(const UWorld* World);
	/** Logs the first non-empty ClientWorldHint of those worlds. */
	ADAMSKAIHOST_API void LogClientWorldHint(TConstArrayView<UADSKWorldSubsystem*> Subsystems, FOutputDevice& Output);
	ADAMSKAIHOST_API bool ResolveBrains(TConstArrayView<UADSKWorldSubsystem*> Subsystems,
		FStringView Address, TArray<UADSKBrainComponent*>& Out, FOutputDevice& Output);
	ADAMSKAIHOST_API bool ParseEvent(const UADSKBrainComponent& Brain, const FString& Event,
		TConstArrayView<FString> Args, TArray<FADSKNamedValue>& Out, FString& Error);
	ADAMSKAIHOST_API void Reload(UWorld* World, FStringView SourcePathOrAll, FOutputDevice& Out);
	ADAMSKAIHOST_API void Restart(UWorld* World, FStringView Address, FOutputDevice& Out);
	ADAMSKAIHOST_API void Event(UWorld* World, FStringView Address, const FString& EventName,
		TConstArrayView<FString> Args, FOutputDevice& Out);
}

#endif // !UE_BUILD_SHIPPING
