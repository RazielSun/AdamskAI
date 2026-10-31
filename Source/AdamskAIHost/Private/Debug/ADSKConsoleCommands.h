// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class UWorld;

class FADSKConsoleCommands
{
public:
	void Register();
	void Unregister();
	// Public only because Unreal's delegate factory takes a member-function pointer.
	void ExecuteStatus(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const;
	void ExecuteReload(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const;
	void ExecuteRestart(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const;
	void ExecuteEvent(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const;
	void ExecuteDumpTrace(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const;
private:
	bool bRegistered = false;
};
