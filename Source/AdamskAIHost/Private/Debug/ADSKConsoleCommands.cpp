// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Debug/ADSKConsoleCommands.h"

#if !UE_BUILD_SHIPPING
#include "Brain/ADSKBrainComponent.h"
#include "Debug/ADSKDebugActions.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Debug/ADSKTraceExport.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

void FADSKConsoleCommands::Register()
{
	if (bRegistered) return; bRegistered = true;
	IConsoleManager& Console = IConsoleManager::Get();
	// The output-device form sends each reply to the caller: the ~ console shows it, not only the Output Log.
	const auto Bind = [this](void (FADSKConsoleCommands::*Execute)(const TArray<FString>&, UWorld*, FOutputDevice&) const)
	{
		return FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[this, Execute](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) { (this->*Execute)(Args, World, Out); });
	};
	Console.RegisterConsoleCommand(TEXT("adsk.Status"), TEXT("List brains: brain= and pawn= names, source, state, hash; indented lines add execution, slots, events and recent records."), Bind(&FADSKConsoleCommands::ExecuteStatus), ECVF_Default);
	Console.RegisterConsoleCommand(TEXT("adsk.Reload"), TEXT("Reload [relative-file|all]."), Bind(&FADSKConsoleCommands::ExecuteReload), ECVF_Default);
	Console.RegisterConsoleCommand(TEXT("adsk.Restart"), TEXT("Restart <all|brain name|pawn name|full path>."), Bind(&FADSKConsoleCommands::ExecuteRestart), ECVF_Default);
	Console.RegisterConsoleCommand(TEXT("adsk.Event"), TEXT("Event <all|brain name|pawn name|full path> <event> [name=value]... Vector is x,y,z."), Bind(&FADSKConsoleCommands::ExecuteEvent), ECVF_Default);
	Console.RegisterConsoleCommand(TEXT("adsk.DumpTrace"), TEXT("DumpTrace <all|brain name|pawn name|full path>."), Bind(&FADSKConsoleCommands::ExecuteDumpTrace), ECVF_Default);
}
void FADSKConsoleCommands::Unregister()
{
	if (!bRegistered) return; bRegistered = false; IConsoleManager& Console = IConsoleManager::Get();
	for (const TCHAR* Name : {TEXT("adsk.Status"), TEXT("adsk.Reload"), TEXT("adsk.Restart"), TEXT("adsk.Event"), TEXT("adsk.DumpTrace")}) Console.UnregisterConsoleObject(Name, false);
}
void FADSKConsoleCommands::ExecuteStatus(const TArray<FString>&, UWorld* World, FOutputDevice& Out) const
{
	const auto Subsystems = ADSKDebugActions::ResolveWorlds(World); if (Subsystems.IsEmpty()) { Out.Logf(TEXT("adsk: no Game/PIE world")); return; }
	int32 Count = 0;
	for (UADSKWorldSubsystem* Subsystem : Subsystems)
	{
		const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(Subsystem->GetWorld());
		Count += Snapshot.Rows.Num();
		for (const FString& Line : Snapshot.ToLines()) Out.Log(*Line);
	}
	Out.Logf(TEXT("adsk: %d brain(s)"), Count);
	if (Count == 0) ADSKDebugActions::LogClientWorldHint(Subsystems, Out);
}
void FADSKConsoleCommands::ExecuteReload(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const
{
	ADSKDebugActions::Reload(World, Args.Num() == 1 ? Args[0] : TEXT("all"), Out);
}
void FADSKConsoleCommands::ExecuteRestart(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const
{
	if (Args.Num() == 1) ADSKDebugActions::Restart(World, Args[0], Out);
}
void FADSKConsoleCommands::ExecuteEvent(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const
{
	if (Args.Num() >= 2) ADSKDebugActions::Event(World, Args[0], Args[1], MakeArrayView(Args).Slice(2, Args.Num() - 2), Out);
}
void FADSKConsoleCommands::ExecuteDumpTrace(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out) const
{
	TArray<UADSKBrainComponent*> Brains;
	if (Args.Num() != 1 || !ADSKDebugActions::ResolveBrains(ADSKDebugActions::ResolveWorlds(World), Args[0], Brains, Out)) return;
	for (UADSKBrainComponent* Brain : Brains) { const FADSKTraceExportResult Result = FADSKTraceExport::Write(*Brain, World ? World->GetName() : TEXT("world")); Out.Logf(TEXT("adsk: %s"), Result.IsSuccess() ? *Result.Path : *Result.Error); }
}
#else
void FADSKConsoleCommands::Register() {}
void FADSKConsoleCommands::Unregister() {}
#endif
