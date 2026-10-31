// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

struct ADAMSKAIHOST_API FADSKSourcePath
{
	/** A relative `.xml` path with `/` separators, optionally prefixed `@Mount/` (FADSKBehaviorRoots). */
	static bool Normalize(FStringView Input, FString& OutPath);
	/** Splits a normalized `@Mount/rest` into the mount and rest; false for a path under BehaviorRoot. */
	static bool SplitMount(const FString& Path, FString& OutMount, FString& OutRelative);
	static bool Resolve(const FString& Root, FStringView Relative, FString& OutFilename);
};
