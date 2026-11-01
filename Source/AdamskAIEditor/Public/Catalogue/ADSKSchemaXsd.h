// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKNodeRegistry;

/** XML Schema of XML v1 and one registry's nodes for IDE help; checks shape only, the compiler stays the authority. */
struct ADAMSKAIEDITOR_API FADSKSchemaXsd
{
	static bool Render(const FADSKNodeRegistry& Registry, const FString& RegistryLabel, FString& OutXsd, FString& OutError);
};
