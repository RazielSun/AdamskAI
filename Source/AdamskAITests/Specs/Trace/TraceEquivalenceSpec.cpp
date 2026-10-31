// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "CQTest.h"
#include "Support/GuardScenarioDriver.h"

namespace FTraceEquivalenceSpecPrivate
{
	const FVector Suspicion(300, 40, 0);
	const FVector Threat(500, 10, 0);
	const FVector Refresh(600, 20, 0);

	FString RunFull(FAutomationTestBase& Test, int32 Capacity, TSharedPtr<FADSKRecorder>* OutRecorder = nullptr)
	{
		FADSKGuardScenarioDriver Driver(Test, Capacity);
		Driver.Event(TEXT("Suspicious"), &Suspicion);
		Driver.FinishInvestigate();
		Driver.FinishSearch();
		Driver.Event(TEXT("ThreatConfirmed"), &Threat);
		Driver.Event(TEXT("ThreatConfirmed"), &Refresh);
		Driver.Event(TEXT("ContactLost"));
		Driver.FinishSearch();
		Driver.FinishPatrolCycle();
		ADSKTest::Expect(Driver.StopAndBalanced());
		if (OutRecorder) *OutRecorder = Driver.Recorder;
		return Driver.Decisions;
	}
}

using namespace FTraceEquivalenceSpecPrivate;

TEST_CLASS_WITH_FLAGS(FTraceEquivalenceSpec, "AdamskAI.Trace.Equivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RecorderAbsencePresenceAndOverflowDoNotChangeDecisions)
	{
		TSharedPtr<FADSKRecorder> Large;
		TSharedPtr<FADSKRecorder> Tiny;
		const FString None = RunFull(*TestRunner, 0);
		const FString Full = RunFull(*TestRunner, 8192, &Large);
		const FString Overflow = RunFull(*TestRunner, 2, &Tiny);
		ASSERT_THAT(AreEqual(None, Full));
		ASSERT_THAT(AreEqual(Full, Overflow));
		const TArray<FADSKRecord> TinyRecords = Tiny->Snapshot();
		ASSERT_THAT(AreEqual(2, TinyRecords.Num()));
		ASSERT_THAT(AreEqual(EADSKRecordKind::Gap, TinyRecords[0].Kind, FString::Printf(TEXT("kind is %s"), LexToString(TinyRecords[0].Kind))));
		ASSERT_THAT(AreEqual(Tiny->GetTotalRecorded() - 1, TinyRecords[0].LostCount));
		ASSERT_THAT(IsFalse(Large->Snapshot().ContainsByPredicate([](const FADSKRecord& R) { return R.Kind == EADSKRecordKind::Gap; }), TEXT("the large recorder overflowed")));
		for (const EADSKRecordKind Kind : {EADSKRecordKind::InstanceStarted, EADSKRecordKind::InstanceStopped,
			EADSKRecordKind::InputSet, EADSKRecordKind::EventEnqueued, EADSKRecordKind::EventConsumed,
			EADSKRecordKind::StateEntered, EADSKRecordKind::StateExited, EADSKRecordKind::GuardValue,
			EADSKRecordKind::NodeStarted, EADSKRecordKind::NodeEnded, EADSKRecordKind::ResultAccepted})
		{
			ASSERT_THAT(IsTrue(Large->Snapshot().ContainsByPredicate([Kind](const FADSKRecord& R) { return R.Kind == Kind; }), FString::Printf(TEXT("no %s record"), LexToString(Kind))));
		}
	}
};

#endif
