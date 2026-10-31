// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/GuardFixture.h"
#include "CQTest.h"

namespace FExecutionLifecycleSpecPrivate
{
	FADSKTestRuntime MakeLeafRuntime()
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::LeafXml());
		Runtime.Instance->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(10, 0, 0)));
		ADSKTest::Expect(ADSKTest::Start(*Runtime.Instance));
		return Runtime;
	}
}

using namespace FExecutionLifecycleSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionLifecycleSpec, "AdamskAI.Execution.Lifecycle.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ImmediateFailureUsesExactEndReason)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		Runtime.Log->PartialEntryIds.Add(TEXT("acquire"));
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:move:Cancelled")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:AcquireResource:acquire:PartialEntry")));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(OrdinaryImmediateFailureIsNotPartialEntry)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:AcquireResource:acquire:Failed")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("PartialEntry")));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionResultSpec, "AdamskAI.Execution.Lifecycle.Results",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ResultsAreQueuedAcceptedDuplicatedAndStale)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		const FADSKActivationHandle First = Runtime.Instance->GetActiveLeafHandle();
		Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded);
		Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded);
		ADSK_ASSERT(Runtime.LacksLine(TEXT("End:MoveTo")));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultAccepted)));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultDuplicate)));
		Runtime.Instance->EnqueueEvent(TEXT("Again"));
		Runtime.Instance->Tick(0.f);
		const FADSKActivationHandle Second = Runtime.Instance->GetActiveLeafHandle();
		ASSERT_THAT(IsTrue(Second.IsValid(), TEXT("no leaf is running after Again")));
		ASSERT_THAT(IsTrue(Second != First, TEXT("Again did not restart the leaf")));
		Runtime.Instance->EnqueueEvent(TEXT("Again"));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle() != Second, TEXT("the second Again did not restart the leaf")));
		Runtime.Instance->PostActionResult(Second, EADSKNodeStatus::Failed);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultStale)));
	}

	TEST_METHOD(ResultQueueHasIndependentSixtyFourLimit)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		const FADSKActivationHandle Handle = Runtime.Instance->GetActiveLeafHandle();
		for (int32 Index = 0; Index < ADSK::ActionResultCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(Handle, EADSKNodeStatus::Succeeded)));
		}
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->PostActionResult(Handle, EADSKNodeStatus::Succeeded)));
		Runtime.Instance->Stop();
		ASSERT_THAT(AreEqual(EADSKInputResult::NotRunning, Runtime.Instance->PostActionResult(Handle, EADSKNodeStatus::Succeeded)));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionCompletionSpec, "AdamskAI.Execution.Lifecycle.Completion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(HigherPriorityEventMakesOldCompletionStale)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		const FADSKActivationHandle Handle = Runtime.Instance->GetActiveLeafHandle();
		Runtime.Instance->PostActionResult(Handle, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("fail")}));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::CompletionStale)));
	}

	TEST_METHOD(UnhandledCompletionHoldsOnceAndExternalEventEscapes)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		Runtime.Instance->PostActionResult(Runtime.Instance->GetActiveLeafHandle(), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->IsHolding(), TEXT("not holding") + Runtime.Trace()));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::Hold)));
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsFalse(Runtime.Instance->IsHolding(), TEXT("still holding after Go")));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionIsolationSpec, "AdamskAI.Execution.Lifecycle.Isolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SharedDefinitionKeepsInstanceStateIndependent)
	{
		FADSKTestRuntime Runtime = MakeLeafRuntime();
		FADSKInstance Other(Runtime.Definition.ToSharedRef());
		Other.SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(20, 0, 0)));
		Other.Start();
		const FADSKActivationHandle A = Runtime.Instance->GetActiveLeafHandle();
		const FADSKActivationHandle B = Other.GetActiveLeafHandle();
		Runtime.Instance->PostActionResult(A, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle() != A, TEXT("the posted result did not end the leaf")));
		ASSERT_THAT(IsTrue(Other.GetActiveLeafHandle() == B, TEXT("the other instance's leaf changed")));
		ADSK_ASSERT(ADSKTest::PathIs(Other, {TEXT("m"), TEXT("idle")}));
		ASSERT_THAT(AreEqual(Other.GetGeneration(), Runtime.Instance->GetGeneration()));
		Other.Stop();
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(ASeededInstanceIssuesHandlesAboveItsSeed)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::LeafXml());
		FADSKInstance Seeded(Runtime.Definition.ToSharedRef(), nullptr, nullptr, 7);
		ASSERT_THAT(AreEqual(7u, Seeded.GetGeneration()));
		Seeded.SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(10, 0, 0)));
		ADSK_ASSERT(ADSKTest::Start(Seeded));
		ASSERT_THAT(AreEqual(8u, Seeded.GetGeneration()));
		ASSERT_THAT(AreEqual(8u, Seeded.GetActiveLeafHandle().Generation));
		Seeded.Stop();
	}
};

#endif
