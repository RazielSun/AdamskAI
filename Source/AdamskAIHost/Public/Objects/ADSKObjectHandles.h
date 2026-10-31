// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Core/ADSKTypes.h"
#include "CoreMinimal.h"

/**
 * Opaque ids for Object slots, so the core stays value-only. Process-wide, game-thread only. One object keeps one id,
 * so `is` compares identity; ids are never reused, and a destroyed object's id resolves to null.
 */
struct ADAMSKAIHOST_API FADSKObjectHandles
{
	/** `none` for null. */
	static FADSKValue Make(const UObject* Object);
	static UObject* Resolve(uint64 Id);
	/** Null for `none`, a value of another type, or a destroyed object. */
	static UObject* Resolve(const FADSKValue& Value);
	template <typename T> static T* Resolve(uint64 Id) { return Cast<T>(Resolve(Id)); }
	template <typename T> static T* Resolve(const FADSKValue& Value) { return Cast<T>(Resolve(Value)); }
	/** `Object#3 (BP_Player_C_0)`, `Object#3 (destroyed)` or `none`; for debug views only. */
	static FString Describe(const FADSKValue& Value);
	static int32 GetTrackedCountForTest();
};
