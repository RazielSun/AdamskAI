// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKRegistryAssembly.h"
#include "Sources/ADSKSourcePath.h"
#include "Sources/ADSKValidation.h"
#include "Program/ADSKDefinition.h"
#include "CQTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Support/TestChecks.h"

namespace FSourceValidationSpecPrivate
{
	FString ValidXml()
	{
		return TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"0\"/></Tree></Agent>");
	}
}

using namespace FSourceValidationSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSourcePathSpec, "AdamskAI.Host.SourcePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NormalizesOnlySafeLowercaseXmlPaths)
	{
		FString Path;
		ASSERT_THAT(IsTrue(FADSKSourcePath::Normalize(TEXT("folder\\\\sub//agent.xml"), Path), TEXT("the mixed-separator path was rejected")));
		ASSERT_THAT(AreEqual(FString(TEXT("folder/sub/agent.xml")), Path));
		for (const TCHAR* Bad : {TEXT(""), TEXT("/a.xml"), TEXT("C:/a.xml"), TEXT("./a.xml"), TEXT("x/../a.xml"), TEXT("a.XML"), TEXT("a.xml/..")})
		{
			ASSERT_THAT(IsFalse(FADSKSourcePath::Normalize(Bad, Path), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}
};

TEST_CLASS_WITH_FLAGS(FSourceValidationSpec, "AdamskAI.Host.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ReadsSortsAndReportsMissingAndOversizeSources)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		ASSERT_THAT(IsTrue(Registry.IsValid(), TEXT("the catalogue did not assemble: ") + FString::Join(Errors, TEXT("; "))));
		const FString Root = FPaths::ProjectSavedDir() / TEXT("Automation/AdamskAI") / FGuid::NewGuid().ToString();
		IFileManager::Get().MakeDirectory(*(Root / TEXT("nested")), true);
		FFileHelper::SaveStringToFile(ValidXml(), *(Root / TEXT("z.xml")));
		FFileHelper::SaveStringToFile(ValidXml(), *(Root / TEXT("nested/a.xml")));
		const TArray<FADSKSourceReadResult> Results = FADSKValidation::ValidateFiles(Root, Registry.ToSharedRef());
		ASSERT_THAT(AreEqual(2, Results.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("nested/a.xml")), Results[0].Result.Definition->GetSourceName()));
		const FADSKSourceReadResult Missing = FADSKValidation::ReadSource(Root, TEXT("missing.xml"), Registry.ToSharedRef());
		ASSERT_THAT(IsTrue(Missing.bReadFailed, TEXT("missing.xml was read")));
		ASSERT_THAT(AreEqual(FString(TEXT("source-read")), Missing.ReportCode));
		const FString Huge = FString::ChrN(FADSKCompiler::MaxSourceBytes + 1, TEXT('x'));
		FFileHelper::SaveStringToFile(Huge, *(Root / TEXT("huge.xml")));
		const FADSKSourceReadResult Oversize = FADSKValidation::ReadSource(Root, TEXT("huge.xml"), Registry.ToSharedRef());
		ASSERT_THAT(IsFalse(Oversize.bReadFailed, TEXT("huge.xml was not read")));
		ASSERT_THAT(AreEqual(1, Oversize.Result.Count(EADSKDiagnosticCode::LimitExceeded), ADSKTest::FormatDiagnostics(Oversize.Result)));
		ASSERT_THAT(IsTrue(Oversize.Text.IsEmpty(), TEXT("the oversize text was kept")));
		IFileManager::Get().DeleteDirectory(*Root, false, true);
	}
};

#endif
