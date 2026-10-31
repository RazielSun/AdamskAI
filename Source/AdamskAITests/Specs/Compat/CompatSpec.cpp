// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CQTest.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Compiler/ADSKCompiler.h"
#include "Program/ADSKNormalizedDump.h"
#include "Support/Golden.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"

namespace FCompatSpecPrivate
{
	/** Plugin-relative directories whose `.xml.tables` baselines were frozen when XML v1 shipped. */
	const TCHAR* const BaselineRoots[] = {TEXT("Fixtures/compat/Examples"), TEXT("Fixtures/compat/Fixtures/valid")};

	/** New files never get a baseline, so neither check nor capture looks for sources without one. */
	int32 ForEachBaseline(TFunctionRef<void(const FString& Baseline, const FString& Source)> Visit)
	{
		const FString Base = FADSKTestPaths::PluginBase();
		int32 Visited = 0;
		for (const TCHAR* BaselineRoot : BaselineRoots)
		{
			const FString SourceRoot = FString(BaselineRoot).RightChop(FCString::Strlen(TEXT("Fixtures/compat/")));
			TArray<FString> Baselines;
			IFileManager::Get().FindFiles(Baselines, *(Base / BaselineRoot / TEXT("*.xml.tables")), true, false);
			Baselines.Sort();
			for (const FString& Baseline : Baselines)
			{
				Visit(Base / BaselineRoot / Baseline, Base / SourceRoot / Baseline.LeftChop(FCString::Strlen(TEXT(".tables"))));
				++Visited;
			}
		}
		return Visited;
	}

	bool DumpFile(const FString& SourcePath, FString& OutDump, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *SourcePath))
		{
			OutError = FString::Printf(TEXT("missing source %s"), *SourcePath);
			return false;
		}
		const FADSKCompileResult Result = FADSKCompiler::Compile(SourcePath, Text, ADSKTest::PluginCatalogue());
		if (!Result.IsSuccess())
		{
			OutError = FString::Printf(TEXT("%s does not compile:%s"), *SourcePath, *ADSKTest::FormatDiagnostics(Result));
			return false;
		}
		OutDump = ADSKTest::DumpNormalizedTables(*Result.Definition);
		return true;
	}

	bool CaptureRequested()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("ADSK_CAPTURE_BASELINES")) == TEXT("1");
	}
}

using namespace FCompatSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCompatSpecStable, "AdamskAI.Compat.NormalizedLegacyTablesStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(Run)
	{
		const int32 Checked = ForEachBaseline([this](const FString& Baseline, const FString& Source)
		{
			FString Dump, Error;
			if (!DumpFile(Source, Dump, Error))
			{
				TestRunner->AddError(Error);
				return;
			}
			const FString Diff = ADSKTest::LineDifferences(ADSKTest::ReadNormalized(Baseline), Dump);
			if (!Diff.IsEmpty())
			{
				TestRunner->AddError(FString::Printf(TEXT("%s differs from its frozen baseline:%s"), *Baseline, *Diff));
			}
		});
		ASSERT_THAT(IsTrue(Checked > 0, TEXT("no baselines found under Fixtures/compat")));
	}
};

/** XML v1 compatibility forbids rewriting baselines outside a deliberate format change; runs only with ADSK_CAPTURE_BASELINES=1. */
TEST_CLASS_WITH_FLAGS(FCompatSpecCapture, "AdamskAI.Compat.CaptureBaselines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(Run)
	{
		if (!CaptureRequested())
		{
			TestRunner->AddInfo(TEXT("skipped: set ADSK_CAPTURE_BASELINES=1 to rewrite Fixtures/compat"));
			return;
		}
		int32 Written = 0;
		ForEachBaseline([this, &Written](const FString& Baseline, const FString& Source)
		{
			FString Dump, Error;
			if (!DumpFile(Source, Dump, Error))
			{
				TestRunner->AddError(Error);
				return;
			}
			if (!FFileHelper::SaveStringToFile(Dump, *Baseline, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				TestRunner->AddError(TEXT("cannot write ") + Baseline);
				return;
			}
			++Written;
		});
		TestRunner->AddInfo(FString::Printf(TEXT("rewrote %d baselines"), Written));
	}
};

#endif
