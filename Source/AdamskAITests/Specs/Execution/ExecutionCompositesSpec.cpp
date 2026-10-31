// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/GuardFixture.h"
#include "CQTest.h"

namespace FExecutionCompositesSpecPrivate
{
	FADSKTestRuntime MakeComposite(bool bOk = true)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::CompositeXml(), 2048);
		Runtime.Instance->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(10, 0, 0)));
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(bOk));
		ADSKTest::Expect(ADSKTest::Start(*Runtime.Instance));
		return Runtime;
	}

	int32 StartsFor(const FADSKTestRuntime& Runtime, int32 Node)
	{
		int32 Result = 0;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			Result += Record.Kind == EADSKRecordKind::NodeStarted && Record.Node == Node ? 1 : 0;
		}
		return Result;
	}
}

using namespace FExecutionCompositesSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionGuardSpec, "AdamskAI.Execution.Composites.Guard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FalseEntryNeverStartsChild)
	{
		FADSKTestRuntime Runtime = MakeComposite(false);
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo:guardMove")));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::GuardValue)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(FalseBeforeUpdateCancelsChildWithoutUpdatingIt)
	{
		FADSKTestRuntime Runtime = MakeComposite();
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.5f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:guardMove:Cancelled")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Update:MoveTo:guardMove")));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionSequenceSpec, "AdamskAI.Execution.Composites.Sequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ResultStartsWaitWithZeroAndTenLaterUpdatesCompleteIt)
	{
		FADSKTestRuntime Runtime = MakeComposite();
		Runtime.Instance->EnqueueEvent(TEXT("ToSeq"));
		Runtime.Instance->Tick(0.5f);
		const FADSKActivationHandle Move = Runtime.Instance->GetActiveLeafHandle();
		Runtime.Instance->PostActionResult(Move, EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.5f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("no leaf is running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
		for (int32 Index = 0; Index < 9; ++Index) Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("no leaf is running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(IsFalse(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("a leaf is still running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
	}

	TEST_METHOD(ZeroDeltaDoesNotAdvanceWait)
	{
		FADSKTestRuntime Runtime = MakeComposite();
		Runtime.Instance->EnqueueEvent(TEXT("ToSeq"));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->PostActionResult(Runtime.Instance->GetActiveLeafHandle(), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Tick(0.f);
		for (int32 Index = 0; Index < 20; ++Index) Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("no leaf is running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionOutputSpec, "AdamskAI.Execution.Composites.Output",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(OutputIsVisibleToLaterSameTickStart)
	{
		FADSKTestRuntime Runtime = MakeComposite();
		Runtime.Instance->EnqueueEvent(TEXT("ToSeq"));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->EnqueueEvent(TEXT("ToSense"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("Goal:seenMove:X=1.000 Y=2.000 Z=3.000")));
	}

	TEST_METHOD(ZeroDurationWaitCompletesDuringStart)
	{
		FADSKTestRuntime Runtime = MakeComposite();
		Runtime.Instance->EnqueueEvent(TEXT("ToSeq"));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->EnqueueEvent(TEXT("ToSense"));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->EnqueueEvent(TEXT("ToImmediate"));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsFalse(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("a leaf is still running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionBudgetSpec, "AdamskAI.Execution.Composites.Budget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StartStopsAtSixtyFourThenCompletesOnNextTick)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::BudgetXml(), 2048);
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(ADSK::ProcedureOperationBudget, Runtime.Count(EADSKRecordKind::NodeStarted)));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::BudgetDeferred)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(71, Runtime.Count(EADSKRecordKind::NodeStarted)));
		ASSERT_THAT(IsFalse(Runtime.Instance->IsHolding(), TEXT("holding in ") + ADSKTest::FormatPath(*Runtime.Instance)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(Runtime.Instance->IsHolding(), TEXT("not holding") + Runtime.Trace()));
	}

	TEST_METHOD(DeferredGuardRechecksChangedInputBeforeChildStart)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::BudgetXml(true), 2048);
		Runtime.Instance->Start();
		const int32 Never = Runtime.Definition->FindNode(TEXT("never"));
		ASSERT_THAT(AreEqual(0, StartsFor(Runtime, Never)));
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(0, StartsFor(Runtime, Never)));
		Runtime.Instance->Stop();
	}
};

#endif
