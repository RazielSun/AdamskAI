// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Program/ADSKDiagnostic.h"
#include "CoreMinimal.h"

class FADSKNodeRegistry;

/** One expected diagnostic: `line severity code [node-id]`. Messages are never compared. */
struct ADAMSKAIEDITOR_API FADSKFixtureRow
{
	int32 Line = 0;
	EADSKSeverity Severity = EADSKSeverity::Error;
	EADSKDiagnosticCode Code = EADSKDiagnosticCode::XmlMalformed;
	FName NodeId;
};

struct ADAMSKAIEDITOR_API FADSKFixtureFailure
{
	FString Path;
	FString Message;
};

struct ADAMSKAIEDITOR_API FADSKFixtureReport
{
	int32 ValidCount = 0;
	int32 InvalidCount = 0;
	TArray<FADSKFixtureFailure> Failures;
	/** Codes that an invalid fixture expected and the compiler produced, exactly. */
	TSet<EADSKDiagnosticCode> CoveredCodes;

	bool IsClean() const { return Failures.IsEmpty(); }
};

struct ADAMSKAIEDITOR_API FADSKFixtures
{
	/** Allowed only on the first line of an .expect file. */
	static constexpr const TCHAR* PadToLimitDirective = TEXT("# generate: pad-to-limit");

	/** Every stable code, discovered through LexToString so a new code cannot be forgotten. */
	static TArray<EADSKDiagnosticCode> AllCodes();
	static bool CodeFromString(const FString& Name, EADSKDiagnosticCode& OutCode);

	static bool ParseExpect(const FString& Text, TArray<FADSKFixtureRow>& OutRows, bool& bOutPadToLimit, FString& OutError);

	/** Expects xml files under `<Root>/valid` and `<Root>/invalid`, plus one `.expect` per invalid file. */
	static FADSKFixtureReport Run(const FString& Root, const TSharedRef<const FADSKNodeRegistry>& Registry);
};
