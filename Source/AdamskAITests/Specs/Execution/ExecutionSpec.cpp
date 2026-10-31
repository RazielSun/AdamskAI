// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/GuardFixture.h"
#include "CQTest.h"

namespace FExecutionSpecPrivate
{
	FADSKTestRuntime MakeStarted()
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::HsmXml());
		Runtime.Instance->SetInput(TEXT("Req"), FADSKValue::MakeVector(FVector(1, 2, 3)));
		ADSKTest::Expect(ADSKTest::Start(*Runtime.Instance));
		return Runtime;
	}

	FString PriorityBoundsXml()
	{
		return TEXT("<Agent version=\"1\" id=\"bounds\"><Events>")
			TEXT("<Event name=\"Low\" priority=\"-2147483648\"/><Event name=\"High\" priority=\"2147483647\"/>")
			TEXT("</Events><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">")
			TEXT("<On event=\"Low\"/><On event=\"High\"/></State></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
	}

	TArray<FString> Consumed(const FADSKTestRuntime& Runtime)
	{
		TArray<FString> Names;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::EventConsumed)
			{
				Names.Add(Runtime.Definition->GetEvents()[Record.Event].Name);
			}
		}
		return Names;
	}

	TArray<EADSKRecordKind> Decisions(const FADSKRecorder& Recorder)
	{
		TArray<EADSKRecordKind> Result;
		for (const FADSKRecord& Record : Recorder.Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::EventConsumed || Record.Kind == EADSKRecordKind::EventUnhandled)
			{
				Result.Add(Record.Kind);
			}
		}
		return Result;
	}
}

using namespace FExecutionSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionStartSpec, "AdamskAI.Execution.Input.Start",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(MissingRequiredInputIsAtomicAndOrdered)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::HsmXml());
		const FADSKStartReport Report = Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(EADSKStartResult::MissingRequiredInput, Report.Result, FString::Printf(TEXT("start returned %s"), LexToString(Report.Result))));
		ASSERT_THAT(AreEqual(1, Report.MissingInputs.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Req")), Report.MissingInputs[0]));
		ASSERT_THAT(AreEqual(static_cast<uint32>(0), Runtime.Instance->GetGeneration()));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveStatePath().IsEmpty(), TEXT("active path ") + ADSKTest::FormatPath(*Runtime.Instance)));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::StartFailed)));
	}

	TEST_METHOD(InitialPathStopAndPersistentInputsAreDeterministic)
	{
		FADSKTestRuntime Runtime = MakeStarted();
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a1"), TEXT("a1x")}));
		const uint32 FirstGeneration = Runtime.Instance->GetGeneration();
		Runtime.Instance->Stop();
		Runtime.Instance->Stop();
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::InstanceStopped)));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FirstGeneration + 2, Runtime.Instance->GetGeneration()));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a1"), TEXT("a1x")}));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionInputSpec, "AdamskAI.Execution.Input.Results",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(InputAndEventValidationReturnsEveryResult)
	{
		FADSKTestRuntime Runtime(FADSKGuardFixture::HsmXml());
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->SetInput(TEXT("L"), FADSKValue::MakeVector(FVector::ZeroVector))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(TEXT("Req"), FADSKValue::MakeBool(true))));
		ASSERT_THAT(AreEqual(EADSKInputResult::NotRunning, Runtime.Instance->EnqueueEvent(TEXT("Noise"))));
		Runtime.Instance->SetInput(TEXT("Req"), FADSKValue::MakeVector(FVector::ZeroVector));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->EnqueueEvent(TEXT("missing"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Reserved, Runtime.Instance->EnqueueEvent(TEXT("tree.success"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::MissingParameter, Runtime.Instance->EnqueueEvent(TEXT("Lo"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnexpectedParameter, Runtime.Instance->EnqueueEvent(TEXT("Lo"), {ADSKParam(TEXT("Bad"), FADSKValue::MakeVector(FVector::ZeroVector))})));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->EnqueueEvent(TEXT("Lo"), {ADSKParam(TEXT("V"), FADSKValue::MakeBool(true))})));
	}

	TEST_METHOD(ExternalQueueRejectsOnlyTheSixtyFifth)
	{
		FADSKTestRuntime Runtime = MakeStarted();
		for (int32 Index = 0; Index < ADSK::ExternalEventCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Noise"))));
		}
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->EnqueueEvent(TEXT("Noise"))));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::InputRejected)));
	}
};

TEST_CLASS_WITH_FLAGS(FExecutionDispatchSpec, "AdamskAI.Execution.Dispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(PriorityLeafLookupAndInternalBindDoNotTransition)
	{
		FADSKTestRuntime Runtime = MakeStarted();
		Runtime.Instance->EnqueueEvent(TEXT("Lo"), {ADSKParam(TEXT("V"), FADSKValue::MakeVector(FVector(4, 5, 6)))});
		Runtime.Instance->EnqueueEvent(TEXT("Noise"));
		Runtime.Instance->EnqueueEvent(TEXT("Hi"));
		Runtime.Instance->EnqueueEvent(TEXT("E1"));
		Runtime.Instance->Tick(0.f);
		FADSKValue Local;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("L"), Local), TEXT("no slot L")));
		ASSERT_THAT(AreEqual(FVector(4, 5, 6), Local.Vector));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a1"), TEXT("a1x")}));
		const TArray<EADSKRecordKind> Order = Decisions(*Runtime.Recorder);
		ASSERT_THAT(AreEqual(4, Order.Num()));
		ASSERT_THAT(AreEqual(EADSKRecordKind::EventConsumed, Order[0], FString::Printf(TEXT("first decision is %s"), LexToString(Order[0]))));
		ASSERT_THAT(AreEqual(EADSKRecordKind::EventConsumed, Order[1], FString::Printf(TEXT("second decision is %s"), LexToString(Order[1]))));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::EventUnhandled)));
	}

	TEST_METHOD(ExternalDomainsExitAndEnterInDefinedOrder)
	{
		FADSKTestRuntime Runtime = MakeStarted();
		Runtime.Instance->EnqueueEvent(TEXT("Up"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a2")}));
		Runtime.Instance->EnqueueEvent(TEXT("E1"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("b")}));
		Runtime.Instance->EnqueueEvent(TEXT("Back"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a1"), TEXT("a1x")}));
		Runtime.Instance->EnqueueEvent(TEXT("Self"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("a"), TEXT("a1"), TEXT("a1x")}));
	}

	TEST_METHOD(EventBudgetAndInvalidDeltaLeaveExpectedWork)
	{
		FADSKTestRuntime Runtime = MakeStarted();
		for (int32 Index = 0; Index < 20; ++Index) Runtime.Instance->EnqueueEvent(TEXT("Noise"));
		ASSERT_THAT(AreEqual(EADSKTickResult::InvalidDelta, Runtime.Instance->Tick(-1.f)));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), Runtime.Instance->GetTickCount()));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(16, Runtime.Count(EADSKRecordKind::EventUnhandled)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(20, Runtime.Count(EADSKRecordKind::EventUnhandled)));
	}

	TEST_METHOD(TheLowestInt32PriorityIsDispatchedAfterHigherOnes)
	{
		FADSKTestRuntime Runtime(PriorityBoundsXml());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Low"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("High"))));
		Runtime.Instance->Tick(0.f);
		const TArray<FString> Order = Consumed(Runtime);
		ASSERT_THAT(AreEqual(2, Order.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("High")), Order[0]));
		ASSERT_THAT(AreEqual(FString(TEXT("Low")), Order[1]));
	}

	TEST_METHOD(LowestPriorityEventsDrainInsteadOfFillingTheQueue)
	{
		FADSKTestRuntime Runtime(PriorityBoundsXml(), 4096);
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		// Five full queues' worth: each tick drains at most the dispatch budget, never leaves a residue.
		for (int32 Round = 0; Round < 5; ++Round)
		{
			for (int32 Index = 0; Index < 16; ++Index)
			{
				ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Low"))));
			}
			Runtime.Instance->Tick(0.f);
		}
		ASSERT_THAT(AreEqual(80, Consumed(Runtime).Num()));
		ASSERT_THAT(AreEqual(0, Runtime.Count(EADSKRecordKind::InputRejected)));
	}
};

#endif
