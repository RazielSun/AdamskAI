// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "Program/ADSKLint.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FStateSelectSpecPrivate
{
	const FString WaitTree = TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>");

	/** Near and Far score a distance in opposite directions, equal at 450, as a melee enemy's plan might. */
	FString Agent(const FString& Engage, const FString& Select = TEXT("best"))
	{
		return TEXT("<Agent version=\"1\" id=\"select\"><Inputs><Input name=\"Distance\" type=\"Float\" default=\"100\"/>"
			"<Input name=\"Flag\" type=\"Bool\" default=\"true\"/></Inputs>"
			"<Locals><Local name=\"Bound\" type=\"Float\"/></Locals>"
			"<Events><Event name=\"Replan\"/><Event name=\"Jump\"/><Event name=\"Set\"><Param name=\"V\" type=\"Float\"/></Event></Events>"
			"<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"t\"><On event=\"Replan\" goto=\"engage\"/>"
			"<On event=\"Set\" goto=\"engage\"><Bind param=\"V\" to=\"Bound\"/></On></State>"
			"<State id=\"engage\" initial=\"fallback\" select=\"") + Select + TEXT("\">")
			+ Engage + TEXT("</State></StateMachine>") + WaitTree + TEXT("</Agent>");
	}

	/** Replan from a child selects again without leaving engage; from engage's own handler it would leave and re-enter. */
	const FString NearFar = TEXT("<State id=\"near\" tree=\"t\" score=\"Distance\" weight=\"-0.004\" bias=\"3\">"
		"<On event=\"Jump\" goto=\"far\"/><On event=\"Replan\" goto=\"engage\"/></State>"
		"<State id=\"far\" tree=\"t\" score=\"Distance\" weight=\"0.004\" bias=\"-0.6\"><On event=\"Replan\" goto=\"engage\"/></State>"
		"<State id=\"fallback\" tree=\"t\"/>");

	TArray<FString> Selected(const FADSKTestRuntime& Runtime)
	{
		TArray<FString> Result;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::StateSelected)
			{
				Result.Add(ADSK::FormatRecord(*Runtime.Definition, Record));
			}
		}
		return Result;
	}

	FString LeafAfterReplan(const FString& Xml, int32 Seed, TFunctionRef<void(FADSKInstance&)> Setup = [](FADSKInstance&) {})
	{
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->SetRandomSeed(Seed);
		Setup(*Runtime.Instance);
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Replan"));
		Runtime.Instance->Tick(0.f);
		return Runtime.Instance->GetActiveStatePath().Last();
	}
}

using namespace FStateSelectSpecPrivate;

TEST_CLASS_WITH_FLAGS(FStateSelectCompilerSpec, "AdamskAI.Execution.StateSelect.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AcceptsScoredChildrenAndLintsThemReachable)
	{
		const FADSKCompileResult Result = Load(Agent(NearFar));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const TConstArrayView<FADSKStateDefinition> States = Result.Definition->GetStates();
		const int32 Engage = Result.Definition->FindState(TEXT("engage"));
		const int32 Far = Result.Definition->FindState(TEXT("far"));
		ASSERT_THAT(IsTrue(States[Engage].Select == EADSKStateSelect::Best, TEXT("engage does not select best")));
		ASSERT_THAT(IsTrue(States[Far].bScored && States[Far].ScoreSlot == Result.Definition->FindSlot(TEXT("Distance"))
			&& States[Far].ScoreWeight == 0.004f && States[Far].ScoreBias == -0.6f, TEXT("far's score is not linked")));
		ASSERT_THAT(IsFalse(States[Result.Definition->FindState(TEXT("fallback"))].bScored, TEXT("fallback scored")));
		for (const FADSKLintNote& Note : ADSK::FindLintNotes(*Result.Definition))
		{
			ASSERT_THAT(IsFalse(FString(Note.Code) == TEXT("unreachable-state") || Note.Message.Contains(TEXT("'Distance'")), Note.Message));
		}
	}

	TEST_METHOD(RejectsMisplacedOrMalformedSelection)
	{
		ASSERT_THAT(AreEqual(1, Load(Agent(NearFar, TEXT("maybe"))).Count(Code::UnknownEnumValue)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<State id=\"fallback\" tree=\"t\" select=\"best\"/><State id=\"x\" weight=\"1\"/>"))).Count(Code::UnknownAttribute)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<State id=\"fallback\"/><State id=\"x\" score=\"Missing\"/>"))).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<State id=\"fallback\"/><State id=\"x\" score=\"Flag\"/>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<State id=\"fallback\"/><State id=\"x\" weight=\"heavy\"/>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<State id=\"fallback\"/><State id=\"x\"/>"))).Count(Code::MissingRequired)));

		const FString Unselected = TEXT("<Agent version=\"1\" id=\"select\"><StateMachine id=\"m\" initial=\"p\">"
			"<State id=\"p\" initial=\"a\"><State id=\"a\"/><State id=\"b\" weight=\"2\"/></State></StateMachine></Agent>");
		ASSERT_THAT(AreEqual(1, Load(Unselected).Count(Code::UnknownAttribute)));
	}
};

TEST_CLASS_WITH_FLAGS(FStateSelectExecutionSpec, "AdamskAI.Execution.StateSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(BestPicksTheHighestScoreAndReselectsOnReentry)
	{
		FADSKTestRuntime Runtime(Agent(NearFar));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Replan"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("engage"), TEXT("near")}));

		// Re-entering the parent from inside it selects again on the new facts, without leaving engage.
		Runtime.Instance->SetInput(TEXT("Distance"), FADSKValue::MakeFloat(800.f));
		Runtime.Instance->EnqueueEvent(TEXT("Replan"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("engage"), TEXT("far")}));
		ASSERT_THAT(AreEqual(1, Runtime.Recorder->Snapshot().FilterByPredicate([&Runtime](const FADSKRecord& Record)
		{
			return Record.Kind == EADSKRecordKind::StateEntered && Record.State == Runtime.Definition->FindState(TEXT("engage"));
		}).Num()));
		const TArray<FString> Lines = Selected(Runtime);
		ASSERT_THAT(AreEqual(2, Lines.Num()));
		ASSERT_THAT(IsTrue(Lines[0].Contains(TEXT("state-selected state=near value=2.6")), Lines[0]));
		ASSERT_THAT(IsTrue(Lines[1].Contains(TEXT("state-selected state=far value=2.6")), Lines[1]));
	}

	TEST_METHOD(TiesGoToDocumentOrderAndNoPositiveScoreEntersInitial)
	{
		const FString Tied = TEXT("<State id=\"near\" tree=\"t\" weight=\"2\"/><State id=\"far\" tree=\"t\" weight=\"2\"/><State id=\"fallback\" tree=\"t\"/>");
		ASSERT_THAT(AreEqual(FString(TEXT("near")), LeafAfterReplan(Agent(Tied), 1)));

		const FString Negative = TEXT("<State id=\"near\" tree=\"t\" score=\"Distance\" weight=\"-1\"/><State id=\"fallback\" tree=\"t\"/>");
		FADSKTestRuntime Runtime(Agent(Negative));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Replan"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("engage"), TEXT("fallback")}));
		const TArray<FString> Lines = Selected(Runtime);
		ASSERT_THAT(AreEqual(1, Lines.Num()));
		ASSERT_THAT(IsTrue(Lines[0].Contains(TEXT("state-selected reason=no-score state=fallback")), Lines[0]));
	}

	TEST_METHOD(WeightedDrawsInProportionAndReplaysBySeed)
	{
		const FString Children = TEXT("<State id=\"near\" tree=\"t\" weight=\"3\"/><State id=\"far\" tree=\"t\" weight=\"1\"/><State id=\"fallback\" tree=\"t\"/>");
		const FString Xml = Agent(Children, TEXT("weighted"));
		int32 Near = 0;
		for (int32 Seed = 1; Seed <= 400; ++Seed)
		{
			const FString Leaf = LeafAfterReplan(Xml, Seed);
			ASSERT_THAT(IsTrue(Leaf == TEXT("near") || Leaf == TEXT("far"), Leaf));
			ASSERT_THAT(AreEqual(Leaf, LeafAfterReplan(Xml, Seed)));
			Near += Leaf == TEXT("near") ? 1 : 0;
		}
		ASSERT_THAT(IsTrue(Near > 250 && Near < 350, FString::Printf(TEXT("near drawn %d of 400 times at weight 3:1"), Near)));
	}

	TEST_METHOD(ADirectGotoBypassesSelectionAndBindsComeFirst)
	{
		FADSKTestRuntime Direct(Agent(NearFar));
		ADSK_ASSERT(ADSKTest::Start(*Direct.Instance));
		Direct.Instance->EnqueueEvent(TEXT("Replan"));
		Direct.Instance->Tick(0.f);
		Direct.Instance->EnqueueEvent(TEXT("Jump"));
		Direct.Instance->Tick(0.f);
		ADSK_ASSERT(Direct.PathIs({TEXT("m"), TEXT("engage"), TEXT("far")}));
		ASSERT_THAT(AreEqual(1, Selected(Direct).Num()));

		const FString ByBound = TEXT("<State id=\"near\" tree=\"t\" score=\"Bound\" weight=\"-1\" bias=\"1\"/>"
			"<State id=\"far\" tree=\"t\" score=\"Bound\"/><State id=\"fallback\" tree=\"t\"/>");
		FADSKTestRuntime Bound(Agent(ByBound));
		ADSK_ASSERT(ADSKTest::Start(*Bound.Instance));
		Bound.Instance->EnqueueEvent(TEXT("Set"), {ADSKParam(TEXT("V"), FADSKValue::MakeFloat(5.f))});
		Bound.Instance->Tick(0.f);
		ADSK_ASSERT(Bound.PathIs({TEXT("m"), TEXT("engage"), TEXT("far")}));
	}

	TEST_METHOD(TheRootSelectsAtStart)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"select\"><Inputs><Input name=\"Brave\" type=\"Int\" default=\"1\"/></Inputs>"
			"<StateMachine id=\"m\" initial=\"idle\" select=\"best\"><State id=\"idle\" tree=\"t\"/>"
			"<State id=\"patrol\" tree=\"t\" score=\"Brave\"/></StateMachine>") + WaitTree + TEXT("</Agent>");
		FADSKTestRuntime Runtime(Xml);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("patrol")}));
	}

	TEST_METHOD(WeightedScoresPastTheFloatRangeFallBackToTheBest)
	{
		// Each score is finite, their sum is not: the draw cannot pick, so the best (first of equals) does.
		const FString Huge = TEXT("<State id=\"near\" tree=\"t\" score=\"Distance\"/><State id=\"far\" tree=\"t\" score=\"Distance\"/>"
			"<State id=\"fallback\" tree=\"t\"/>");
		for (int32 Seed = 1; Seed <= 4; ++Seed)
		{
			const FString Leaf = LeafAfterReplan(Agent(Huge, TEXT("weighted")), Seed,
				[](FADSKInstance& Instance) { Instance.SetInput(TEXT("Distance"), FADSKValue::MakeFloat(3e38f)); });
			ASSERT_THAT(AreEqual(FString(TEXT("near")), Leaf));
		}
	}

	TEST_METHOD(AScoreOnTheMachineIsReportedOnce)
	{
		const FADSKCompileResult Result = Load(Agent(NearFar).Replace(TEXT("<StateMachine id=\"m\" initial=\"idle\">"),
			TEXT("<StateMachine id=\"m\" initial=\"idle\" score=\"Distance\">")));
		ASSERT_THAT(AreEqual(1, Result.Diagnostics.Num(), ADSKTest::FormatDiagnostics(Result)));
	}
};

#endif
