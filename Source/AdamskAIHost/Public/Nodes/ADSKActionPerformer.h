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

/**
 * What `<Act action="..." in-target="..."/>` runs: an attack, a shot, an ability, anything the pawn does by name and
 * ends on its own time. Implemented by the brain's host actor (the pawn) or, when the pawn does not, by the brain's
 * owner (its controller). The file decides when; the performer does it and says when it is over.
 */
class ADAMSKAIHOST_API IADSKActionPerformer
{
	GENERATED_BODY()

public:
	/**
	 * Starts Action at Target (null when the slot is none). True when it started: call Done when it ends by itself,
	 * or never for an action that lasts until the node is left (firing). False when it cannot start: the node fails.
	 */
	virtual bool StartAction(FName Action, AActor* Target, FADSKActionDone Done) = 0;

	/** The node was left before Done: stop the action. Done must not be called afterwards. */
	virtual void StopAction(FName Action) = 0;

	/** The target slot names another actor while the action runs; the default keeps the first target. */
	virtual void RetargetAction(FName Action, AActor* Target) {}
};
