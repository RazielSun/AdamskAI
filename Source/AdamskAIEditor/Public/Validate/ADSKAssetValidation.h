// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Compiler/ADSKCompiler.h"
#include "CoreMinimal.h"

class FADSKDefinition;

struct ADAMSKAIEDITOR_API FADSKAssetValidation
{
	/** The caller must ensure the Asset Registry scan is complete. */
	static void AppendMissingAssets(const FADSKDefinition& Definition, FADSKCompileResult& InOutResult);
};
