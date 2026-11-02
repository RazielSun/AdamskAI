// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FExecutionReentrySpecPrivate
{
	enum class EAction : uint8 { None, Stop, NestedCalls };

	struct FHooks
	{
		FADSKInstance* Instance = nullptr;
		EAction OnStart = EAction::None;
		EAction OnUpdate = EAction::None;
		EAction OnEnd = EAction::None;
		EAction OnCompositeStart = EAction::None;
		EADSKNodeStatus UpdateStatus = EADSKNodeStatus::Running;
		TArray<FString> Lines;
		TOptional<EADSKTickResult> NestedTick;
		TOptional<EADSKStartResult> NestedStart;
		TOptional<EADSKInputResult> NestedEvent;
		int32 Starts = 0;
		int32 Ends = 0;

		void Act(EAction Action)
		{
			if (Action == EAction::Stop) { Instance->Stop(); }
			if (Action == EAction::NestedCalls)
			{
				NestedTick = Instance->Tick(0.1f);
				NestedStart = Instance->Start().Result;
				NestedEvent = Instance->EnqueueEvent(TEXT("Poke"));
			}
		}
	};

	struct FRuntime
	{
		TSharedRef<FHooks> Hooks = MakeShared<FHooks>();
		TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(256);
		TUniquePtr<FADSKInstance> Instance;

		explicit FRuntime(const TCHAR* Tree = TEXT("<Sequence id=\"seq\"><Reenter id=\"first\"/><Reenter id=\"second\"/></Sequence>"))
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			FADSKNodeDescriptor Descriptor;
			Descriptor.Name = TEXT("Reenter");
			const TSharedRef<FHooks> H = Hooks;
			Descriptor.OnStart = [H](FADSKNodeContext& Context)
			{
				++H->Starts;
				H->Lines.Add(TEXT("Start:") + Context.GetNodeId().ToString());
				H->Act(H->OnStart);
				return EADSKNodeStatus::Running;
			};
			Descriptor.OnUpdate = [H](FADSKNodeContext& Context, float)
			{
				H->Act(H->OnUpdate);
				return H->UpdateStatus;
			};
			Descriptor.OnEnd = [H](FADSKNodeContext& Context, EADSKEndReason Reason)
			{
				++H->Ends;
				H->Lines.Add(FString::Printf(TEXT("End:%s:%d"), *Context.GetNodeId().ToString(), static_cast<int32>(Reason)));
				H->Act(H->OnEnd);
			};
			ADSKTest::Register(*Registry, MoveTemp(Descriptor));
			FADSKNodeDescriptor Composite;
			Composite.Name = TEXT("ReenterComposite");
			Composite.Kind = EADSKNodeKind::Composite;
			Composite.MinChildren = 1;
			Composite.MaxChildren = 1;
			Composite.OnCompositeStart = [H](FADSKNodeContext&)
			{
				H->Act(H->OnCompositeStart);
				return FADSKCompositeStep::Start(0);
			};
			Composite.OnChildFinished = [](FADSKNodeContext&, int32, EADSKNodeStatus Status)
			{
				return Status == EADSKNodeStatus::Succeeded ? FADSKCompositeStep::Succeed() : FADSKCompositeStep::Fail();
			};
			ADSKTest::Register(*Registry, MoveTemp(Composite));
			Registry->Freeze();
			const FString Xml = TEXT("<Agent version=\"1\" id=\"r\"><Inputs/><Locals/><Events><Event name=\"Poke\"/></Events>")
				TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\">") + FString(Tree) + TEXT("</Tree></Agent>");
			Instance = MakeUnique<FADSKInstance>(ADSKTest::LoadOrStandIn(TEXT("reentry.xml"), Xml, Registry), Recorder);
			Hooks->Instance = Instance.Get();
		}

		int32 Count(EADSKRecordKind Kind) const { return ADSKTest::CountRecords(Recorder.Get(), Kind); }

		int32 Ended(EADSKRecordReason Reason) const
		{
			return Recorder->Snapshot().FilterByPredicate([Reason](const FADSKRecord& Record) { return Record.Kind == EADSKRecordKind::NodeEnded && Record.Reason == Reason; }).Num();
		}

		FADSKCheck Balanced() const
		{
			return Hooks->Starts == Hooks->Ends ? FADSKCheck::Pass()
				: FADSKCheck::Fail(FString::Printf(TEXT("%d starts but %d ends; calls:\n  %s"), Hooks->Starts, Hooks->Ends, *FString::Join(Hooks->Lines, TEXT("\n  "))));
		}
	};
}

using namespace FExecutionReentrySpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionReentrySpec, "AdamskAI.Execution.Reentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StopFromStartStopsAfterStartReturns)
	{
		FRuntime Runtime;
		Runtime.Hooks->OnStart = EAction::Stop;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ADSK_ASSERT(Runtime.Balanced());
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::InstanceStopped)));
	}

	TEST_METHOD(StopFromUpdateEndsTheTickWithoutStartingTheNextLeaf)
	{
		FRuntime Runtime;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Hooks->OnUpdate = EAction::Stop;
		Runtime.Hooks->UpdateStatus = EADSKNodeStatus::Succeeded;
		ASSERT_THAT(AreEqual(EADSKTickResult::Advanced, Runtime.Instance->Tick(0.1f)));
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ASSERT_THAT(IsFalse(Runtime.Hooks->Lines.Contains(TEXT("Start:second")), FString::Join(Runtime.Hooks->Lines, TEXT(", "))));
		ADSK_ASSERT(Runtime.Balanced());
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::InstanceStopped)));
	}

	TEST_METHOD(StopFromUpdateWhileStillRunningCancelsTheLeaf)
	{
		FRuntime Runtime;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Hooks->OnUpdate = EAction::Stop;
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ASSERT_THAT(IsTrue(Runtime.Hooks->Lines.Contains(FString::Printf(TEXT("End:first:%d"), static_cast<int32>(EADSKEndReason::Cancelled))), FString::Join(Runtime.Hooks->Lines, TEXT(", "))));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(StopFromEndDuringStopAndDuringTick)
	{
		FRuntime Runtime;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Hooks->OnEnd = EAction::Stop;
		Runtime.Instance->Stop();
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::InstanceStopped)));

		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Hooks->UpdateStatus = EADSKNodeStatus::Succeeded;
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ADSK_ASSERT(Runtime.Balanced());
		ASSERT_THAT(AreEqual(2, Runtime.Count(EADSKRecordKind::InstanceStopped)));
	}

	TEST_METHOD(StopFromACompositeStartCancelsInsteadOfFailing)
	{
		FRuntime Runtime(TEXT("<Sequence id=\"seq\"><ReenterComposite id=\"c\"><Reenter id=\"first\"/></ReenterComposite><Reenter id=\"second\"/></Sequence>"));
		Runtime.Hooks->OnCompositeStart = EAction::Stop;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(IsFalse(Runtime.Instance->IsRunning(), TEXT("still running after the stop")));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Starts, FString::Join(Runtime.Hooks->Lines, TEXT(", "))));
		ASSERT_THAT(AreEqual(0, Runtime.Ended(EADSKRecordReason::Failed), TEXT("a node ended failed although the instance was stopped")));
		ASSERT_THAT(AreEqual(2, Runtime.Ended(EADSKRecordReason::Cancelled)));
	}

	TEST_METHOD(NestedTickAndStartAreRejectedInputIsQueued)
	{
		FRuntime Runtime;
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Hooks->OnUpdate = EAction::NestedCalls;
		const uint64 Ticks = Runtime.Instance->GetTickCount();
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(Ticks + 1, Runtime.Instance->GetTickCount()));
		// IsTrue, not AreEqual: CQTest before 5.8 cannot print a TOptional.
		ASSERT_THAT(IsTrue(Runtime.Hooks->NestedTick == TOptional<EADSKTickResult>(EADSKTickResult::Reentrant), TEXT("NestedTick is not Reentrant")));
		ASSERT_THAT(IsTrue(Runtime.Hooks->NestedStart == TOptional<EADSKStartResult>(EADSKStartResult::Reentrant), TEXT("NestedStart is not Reentrant")));
		ASSERT_THAT(IsTrue(Runtime.Hooks->NestedEvent == TOptional<EADSKInputResult>(EADSKInputResult::Accepted), TEXT("NestedEvent is not Accepted")));
		TArray<FString> Queued;
		Runtime.Instance->GetQueuedEvents(Queued);
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("Poke")}, Queued));
		ASSERT_THAT(IsTrue(Runtime.Instance->IsRunning(), TEXT("the nested calls stopped the instance")));
	}
};

#endif
