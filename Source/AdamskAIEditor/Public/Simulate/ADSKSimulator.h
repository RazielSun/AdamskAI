// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Validate/ADSKValidateReport.h"
#include "CoreMinimal.h"

class FADSKNodeRegistry;

/** What one scenario run produced. */
struct ADAMSKAIEDITOR_API FADSKSimulationReport
{
	/** The transitions taken (capped), then one summary line. */
	TArray<FString> Lines;
	/** Compile diagnostics, failed expectations and findings (`simulate-*` codes); an error fails the run. */
	TArray<FADSKReportRow> Rows;
	/** Every trace record as adsk.DumpTrace formats it, each line prefixed with its simulated time. */
	FString Trace;
	/** Set when the scenario cannot run at all (unreadable, malformed, names something the file lacks): exit 2. */
	FString Error;
};

/** Runs a file headless against a JSON scenario; reports livelocks, holds and `expect` failures. */
struct ADAMSKAIEDITOR_API FADSKSimulator
{
	static FADSKSimulationReport Run(const FString& ScenarioPath, const FString& DisplayPath, const TSharedRef<const FADSKNodeRegistry>& Registry);
};
