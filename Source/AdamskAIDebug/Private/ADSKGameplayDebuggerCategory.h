// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

#if WITH_GAMEPLAY_DEBUGGER_MENU

#include "GameplayDebuggerCategory.h"

class AActor;
class UADSKBrainComponent;

class FADSKGameplayDebuggerCategory : public FGameplayDebuggerCategory
{
public:
	FADSKGameplayDebuggerCategory();
	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();
	/** Uses the brain on the actor itself, else on its controller. */
	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
};

#endif
