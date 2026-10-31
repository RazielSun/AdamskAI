// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Validate/ADSKValidateReport.h"
#include "Validate/AdamskAIValidateCommandlet.h"

class FADSKNodeRegistry;

namespace ADSKValidateModes
{
	enum class EMode : uint8 { Default, Paths, Fixtures, Describe, Catalog, Xsd, Simulate };

	struct FOptions
	{
		EMode Mode = EMode::Default;
		FString Argument;
		bool bFullRegistry = false;
		bool bCheck = false;
		/** `-projectcatalog` / `-projectxsd`: the target is `node-catalog.md` / `adamskai.xsd` in `<project>/Content/<BehaviorRoot>/`. */
		bool bProject = false;

		const TCHAR* ModeName() const;
	};

	struct FOutcome
	{
		/** Printed before the rows, as they are (a simulation's transitions and summary). */
		TArray<FString> Lines;
		TArray<FADSKReportRow> Rows;
		int32 Files = 0;
		/** Set for usage, setup and write failures; the exit code is then 2. */
		FString Error;
		/** Printed before the summary when set; never changes the exit code. */
		FString Note;
		bool bWritesReport = false;
	};

	bool ParseOptions(const FString& Params, FOptions& OutOptions, FString& OutError);

	void Run(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& OutOutcome);

	FADSKValidateEnvironment Resolve(const FADSKValidateEnvironment* Override);
}
