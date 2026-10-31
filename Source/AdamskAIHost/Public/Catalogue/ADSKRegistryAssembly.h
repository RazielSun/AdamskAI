// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

class FADSKNodeRegistry;
class IADSKNodeProvider;

/** Process catalogue assembly. It never exposes an unfrozen or partial registry. */
class ADAMSKAIHOST_API FADSKRegistryAssembly
{
public:
	static TSharedPtr<const FADSKNodeRegistry> TryGet(TArray<FString>& OutErrors);
	/** A fresh frozen registry of the core and host nodes plus Providers, apart from the shared one TryGet assembles. */
	static TSharedPtr<const FADSKNodeRegistry> AssembleIsolated(TConstArrayView<IADSKNodeProvider*> Providers, TArray<FString>& OutErrors);
	/** The core and host nodes, without project providers. */
	static TSharedPtr<const FADSKNodeRegistry> BuildPluginCatalogue(TArray<FString>& OutErrors);
	static bool HasAttempted();
	static bool ReleaseAssembled();
	/**
	 * The next TryGet assembles a new registry; compiled definitions keep theirs. A provider's module must outlive
	 * every definition that uses its nodes.
	 */
	static void MarkProvidersChanged();
	static uint32 GetGeneration();
};
