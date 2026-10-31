// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Fixtures/ADSKFixtures.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Support/TempTree.h"

namespace FFixtureSpecPrivate
{
	TSharedRef<const FADSKNodeRegistry> PluginRegistry()
	{
		return ADSKTest::PluginCatalogue();
	}

	FString Failures(const FADSKFixtureReport& Report)
	{
		TArray<FString> Lines;
		for (const FADSKFixtureFailure& Failure : Report.Failures) { Lines.Add(Failure.Path + TEXT(": ") + Failure.Message); }
		return FString::Join(Lines, TEXT("\n"));
	}

	FADSKCheck FailsWith(const FADSKFixtureReport& Report, const TCHAR* Path, const TCHAR* Text)
	{
		if (Report.Failures.ContainsByPredicate([&](const FADSKFixtureFailure& F) { return F.Path == Path && F.Message.Contains(Text); }))
		{
			return FADSKCheck::Pass();
		}
		return FADSKCheck::Fail(FString::Printf(TEXT("no failure for %s containing [%s]; failures:\n%s"), Path, Text, *Failures(Report)));
	}

	const TCHAR* UnknownNodeXml = TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>\n<Tree id=\"t\"><Nope id=\"w\"/></Tree></Agent>");
}

using namespace FFixtureSpecPrivate;

TEST_CLASS_WITH_FLAGS(FFixtureSetSpec, "AdamskAI.Fixtures.Set",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CommittedFixturesMatchTheCompilerExactly)
	{
		const FADSKFixtureReport Report = FADSKFixtures::Run(FADSKTestPaths::PluginBase() / TEXT("Fixtures"), PluginRegistry());
		ASSERT_THAT(IsTrue(Report.IsClean(), Failures(Report)));
		ASSERT_THAT(IsTrue(Report.ValidCount >= 5 && Report.InvalidCount >= 20, FString::Printf(TEXT("%d valid and %d invalid fixtures"), Report.ValidCount, Report.InvalidCount)));
	}

	TEST_METHOD(EveryDiagnosticCodeHasAnExactExpectation)
	{
		const FADSKFixtureReport Report = FADSKFixtures::Run(FADSKTestPaths::PluginBase() / TEXT("Fixtures"), PluginRegistry());
		const TArray<EADSKDiagnosticCode> Codes = FADSKFixtures::AllCodes();
		ASSERT_THAT(IsTrue(Codes.Num() >= 15, FString::Printf(TEXT("only %d diagnostic codes"), Codes.Num())));
		for (const EADSKDiagnosticCode Code : Codes)
		{
			// The compiler never emits missing-asset; the editor's Asset Registry check does, and the Validate rows cover it.
			if (Code == EADSKDiagnosticCode::MissingAsset) { continue; }
			ASSERT_THAT(IsTrue(Report.CoveredCodes.Contains(Code), FString::Printf(TEXT("no fixture covers %s"), LexToString(Code))));
		}
	}

	TEST_METHOD(EveryLimitHasItsOwnFixture)
	{
		const FString Invalid = FADSKTestPaths::PluginBase() / TEXT("Fixtures/invalid");
		for (const TCHAR* Name : {TEXT("limit_source_bytes"), TEXT("limit_element_count"), TEXT("limit_tree_depth"), TEXT("limit_state_depth"), TEXT("limit_state_count")})
		{
			ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(*(Invalid / Name + TEXT(".xml"))), TEXT("missing ") + Invalid / Name + TEXT(".xml")));
			ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(*(Invalid / Name + TEXT(".expect"))), TEXT("missing ") + Invalid / Name + TEXT(".expect")));
		}
	}
};

TEST_CLASS_WITH_FLAGS(FFixtureRunnerSpec, "AdamskAI.Fixtures.Runner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ParsesRowsAndRejectsBadSyntax)
	{
		TArray<FADSKFixtureRow> Rows;
		bool bPad = false;
		FString Error;
		ASSERT_THAT(IsTrue(FADSKFixtures::ParseExpect(TEXT("# generate: pad-to-limit\n0 error limit-exceeded\n3 warning unreferenced-tree t1\n"), Rows, bPad, Error), Error));
		ASSERT_THAT(IsTrue(bPad, TEXT("the pad directive was not read")));
		ASSERT_THAT(AreEqual(2, Rows.Num()));
		ASSERT_THAT(AreEqual(3, Rows[1].Line));
		ASSERT_THAT(AreEqual(EADSKSeverity::Warning, Rows[1].Severity, FString::Printf(TEXT("severity is %s"), LexToString(Rows[1].Severity))));
		ASSERT_THAT(AreEqual(FName(TEXT("t1")), Rows[1].NodeId));
		for (const TCHAR* Bad : {TEXT(""), TEXT("1 error\n"), TEXT("x error unknown-node\n"), TEXT("1 fatal unknown-node\n"), TEXT("1 error nonsense\n"), TEXT("1 error unknown-node a b\n"),
				 TEXT("-1 error unknown-node\n"), TEXT("1 error unknown-node\n# generate: pad-to-limit\n"), TEXT("# note\n1 error unknown-node\n"), TEXT("1 error unknown-node\n\n1 error unknown-node\n")})
		{
			ASSERT_THAT(IsFalse(FADSKFixtures::ParseExpect(Bad, Rows, bPad, Error), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(ReportsEachStructuralFailure)
	{
		FADSKTempTree Tree;
		Tree.Write(TEXT("valid/ok.xml"), FADSKTestPaths::ValidAgentXml());
		Tree.Write(TEXT("valid/broken.xml"), UnknownNodeXml);
		Tree.Write(TEXT("valid/stray.expect"), TEXT("1 error unknown-node\n"));
		Tree.Write(TEXT("valid/notes.txt"), TEXT("x"));
		Tree.Write(TEXT("invalid/noexpect.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/orphan.expect"), TEXT("2 error unknown-node\n"));
		Tree.Write(TEXT("invalid/ok.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/ok.expect"), TEXT("2 error unknown-node\n"));
		Tree.Write(TEXT("invalid/syntax.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/syntax.expect"), TEXT("two error unknown-node\n"));
		Tree.Write(TEXT("invalid/wrongline.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/wrongline.expect"), TEXT("9 error unknown-node\n"));
		const FADSKFixtureReport Report = FADSKFixtures::Run(Tree.Root, PluginRegistry());
		ASSERT_THAT(IsFalse(Report.IsClean(), TEXT("the report is clean")));
		ADSK_ASSERT(FailsWith(Report, TEXT("valid/broken.xml"), TEXT("diagnostics")));
		ADSK_ASSERT(FailsWith(Report, TEXT("valid/stray.expect"), TEXT("orphan")));
		ADSK_ASSERT(FailsWith(Report, TEXT("valid/notes.txt"), TEXT("unexpected")));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/noexpect.xml"), TEXT("missing")));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/orphan.expect"), TEXT("orphan")));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/ok.xml"), TEXT("duplicate fixture basename")));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/syntax.expect"), TEXT("line 1")));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/wrongline.xml"), TEXT("expected [9 error unknown-node] got [2 error unknown-node]")));
	}

	TEST_METHOD(ExtraAndMissingRowsAndOrderAreDifferences)
	{
		FADSKTempTree Tree;
		Tree.Write(TEXT("invalid/extra.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/extra.expect"), TEXT("2 error unknown-node\n2 error unknown-node\n"));
		Tree.Write(TEXT("invalid/exact.xml"), UnknownNodeXml);
		Tree.Write(TEXT("invalid/exact.expect"), TEXT("2 error unknown-node\n"));
		const FADSKFixtureReport Report = FADSKFixtures::Run(Tree.Root, PluginRegistry());
		ASSERT_THAT(AreEqual(1, Report.Failures.Num()));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/extra.xml"), TEXT("expected")));
		ASSERT_THAT(IsTrue(Report.CoveredCodes.Contains(EADSKDiagnosticCode::UnknownNode), TEXT("unknown-node is not covered")));
	}

	TEST_METHOD(PadDirectiveGrowsTheSourceInMemoryOnly)
	{
		FADSKTempTree Tree;
		Tree.Write(TEXT("invalid/pad.xml"), FADSKTestPaths::ValidAgentXml());
		Tree.Write(TEXT("invalid/pad.expect"), TEXT("# generate: pad-to-limit\n0 error limit-exceeded\n"));
		Tree.Write(TEXT("invalid/nopad.xml"), FADSKTestPaths::ValidAgentXml());
		Tree.Write(TEXT("invalid/nopad.expect"), TEXT("0 error limit-exceeded\n"));
		const FADSKFixtureReport Report = FADSKFixtures::Run(Tree.Root, PluginRegistry());
		ASSERT_THAT(IsTrue(Report.CoveredCodes.Contains(EADSKDiagnosticCode::LimitExceeded), TEXT("limit-exceeded is not covered")));
		ASSERT_THAT(AreEqual(1, Report.Failures.Num()));
		ADSK_ASSERT(FailsWith(Report, TEXT("invalid/nopad.xml"), TEXT("got [none]")));
		ASSERT_THAT(IsTrue(IFileManager::Get().FileSize(*Tree.Path(TEXT("invalid/pad.xml"))) < 1024, FString::Printf(TEXT("pad.xml grew to %lld bytes"), IFileManager::Get().FileSize(*Tree.Path(TEXT("invalid/pad.xml"))))));
	}

	TEST_METHOD(MissingRootIsAFailure)
	{
		FADSKTempTree Tree;
		ASSERT_THAT(IsFalse(FADSKFixtures::Run(Tree.Path(TEXT("absent")), PluginRegistry()).IsClean(), TEXT("a missing root is clean")));
	}
};

#endif
