// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKNodeRegistry;

struct ADAMSKAIEDITOR_API FADSKCatalogue
{
	/** Requires a frozen registry; renders LF markdown with exactly one trailing newline. */
	static bool Render(const FADSKNodeRegistry& Registry, const FString& RegistryLabel, FString& OutMarkdown, FString& OutError);

	/** Exact comparison after CRLF normalization; nothing else is normalized. */
	static bool Matches(const FString& Generated, const FString& Existing);
};
