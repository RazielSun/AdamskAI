// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "Program/ADSKHoldAnalysis.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FChanceSpecPrivate
{
	FString Agent(const FString& Handlers, const FString& Trees = TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>"))
	{
		return TEXT("<Agent version=\"1\" id=\"chance\"><Inputs><Input name=\"Gate\" type=\"Bool\" default=\"true\"/></Inputs>"
			"<Locals><Local name=\"Rolled\" type=\"Int\"/></Locals><Events><Event name=\"Go\"/></Events>"
			"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">") + Handlers
			+ TEXT("</State><State id=\"a\"/><State id=\"b\"/></StateMachine>") + Trees + TEXT("</Agent>");
	}

	/** The leaf state one Go leads to, with the instance seeded. */
	FString TargetAfterGo(const FString& Xml, int32 Seed)
	{
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->SetRandomSeed(Seed);
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		return Runtime.Instance->GetActiveStatePath().Last();
	}
}

using namespace FChanceSpecPrivate;

TEST_CLASS_WITH_FLAGS(FChanceCompilerSpec, "AdamskAI.Execution.Chance.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RejectsValuesOutsideZeroToOne)
	{
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"0\"/>"))).Count(Code::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"1.5\"/>"))).Count(Code::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"-0.5\"/>"))).Count(Code::OutOfRange)));
		// Not a number, so a slot name (K3), and no slot has it.
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"half\"/>"))).Count(Code::UnresolvedReference)));
		const FADSKCompileResult One = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"1\"/>")));
		ASSERT_THAT(IsTrue(One.IsSuccess() && One.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(One)));
	}

	TEST_METHOD(AHandlerThatMayLoseItsDrawHidesNothing)
	{
		const FADSKCompileResult Fallthrough = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"0.5\"/><On event=\"Go\" goto=\"b\"/>")));
		ASSERT_THAT(IsTrue(Fallthrough.IsSuccess() && Fallthrough.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Fallthrough)));
		const FADSKCompileResult TwoDraws = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"0.5\"/><On event=\"Go\" goto=\"b\" chance=\"0.5\"/>")));
		ASSERT_THAT(IsTrue(TwoDraws.IsSuccess() && TwoDraws.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(TwoDraws)));
		ASSERT_THAT(IsTrue(TwoDraws.Definition->GetTransitions()[0].Chance == 0.5f, TEXT("chance 0.5 not linked")));

		const FADSKCompileResult Hidden = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\"/><On event=\"Go\" goto=\"b\" if=\"Gate\" chance=\"0.5\"/>")));
		ASSERT_THAT(AreEqual(1, Hidden.Count(Code::UnreachableTransition)));
		const FADSKCompileResult Behind = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\"/><On event=\"Go\" goto=\"b\" chance=\"0.5\"/>")));
		ASSERT_THAT(AreEqual(1, Behind.Count(Code::DuplicateTransition)));
		const FADSKCompileResult Duplicate = Load(Agent(TEXT("<On event=\"Go\" goto=\"a\" if=\"Gate\"/><On event=\"Go\" goto=\"b\" if=\"Gate\" chance=\"0.5\"/>")));
		ASSERT_THAT(AreEqual(1, Duplicate.Count(Code::DuplicateTransition)));
	}

	TEST_METHOD(ACompletionHandledOnlyByChanceIsAHoldRisk)
	{
		const FString Trees = TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree>");
		const FADSKCompileResult Risky = Load(Agent(TEXT("<On event=\"tree.success\" goto=\"a\" chance=\"0.5\"/>"), Trees));
		ASSERT_THAT(IsTrue(Risky.IsSuccess(), ADSKTest::FormatDiagnostics(Risky)));
		const TArray<FADSKHoldRisk> Risks = ADSK::FindHoldRisks(*Risky.Definition);
		ASSERT_THAT(AreEqual(1, Risks.Num()));
		ASSERT_THAT(IsTrue(Risks[0].bConditionalOnly, TEXT("a chance handler counts as conditional")));

		const FADSKCompileResult Covered = Load(Agent(TEXT("<On event=\"tree.success\" goto=\"a\" chance=\"0.5\"/><On event=\"tree.success\" goto=\"b\"/>"), Trees));
		ASSERT_THAT(AreEqual(0, ADSK::FindHoldRisks(*Covered.Definition).Num()));
	}
};

TEST_CLASS_WITH_FLAGS(FChanceExecutionSpec, "AdamskAI.Execution.Chance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ALostDrawFallsToTheNextHandlerAndSeedsReplay)
	{
		const FString Xml = Agent(TEXT("<On event=\"Go\" goto=\"a\" chance=\"0.25\"/><On event=\"Go\" goto=\"b\"/>"));
		int32 TakenA = 0;
		for (int32 Seed = 1; Seed <= 400; ++Seed)
		{
			const FString Target = TargetAfterGo(Xml, Seed);
			ASSERT_THAT(IsTrue(Target == TEXT("a") || Target == TEXT("b"), Target));
			ASSERT_THAT(AreEqual(Target, TargetAfterGo(Xml, Seed)));
			TakenA += Target == TEXT("a") ? 1 : 0;
		}
		ASSERT_THAT(IsTrue(TakenA > 60 && TakenA < 140, FString::Printf(TEXT("a taken %d of 400 times at chance 0.25"), TakenA)));
	}

	TEST_METHOD(AllLostDrawsContinueInTheParent)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"chance\"><Events><Event name=\"Go\"/></Events>"
			"<StateMachine id=\"m\" initial=\"p\"><State id=\"p\" initial=\"s\"><On event=\"Go\" goto=\"b\"/>"
			"<State id=\"s\" tree=\"t\"><On event=\"Go\" goto=\"a\" chance=\"0.000001\"/></State><State id=\"a\"/></State>"
			"<State id=\"b\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
		ASSERT_THAT(AreEqual(FString(TEXT("b")), TargetAfterGo(Xml, 3)));
	}

	TEST_METHOD(DrawsOnlyWhenTheConditionPasses)
	{
		// The tree rolls after Go is handled: a chance handler whose condition failed must not have shifted the stream.
		const FString Trees = TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>"
			"<Tree id=\"r\"><Sequence id=\"seq\"><Roll id=\"roll\" count=\"1000000\" out-value=\"Rolled\"/><Wait id=\"rw\" duration=\"100\"/></Sequence></Tree>");
		auto RolledAfterGo = [&](const TCHAR* Handlers)
		{
			const FString Xml = TEXT("<Agent version=\"1\" id=\"chance\"><Inputs><Input name=\"Gate\" type=\"Bool\" default=\"false\"/></Inputs>"
				"<Locals><Local name=\"Rolled\" type=\"Int\"/></Locals><Events><Event name=\"Go\"/></Events>"
				"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">") + FString(Handlers)
				+ TEXT("<On event=\"Go\" goto=\"roller\"/></State><State id=\"a\"/><State id=\"roller\" tree=\"r\"/></StateMachine>") + Trees + TEXT("</Agent>");
			FADSKTestRuntime Runtime(Xml);
			Runtime.Instance->SetRandomSeed(11);
			Runtime.Instance->Start();
			Runtime.Instance->EnqueueEvent(TEXT("Go"));
			Runtime.Instance->Tick(0.f);
			FADSKValue Rolled;
			Runtime.Instance->GetValue(TEXT("Rolled"), Rolled);
			return Rolled.Int;
		};
		const int32 Plain = RolledAfterGo(TEXT(""));
		ASSERT_THAT(AreEqual(Plain, RolledAfterGo(TEXT("<On event=\"Go\" goto=\"a\" if=\"Gate\" chance=\"0.5\"/>"))));
	}
};

#endif
