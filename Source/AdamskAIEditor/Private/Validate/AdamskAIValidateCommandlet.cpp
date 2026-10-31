// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Validate/AdamskAIValidateCommandlet.h"

#include "Validate/ADSKValidateModes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdamskAIValidate, Log, All);

namespace ADSKValidateCommandletPrivate
{
	const FADSKValidateEnvironment* EnvironmentOverride = nullptr;

	void Print(const FADSKValidateEnvironment& Environment, const FString& Line)
	{
		if (Environment.Print) { Environment.Print(Line); }
		else { UE_LOG(LogAdamskAIValidate, Display, TEXT("%s"), *Line); }
	}

	int32 Finish(const ADSKValidateModes::FOptions& Options, const ADSKValidateModes::FOutcome& Outcome, const FADSKValidateEnvironment& Environment, bool bMarker = true)
	{
		int32 Exit = !Outcome.Error.IsEmpty() ? 2 : FADSKValidateReport::ExitCodeFor(Outcome.Rows);
		int32 Errors = 0, Notes = 0;
		for (const FString& Line : Outcome.Lines) { Print(Environment, Line); }
		for (const bool bNotes : {false, true})
		{
			for (const FADSKReportRow& Row : Outcome.Rows)
			{
				if (Row.IsNote() != bNotes) continue;
				Print(Environment, FADSKValidateReport::FormatRow(Row));
				Errors += Row.IsError() ? 1 : 0;
				Notes += Row.IsNote() ? 1 : 0;
			}
		}
		if (!Outcome.Note.IsEmpty()) { Print(Environment, FString::Printf(TEXT("note: %s"), *Outcome.Note)); }
		if (!Outcome.Error.IsEmpty()) { Print(Environment, FString::Printf(TEXT("error: %s"), *Outcome.Error)); }
		else if (Outcome.bWritesReport)
		{
			FString Error;
			const FString Path = Environment.SavedDir / TEXT("AdamskAI/validate.json");
			if (!FADSKValidateReport::WriteAtomic(Path, FADSKValidateReport::ToJson(Outcome.Rows, Options.ModeName(), Outcome.Files, Exit), Environment.WriteHooks, Error))
			{
				Print(Environment, FString::Printf(TEXT("error: %s"), *Error));
				Exit = 2;
			}
		}
		Print(Environment, FString::Printf(TEXT("summary: mode=%s files=%d errors=%d warnings=%d notes=%d"), Options.ModeName(), Outcome.Files, Errors, Outcome.Rows.Num() - Errors - Notes, Notes));
		if (bMarker) Print(Environment, FString::Printf(TEXT("ADSK_VALIDATE_COMPLETE exit=%d"), Exit));
		return Exit;
	}

	/**
	 * `-all`: default, fixtures, and the plugin's and the project's catalogue and schema checks in one editor launch.
	 * Each mode prints its own rows and summary; the one marker carries the worst exit, 2 over 1 over 0.
	 */
	int32 RunAll(const FADSKValidateEnvironment& Environment)
	{
		const FString Catalog = Environment.PluginBase / TEXT("docs/node-catalog.md");
		const FString Schema = Environment.PluginBase / TEXT("docs/adamskai.xsd");
		const FString Modes[] = {FString(), TEXT("-fixtures"), FString::Printf(TEXT("-catalog=\"%s\" -registry=plugin -check"), *Catalog), TEXT("-projectcatalog -check"),
			FString::Printf(TEXT("-xsd=\"%s\" -registry=plugin -check"), *Schema), TEXT("-projectxsd -check")};
		int32 Worst = 0;
		for (const FString& Mode : Modes)
		{
			ADSKValidateModes::FOptions Options;
			ADSKValidateModes::FOutcome Outcome;
			if (ADSKValidateModes::ParseOptions(Mode, Options, Outcome.Error)) { ADSKValidateModes::Run(Options, Environment, Outcome); }
			Worst = FMath::Max(Worst, Finish(Options, Outcome, Environment, false));
		}
		Print(Environment, FString::Printf(TEXT("ADSK_VALIDATE_COMPLETE exit=%d"), Worst));
		return Worst;
	}
}

UAdamskAIValidateCommandlet::UAdamskAIValidateCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

void UAdamskAIValidateCommandlet::SetEnvironmentOverride(const FADSKValidateEnvironment* Environment)
{
	ADSKValidateCommandletPrivate::EnvironmentOverride = Environment;
}

int32 UAdamskAIValidateCommandlet::Main(const FString& Params)
{
	const FADSKValidateEnvironment Environment = ADSKValidateModes::Resolve(ADSKValidateCommandletPrivate::EnvironmentOverride);
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	if (Switches.Contains(TEXT("all")) && !Environment.PluginBase.IsEmpty())
	{
		return ADSKValidateCommandletPrivate::RunAll(Environment);
	}
	ADSKValidateModes::FOptions Options;
	ADSKValidateModes::FOutcome Outcome;
	if (ADSKValidateModes::ParseOptions(Params, Options, Outcome.Error))
	{
		if (Environment.PluginBase.IsEmpty()) { Outcome.Error = TEXT("AdamskAI plugin directory could not be resolved"); }
		else { ADSKValidateModes::Run(Options, Environment, Outcome); }
	}
	return ADSKValidateCommandletPrivate::Finish(Options, Outcome, Environment);
}
