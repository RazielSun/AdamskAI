// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/GuardScenarioDriver.h"
#include "CQTest.h"

namespace FGuardScenarioSpecPrivate
{
	const FVector Suspicion(300, 40, 0);
	const FVector Threat(500, 10, 0);
	const FVector Refresh(600, 20, 0);

	int32 FindLine(const FADSKTestLeafLog& Log, const TCHAR* Fragment)
	{
		return Log.Lines.IndexOfByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}
}

using namespace FGuardScenarioSpecPrivate;

TEST_CLASS_WITH_FLAGS(FGuardRoutineSpec, "AdamskAI.GuardScenario.Routine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(PatrolBothWaitsReentersWithNewActivations)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		const FADSKActivationHandle Old = Driver.ActiveHandle();
		Driver.FinishPatrolCycle();
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("routine")}));
		ASSERT_THAT(IsTrue(Driver.ActiveHandle().IsValid(), TEXT("no leaf is running")));
		ASSERT_THAT(IsTrue(Driver.ActiveHandle() != Old, TEXT("the patrol did not re-enter with a new activation")));
		Driver.Instance->PostActionResult(Old, EADSKNodeStatus::Succeeded);
		Driver.Advance(0.f);
		ASSERT_THAT(IsTrue(Driver.Recorder->Snapshot().ContainsByPredicate([](const FADSKRecord& R) { return R.Kind == EADSKRecordKind::ResultStale; }),
			TEXT("the old result was not recorded as stale") + Driver.Trace()));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}

	TEST_METHOD(SuspicionBindsThenCancelsBeforeInvestigateStarts)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("investigate")}));
		ASSERT_THAT(AreEqual(Suspicion, Driver.Interest()));
		ASSERT_THAT(IsTrue(FindLine(*Driver.Log, TEXT("End:MoveTo:toA:Cancelled")) < FindLine(*Driver.Log, TEXT("Start:MoveTo:toInterest")),
			FString::Printf(TEXT("toA cancelled at log line %d, toInterest started at %d"), FindLine(*Driver.Log, TEXT("End:MoveTo:toA:Cancelled")), FindLine(*Driver.Log, TEXT("Start:MoveTo:toInterest")))));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}

	TEST_METHOD(DuplicateSuspicionIsUnhandledWithoutMutation)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		const FADSKActivationHandle Handle = Driver.ActiveHandle();
		const FVector Other(999, 0, 0);
		Driver.Event(TEXT("Suspicious"), &Other);
		ASSERT_THAT(IsTrue(Driver.ActiveHandle() == Handle, TEXT("the duplicate restarted the leaf")));
		ASSERT_THAT(AreEqual(Suspicion, Driver.Interest()));
		ASSERT_THAT(IsTrue(Driver.Recorder->Snapshot().ContainsByPredicate([](const FADSKRecord& R) { return R.Kind == EADSKRecordKind::EventUnhandled; }),
			TEXT("the duplicate was not recorded as unhandled") + Driver.Trace()));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}
};

TEST_CLASS_WITH_FLAGS(FGuardSearchSpec, "AdamskAI.GuardScenario.Search",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(InvestigateAndFiniteSearchReturnToRoutine)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		Driver.FinishInvestigate();
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("search")}));
		Driver.FinishSearch();
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("routine")}));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}

	TEST_METHOD(InvestigationFailureAlsoFallsBackToSearch)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		Driver.FinishInvestigate(EADSKNodeStatus::Failed);
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("search")}));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}
};

TEST_CLASS_WITH_FLAGS(FGuardAlertSpec, "AdamskAI.GuardScenario.Alert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ThreatRefreshIsInternalWithoutRestart)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("ThreatConfirmed"), &Threat);
		const FADSKActivationHandle Watch = Driver.ActiveHandle();
		Driver.Event(TEXT("ThreatConfirmed"), &Refresh);
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("alert")}));
		ASSERT_THAT(IsTrue(Driver.ActiveHandle() == Watch, TEXT("the refresh restarted the watch")));
		ASSERT_THAT(AreEqual(Refresh, Driver.Interest()));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}

	TEST_METHOD(ContactLostCancelsWatchSearchesAndReturns)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Event(TEXT("ThreatConfirmed"), &Threat);
		Driver.Event(TEXT("ContactLost"));
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("search")}));
		ASSERT_THAT(IsTrue(Driver.Log->Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("watchInterest:Cancelled")); }),
			TEXT("watchInterest was not cancelled; log:\n  ") + FString::Join(Driver.Log->Lines, TEXT("\n  "))));
		Driver.FinishSearch();
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("routine")}));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}
};

TEST_CLASS_WITH_FLAGS(FGuardHoldSpec, "AdamskAI.GuardScenario.Hold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FalseGuardCompletesUnhandledOnceThenSuspicionEscapes)
	{
		FADSKGuardScenarioDriver Driver(*TestRunner, 8192);
		Driver.Instance->SetInput(TEXT("MayPatrol"), FADSKValue::MakeBool(false));
		Driver.Advance(0.f);
		Driver.Advance(0.f);
		Driver.Advance(0.f);
		ASSERT_THAT(IsTrue(Driver.Instance->IsHolding(), TEXT("not holding in ") + ADSKTest::FormatPath(*Driver.Instance)));
		int32 Holds = 0;
		for (const FADSKRecord& R : Driver.Recorder->Snapshot()) Holds += R.Kind == EADSKRecordKind::Hold ? 1 : 0;
		ASSERT_THAT(AreEqual(1, Holds));
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		ASSERT_THAT(IsFalse(Driver.Instance->IsHolding(), TEXT("still holding after the suspicion")));
		ADSK_ASSERT(Driver.PathIs({TEXT("mode"), TEXT("active"), TEXT("investigate")}));
		ADSK_ASSERT(Driver.StopAndBalanced());
	}
};

#endif
