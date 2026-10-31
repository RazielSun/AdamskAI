// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FHistorySpecPrivate
{
	/** fight remembers phase-two once Out has taken it to stagger; Back returns. The run semantics are conformance files. */
	FString Agent(const TCHAR* History = TEXT("shallow"), const TCHAR* PhaseTwo = TEXT("phase-two"))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"history\"><Events><Event name=\"Next\"/><Event name=\"Out\"/><Event name=\"Back\"/></Events>"
			"<StateMachine id=\"m\" initial=\"fight\"><State id=\"fight\" initial=\"phase-one\" history=\"%s\">"
			"<State id=\"phase-one\" tree=\"t\"><On event=\"Next\" goto=\"%s\"/></State>"
			"<State id=\"%s\" tree=\"t\"><On event=\"Out\" goto=\"stagger\"/></State></State>"
			"<State id=\"stagger\" tree=\"t\"><On event=\"Back\" goto=\"fight\"/></State></StateMachine>"
			"<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>"), History, PhaseTwo, PhaseTwo);
	}

	void Drive(FADSKInstance& Instance, std::initializer_list<const TCHAR*> Events)
	{
		for (const TCHAR* Event : Events)
		{
			Instance.EnqueueEvent(Event);
			Instance.Tick(0.f);
		}
	}
}

using namespace FHistorySpecPrivate;

TEST_CLASS_WITH_FLAGS(FHistoryCompilerSpec, "AdamskAI.Execution.History.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AcceptsShallowAndDeepOnStatesWithChildrenOnly)
	{
		const FADSKCompileResult Shallow = Load(Agent());
		ASSERT_THAT(IsTrue(Shallow.IsSuccess() && Shallow.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Shallow)));
		ASSERT_THAT(IsTrue(Shallow.Definition->GetStates()[Shallow.Definition->FindState(TEXT("fight"))].History == EADSKStateHistory::Shallow, TEXT("fight is not shallow")));
		const FADSKCompileResult Deep = Load(Agent(TEXT("deep")));
		ASSERT_THAT(IsTrue(Deep.Definition->GetStates()[Deep.Definition->FindState(TEXT("fight"))].History == EADSKStateHistory::Deep, TEXT("fight is not deep")));

		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("always"))).Count(Code::UnknownEnumValue)));
		const FString OnLeaf = TEXT("<Agent version=\"1\" id=\"h\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" history=\"shallow\"/></StateMachine></Agent>");
		ASSERT_THAT(AreEqual(1, Load(OnLeaf).Count(Code::UnknownAttribute)));
		const FString OnMachine = TEXT("<Agent version=\"1\" id=\"h\"><StateMachine id=\"m\" initial=\"s\" history=\"deep\"><State id=\"s\"/></StateMachine></Agent>");
		ASSERT_THAT(AreEqual(1, Load(OnMachine).Count(Code::UnknownAttribute)));
	}
};

TEST_CLASS_WITH_FLAGS(FHistoryExecutionSpec, "AdamskAI.Execution.History",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheResumedChildIsTracedAndAStartForgetsIt)
	{
		FADSKTestRuntime Runtime(Agent());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Drive(*Runtime.Instance, {TEXT("Next"), TEXT("Out"), TEXT("Back")});
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("fight"), TEXT("phase-two")}));
		ASSERT_THAT(IsTrue(Runtime.Trace().Contains(TEXT("state-selected reason=history state=phase-two")), Runtime.Trace()));

		Runtime.Instance->Stop();
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Drive(*Runtime.Instance, {TEXT("Out"), TEXT("Back")});
		// phase-one has no Out handler: still there, and a restart forgot phase-two.
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("fight"), TEXT("phase-one")}));
	}

	TEST_METHOD(ASnapshotKeepsTheHistoryWhileTheChildStillBelongs)
	{
		FADSKTestRuntime Original(Agent());
		ADSK_ASSERT(ADSKTest::Start(*Original.Instance));
		Drive(*Original.Instance, {TEXT("Next"), TEXT("Out")});
		ADSK_ASSERT(Original.PathIs({TEXT("m"), TEXT("stagger")}));
		FADSKSnapshot Snapshot;
		ASSERT_THAT(IsTrue(Original.Instance->CaptureSnapshot(Snapshot), TEXT("capture refused")));
		ASSERT_THAT(IsTrue(Snapshot.History.ContainsByPredicate([](const FADSKSnapshotHistory& Each) { return Each.State == TEXT("fight") && Each.Child == TEXT("phase-two"); }),
			TEXT("fight's history not captured")));

		FADSKTestRuntime Restored(Agent());
		const FADSKStartReport Report = Restored.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(Report.Dropped.IsEmpty(), FString::Join(Report.Dropped, TEXT(", "))));
		Drive(*Restored.Instance, {TEXT("Back")});
		ADSK_ASSERT(Restored.PathIs({TEXT("m"), TEXT("fight"), TEXT("phase-two")}));

		// The edited file renamed phase-two: the remembered child is gone, so fight enters initial.
		FADSKTestRuntime Edited(Agent(TEXT("shallow"), TEXT("phase-three")));
		const FADSKStartReport EditedReport = Edited.Instance->Start(Snapshot);
		ASSERT_THAT(IsTrue(EditedReport.Dropped.Contains(TEXT("history fight")), FString::Join(EditedReport.Dropped, TEXT(", "))));
		Drive(*Edited.Instance, {TEXT("Back")});
		ADSK_ASSERT(Edited.PathIs({TEXT("m"), TEXT("fight"), TEXT("phase-one")}));
	}
};

#endif
