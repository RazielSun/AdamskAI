// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Validate/ADSKValidateReport.h"
#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "AdamskAIValidateCommandlet.generated.h"

struct ADAMSKAIEDITOR_API FADSKValidateEnvironment
{
	/** Plugin base directory; empty resolves AdamskAI through IPluginManager. */
	FString PluginBase;
	FString ProjectDir;
	FString SavedDir;
	/** Receives each printed line; empty logs to LogAdamskAIValidate. */
	TFunction<void(const FString&)> Print;
	FADSKAtomicWriteHooks WriteHooks;
};

/**
 * `-run=AdamskAIValidate` with one mode: `-paths=<file|dir>[+...]`, none (examples, then project AI),
 * `-fixtures`, `-describe=<out.json> [-registry=plugin|full]`, `-catalog=<out.md>` / `-xsd=<out.xsd>` [-registry=] [-check],
 * `-projectcatalog` / `-projectxsd` [-check], `-simulate=<scenario.json|dir>[+...]` or `-all`.
 * Exit 0 clean or exact, 1 diagnostics, findings or stale output, 2 usage, setup or write failure. The final
 * line is always `ADSK_VALIDATE_COMPLETE exit=N`.
 */
UCLASS()
class ADAMSKAIEDITOR_API UAdamskAIValidateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAdamskAIValidateCommandlet();

	virtual int32 Main(const FString& Params) override;

	/** Test hook: Main resolves roots from this for as long as it is set; null restores the defaults. */
	static void SetEnvironmentOverride(const FADSKValidateEnvironment* Environment);
};
