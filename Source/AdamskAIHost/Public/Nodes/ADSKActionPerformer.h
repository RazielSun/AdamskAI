// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ADSKActionPerformer.generated.h"

/** Reports the action's end: true for success, false for failure. Call it at most once; after StopAction, never. */
using FADSKActionDone = TFunction<void(bool bSucceeded)>;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UADSKActionPerformer : public UInterface
{
	GENERATED_BODY()
};

/** What `<Act>` runs by name (attack, shot, ability). Implemented by the pawn, else by the controller. */
class ADAMSKAIHOST_API IADSKActionPerformer
{
	GENERATED_BODY()

public:
	/** True when started: call Done when it ends by itself. False fails the node. */
	virtual bool StartAction(FName Action, AActor* Target, FADSKActionDone Done) = 0;

	/** The node was left before Done: stop the action. Done must not be called afterwards. */
	virtual void StopAction(FName Action) = 0;

	/** The target slot names another actor while the action runs; the default keeps the first target. */
	virtual void RetargetAction(FName Action, AActor* Target) {}
};
