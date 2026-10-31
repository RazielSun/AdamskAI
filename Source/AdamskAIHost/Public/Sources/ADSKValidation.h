// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Compiler/ADSKCompiler.h"

class FADSKNodeRegistry;

struct ADAMSKAIHOST_API FADSKSourceReadResult
{
	bool bReadFailed = false;
	/** Host-owned stable report code; empty for a successfully read source. */
	FString ReportCode;
	FString Text;
	FADSKCompileResult Result;
};

struct ADAMSKAIHOST_API FADSKSourceLocation
{
	FString Path;
	/** The absolute file; empty when the path is invalid or names an unknown mount. */
	FString Filename;
};

struct ADAMSKAIHOST_API FADSKValidation
{
	/** Resolves a behavior path against Root and the mounts. Game thread: mounts change there. */
	static FADSKSourceLocation Locate(const FString& Root, FStringView Relative);
	/** Reads and compiles a located file. Any thread, so a compile task locates first and reads on the worker. */
	static FADSKSourceReadResult ReadSource(const FADSKSourceLocation& Location, const TSharedRef<const FADSKNodeRegistry>& Registry);
	/** Locate, then ReadSource. Game thread. */
	static FADSKSourceReadResult ReadSource(const FString& Root, FStringView Relative, const TSharedRef<const FADSKNodeRegistry>& Registry);
	static TArray<FADSKSourceReadResult> ValidateFiles(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry);
};
