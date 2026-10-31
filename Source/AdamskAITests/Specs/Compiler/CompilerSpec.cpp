// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

TEST_CLASS_WITH_FLAGS(FCompilerBaseSpec, "AdamskAI.Compiler.Base",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BaseDocumentAndGuardFixtureAreClean)
	{
		const FADSKCompileResult Base = Load(FString::Join(BaseLines(), TEXT("\n")));
		ASSERT_THAT(IsTrue(Base.Diagnostics.IsEmpty() && Base.IsSuccess(), ADSKTest::FormatDiagnostics(Base)));
		const FADSKCompileResult Guard = Load(FADSKGuardFixture::GuardXml(*TestRunner));
		ASSERT_THAT(IsTrue(Guard.Diagnostics.IsEmpty() && Guard.IsSuccess() && !Guard.HasError(), ADSKTest::FormatDiagnostics(Guard)));
		const FADSKCompileResult Minimal = Load(FADSKGuardFixture::MinimalAgent());
		ASSERT_THAT(IsTrue(Minimal.IsSuccess(), ADSKTest::FormatDiagnostics(Minimal)));
	}

	TEST_METHOD(UnusedTreeWarnsButStillProducesADefinition)
	{
		const FADSKCompileResult Result = Load(FADSKGuardFixture::GuardXmlWithUnusedTree(*TestRunner));
		ASSERT_THAT(AreEqual(1, Result.Diagnostics.Num()));
		ADSK_ASSERT(Has(Result, Code::UnreferencedTree, EADSKSeverity::Warning, FADSKGuardFixture::LineOf(FADSKGuardFixture::GuardXmlWithUnusedTree(*TestRunner), "<Tree id=\"unused\">")));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && !Result.HasError(), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(FixtureTreesAreInsertedBeforeTheClosingTag)
	{
		const FString Xml = FADSKGuardFixture::GuardXml(*TestRunner);
		ASSERT_THAT(IsTrue(Xml.TrimEnd().EndsWith(TEXT("</Agent>")), TEXT("the fixture does not end with </Agent>")));
		ASSERT_THAT(IsTrue(FADSKGuardFixture::LineOf(Xml, "<Tree id=\"watch\">") < FADSKGuardFixture::LineOf(Xml, "</Agent>"),
			FString::Printf(TEXT("the watch tree is at line %d, </Agent> at %d"), FADSKGuardFixture::LineOf(Xml, "<Tree id=\"watch\">"), FADSKGuardFixture::LineOf(Xml, "</Agent>"))));
	}

	TEST_METHOD(AcceptedVariationsStayClean)
	{
		ADSK_ASSERT(Accepts(9, "<Event name=\"Ping\" priority=\"-3\"><Param name=\"P\" type=\"Vector\"/></Event>"));
		ADSK_ASSERT(Accepts(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\" goto=\"s\"><Bind param=\"P\" to=\"L\"/></On><On event=\"tree.success\"/></State>"));
		ADSK_ASSERT(Accepts(15, "<Sequence id=\"w\"><Wait id=\"w2\" duration=\"0\"/></Sequence>"));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerAgentSpec, "AdamskAI.Compiler.Agent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RootAttributesAndSections)
	{
		ADSK_ASSERT(Rejects(1, "<Agent version=\"1\">", Code::MissingRequired, 1));
		ADSK_ASSERT(Rejects(1, "<Agent version=\"1\" id=\"a\" x=\"y\">", Code::UnknownAttribute, 1));
		ADSK_ASSERT(Rejects(7, "</Locals><Bogus/>", Code::UnknownElement, 7));
		ADSK_ASSERT(Rejects(13, "</StateMachine><Locals/>", Code::BadArity, 13));
		ADSK_ASSERT(Rejects(7, "</Locals><Locals/>", Code::BadArity, 7));
	}

	TEST_METHOD(ExactlyOneStateMachine)
	{
		ADSK_ASSERT(Rejects(13, "</StateMachine><StateMachine id=\"m2\" initial=\"x\"><State id=\"x\"/></StateMachine>", Code::BadArity, 13));
		const FADSKCompileResult None = Load(FADSKGuardFixture::AgentWith(FString(), FString(), FString(), FString(), FString()));
		ADSK_ASSERT(Has(None, Code::MissingRequired, EADSKSeverity::Error, 1));
		ASSERT_THAT(IsFalse(None.IsSuccess(), ADSKTest::FormatDiagnostics(None)));
	}

	TEST_METHOD(InputRules)
	{
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\" required=\"true\" default=\"1,2,3\"/>", Code::BadArity, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\"/>", Code::MissingRequired, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\" default=\"1,2\"/>", Code::TypeMismatch, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Quat\" required=\"true\"/>", Code::TypeMismatch, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\" required=\"yes\"/>", Code::TypeMismatch, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\" required=\"true\" extra=\"1\"/>", Code::UnknownAttribute, 3));
		ADSK_ASSERT(Rejects(3, "<Input type=\"Vector\" required=\"true\"/>", Code::MissingRequired, 3));
		ADSK_ASSERT(Rejects(3, "<Input name=\"In\" type=\"Vector\" required=\"true\"><X/></Input>", Code::UnknownElement, 3));
	}

	TEST_METHOD(LocalRules)
	{
		ADSK_ASSERT(Rejects(6, "<Local name=\"In\" type=\"Vector\"/>", Code::DuplicateId, 6));
		ADSK_ASSERT(Rejects(6, "<Local name=\"L\" type=\"Vector\" default=\"1,2,3\"/>", Code::UnknownAttribute, 6));
		ADSK_ASSERT(Rejects(6, "<Other/>", Code::UnknownElement, 6));
		ADSK_ASSERT(Rejects(6, "<Local name=\"L\" type=\"Vector\"/><Local name=\"L\" type=\"Bool\"/>", Code::DuplicateId, 6));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerEventSpec, "AdamskAI.Compiler.Events",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EventNamesAndParams)
	{
		ADSK_ASSERT(Rejects(9, "<Event name=\"tree.success\"/>", Code::DuplicateId, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"tree.failure\"/>", Code::DuplicateId, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\"/><Event name=\"Ping\"/>", Code::DuplicateId, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\"><Param name=\"P\" type=\"Vector\"/><Param name=\"P\" type=\"Bool\"/></Event>", Code::DuplicateId, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\"><Junk/></Event>", Code::UnknownElement, 9));
		ADSK_ASSERT(Rejects(9, "<Event priority=\"1\"/>", Code::MissingRequired, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" colour=\"red\"/>", Code::UnknownAttribute, 9));
	}

	TEST_METHOD(PriorityIsAStrictInt32)
	{
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" priority=\"1.5\"/>", Code::TypeMismatch, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" priority=\"+1\"/>", Code::TypeMismatch, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" priority=\"\"/>", Code::TypeMismatch, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" priority=\"99999999999\"/>", Code::OutOfRange, 9));
		ADSK_ASSERT(Rejects(9, "<Event name=\"Ping\" priority=\"-2147483649\"/>", Code::OutOfRange, 9));
		ADSK_ASSERT(Accepts(9, "<Event name=\"Ping\" priority=\"2147483647\"/>"));
		ADSK_ASSERT(Accepts(9, "<Event name=\"Ping\" priority=\"-2147483648\"/>"));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerStateSpec, "AdamskAI.Compiler.States",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StateStructure)
	{
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"/><State id=\"s\"/>", Code::DuplicateId, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\"><State id=\"c\" tree=\"t\"/></State>", Code::MissingRequired, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" initial=\"zz\"><State id=\"c\" tree=\"t\"/></State>", Code::UnresolvedReference, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" initial=\"c\" tree=\"t\"><State id=\"c\"/></State>", Code::UnknownAttribute, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" initial=\"x\" tree=\"t\"/>", Code::UnknownAttribute, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><Junk/></State>", Code::UnknownElement, 12));
		ADSK_ASSERT(Rejects(12, "<State tree=\"t\"/>", Code::MissingRequired, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"nope\"/>", Code::UnresolvedReference, 12));
	}

	TEST_METHOD(MachineNeedsAStateAndAValidInitial)
	{
		ADSK_ASSERT(Rejects(12, "", Code::BadArity, 11));
		ADSK_ASSERT(Rejects(11, "<StateMachine id=\"m\">", Code::MissingRequired, 11));
		ADSK_ASSERT(Rejects(11, "<StateMachine id=\"m\" initial=\"gone\">", Code::UnresolvedReference, 11));
		ADSK_ASSERT(Rejects(11, "<StateMachine id=\"m\" initial=\"s\" tree=\"t\">", Code::UnknownAttribute, 11));
	}

	TEST_METHOD(StateDepthNineIsRejected)
	{
		FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s2\">\n");
		for (int32 Depth = 2; Depth <= 9; ++Depth)
		{
			Machine += Depth < 9 ? FString::Printf(TEXT("<State id=\"s%d\" initial=\"s%d\">\n"), Depth, Depth + 1)
							 : FString::Printf(TEXT("<State id=\"s%d\"/>\n"), Depth);
		}
		for (int32 Depth = 2; Depth <= 8; ++Depth)
		{
			Machine += TEXT("</State>\n");
		}
		Machine += TEXT("</StateMachine>\n");
		const FString Xml = FADSKGuardFixture::AgentWith(FString(), FString(), FString(), Machine, FString());
		const FADSKCompileResult Result = Load(Xml);
		ADSK_ASSERT(Has(Result, Code::LimitExceeded, EADSKSeverity::Error, FADSKGuardFixture::LineOf(Xml, "id=\"s9\"")));
		ASSERT_THAT(IsFalse(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(SixtyFiveStatesAreRejectedOnceAtTheSixtyFifth)
	{
		FString Machine = TEXT("<StateMachine id=\"m\" initial=\"c1\">\n");
		for (int32 Index = 1; Index <= 64; ++Index)
		{
			Machine += FString::Printf(TEXT("<State id=\"c%d\"/>\n"), Index);
		}
		Machine += TEXT("</StateMachine>\n");
		const FString Xml = FADSKGuardFixture::AgentWith(FString(), FString(), FString(), Machine, FString());
		const FADSKCompileResult Result = Load(Xml);
		ASSERT_THAT(AreEqual(1, Result.Count(Code::LimitExceeded)));
		ADSK_ASSERT(Has(Result, Code::LimitExceeded, EADSKSeverity::Error, FADSKGuardFixture::LineOf(Xml, "id=\"c64\"")));
		const FADSKCompileResult SixtyFour = Load(FADSKGuardFixture::AgentWith(FString(), FString(), FString(), Machine.Replace(TEXT("<State id=\"c64\"/>\n"), TEXT("")), FString()));
		ASSERT_THAT(IsTrue(SixtyFour.IsSuccess(), ADSKTest::FormatDiagnostics(SixtyFour)));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerTransitionSpec, "AdamskAI.Compiler.Transitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(OnRules)
	{
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Nope\"/></State>", Code::UnresolvedReference, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"/><On event=\"Ping\"/></State>", Code::DuplicateTransition, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\" goto=\"zz\"/></State>", Code::UnresolvedReference, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\" x=\"1\"/></State>", Code::UnknownAttribute, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On goto=\"s\"/></State>", Code::MissingRequired, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Junk/></On></State>", Code::UnknownElement, 12));
	}

	TEST_METHOD(BindRules)
	{
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Bind param=\"Q\" to=\"L\"/></On></State>", Code::UnresolvedReference, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Bind param=\"P\" to=\"In\"/></On></State>", Code::UnresolvedReference, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Bind param=\"P\" to=\"LB\"/></On></State>", Code::TypeMismatch, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Bind param=\"P\" to=\"L\" x=\"1\"/></On></State>", Code::UnknownAttribute, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"Ping\"><Bind param=\"P\"/></On></State>", Code::MissingRequired, 12));
		ADSK_ASSERT(Rejects(12, "<State id=\"s\" tree=\"t\"><On event=\"tree.success\"><Bind param=\"P\" to=\"L\"/></On></State>", Code::UnresolvedReference, 12));
	}
};

TEST_CLASS_WITH_FLAGS(FCompilerTreeSpec, "AdamskAI.Compiler.Trees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TreeRules)
	{
		ADSK_ASSERT(Rejects(16, "</Tree><Tree id=\"t\"><Wait id=\"w2\" duration=\"1\"/></Tree>", Code::DuplicateId, 16));
		ADSK_ASSERT(Rejects(15, "<Wait id=\"w\" duration=\"1\"/><Wait id=\"w2\" duration=\"1\"/>", Code::BadArity, 14));
		ADSK_ASSERT(Rejects(15, "", Code::BadArity, 14));
		ADSK_ASSERT(Rejects(14, "<Tree id=\"t\" x=\"1\">", Code::UnknownAttribute, 14));
		ADSK_ASSERT(Rejects(14, "<Tree>", Code::MissingRequired, 14));
	}

	TEST_METHOD(UnreferencedTreeIsAWarningNotAnError)
	{
		const FADSKCompileResult Result = Load(Mutate(12, "<State id=\"s\"/>"));
		ASSERT_THAT(AreEqual(1, Result.Diagnostics.Num()));
		ADSK_ASSERT(Has(Result, Code::UnreferencedTree, EADSKSeverity::Warning, 14));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && !Result.HasError(), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(TreeDepthSeventeenIsRejected)
	{
		const TCHAR* Machine = TEXT("<StateMachine id=\"m\" initial=\"s\">\n<State id=\"s\" tree=\"t\"/>\n</StateMachine>\n");
		auto Build = [Machine](int32 Sequences)
		{
			FString Tree = TEXT("<Tree id=\"t\">\n");
			for (int32 Depth = 1; Depth <= Sequences; ++Depth)
			{
				Tree += FString::Printf(TEXT("<Sequence id=\"q%d\">\n"), Depth);
			}
			Tree += TEXT("<Wait id=\"deep\" duration=\"1\"/>\n");
			for (int32 Depth = 1; Depth <= Sequences; ++Depth)
			{
				Tree += TEXT("</Sequence>\n");
			}
			return FADSKGuardFixture::AgentWith(FString(), FString(), FString(), Machine, Tree + TEXT("</Tree>\n"));
		};
		const FString TooDeep = Build(16);
		ADSK_ASSERT(Has(Load(TooDeep), Code::LimitExceeded, EADSKSeverity::Error, FADSKGuardFixture::LineOf(TooDeep, "id=\"deep\"")));
		const FADSKCompileResult Fifteen = Load(Build(15));
		ASSERT_THAT(IsTrue(Fifteen.IsSuccess(), ADSKTest::FormatDiagnostics(Fifteen)));
	}

	TEST_METHOD(SourceErrorsSkipCompilerReferenceDiagnostics)
	{
		// The duplicate tree id (source walk) suppresses the unresolved initial (compiler pass, line 11).
		const FString Xml = Mutate(11, "<StateMachine id=\"m\" initial=\"gone\">").Replace(TEXT("</Tree>\n</Agent>"), TEXT("</Tree><Tree id=\"t\"><Wait id=\"w2\" duration=\"1\"/></Tree>\n</Agent>"));
		const FADSKCompileResult Result = Load(Xml);
		const int32 Duplicate = Result.Diagnostics.IndexOfByPredicate([](const FADSKDiagnostic& D) { return D.Code == Code::DuplicateId; });
		const int32 Unresolved = Result.Diagnostics.IndexOfByPredicate([](const FADSKDiagnostic& D) { return D.Code == Code::UnresolvedReference; });
		ASSERT_THAT(IsTrue(Duplicate != INDEX_NONE && Unresolved == INDEX_NONE, ADSKTest::FormatDiagnostics(Result)));
	}
};

#endif
