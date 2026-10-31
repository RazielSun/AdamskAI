// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKDefinition;

/** Something a valid file most likely did not mean; callers report it as a note, never as an error. */
struct FADSKLintNote
{
	/** Kebab-case: `unreachable-state`, `unhandled-event`, `unread-local` or `unread-input`. */
	const TCHAR* Code = TEXT("");
	int32 Line = 0;
	FString Message;
};

namespace ADSK
{
	/**
	 * Static checks on a compiled definition, in that code order then declaration order:
	 * - unreachable-state: no initial chain or transition target ever enters the state;
	 * - unhandled-event: no transition anywhere names the event, so raising it does nothing;
	 * - unread-local: no port, condition, change event, Emit, Param or Set argument reads the Local;
	 * - unread-input: the same for an Input, which the host then publishes for nothing.
	 */
	ADAMSKAI_API TArray<FADSKLintNote> FindLintNotes(const FADSKDefinition& Definition);
}
