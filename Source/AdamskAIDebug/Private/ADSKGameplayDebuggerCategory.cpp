// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKGameplayDebuggerCategory.h"

#if WITH_GAMEPLAY_DEBUGGER_MENU

#include "Debug/ADSKDebugSnapshot.h"

FADSKGameplayDebuggerCategory::FADSKGameplayDebuggerCategory()
{
	bShowOnlyWithDebugActor = true;
}

TSharedRef<FGameplayDebuggerCategory> FADSKGameplayDebuggerCategory::MakeInstance()
{
	return MakeShared<FADSKGameplayDebuggerCategory>();
}

void FADSKGameplayDebuggerCategory::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	for (const FString& Line : FADSKDebugSnapshot::GameplayDebuggerLines(DebugActor))
	{
		AddTextLine(Line);
	}
}

#endif
