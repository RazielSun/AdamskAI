// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FEmitSpecPrivate
{
	FString Agent(const FString& Events, const FString& Machine, const FString& Trees)
	{
		return TEXT("<Agent version=\"1\" id=\"emit\"><Events>") + Events + TEXT("</Events>")
			+ Machine + Trees + TEXT("</Agent>");
	}

	TArray<FString> Consumed(const FADSKTestRuntime& Runtime)
	{
		TArray<FString> Result;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::EventConsumed)
			{
				Result.Add(Runtime.Definition->GetEvents()[Record.Event].Name);
			}
		}
		return Result;
	}
}

using namespace FEmitSpecPrivate;

TEST_CLASS_WITH_FLAGS(FEmitCompilerSpec, "AdamskAI.Execution.Emit.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RequiresDeclaredParameterlessNonReservedEvent)
	{
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>");
		auto LoadEmit = [&](const FString& Events, const FString& Node)
		{
			return Load(Agent(Events, Machine, TEXT("<Tree id=\"t\">") + Node + TEXT("</Tree>")));
		};
		ASSERT_THAT(AreEqual(1, LoadEmit(TEXT("<Event name=\"Ready\"/>"), TEXT("<Emit id=\"e\"/>")).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, LoadEmit(TEXT("<Event name=\"Ready\"/>"), TEXT("<Emit id=\"e\" event=\"Missing\"/>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, LoadEmit(TEXT("<Event name=\"Ready\"/>"), TEXT("<Emit id=\"e\" event=\"tree.success\"/>")).Count(Code::TypeMismatch)));
		// A parameterized event needs every parameter supplied (EmitArgsSpec covers the rest).
		ASSERT_THAT(AreEqual(1, LoadEmit(TEXT("<Event name=\"Ready\"><Param name=\"N\" type=\"Int\"/></Event>"), TEXT("<Emit id=\"e\" event=\"Ready\"/>")).Count(Code::MissingRequired)));
		const FADSKCompileResult Valid = LoadEmit(TEXT("<Event name=\"Ready\"/>"), TEXT("<Emit event=\"Ready\" id=\"e\"/>"));
		ASSERT_THAT(IsTrue(Valid.IsSuccess() && Valid.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Valid)));
		ASSERT_THAT(AreEqual(Valid.Definition->FindEvent(TEXT("Ready")), Valid.Definition->GetNodes()[0].EmitEvent));
	}
};

TEST_CLASS_WITH_FLAGS(FEmitExecutionSpec, "AdamskAI.Execution.Emit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(IsVisibleImmediatelyAndDeliveredOnNextTick)
	{
		const FString Events = TEXT("<Event name=\"Handoff\" priority=\"5\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"emit\"><State id=\"emit\" tree=\"emitTree\">"
			"<On event=\"Handoff\" goto=\"done\"/></State><State id=\"done\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"emitTree\"><Emit id=\"handoff\" event=\"Handoff\"/></Tree>");
		FADSKTestRuntime Runtime(Agent(Events, Machine, Trees));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("emit")}));
		TArray<FString> Queued;
		Runtime.Instance->GetQueuedEvents(Queued);
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("Handoff")}, Queued));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("done")}));
	}

	TEST_METHOD(EmitStartedInsideDispatchCannotRedispatchInSameTick)
	{
		const FString Events = TEXT("<Event name=\"Start\" priority=\"10\"/><Event name=\"Handoff\" priority=\"20\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\">"
			"<On event=\"Start\" goto=\"emit\"/></State><State id=\"emit\" tree=\"emitTree\">"
			"<On event=\"Handoff\" goto=\"done\"/></State><State id=\"done\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"wait\"><Wait id=\"wait-node\" duration=\"100\"/></Tree>"
			"<Tree id=\"emitTree\"><Emit id=\"handoff\" event=\"Handoff\"/></Tree>");
		FADSKTestRuntime Runtime(Agent(Events, Machine, Trees));
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Start"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("emit")}));
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("Start")}, Consumed(Runtime)));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("done")}));
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("Start"), TEXT("Handoff")}), Consumed(Runtime)));
	}

	TEST_METHOD(FutureEmitAndHostEventsSharePriorityAndSequenceQueue)
	{
		const FString Events = TEXT("<Event name=\"Emitted\" priority=\"0\"/><Event name=\"High\" priority=\"5\"/>"
			"<Event name=\"Later\" priority=\"0\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"Emitted\"/><On event=\"High\"/><On event=\"Later\"/></State></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"t\"><Emit id=\"emit\" event=\"Emitted\"/></Tree>");
		FADSKTestRuntime Runtime(Agent(Events, Machine, Trees));
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Later"));
		Runtime.Instance->EnqueueEvent(TEXT("High"));
		TArray<FString> Queued;
		Runtime.Instance->GetQueuedEvents(Queued);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("High"), TEXT("Emitted"), TEXT("Later")}), Queued));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("High"), TEXT("Emitted"), TEXT("Later")}), Consumed(Runtime)));

		Runtime.Instance->EnqueueEvent(TEXT("Later"));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("Later")), Consumed(Runtime).Last()));
	}

	TEST_METHOD(QueueFullFailsEmitAndRecordsNormalRejectionShape)
	{
		const FString Events = TEXT("<Event name=\"Trigger\" priority=\"100\"/><Event name=\"Low\" priority=\"0\"/>"
			"<Event name=\"First\"/><Event name=\"Overflow\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\">"
			"<On event=\"Trigger\" goto=\"emit\"/></State><State id=\"emit\" tree=\"emitTree\">"
			"<On event=\"tree.failure\" goto=\"failed\"/></State><State id=\"failed\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"wait\"><Wait id=\"wait-node\" duration=\"100\"/></Tree>"
			"<Tree id=\"emitTree\"><Sequence id=\"pair\"><Emit id=\"first\" event=\"First\"/>"
			"<Emit id=\"overflow\" event=\"Overflow\"/></Sequence></Tree>");
		FADSKTestRuntime Runtime(Agent(Events, Machine, Trees), 4096);
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Trigger"));
		for (int32 Index = 1; Index < ADSK::ExternalEventCapacity; ++Index)
		{
			Runtime.Instance->EnqueueEvent(TEXT("Low"));
		}
		Runtime.Instance->Tick(0.f);
		const int32 OverflowEvent = Runtime.Definition->FindEvent(TEXT("Overflow"));
		ASSERT_THAT(IsTrue(Runtime.Recorder->Snapshot().ContainsByPredicate([OverflowEvent](const FADSKRecord& Record)
		{
			return Record.Kind == EADSKRecordKind::InputRejected && Record.Reason == EADSKRecordReason::QueueFull
				&& Record.Event == OverflowEvent;
		}), TEXT("the overflow emit was not rejected as queue-full") + Runtime.Trace()));
		ASSERT_THAT(IsTrue(Runtime.Recorder->Snapshot().ContainsByPredicate([&Runtime](const FADSKRecord& Record)
		{
			return Record.Kind == EADSKRecordKind::NodeEnded && Record.Status == EADSKNodeStatus::Failed
				&& Runtime.Definition->GetNodes()[Record.Node].Id == FName(TEXT("overflow"));
		}), TEXT("the overflow emit did not fail") + Runtime.Trace()));
		for (int32 Tick = 0; Tick < 5 && !Runtime.PathIs({TEXT("m"), TEXT("failed")}); ++Tick)
		{
			Runtime.Instance->Tick(0.f);
		}
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("failed")}));
	}
};

#endif
