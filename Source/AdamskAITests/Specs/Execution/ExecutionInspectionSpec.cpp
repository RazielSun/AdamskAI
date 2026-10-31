// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FExecutionInspectionSpecPrivate
{
	FString InspectionXml()
	{
		return TEXT("<Agent version=\"1\" id=\"inspect\"><Events>")
			TEXT("<Event name=\"low\" priority=\"0\"/><Event name=\"high\" priority=\"5\"/>")
			TEXT("</Events><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">")
			TEXT("<On event=\"low\"/><On event=\"high\"/></State></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"inspectWait\" duration=\"100\"/></Tree></Agent>");
	}

	void EnqueueMixed(FADSKInstance& Instance)
	{
		Instance.EnqueueEvent(TEXT("low"));
		Instance.EnqueueEvent(TEXT("high"));
		Instance.EnqueueEvent(TEXT("low"));
	}
}

using namespace FExecutionInspectionSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionInspectionSpec, "AdamskAI.Execution.Inspection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NodeFollowsLifecycle)
	{
		FADSKTestRuntime Runtime(InspectionXml());
		TArray<FString> Queue;
		Runtime.Instance->GetQueuedEvents(Queue);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveNodeId().IsNone(), TEXT("active node ") + Runtime.Instance->GetActiveNodeId().ToString()));
		ASSERT_THAT(IsTrue(Queue.IsEmpty(), TEXT("queued: ") + FString::Join(Queue, TEXT(", "))));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FName(TEXT("inspectWait")), Runtime.Instance->GetActiveNodeId()));
		EnqueueMixed(*Runtime.Instance);
		Runtime.Instance->Stop();
		Runtime.Instance->GetQueuedEvents(Queue);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveNodeId().IsNone(), TEXT("active node ") + Runtime.Instance->GetActiveNodeId().ToString()));
		ASSERT_THAT(IsTrue(Queue.IsEmpty(), TEXT("queued: ") + FString::Join(Queue, TEXT(", "))));
	}

	TEST_METHOD(QueuedEventsInDispatchOrderAndRepeatable)
	{
		FADSKTestRuntime Runtime(InspectionXml());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->Tick(0.f);
		EnqueueMixed(*Runtime.Instance);
		const TArray<FString> Expected = {TEXT("high"), TEXT("low"), TEXT("low")};
		TArray<FString> First = {TEXT("stale")};
		TArray<FString> Second;
		Runtime.Instance->GetQueuedEvents(First);
		Runtime.Instance->GetQueuedEvents(Second);
		ASSERT_THAT(AreEqual(Expected, First));
		ASSERT_THAT(AreEqual(Expected, Second));
	}

	TEST_METHOD(InspectionDoesNotPerturbExecution)
	{
		FADSKTestRuntime Inspected(InspectionXml());
		FADSKTestRuntime Control(InspectionXml());
		ADSK_ASSERT(ADSKTest::Start(*Inspected.Instance));
		ADSK_ASSERT(ADSKTest::Start(*Control.Instance));
		EnqueueMixed(*Inspected.Instance);
		EnqueueMixed(*Control.Instance);
		TArray<FString> Queue;
		Inspected.Instance->GetQueuedEvents(Queue);
		Inspected.Instance->GetActiveNodeId();
		Inspected.Instance->Tick(0.f);
		Control.Instance->Tick(0.f);
		const TArray<FADSKRecord> A = Inspected.Recorder->Snapshot();
		const TArray<FADSKRecord> B = Control.Recorder->Snapshot();
		ASSERT_THAT(AreEqual(B.Num(), A.Num()));
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(A[Index].Kind == B[Index].Kind && A[Index].Event == B[Index].Event
				&& A[Index].Node == B[Index].Node && A[Index].State == B[Index].State,
				ADSK::FormatRecord(*Inspected.Definition, A[Index]) + TEXT(" vs ") + ADSK::FormatRecord(*Control.Definition, B[Index])));
		}
		ASSERT_THAT(AreEqual(Control.Instance->GetActiveStatePath(), Inspected.Instance->GetActiveStatePath()));
	}
};

#endif
