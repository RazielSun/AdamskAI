// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Sources/ADSKValidation.h"
#include "CoreMinimal.h"

struct ADAMSKAIEDITOR_API FADSKReportRow
{
	FString Path;
	int32 Line = 0;
	FString Severity;
	FString Code;
	FString Node;
	FString Message;

	bool IsError() const { return Severity == TEXT("error"); }
	/** Advice only: printed and serialized, but never part of the exit code or the warning count. */
	bool IsNote() const { return Severity == TEXT("note"); }
};

/** Hooks for the atomic writer; an unset function uses the real file system. */
struct ADAMSKAIEDITOR_API FADSKAtomicWriteHooks
{
	TFunction<bool(const FString& TempPath, const FString& Content)> Save;
	TFunction<bool(const FString& TempPath, const FString& FinalPath)> Replace;
};

struct ADAMSKAIEDITOR_API FADSKValidateReport
{
	static constexpr int32 JsonVersion = 1;

	static FString FormatRow(const FADSKReportRow& Row);
	static FADSKReportRow MakeRow(const FString& Path, const FString& Severity, const FString& Code, const FString& Message, int32 Line = 0, const FString& Node = FString());

	/** Compiler and host read diagnostics share one shape; a host report code replaces the compiler code. */
	static void AppendResult(const FADSKSourceReadResult& Result, const FString& DisplayBase, TArray<FADSKReportRow>& OutRows);
	/** Note rows for a compiled source: `hold-risk` (ADSK::FindHoldRisks), then the lint codes (ADSK::FindLintNotes). */
	static void AppendNotes(const FADSKSourceReadResult& Result, const FString& DisplayBase, TArray<FADSKReportRow>& OutRows);
	static int32 ExitCodeFor(const TArray<FADSKReportRow>& Rows);

	static FString ToJson(const TArray<FADSKReportRow>& Rows, const FString& Mode, int32 Files, int32 ExitCode);

	/** Writes a sibling temp, then replaces the target. Any failure removes the temp and leaves an existing target intact. */
	static bool WriteAtomic(const FString& FinalPath, const FString& Content, const FADSKAtomicWriteHooks& Hooks, FString& OutError);
};
