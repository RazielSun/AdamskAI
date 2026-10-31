// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Validate/ADSKValidateModes.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Validate/ADSKAssetValidation.h"
#include "Catalogue/ADSKCatalogue.h"
#include "Catalogue/ADSKSchemaXsd.h"
#include "Simulate/ADSKSimulator.h"
#include "Validate/ADSKStaging.h"
#include "Fixtures/ADSKFixtures.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Settings/ADSKSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ADSKValidateModes
{
	namespace Private
	{
		const FString* FindParam(const TMap<FString, FString>& Params, const TCHAR* Key)
		{
			for (const TPair<FString, FString>& Pair : Params)
			{
				if (Pair.Key.Equals(Key, ESearchCase::IgnoreCase)) { return &Pair.Value; }
			}
			return nullptr;
		}

		bool HasSwitch(const TArray<FString>& Switches, const TCHAR* Name)
		{
			return Switches.ContainsByPredicate([Name](const FString& Switch) { return Switch.Equals(Name, ESearchCase::IgnoreCase); });
		}

		FString Full(const FString& Path)
		{
			FString Result = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Result);
			FPaths::RemoveDuplicateSlashes(Result);
			return Result;
		}

		FString Display(const FADSKValidateEnvironment& Environment, const FString& Path)
		{
			const FString Project = Full(Environment.ProjectDir) / TEXT("");
			const FString Absolute = Full(Path);
			return Absolute.StartsWith(Project) ? Absolute.RightChop(Project.Len()) : Absolute;
		}

		FString ProjectBehaviorRoot(const FADSKValidateEnvironment& Environment)
		{
			return Environment.ProjectDir / TEXT("Content") / UADSKSettings::GetBehaviorRoot();
		}

		FString StagingNote()
		{
			const TCHAR* Section = TEXT("/Script/UnrealEd.ProjectPackagingSettings");
			TArray<FString> Entries;
			TArray<FString> NonUfs;
			GConfig->GetArray(Section, TEXT("DirectoriesToAlwaysStageAsUFS"), Entries, GGameIni);
			GConfig->GetArray(Section, TEXT("DirectoriesToAlwaysStageAsNonUFS"), NonUfs, GGameIni);
			Entries.Append(NonUfs);
			const FString Root = UADSKSettings::GetBehaviorRoot();
			if (ADSKStaging::IsStagedBy(Root, Entries)) { return FString(); }
			return FString::Printf(TEXT("behavior root '%s' is not staged; a packaged build will not find its files. Add +DirectoriesToAlwaysStageAsUFS=(Path=\"%s\") under [%s] in DefaultGame.ini"), *Root, *Root, Section);
		}

		TSharedPtr<const FADSKNodeRegistry> GetRegistry(bool bFull, FString& OutError)
		{
			TArray<FString> Errors;
			const TSharedPtr<const FADSKNodeRegistry> Registry = bFull ? FADSKRegistryAssembly::TryGet(Errors) : FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
			if (!Registry.IsValid() || !Errors.IsEmpty())
			{
				OutError = TEXT("registry assembly failed: ") + FString::Join(Errors, TEXT("; "));
				return nullptr;
			}
			return Registry;
		}

		/** Waits for the Asset Registry's initial scan, so absence is a fact rather than a race. */
		void EnsureAssetScan()
		{
			if (IAssetRegistry* Registry = IAssetRegistry::Get()) { Registry->SearchAllAssets(true); }
		}

		void AppendAssetWarnings(FADSKSourceReadResult& Result)
		{
			if (Result.Result.Definition.IsValid()) { FADSKAssetValidation::AppendMissingAssets(*Result.Result.Definition, Result.Result); }
		}

		void ValidateDirectory(const FADSKValidateEnvironment& Environment, const FString& Dir, const TSharedRef<const FADSKNodeRegistry>& Registry, FOutcome& Outcome)
		{
			TArray<FADSKSourceReadResult> Results = FADSKValidation::ValidateFiles(Dir, Registry);
			Outcome.Files += Results.Num();
			for (FADSKSourceReadResult& Result : Results)
			{
				AppendAssetWarnings(Result);
				FADSKValidateReport::AppendResult(Result, Display(Environment, Dir), Outcome.Rows);
				FADSKValidateReport::AppendNotes(Result, Display(Environment, Dir), Outcome.Rows);
			}
		}

		void ValidateEntry(const FADSKValidateEnvironment& Environment, const FString& Entry, const TSharedRef<const FADSKNodeRegistry>& Registry, FOutcome& Outcome)
		{
			const FString Path = Full(Entry);
			if (IFileManager::Get().DirectoryExists(*Path)) { ValidateDirectory(Environment, Path, Registry, Outcome); return; }
			++Outcome.Files;
			if (!Path.EndsWith(TEXT(".xml")) || !IFileManager::Get().FileExists(*Path))
			{
				const FString Message = !Path.EndsWith(TEXT(".xml"))
					? FString::Printf(TEXT("not a directory or a .xml file: %s"), *Path)
					: FString::Printf(TEXT("no such file: %s (a relative path is resolved against %s)"), *Path, *FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir()));
				Outcome.Rows.Add(FADSKValidateReport::MakeRow(Display(Environment, Path), TEXT("error"), TEXT("source-read"), Message));
				return;
			}
			FADSKSourceReadResult Result = FADSKValidation::ReadSource(FPaths::GetPath(Path), FPaths::GetCleanFilename(Path), Registry);
			AppendAssetWarnings(Result);
			FADSKValidateReport::AppendResult(Result, Display(Environment, FPaths::GetPath(Path)), Outcome.Rows);
			FADSKValidateReport::AppendNotes(Result, Display(Environment, FPaths::GetPath(Path)), Outcome.Rows);
		}

		void RunPaths(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(true, Outcome.Error);
			if (!Registry.IsValid()) { return; }
			EnsureAssetScan();
			TArray<FString> Entries;
			Options.Argument.ParseIntoArray(Entries, TEXT("+"), true);
			for (const FString& Entry : Entries) { ValidateEntry(Environment, Entry, Registry.ToSharedRef(), Outcome); }
		}

		void RunDefault(const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const FString Examples = Environment.PluginBase / TEXT("Examples");
			if (!IFileManager::Get().DirectoryExists(*Examples)) { Outcome.Error = TEXT("plugin Examples directory is missing: ") + Examples; return; }
			const TSharedPtr<const FADSKNodeRegistry> Plugin = GetRegistry(false, Outcome.Error);
			if (!Plugin.IsValid()) { return; }
			EnsureAssetScan();
			ValidateDirectory(Environment, Examples, Plugin.ToSharedRef(), Outcome);
			// The demo scenes' files use plugin nodes only, like the examples.
			const FString Demo = Environment.PluginBase / TEXT("Demo");
			if (IFileManager::Get().DirectoryExists(*Demo)) { ValidateDirectory(Environment, Demo, Plugin.ToSharedRef(), Outcome); }
			const FString ProjectAI = ProjectBehaviorRoot(Environment);
			if (IFileManager::Get().DirectoryExists(*ProjectAI))
			{
				const TSharedPtr<const FADSKNodeRegistry> Full = GetRegistry(true, Outcome.Error);
				if (!Full.IsValid()) { return; }
				ValidateDirectory(Environment, ProjectAI, Full.ToSharedRef(), Outcome);
				Outcome.Note = StagingNote();
			}
		}

		void RunFixtures(const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const FString Root = Environment.PluginBase / TEXT("Fixtures");
			if (!IFileManager::Get().DirectoryExists(*Root)) { Outcome.Error = TEXT("plugin Fixtures directory is missing: ") + Root; return; }
			const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(false, Outcome.Error);
			if (!Registry.IsValid()) { return; }
			const FADSKFixtureReport Report = FADSKFixtures::Run(Root, Registry.ToSharedRef());
			Outcome.Files = Report.ValidCount + Report.InvalidCount;
			for (const FADSKFixtureFailure& Failure : Report.Failures)
			{
				Outcome.Rows.Add(FADSKValidateReport::MakeRow(Display(Environment, Root / Failure.Path), TEXT("error"), TEXT("fixture"), Failure.Message));
			}
		}

		void RunDescribe(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(Options.bFullRegistry, Outcome.Error);
			if (!Registry.IsValid()) { return; }
			FString Error;
			if (!FADSKValidateReport::WriteAtomic(Full(Options.Argument), Registry->ExportSchemaJson(), Environment.WriteHooks, Error)) { Outcome.Error = Error; }
		}

		/** `-catalog` and `-xsd`: two renderings of one registry, written or checked alike; both report `catalogue-stale`. */
		void RunGenerated(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const bool bXsd = Options.Mode == EMode::Xsd;
			const TCHAR* What = bXsd ? TEXT("schema") : TEXT("catalogue");
			const TCHAR* Flag = Options.bProject ? (bXsd ? TEXT("-projectxsd") : TEXT("-projectcatalog")) : (bXsd ? TEXT("-xsd") : TEXT("-catalog"));
			const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(Options.bFullRegistry, Outcome.Error);
			if (!Registry.IsValid()) { return; }
			const FString Target = Options.bProject ? Full(ProjectBehaviorRoot(Environment) / (bXsd ? TEXT("adamskai.xsd") : TEXT("node-catalog.md"))) : Full(Options.Argument);
			if (Options.bProject && Options.bCheck && !IFileManager::Get().FileExists(*Target))
			{
				// A project without provider nodes has nothing beyond the plugin's to commit.
				const TSharedPtr<const FADSKNodeRegistry> Plugin = GetRegistry(false, Outcome.Error);
				if (!Plugin.IsValid()) { return; }
				if (Plugin->Num() == Registry->Num())
				{
					Outcome.Note = FString::Printf(TEXT("no project node providers; project %s not required"), What);
					return;
				}
			}
			FString Rendered;
			const FString Label = Options.bFullRegistry ? TEXT("full") : TEXT("plugin");
			if (!(bXsd ? FADSKSchemaXsd::Render(*Registry, Label, Rendered, Outcome.Error) : FADSKCatalogue::Render(*Registry, Label, Rendered, Outcome.Error))) { return; }
			if (!Options.bCheck)
			{
				FString Error;
				if (!FADSKValidateReport::WriteAtomic(Target, Rendered, Environment.WriteHooks, Error)) { Outcome.Error = Error; }
				return;
			}
			FString Existing;
			if (!FFileHelper::LoadFileToString(Existing, *Target))
			{
				Outcome.Rows.Add(FADSKValidateReport::MakeRow(Display(Environment, Target), TEXT("error"), TEXT("catalogue-stale"), FString::Printf(TEXT("%s file is missing; regenerate with %s"), What, Flag)));
			}
			else if (!FADSKCatalogue::Matches(Rendered, Existing))
			{
				Outcome.Rows.Add(FADSKValidateReport::MakeRow(Display(Environment, Target), TEXT("error"), TEXT("catalogue-stale"), FString::Printf(TEXT("%s is stale; regenerate with %s"), What, Flag)));
			}
		}

		/** `-simulate=<scenario.json|dir>[+...]`: each scenario against the full catalogue; traces go to Saved/AdamskAI/simulate/. */
		void RunSimulate(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& Outcome)
		{
			const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(true, Outcome.Error);
			if (!Registry.IsValid()) { return; }
			TArray<FString> Entries, Scenarios;
			Options.Argument.ParseIntoArray(Entries, TEXT("+"), true);
			for (const FString& Entry : Entries)
			{
				const FString Path = Full(Entry);
				if (IFileManager::Get().DirectoryExists(*Path))
				{
					TArray<FString> Found;
					IFileManager::Get().FindFilesRecursive(Found, *Path, TEXT("*.json"), true, false);
					Found.Sort();
					Scenarios.Append(Found);
				}
				else if (IFileManager::Get().FileExists(*Path)) { Scenarios.Add(Path); }
				else { Outcome.Error = TEXT("no such scenario file or directory: ") + Path; return; }
			}
			for (const FString& Scenario : Scenarios)
			{
				const FADSKSimulationReport Report = FADSKSimulator::Run(Scenario, Display(Environment, Scenario), Registry.ToSharedRef());
				++Outcome.Files;
				if (!Report.Error.IsEmpty()) { Outcome.Error = Report.Error; return; }
				Outcome.Lines.Append(Report.Lines);
				Outcome.Rows.Append(Report.Rows);
				FString Error;
				const FString Trace = Environment.SavedDir / TEXT("AdamskAI/simulate") / (FPaths::GetBaseFilename(Scenario) + TEXT(".trace"));
				if (!FADSKValidateReport::WriteAtomic(Trace, Report.Trace, Environment.WriteHooks, Error)) { Outcome.Error = Error; return; }
			}
			if (Scenarios.IsEmpty()) { Outcome.Error = TEXT("no scenario files found"); }
		}
	}

	const TCHAR* FOptions::ModeName() const
	{
		switch (Mode)
		{
		case EMode::Paths: return TEXT("paths");
		case EMode::Fixtures: return TEXT("fixtures");
		case EMode::Describe: return TEXT("describe");
		case EMode::Catalog: return TEXT("catalog");
		case EMode::Xsd: return TEXT("xsd");
		case EMode::Simulate: return TEXT("simulate");
		default: return TEXT("default");
		}
	}

	bool ParseOptions(const FString& Params, FOptions& OutOptions, FString& OutError)
	{
		TArray<FString> Tokens, Switches;
		TMap<FString, FString> Values;
		UCommandlet::ParseCommandLine(*Params, Tokens, Switches, Values);
		for (const FString& Token : Tokens)
		{
			if (!Token.EndsWith(TEXT(".uproject"))) { OutError = FString::Printf(TEXT("unknown mode '%s'"), *Token); return false; }
		}
		const struct { const TCHAR* Key; EMode Mode; } Valued[] = {{TEXT("paths"), EMode::Paths}, {TEXT("describe"), EMode::Describe}, {TEXT("catalog"), EMode::Catalog},
			{TEXT("xsd"), EMode::Xsd}, {TEXT("simulate"), EMode::Simulate}};
		int32 Modes = Private::HasSwitch(Switches, TEXT("fixtures")) ? 1 : 0;
		OutOptions.Mode = Modes ? EMode::Fixtures : EMode::Default;
		for (const auto& Entry : Valued)
		{
			if (Private::HasSwitch(Switches, Entry.Key)) { OutError = FString::Printf(TEXT("-%s needs a value"), Entry.Key); return false; }
			if (const FString* Value = Private::FindParam(Values, Entry.Key))
			{
				if (Value->IsEmpty()) { OutError = FString::Printf(TEXT("-%s needs a value"), Entry.Key); return false; }
				OutOptions.Mode = Entry.Mode;
				OutOptions.Argument = *Value;
				++Modes;
			}
		}
		if (Private::FindParam(Values, TEXT("fixtures"))) { OutError = TEXT("-fixtures takes no value"); return false; }
		for (const auto& Project : {TPair<const TCHAR*, EMode>(TEXT("projectcatalog"), EMode::Catalog), TPair<const TCHAR*, EMode>(TEXT("projectxsd"), EMode::Xsd)})
		{
			if (Private::FindParam(Values, Project.Key)) { OutError = FString::Printf(TEXT("-%s takes no value"), Project.Key); return false; }
			if (Private::HasSwitch(Switches, Project.Key))
			{
				OutOptions.Mode = Project.Value;
				OutOptions.bProject = true;
				++Modes;
			}
		}
		if (Modes > 1) { OutError = TEXT("conflicting modes; use exactly one of -paths, -fixtures, -describe, -catalog, -xsd, -projectcatalog, -projectxsd, -simulate"); return false; }
		const FString* Registry = Private::FindParam(Values, TEXT("registry"));
		if (Registry && OutOptions.bProject) { OutError = TEXT("-projectcatalog and -projectxsd always use the full registry; drop -registry"); return false; }
		const bool bRenders = OutOptions.Mode == EMode::Catalog || OutOptions.Mode == EMode::Xsd;
		const bool bGenerates = OutOptions.Mode == EMode::Describe || bRenders;
		if (Registry && (!bGenerates || (*Registry != TEXT("plugin") && *Registry != TEXT("full"))))
		{
			OutError = bGenerates ? FString::Printf(TEXT("bad -registry '%s'; use plugin or full"), **Registry) : FString(TEXT("-registry applies only to -describe, -catalog and -xsd"));
			return false;
		}
		OutOptions.bFullRegistry = OutOptions.bProject || (Registry && *Registry == TEXT("full"));
		OutOptions.bCheck = Private::HasSwitch(Switches, TEXT("check"));
		if (OutOptions.bCheck && !bRenders) { OutError = TEXT("-check applies only to -catalog and -xsd"); return false; }
		return true;
	}

	void Run(const FOptions& Options, const FADSKValidateEnvironment& Environment, FOutcome& OutOutcome)
	{
		switch (Options.Mode)
		{
		case EMode::Paths: OutOutcome.bWritesReport = true; Private::RunPaths(Options, Environment, OutOutcome); break;
		case EMode::Default: OutOutcome.bWritesReport = true; Private::RunDefault(Environment, OutOutcome); break;
		case EMode::Fixtures: OutOutcome.bWritesReport = true; Private::RunFixtures(Environment, OutOutcome); break;
		case EMode::Describe: Private::RunDescribe(Options, Environment, OutOutcome); break;
		case EMode::Catalog:
		case EMode::Xsd: Private::RunGenerated(Options, Environment, OutOutcome); break;
		case EMode::Simulate: Private::RunSimulate(Options, Environment, OutOutcome); break;
		}
		if (OutOutcome.bWritesReport && OutOutcome.Error.IsEmpty() && OutOutcome.Files == 0) { OutOutcome.Error = TEXT("no behavior files found"); }
	}

	FADSKValidateEnvironment Resolve(const FADSKValidateEnvironment* Override)
	{
		FADSKValidateEnvironment Result;
		if (Override) { Result = *Override; }
		if (Result.PluginBase.IsEmpty())
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AdamskAI"));
			if (Plugin.IsValid()) { Result.PluginBase = Plugin->GetBaseDir(); }
		}
		if (Result.ProjectDir.IsEmpty()) { Result.ProjectDir = FPaths::ProjectDir(); }
		if (Result.SavedDir.IsEmpty()) { Result.SavedDir = Result.ProjectDir / TEXT("Saved"); }
		return Result;
	}
}
