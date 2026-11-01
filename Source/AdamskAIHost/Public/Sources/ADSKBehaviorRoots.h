// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

/** Extra behavior directories, addressed as `@<Mount>/<relative>.xml`. Game thread. */
class ADAMSKAIHOST_API FADSKBehaviorRoots
{
public:
	/** A letter, then letters, digits, `_` or `-`. */
	static bool IsMountName(FStringView Name);
	/** False when the name is invalid or already mounted (registered or configured). */
	static bool Register(const FString& Mount, const FString& Directory);
	static bool Unregister(const FString& Mount);
	/** A registered mount first, then a configured one (its directory relative to the project directory). */
	static bool Find(const FString& Mount, FString& OutDirectory);
	/** Every mount with its absolute directory, sorted by name. */
	static TArray<TPair<FString, FString>> GetAll();
	/** BehaviorRoot's directory first, then every mount's. */
	static TArray<FString> GetAllDirectories();
	/** Broadcast after Register or Unregister changed the mounts. */
	static FSimpleMulticastDelegate& OnChanged();
};
