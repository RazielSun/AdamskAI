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
	/** unreachable-state, unhandled-event, unread-local, unread-input; in that order, then declaration order. */
	ADAMSKAI_API TArray<FADSKLintNote> FindLintNotes(const FADSKDefinition& Definition);
}
