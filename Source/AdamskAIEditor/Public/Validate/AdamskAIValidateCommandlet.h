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
 * `-run=AdamskAIValidate` modes: `-paths=`, none, `-fixtures`, `-describe=`, `-catalog=`/`-xsd=`, `-projectcatalog`/
 * `-projectxsd`, `-simulate=`, `-all` (docs/dev/testing.md). Exit 0/1/2; last line `ADSK_VALIDATE_COMPLETE exit=N`.
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
