// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKRegistryAssembly.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Program/ADSKLint.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/TempTree.h"
#include "CQTest.h"

namespace FLintSpecPrivate
{
	/** One line per element, so a note's line names its element: Goal 3, Spare 4, Plan 7, Unused 8, Poke 11, Nobody 12, s 15, lost 16. */
	FString LintXml(const TCHAR* Extra = TEXT(""))
	{
		return FString::Join(TArray<FString>{
			TEXT("<Agent version=\"1\" id=\"l\">"),
			TEXT("<Inputs>"),
			TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/>"),
			TEXT("<Input name=\"Spare\" type=\"Bool\" default=\"true\"/>"),
			TEXT("</Inputs>"),
			TEXT("<Locals>"),
			TEXT("<Local name=\"Plan\" type=\"Vector\"/>"),
			TEXT("<Local name=\"Unused\" type=\"Bool\"/>"),
			TEXT("</Locals>"),
			TEXT("<Events>"),
			TEXT("<Event name=\"Poke\"/>"),
			TEXT("<Event name=\"Nobody\"/>"),
			TEXT("</Events>"),
			TEXT("<StateMachine id=\"m\" initial=\"s\">"),
			TEXT("<State id=\"s\" tree=\"t\"><On event=\"Poke\" goto=\"s\"/><On event=\"tree.success\" goto=\"s\"/><On event=\"tree.failure\" goto=\"s\"/></State>"),
			FString(TEXT("<State id=\"lost\"/>")) + Extra,
			TEXT("</StateMachine>"),
			TEXT("<Tree id=\"t\"><MoveTo id=\"go\" in-goal=\"Goal\"/></Tree>"),
			TEXT("</Agent>"),
		}, TEXT("\n"));
	}

	TArray<FString> Lines(const FString& Xml)
	{
		TArray<FString> Out;
		for (const FADSKLintNote& Note : ADSK::FindLintNotes(*ADSKTest::LoadOrStandIn(TEXT("lint.xml"), Xml, FADSKTestNodes::MakeFrozenRegistry(MakeShared<FADSKTestLeafLog>()))))
		{
			Out.Add(FString::Printf(TEXT("%d %s"), Note.Line, Note.Code));
		}
		return Out;
	}
}

using namespace FLintSpecPrivate;

TEST_CLASS_WITH_FLAGS(FLintSpec, "AdamskAI.Validate.Lint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EachRuleNamesItsElement)
	{
		const TArray<FString> Expected{TEXT("16 unreachable-state"), TEXT("12 unhandled-event"), TEXT("7 unread-local"), TEXT("8 unread-local"), TEXT("4 unread-input")};
		const TArray<FString> Found = Lines(LintXml());
		ASSERT_THAT(AreEqual(Expected, Found, FString::Join(Found, TEXT(", "))));
	}

	TEST_METHOD(ATargetedStateAndItsInitialChainAreReachable)
	{
		const FString Xml = LintXml().Replace(TEXT("<State id=\"lost\"/>"), TEXT("<State id=\"lost\" initial=\"deep\"><State id=\"deep\"/></State>"))
			.Replace(TEXT("<On event=\"Poke\" goto=\"s\"/>"), TEXT("<On event=\"Poke\" goto=\"lost\"/>"));
		ASSERT_THAT(IsFalse(Lines(Xml).ContainsByPredicate([](const FString& Line) { return Line.EndsWith(TEXT("unreachable-state")); }), FString::Join(Lines(Xml), TEXT(", "))));
	}

	TEST_METHOD(EveryExampleIsLintClean)
	{
		// chase_and_wait keeps Home and Reach for the host contract it documents; the rest must stay clean.
		const TArray<FString> Allowed{TEXT("9 unread-input"), TEXT("10 unread-input")};
		TArray<FString> Errors;
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors).ToSharedRef();
		const FString Dir = FADSKTestPaths::PluginBase() / TEXT("Examples");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.xml")), true, false);
		ASSERT_THAT(IsFalse(Files.IsEmpty(), TEXT("no examples under ") + Dir));
		for (const FString& File : Files)
		{
			FString Xml;
			FFileHelper::LoadFileToString(Xml, *(Dir / File));
			const FADSKCompileResult Result = FADSKCompiler::Compile(File, Xml, Registry);
			ASSERT_THAT(IsTrue(Result.IsSuccess(), File + ADSKTest::FormatDiagnostics(Result)));
			for (const FADSKLintNote& Note : ADSK::FindLintNotes(*Result.Definition))
			{
				const FString Line = FString::Printf(TEXT("%d %s"), Note.Line, Note.Code);
				if (!(File == TEXT("chase_and_wait.xml") && Allowed.Contains(Line)))
				{
					TestRunner->AddError(FString::Printf(TEXT("%s:%s: %s"), *File, *Line, *Note.Message));
				}
			}
		}
	}
};

#endif
