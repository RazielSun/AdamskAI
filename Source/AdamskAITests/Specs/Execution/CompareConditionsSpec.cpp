// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FCompareConditionsSpecPrivate
{
	const TCHAR* Inputs = TEXT("<Input name=\"Dist\" type=\"Float\" default=\"5\"/><Input name=\"Limit\" type=\"Float\" default=\"3\"/>")
		TEXT("<Input name=\"Count\" type=\"Int\" default=\"2\"/><Input name=\"Flag\" type=\"Bool\" default=\"false\"/>")
		TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/>");

	FString TransitionXml(const TCHAR* Handlers)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"cmp\"><Inputs>%s</Inputs><Events><Event name=\"Go\"/></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">%s</State><State id=\"near\"/><State id=\"far\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>"), Inputs, Handlers);
	}

	FString GuardXml(const TCHAR* GuardAttributes)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"cmp\"><Inputs>%s</Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Guard id=\"g\" %s><MoveTo id=\"move\" in-goal=\"Goal\" acceptanceRadius=\"1\"/></Guard></Tree></Agent>"), Inputs, GuardAttributes);
	}

	FString StateAfterGo(const TCHAR* Handlers, float Dist)
	{
		FADSKTestRuntime Runtime(TransitionXml(Handlers));
		Runtime.Instance->SetInput(TEXT("Dist"), FADSKValue::MakeFloat(Dist));
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.1f);
		return Runtime.Instance->GetActiveStatePath().Last();
	}
}

using namespace FCompareConditionsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FCompareConditionsSpec, "AdamskAI.Execution.Compare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BelowAndAboveAreStrictAndTakeLiteralsOrSlots)
	{
		const TCHAR* Handlers = TEXT("<On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"far\" if=\"Dist\" above=\"Limit\"/>");
		ASSERT_THAT(AreEqual(FString(TEXT("near")), StateAfterGo(Handlers, 1.f)));
		ASSERT_THAT(AreEqual(FString(TEXT("far")), StateAfterGo(Handlers, 5.f)));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), StateAfterGo(Handlers, 3.f)));
	}

	TEST_METHOD(UnlessNegatesTheComparison)
	{
		const TCHAR* Handlers = TEXT("<On event=\"Go\" goto=\"far\" unless=\"Dist\" below=\"3\"/>");
		ASSERT_THAT(AreEqual(FString(TEXT("far")), StateAfterGo(Handlers, 3.f)));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), StateAfterGo(Handlers, 2.9f)));
	}

	TEST_METHOD(IntSlotsCompareAsInts)
	{
		ASSERT_THAT(AreEqual(FString(TEXT("far")), StateAfterGo(TEXT("<On event=\"Go\" goto=\"far\" if=\"Count\" above=\"1\"/>"), 0.f)));
		ASSERT_THAT(AreEqual(FString(TEXT("s")), StateAfterGo(TEXT("<On event=\"Go\" goto=\"far\" if=\"Count\" below=\"2\"/>"), 0.f)));
	}

	TEST_METHOD(GuardComparesAtStartAndOnRecheck)
	{
		{
			FADSKTestRuntime Runtime(GuardXml(TEXT("in-condition=\"Dist\" below=\"Limit\"")));
			Runtime.Instance->Start();
			ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo:move")));
		}
		FADSKTestRuntime Runtime(GuardXml(TEXT("in-condition=\"Dist\" below=\"Limit\"")));
		Runtime.Instance->SetInput(TEXT("Dist"), FADSKValue::MakeFloat(1.f));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:move")));
		Runtime.Instance->SetInput(TEXT("Limit"), FADSKValue::MakeFloat(0.5f));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:move:Cancelled")));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(GuardInvertAppliesAfterTheComparison)
	{
		FADSKTestRuntime Runtime(GuardXml(TEXT("in-condition=\"Dist\" above=\"10\" invert=\"true\"")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:move")));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(CompilerRejectsWrongTypesConflictsAndMissingConditions)
	{
		const auto On = [](const TCHAR* Handler) { return Load(TransitionXml(Handler)); };
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Flag\" below=\"1\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"near\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"Count\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Count\" above=\"1.5\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" is=\"1\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" if=\"Count\" is=\"1\" below=\"3\"/>")).Count(Code::BadArity)));
		ASSERT_THAT(AreEqual(1, On(TEXT("<On event=\"Go\" goto=\"far\" below=\"3\"/>")).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, Load(GuardXml(TEXT("in-condition=\"Dist\" below=\"1\" above=\"0\""))).Count(Code::BadArity)));
		ASSERT_THAT(AreEqual(1, Load(GuardXml(TEXT("in-condition=\"Flag\" above=\"0\""))).Count(Code::TypeMismatch)));
		const FADSKCompileResult IntAbove = Load(GuardXml(TEXT("in-condition=\"Count\" above=\"0\"")));
		ASSERT_THAT(IsTrue(IntAbove.IsSuccess(), ADSKTest::FormatDiagnostics(IntAbove)));
	}

	TEST_METHOD(DuplicatesUseOperatorAndOperand)
	{
		const auto Duplicates = [](const TCHAR* Handlers) { return Load(TransitionXml(Handlers)).Count(Code::DuplicateTransition); };
		ASSERT_THAT(AreEqual(1, Duplicates(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"3\"/>"))));
		ASSERT_THAT(AreEqual(1, Duplicates(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"Limit\"/><On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"Limit\"/>"))));
		ASSERT_THAT(AreEqual(0, Duplicates(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"4\"/>"))));
		ASSERT_THAT(AreEqual(0, Duplicates(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"near\" if=\"Dist\" above=\"3\"/>"))));
		ASSERT_THAT(AreEqual(0, Duplicates(TEXT("<On event=\"Go\" goto=\"far\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"Limit\"/>"))));
	}

	TEST_METHOD(DumpNamesTheOperatorAndKeepsLegacyEquality)
	{
		const FADSKCompileResult Loaded = Load(TransitionXml(TEXT("<On event=\"Go\" goto=\"near\" if=\"Dist\" below=\"3\"/><On event=\"Go\" goto=\"far\" if=\"Dist\" above=\"Limit\"/><On event=\"Go\" if=\"Count\" is=\"2\"/>")));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Loaded.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("negated=false below=3")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("negated=false above slot=1")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("negated=false value=2")), Dump));
		const FADSKCompileResult Guarded = Load(GuardXml(TEXT("in-condition=\"Dist\" below=\"Limit\"")));
		ASSERT_THAT(IsTrue(ADSKTest::DumpNormalizedTables(*Guarded.Definition).Contains(TEXT("  compare below slot=1")), ADSKTest::DumpNormalizedTables(*Guarded.Definition)));
	}
};

#endif
