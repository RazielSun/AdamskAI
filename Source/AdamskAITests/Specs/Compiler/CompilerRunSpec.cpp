// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FCompilerRunSpecPrivate
{
	/** Line 5 is the first tree line. */
	FString Doc(const FString& Trees)
	{
		return TEXT("<Agent version=\"1\" id=\"run\">\n<Locals><Local name=\"L\" type=\"Vector\"/></Locals>\n")
			TEXT("<StateMachine id=\"m\" initial=\"s\">\n<State id=\"s\" tree=\"main\"/>\n</StateMachine>\n") + Trees + TEXT("\n</Agent>");
	}

	FString Wait(const FString& Id) { return FString::Printf(TEXT("<Wait id=\"%s\" duration=\"0\"/>"), *Id); }
	FString Run(const FString& Id, const FString& Tree) { return FString::Printf(TEXT("<Run id=\"%s\" tree=\"%s\"/>"), *Id, *Tree); }
	FString Tree(const FString& Id, const FString& Body) { return FString::Printf(TEXT("<Tree id=\"%s\">%s</Tree>"), *Id, *Body); }
	FString Seq(const FString& Id, const FString& Body) { return FString::Printf(TEXT("<Sequence id=\"%s\">%s</Sequence>"), *Id, *Body); }

	bool HasCode(const FADSKCompileResult& Result, Code Expected, EADSKSeverity Severity = EADSKSeverity::Error)
	{
		return Result.Diagnostics.ContainsByPredicate([&](const FADSKDiagnostic& D) { return D.Code == Expected && D.Severity == Severity; });
	}

	int32 Count(const FADSKCompileResult& Result, Code Expected)
	{
		int32 Total = 0;
		for (const FADSKDiagnostic& D : Result.Diagnostics) { Total += D.Code == Expected ? 1 : 0; }
		return Total;
	}

	bool HasNode(const FADSKCompileResult& Result, const TCHAR* Id)
	{
		return Result.Definition->GetNodes().ContainsByPredicate([Id](const FADSKNodeDefinition& N) { return N.Id == FName(Id); });
	}

	bool HasNoPlaceholder(const FADSKCompileResult& Result)
	{
		return !Result.Definition->GetNodes().ContainsByPredicate([](const FADSKNodeDefinition& N) { return N.Descriptor == nullptr; });
	}

	FString NodeIds(const FADSKCompileResult& Result)
	{
		return TEXT("nodes: ") + FString::JoinBy(Result.Definition->GetNodes(), TEXT(", "), [](const FADSKNodeDefinition& N) { return N.Id.ToString(); });
	}

	FString LongId(TCHAR Fill, int32 Length, int32 Serial)
	{
		return FString::Printf(TEXT("%04d"), Serial) + FString::ChrN(Length - 4, Fill);
	}
}

using namespace FCompilerRunSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCompilerRunSpec, "AdamskAI.Compiler.Run",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(MissingTargetIsUnresolved)
	{
		const FADSKCompileResult Result = Load(Doc(TEXT("<Tree id=\"main\">\n") + Run(TEXT("r"), TEXT("nope")) + TEXT("\n</Tree>")));
		ADSK_ASSERT(Has(Result, Code::UnresolvedReference, EADSKSeverity::Error, 7, "r"));
		ASSERT_THAT(IsFalse(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(RunRequiresIdAndTreeAndRejectsChildren)
	{
		ADSK_ASSERT(Has(Load(Doc(Tree(TEXT("main"), TEXT("<Run tree=\"main\"/>")))), Code::MissingRequired, EADSKSeverity::Error, 6));
		ADSK_ASSERT(Has(Load(Doc(Tree(TEXT("main"), TEXT("<Run id=\"r\"/>")))), Code::MissingRequired, EADSKSeverity::Error, 6));
		const FADSKCompileResult WithChild = Load(Doc(Tree(TEXT("main"), TEXT("<Run id=\"r\" tree=\"main\">") + Wait(TEXT("w")) + TEXT("</Run>"))));
		ASSERT_THAT(IsTrue(HasCode(WithChild, Code::BadArity), ADSKTest::FormatDiagnostics(WithChild)));
		const FADSKCompileResult WithPort = Load(Doc(Tree(TEXT("main"), TEXT("<Run id=\"r\" tree=\"main\" in-goal=\"L\"/>"))));
		ASSERT_THAT(IsTrue(HasCode(WithPort, Code::UnknownAttribute), ADSKTest::FormatDiagnostics(WithPort)));
	}

	TEST_METHOD(RootRunIsReplacedByTheTargetBody)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Run(TEXT("c"), TEXT("body"))) + Tree(TEXT("body"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(HasNoPlaceholder(Result) && HasNode(Result, TEXT("c/w")), NodeIds(Result)));
		const int32 Root = Result.Definition->GetStates()[Result.Definition->GetStates().IndexOfByPredicate(
			[](const FADSKStateDefinition& S) { return S.Id == TEXT("s"); })].RootNode;
		ASSERT_THAT(AreEqual(FName(TEXT("c/w")), Result.Definition->GetNodes()[Root].Id));
		ASSERT_THAT(AreEqual(INDEX_NONE, Result.Definition->GetNodes()[Root].Parent));
	}

	TEST_METHOD(NestedRunsAccumulatePrefixes)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("top"), Run(TEXT("a"), TEXT("mid"))))
			+ Tree(TEXT("mid"), Seq(TEXT("ms"), Run(TEXT("b"), TEXT("leaf")))) + Tree(TEXT("leaf"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(HasNode(Result, TEXT("a/ms")) && HasNode(Result, TEXT("a/b/w")) && HasNoPlaceholder(Result), NodeIds(Result)));
		const int32 Leaf = Result.Definition->GetNodes().IndexOfByPredicate([](const FADSKNodeDefinition& N) { return N.Id == FName(TEXT("a/b/w")); });
		const int32 Mid = Result.Definition->GetNodes().IndexOfByPredicate([](const FADSKNodeDefinition& N) { return N.Id == FName(TEXT("a/ms")); });
		ASSERT_THAT(AreEqual(Mid, Result.Definition->GetNodes()[Leaf].Parent));
	}

	TEST_METHOD(CallSitesAreIndependent)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("top"), Run(TEXT("x"), TEXT("t")) + Run(TEXT("y"), TEXT("t"))))
			+ Tree(TEXT("t"), Seq(TEXT("ts"), Run(TEXT("n"), TEXT("u")))) + Tree(TEXT("u"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(HasNode(Result, TEXT("x/ts")) && HasNode(Result, TEXT("x/n/w")) && HasNode(Result, TEXT("y/ts")) && HasNode(Result, TEXT("y/n/w")), NodeIds(Result)));
		ASSERT_THAT(IsTrue(HasNode(Result, TEXT("ts")) && HasNode(Result, TEXT("n/w")) && HasNoPlaceholder(Result), NodeIds(Result)));
	}

	TEST_METHOD(RunOnlyTreeIsReferenced)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Run(TEXT("c"), TEXT("body"))) + Tree(TEXT("body"), Wait(TEXT("w")))));
		ASSERT_THAT(IsFalse(HasCode(Result, Code::UnreferencedTree, EADSKSeverity::Warning), ADSKTest::FormatDiagnostics(Result)));
		const FADSKCompileResult Unused = Load(Doc(Tree(TEXT("main"), Wait(TEXT("m"))) + Tree(TEXT("body"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(HasCode(Unused, Code::UnreferencedTree, EADSKSeverity::Warning), ADSKTest::FormatDiagnostics(Unused)));
	}

	TEST_METHOD(CycleReportsAReadablePath)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Run(TEXT("ra"), TEXT("a"))) + Tree(TEXT("a"), Run(TEXT("rb"), TEXT("b")))
			+ Tree(TEXT("b"), Run(TEXT("rc"), TEXT("a")))));
		ASSERT_THAT(IsTrue(!Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D)
			{ return D.Code == Code::UnresolvedReference && D.Message.Contains(TEXT("a -> b -> a")); }), ADSKTest::FormatDiagnostics(Result)));
		const FADSKCompileResult SelfCall = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s"), Run(TEXT("r"), TEXT("main"))))));
		ASSERT_THAT(IsTrue(SelfCall.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D)
			{ return D.Code == Code::UnresolvedReference && D.Message.Contains(TEXT("main -> main")); }), ADSKTest::FormatDiagnostics(SelfCall)));
	}

	TEST_METHOD(DepthIsMeasuredAtTheCallSite)
	{
		FString Body = Wait(TEXT("deep"));
		for (int32 Level = 15; Level >= 1; --Level)
		{
			Body = Seq(FString::Printf(TEXT("q%d"), Level), Body);
		}
		const FADSKCompileResult Shallow = Load(Doc(Tree(TEXT("main"), Run(TEXT("c"), TEXT("t"))) + Tree(TEXT("t"), Body)));
		ASSERT_THAT(IsTrue(Shallow.IsSuccess(), ADSKTest::FormatDiagnostics(Shallow)));
		const FADSKCompileResult Deep = Load(Doc(Tree(TEXT("main"), Seq(TEXT("top"), Run(TEXT("c"), TEXT("t")))) + Tree(TEXT("t"), Body)));
		ASSERT_THAT(IsTrue(HasCode(Deep, Code::LimitExceeded) && Count(Deep, Code::LimitExceeded) == 1, ADSKTest::FormatDiagnostics(Deep)));
	}

	TEST_METHOD(ExpandedNodeCountIsBounded)
	{
		FString Top, Mid, Leaf;
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Top += Run(FString::Printf(TEXT("a%d"), Index), TEXT("l2"));
			Mid += Run(FString::Printf(TEXT("b%d"), Index), TEXT("l3"));
			Leaf += Wait(FString::Printf(TEXT("w%d"), Index));
		}
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s1"), Top)) + Tree(TEXT("l2"), Seq(TEXT("s2"), Mid)) + Tree(TEXT("l3"), Seq(TEXT("s3"), Leaf))));
		ASSERT_THAT(IsTrue(HasCode(Result, Code::LimitExceeded) && !Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(Count(Result, Code::LimitExceeded) <= 40, FString::Printf(TEXT("%d limit diagnostics"), Count(Result, Code::LimitExceeded))));
	}

	TEST_METHOD(GeneratedIdCharactersAreBounded)
	{
		FString Template, Calls;
		for (int32 Index = 0; Index < 33; ++Index)
		{
			Template += Wait(LongId(TEXT('x'), 1000, Index));
		}
		for (int32 Index = 0; Index < 32; ++Index)
		{
			Calls += Run(FString::Printf(TEXT("r%02d"), Index), TEXT("t"));
		}
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s"), Calls)) + Tree(TEXT("t"), Seq(TEXT("ts"), Template))));
		ASSERT_THAT(IsTrue(HasCode(Result, Code::LimitExceeded) && !Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Message.Contains(TEXT("1 Mi")); }), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(EachGeneratedIdFitsAnFName)
	{
		const FADSKCompileResult Fits = Load(Doc(Tree(TEXT("main"), Run(TEXT("ab"), TEXT("t"))) + Tree(TEXT("t"), Wait(LongId(TEXT('x'), 1020, 1)))));
		ASSERT_THAT(IsTrue(Fits.IsSuccess(), ADSKTest::FormatDiagnostics(Fits)));
		const FADSKCompileResult Over = Load(Doc(Tree(TEXT("main"), Run(TEXT("abc"), TEXT("t"))) + Tree(TEXT("t"), Wait(LongId(TEXT('x'), 1020, 1)))));
		ASSERT_THAT(IsTrue(HasCode(Over, Code::LimitExceeded) && !Over.IsSuccess(), ADSKTest::FormatDiagnostics(Over)));
	}

	TEST_METHOD(GeneratedCollisionIsDuplicateId)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s"), Run(TEXT("r"), TEXT("t")) + Wait(TEXT("r/w")))) + Tree(TEXT("t"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(HasCode(Result, Code::DuplicateId) && !Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKCompileResult CaseDiffers = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s"), Run(TEXT("r"), TEXT("t")) + Wait(TEXT("R/w")))) + Tree(TEXT("t"), Wait(TEXT("w")))));
		ASSERT_THAT(IsTrue(HasCode(CaseDiffers, Code::DuplicateId) && !CaseDiffers.IsSuccess(), ADSKTest::FormatDiagnostics(CaseDiffers)));
	}

	TEST_METHOD(SourceErrorsSkipExpansion)
	{
		const FADSKCompileResult Result = Load(Doc(Tree(TEXT("main"), Seq(TEXT("s"), Run(TEXT("r"), TEXT("nope")) + TEXT("<Bogus id=\"b\"/>")))));
		ASSERT_THAT(IsTrue(HasCode(Result, Code::UnknownNode) && !HasCode(Result, Code::UnresolvedReference), ADSKTest::FormatDiagnostics(Result)));
	}
};

#endif
