// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Catalogue/ADSKRegistryAssembly.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Program/ADSKHoldAnalysis.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/TempTree.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

namespace FHoldAnalysisSpecPrivate
{
	TArray<FADSKHoldRisk> Risks(const FString& Tree, const FString& Handlers = FString(), const FString& OuterHandlers = FString())
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"h\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/><Input name=\"Ok\" type=\"Bool\" default=\"true\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"outer\"><State id=\"outer\" initial=\"s\">") + OuterHandlers + TEXT("<State id=\"s\" tree=\"t\">") + Handlers
			+ TEXT("</State><State id=\"other\"/></State></StateMachine><Tree id=\"t\">") + Tree + TEXT("</Tree></Agent>");
		return ADSK::FindHoldRisks(*ADSKTest::LoadOrStandIn(TEXT("spec.xml"), Xml, FADSKTestNodes::MakeFrozenRegistry(MakeShared<FADSKTestLeafLog>())));
	}

	FString Describe(const TArray<FADSKHoldRisk>& Found)
	{
		FString Text = FString::Printf(TEXT("%d risks"), Found.Num());
		for (const FADSKHoldRisk& Risk : Found)
		{
			Text += FString::Printf(TEXT("\n  state %d line %d %s%s"), Risk.State, Risk.Line,
				Risk.bSuccess ? TEXT("success") : TEXT("failure"), Risk.bConditionalOnly ? TEXT(" conditional-only") : TEXT(""));
		}
		return Text;
	}

	FADSKCheck Only(const TArray<FADSKHoldRisk>& Found, bool bSuccess, bool bConditionalOnly)
	{
		if (Found.Num() == 1 && Found[0].bSuccess == bSuccess && Found[0].bConditionalOnly == bConditionalOnly)
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("expected one %s%s risk, got "), bSuccess ? TEXT("success") : TEXT("failure"),
			bConditionalOnly ? TEXT(" conditional-only") : TEXT("")) + Describe(Found));
	}

	FADSKCheck NoRisk(const TArray<FADSKHoldRisk>& Found)
	{
		return Found.IsEmpty() ? FADSKCheck::Pass() : FADSKCheck::Fail(TEXT("expected no risk, got ") + Describe(Found));
	}

	const TCHAR* Both = TEXT("<On event=\"tree.success\" goto=\"other\"/><On event=\"tree.failure\" goto=\"other\"/>");
	const TCHAR* MoveLeaf = TEXT("<MoveTo id=\"move\" in-goal=\"Goal\"/>");
}

using namespace FHoldAnalysisSpecPrivate;

TEST_CLASS_WITH_FLAGS(FHoldAnalysisSpec, "AdamskAI.Validate.HoldAnalysis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnUnhandledOutcomeIsARisk)
	{
		ADSK_ASSERT(Only(Risks(MoveLeaf, TEXT("<On event=\"tree.success\" goto=\"other\"/>")), false, false));
		ADSK_ASSERT(NoRisk(Risks(MoveLeaf, Both)));
	}

	TEST_METHOD(AHandlerOnAnAncestorCovers)
	{
		ADSK_ASSERT(NoRisk(Risks(MoveLeaf, FString(), Both)));
	}

	TEST_METHOD(ConditionalHandlersAloneAreARisk)
	{
		ADSK_ASSERT(Only(Risks(MoveLeaf, TEXT("<On event=\"tree.success\" goto=\"other\" if=\"Ok\"/><On event=\"tree.failure\" goto=\"other\"/>")), true, true));
		ADSK_ASSERT(NoRisk(Risks(MoveLeaf, TEXT("<On event=\"tree.success\" goto=\"other\" if=\"Ok\"/>"), Both)));
	}

	TEST_METHOD(OutcomesFollowTheNodeKinds)
	{
		const FString SuccessOnly = TEXT("<On event=\"tree.success\" goto=\"other\"/>");
		// Wait and Roll never fail; a Selector fails only when every option can; a parallel ends as its main child.
		ADSK_ASSERT(NoRisk(Risks(TEXT("<Wait id=\"w\" duration=\"1\"/>"), SuccessOnly)));
		ADSK_ASSERT(NoRisk(Risks(FString(TEXT("<Selector id=\"sel\">")) + MoveLeaf + TEXT("<Wait id=\"w\" duration=\"1\"/></Selector>"), SuccessOnly)));
		ADSK_ASSERT(NoRisk(Risks(FString(TEXT("<SimpleParallel id=\"par\"><Wait id=\"w\" duration=\"1\"/>")) + MoveLeaf + TEXT("</SimpleParallel>"), SuccessOnly)));
		ADSK_ASSERT(Only(Risks(FString(TEXT("<Sequence id=\"seq\"><Wait id=\"w\" duration=\"1\"/>")) + MoveLeaf + TEXT("</Sequence>"), SuccessOnly), false, false));
		ADSK_ASSERT(Only(Risks(TEXT("<Guard id=\"g\" in-condition=\"Ok\"><Wait id=\"w\" duration=\"1\"/></Guard>"), SuccessOnly), false, false));
		// Provider leaves may end either way, so a failure handler alone leaves success open.
		ASSERT_THAT(AreEqual(1, Risks(FString(TEXT("<Sequence id=\"seq\">")) + MoveLeaf + TEXT("<AcquireResource id=\"a\"/></Sequence>"),
			TEXT("<On event=\"tree.failure\" goto=\"other\"/>")).Num()));
	}

	TEST_METHOD(TheDescriptionNamesStateAndOutcome)
	{
		const TArray<FADSKHoldRisk> Found = Risks(MoveLeaf, TEXT("<On event=\"tree.success\" goto=\"other\"/>"));
		ASSERT_THAT(AreEqual(1, Found.Num()));
		const FString Xml = TEXT("<Agent version=\"1\" id=\"h\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>");
		const FADSKCompileResult Result = Load(Xml);
		const TArray<FADSKHoldRisk> Bare = ADSK::FindHoldRisks(*Result.Definition);
		ADSK_ASSERT(Only(Bare, true, false));
		ASSERT_THAT(AreEqual(FString(TEXT("state 's': tree.success has no handler on its state path; the brain holds there when it does")),
			ADSK::DescribeHoldRisk(*Result.Definition, Bare[0])));
	}

	TEST_METHOD(NoExampleHasAHoldRisk)
	{
		TArray<FString> Errors;
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors).ToSharedRef();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FADSKTestPaths::PluginBase() / TEXT("Examples/*.xml")), true, false);
		ASSERT_THAT(IsTrue(Files.Num() > 0, TEXT("no example files")));
		for (const FString& File : Files)
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *(FADSKTestPaths::PluginBase() / TEXT("Examples") / File));
			const FADSKCompileResult Result = FADSKCompiler::Compile(File, Text, Registry);
			ASSERT_THAT(IsTrue(Result.IsSuccess(), File + ADSKTest::FormatDiagnostics(Result)));
			for (const FADSKHoldRisk& Risk : ADSK::FindHoldRisks(*Result.Definition))
			{
				TestRunner->AddError(FString::Printf(TEXT("%s: %s"), *File, *ADSK::DescribeHoldRisk(*Result.Definition, Risk)));
			}
		}
	}
};

#endif
