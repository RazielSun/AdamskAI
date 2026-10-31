// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKAsyncOps.h"
#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FAsyncOpsSpecPrivate
{
	/** What the test-only async leaf does; shared by value with its node callbacks. */
	struct FHooks
	{
		bool bFailAfterSetCancel = false;
		bool bSucceedAfterSetCancel = false;
		bool bStopInUpdate = false;
		bool bReplaceInUpdate = false;
		bool bCancelCompletes = false;
		int32 Cancels = 0;
		int32 Reads = 0;
		int32 ReadValue = 0;
		FADSKActivationHandle LastHandle;
		TSharedPtr<const FADSKDefinition> Definition;
	};

	struct FRuntime
	{
		TSharedRef<FHooks> Hooks = MakeShared<FHooks>();
		UADSKBrainComponent* Brain = nullptr;
		TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(256);

		explicit FRuntime(const TCHAR* Tree = TEXT("<Sequence id=\"seq\"><AsyncProbe id=\"op\" out-result=\"L\"/><AsyncRead id=\"rd\" in-value=\"L\"/></Sequence>"))
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			const TSharedRef<FHooks> H = Hooks;

			FADSKNodeDescriptor Probe;
			Probe.Name = TEXT("AsyncProbe");
			Probe.Ports.Add({TEXT("result"), EADSKValueType::Int, EADSKPortDirection::Out});
			Probe.OnStart = [H](FADSKNodeContext& Context)
			{
				UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
				const FADSKActivationHandle Handle = Context.GetHandle();
				if (!Brain->GetAsyncOps().Begin(Handle, TEXT("Probe"), Context.GetNodeId(), 0.0)) { return EADSKNodeStatus::Failed; }
				Context.MarkPartiallyEntered();
				H->LastHandle = Handle;
				Brain->GetAsyncOps().SetCancel(Handle, [H, Weak = TWeakObjectPtr<UADSKBrainComponent>(Brain), Handle]()
				{
					++H->Cancels;
					if (H->bCancelCompletes) { if (UADSKBrainComponent* B = Weak.Get()) { B->CompleteAsync(Handle, EADSKNodeStatus::Failed); } }
				});
				return H->bFailAfterSetCancel ? EADSKNodeStatus::Failed : H->bSucceedAfterSetCancel ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Running;
			};
			Probe.OnUpdate = [H](FADSKNodeContext& Context, float)
			{
				UADSKBrainComponent* Brain = Context.GetHost<UADSKBrainComponent>();
				if (H->bStopInUpdate) { H->bStopInUpdate = false; Brain->StopLogic(); }
				if (H->bReplaceInUpdate)
				{
					H->bReplaceInUpdate = false;
					FString Error;
					Brain->ReplaceDefinition(H->Definition.ToSharedRef(), true, Error);
				}
				return EADSKNodeStatus::Running;
			};
			Probe.OnEnd = [](FADSKNodeContext& Context, EADSKEndReason Reason)
			{
				Context.GetHost<UADSKBrainComponent>()->GetAsyncOps().End(Context, Reason, 0);
			};
			ADSKTest::Register(*Registry, MoveTemp(Probe));

			FADSKNodeDescriptor Read;
			Read.Name = TEXT("AsyncRead");
			Read.Ports.Add({TEXT("value"), EADSKValueType::Int, EADSKPortDirection::In});
			Read.OnStart = [H](FADSKNodeContext& Context)
			{
				++H->Reads;
				Context.ReadInt(0, H->ReadValue);
				return EADSKNodeStatus::Running;
			};
			ADSKTest::Register(*Registry, MoveTemp(Read));
			Registry->Freeze();

			const FString Xml = FString::Printf(TEXT("<Agent version=\"1\" id=\"a\"><Inputs/><Locals><Local name=\"L\" type=\"Int\"/></Locals><Events><Event name=\"Go\"/></Events>")
				TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"Go\" goto=\"d\"/></State><State id=\"d\" tree=\"dt\"/></StateMachine>")
				TEXT("<Tree id=\"t\">%s</Tree><Tree id=\"dt\"><Wait id=\"dw\" duration=\"100\"/></Tree></Agent>"), Tree);
			const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("async_ops.xml"), Xml, Registry);
			Hooks->Definition = Definition;
			Brain = NewObject<UADSKBrainComponent>();
			Brain->AddToRoot();
			Brain->InitializeDefinition(Definition, Recorder);
		}

		~FRuntime()
		{
			Brain->Cleanup();
			Brain->RemoveFromRoot();
		}

		FADSKAsyncOps& Ops() const { return Brain->GetAsyncOps(); }
		int32 Count(EADSKRecordKind Kind) const { return ADSKTest::CountRecords(Recorder.Get(), Kind); }
	};

	const FADSKActivationHandle FakeA{7, 1};
	const FADSKActivationHandle FakeB{7, 2};
}

using namespace FAsyncOpsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FAsyncOpsSpec, "AdamskAI.Host.AsyncOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	void ExpectEnsures()
	{
		// An ensure logs a "Handled ensure" banner, its condition and a stack as separate messages.
		for (const TCHAR* Line : {TEXT("Handled ensure"), TEXT("Ensure condition failed"), TEXT("Stack:"), TEXT("[Callstack]")})
		{
			TestRunner->AddExpectedMessagePlain(Line, ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		}
		TestRunner->AddExpectedMessagePlain(TEXT(""), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Exact, /*Occurrences=*/0);
		TestRunner->AddExpectedMessagePlain(TEXT("async op"), ELogVerbosity::Verbose, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
		TestRunner->AddExpectedMessagePlain(TEXT("Script Stack"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/0);
	}

	TEST_METHOD(BeginRejectsInvalidAndDuplicateHandles)
	{
		ExpectEnsures();
		FADSKAsyncOps Ops;
		ASSERT_THAT(IsFalse(Ops.Begin({}, TEXT("K"), TEXT("n"), 0.0), TEXT("an invalid handle began an op")));
		ASSERT_THAT(IsTrue(Ops.Begin(FakeA, TEXT("K"), TEXT("n"), 1.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsFalse(Ops.Begin(FakeA, TEXT("K"), TEXT("n"), 2.0), TEXT("FakeA began twice")));
		ASSERT_THAT(AreEqual(1, Ops.GetOps().Num()));
		ASSERT_THAT(IsTrue(Ops.IsPending(FakeA), TEXT("FakeA is not pending")));
		ASSERT_THAT(IsFalse(Ops.IsPending(FakeB), TEXT("FakeB is pending")));
		ASSERT_THAT(IsFalse(Ops.SetCancel(FakeB, [] {}), TEXT("a cancel was set for an unknown op")));
	}

	TEST_METHOD(CancelAllReportsEveryDetachedEntryButCancelsOnlyUnfinished)
	{
		FADSKAsyncOps Ops;
		int32 Cancels = 0;
		ASSERT_THAT(IsTrue(Ops.Begin(FakeA, TEXT("K"), TEXT("a"), 0.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsTrue(Ops.Begin(FakeB, TEXT("K"), TEXT("b"), 0.0), TEXT("FakeB did not begin")));
		ASSERT_THAT(IsTrue(Ops.SetCancel(FakeA, [&Cancels] { ++Cancels; }), TEXT("no cancel was set for FakeA")));
		ASSERT_THAT(IsTrue(Ops.SetCancel(FakeB, [&Cancels] { ++Cancels; }), TEXT("no cancel was set for FakeB")));
		Ops.Find(FakeB)->Result = EADSKNodeStatus::Succeeded;
		ASSERT_THAT(IsFalse(Ops.SetCancel(FakeB, [] {}), TEXT("a cancel was set for a finished op")));
		ASSERT_THAT(IsFalse(Ops.IsPending(FakeB), TEXT("a finished op is pending")));
		ASSERT_THAT(AreEqual(2, Ops.CancelAll()));
		ASSERT_THAT(AreEqual(1, Cancels));
		ASSERT_THAT(AreEqual(0, Ops.GetOps().Num()));
		ASSERT_THAT(AreEqual(0, Ops.CancelAll()));
	}

	TEST_METHOD(FindByMoveMatchesOnlyValidRequests)
	{
		FADSKAsyncOps Ops;
		ASSERT_THAT(IsTrue(Ops.Begin(FakeA, TEXT("K"), TEXT("a"), 0.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsNull(Ops.FindByMove(FAIRequestID::InvalidRequest)));
		Ops.Find(FakeA)->MoveRequest = FAIRequestID(5);
		ASSERT_THAT(IsNotNull(Ops.FindByMove(FAIRequestID(5))));
		ASSERT_THAT(IsNull(Ops.FindByMove(FAIRequestID(6))));
		ASSERT_THAT(IsNull(Ops.FindByMove(FAIRequestID::InvalidRequest)));
	}

	TEST_METHOD(AcceptedPayloadReachesTheNextSiblingInTheSameTick)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Handle = Runtime.Hooks->LastHandle;
		ASSERT_THAT(AreEqual(1, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(Handle), TEXT("the leaf's op is not pending")));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Accepted, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Succeeded, FADSKValue::MakeInt(7))));
		ASSERT_THAT(IsFalse(Runtime.Ops().IsPending(Handle), TEXT("the completed op is still pending")));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Reads));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Reads));
		ASSERT_THAT(AreEqual(7, Runtime.Hooks->ReadValue));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Cancels));
	}

	TEST_METHOD(FailedCompletionWritesNoPayloadAndDoesNotCancel)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Handle = Runtime.Hooks->LastHandle;
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Accepted, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Failed, FADSKValue::MakeInt(7))));
		Runtime.Brain->TickBrain(0.1f);
		FADSKValue Local;
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->GetValue(TEXT("L"), Local), TEXT("no slot L")));
		ASSERT_THAT(AreEqual(0, Local.Int));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Reads));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(CompletionAfterEndIsStaleAndKeepsTheCoreTrace)
	{
		FRuntime Runtime(TEXT("<Sequence id=\"seq\"><AsyncProbe id=\"op\" out-result=\"L\"/><Wait id=\"w\" duration=\"100\"/></Sequence>"));
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Handle = Runtime.Hooks->LastHandle;
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Accepted, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Succeeded, FADSKValue::MakeInt(1))));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Failed)));
		ASSERT_THAT(AreEqual<uint32>(1, Runtime.Brain->GetStaleAsyncCompletions()));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::ResultDuplicate) >= 1, FString::Printf(TEXT("%d result-duplicate records"), Runtime.Count(EADSKRecordKind::ResultDuplicate))));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual<uint32>(2, Runtime.Brain->GetStaleAsyncCompletions()));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::ResultDuplicate) >= 2, FString::Printf(TEXT("%d result-duplicate records"), Runtime.Count(EADSKRecordKind::ResultDuplicate))));
		const FADSKActivationHandle Unknown{Handle.Generation, Handle.ActivationId + 100};
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Brain->CompleteAsync(Unknown, EADSKNodeStatus::Succeeded)));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::ResultStale) >= 1, FString::Printf(TEXT("%d result-stale records"), Runtime.Count(EADSKRecordKind::ResultStale))));
	}

	TEST_METHOD(FullQueueStillAcceptsTheRunningLeafsCompletion)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Handle = Runtime.Hooks->LastHandle;
		for (int32 Index = 0; Index < ADSK::ActionResultCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Brain->GetInstance()->PostActionResult({99, 1u + Index}, EADSKNodeStatus::Succeeded)));
		}
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Accepted, Runtime.Brain->CompleteAsync(Handle, EADSKNodeStatus::Succeeded)));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultAccepted)));
		ASSERT_THAT(AreEqual(ADSK::ActionResultCapacity, Runtime.Count(EADSKRecordKind::ResultStale)));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Cancels));
	}

	TEST_METHOD(QueueFullLeavesAForeignOperationPendingAndCancellable)
	{
		ExpectEnsures();
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		// An entry that is not the running leaf gets no reserved slot.
		int32 ForeignCancels = 0;
		ASSERT_THAT(IsTrue(Runtime.Ops().Begin(FakeA, TEXT("K"), TEXT("x"), 0.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsTrue(Runtime.Ops().SetCancel(FakeA, [&ForeignCancels] { ++ForeignCancels; }), TEXT("no cancel was set for FakeA")));
		for (int32 Index = 0; Index < ADSK::ActionResultCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Brain->GetInstance()->PostActionResult({99, 1u + Index}, EADSKNodeStatus::Succeeded)));
		}
		TestRunner->AddExpectedMessagePlain(TEXT("result queue full"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, /*Occurrences=*/1);
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::QueueFull, Runtime.Brain->CompleteAsync(FakeA, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(FakeA), TEXT("FakeA is no longer pending")));
		Runtime.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, ForeignCancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(CompletingWithoutARunningInstanceReportsNotRunning)
	{
		ExpectEnsures();
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Handle = Runtime.Hooks->LastHandle;
		Runtime.Ops().Begin(FakeA, TEXT("K"), TEXT("x"), 0.0);
		Runtime.Brain->GetInstance()->Stop();
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::NotRunning, Runtime.Brain->CompleteAsync(FakeA, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(FakeA), TEXT("FakeA is no longer pending")));
		ASSERT_THAT(AreEqual<uint32>(0, Runtime.Brain->GetStaleAsyncCompletions()));
		(void)Handle;
	}

	TEST_METHOD(PartialStartFailureCancelsTheOperation)
	{
		FRuntime Runtime;
		Runtime.Hooks->bFailAfterSetCancel = true;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(SynchronousSuccessDoesNotCancel)
	{
		FRuntime Runtime;
		Runtime.Hooks->bSucceedAfterSetCancel = true;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Reads));
	}

	TEST_METHOD(StateExitCancelsTheOperation)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Brain->GetInstance()->EnqueueEvent(TEXT("Go"))));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(StopLogicCancelsOnceAndLeaksNothing)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(StopLogicFromACallbackDrainsAfterTheStopCompletes)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Hooks->bStopInUpdate = true;
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsFalse(Runtime.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(ReplacementCancelsOldEntriesAndStartsTheNewOneAfterward)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Old = Runtime.Hooks->LastHandle;
		FString Error;
		ASSERT_THAT(IsTrue(Runtime.Brain->ReplaceDefinition(Runtime.Hooks->Definition.ToSharedRef(), true, Error), Error));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(1, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(IsTrue(Runtime.Hooks->LastHandle != Old, TEXT("the leaf did not restart")));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(Runtime.Hooks->LastHandle), TEXT("the new leaf's op is not pending")));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Brain->CompleteAsync(Old, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(Runtime.Hooks->LastHandle), TEXT("the stale completion ended the new leaf's op")));
	}

	TEST_METHOD(ReplacementFromACallbackDefersTheRestartUntilTheDrain)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Old = Runtime.Hooks->LastHandle;
		Runtime.Hooks->bReplaceInUpdate = true;
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(IsTrue(Runtime.Brain->IsADSKRunning(), Runtime.Brain->GetLastError()));
		ASSERT_THAT(AreEqual(1, Runtime.Ops().GetOps().Num()));
		ASSERT_THAT(IsTrue(Runtime.Hooks->LastHandle != Old, TEXT("the leaf did not restart")));
		ASSERT_THAT(IsTrue(Runtime.Ops().IsPending(Runtime.Hooks->LastHandle), TEXT("the new leaf's op is not pending")));
	}

	TEST_METHOD(ReplacementWithoutRestartMakesOldCompletionStale)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKActivationHandle Old = Runtime.Hooks->LastHandle;
		FString Error;
		ASSERT_THAT(IsTrue(Runtime.Brain->ReplaceDefinition(Runtime.Hooks->Definition.ToSharedRef(), false, Error), Error));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Brain->CompleteAsync(Old, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual<uint32>(1, Runtime.Brain->GetStaleAsyncCompletions()));
	}

	TEST_METHOD(CleanupCancelsTheOperation)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Brain->Cleanup();
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(SynchronousCancelCompletionBecomesStale)
	{
		FRuntime Runtime;
		Runtime.Hooks->bCancelCompletes = true;
		Runtime.Brain->StartLogic();
		Runtime.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual<uint32>(1, Runtime.Brain->GetStaleAsyncCompletions()));
	}

	TEST_METHOD(LeakedEntriesAreCancelledAndCounted)
	{
		ExpectEnsures();
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		int32 Leaked = 0;
		ASSERT_THAT(IsTrue(Runtime.Ops().Begin(FakeA, TEXT("Leak"), TEXT("lk"), 0.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsTrue(Runtime.Ops().SetCancel(FakeA, [&Leaked] { ++Leaked; }), TEXT("no cancel was set for FakeA")));
		Runtime.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, Leaked));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(0, Runtime.Ops().GetOps().Num()));
	}

	TEST_METHOD(SecondMoveBindingIsRejected)
	{
		ExpectEnsures();
		FRuntime Runtime;
		ASSERT_THAT(IsTrue(Runtime.Ops().Begin(FakeA, TEXT("K"), TEXT("a"), 0.0), TEXT("FakeA did not begin")));
		ASSERT_THAT(IsTrue(Runtime.Ops().Begin(FakeB, TEXT("K"), TEXT("b"), 0.0), TEXT("FakeB did not begin")));
		ASSERT_THAT(IsFalse(Runtime.Brain->BindMove(FakeA, FAIRequestID::InvalidRequest), TEXT("an invalid request id was bound")));
		ASSERT_THAT(IsFalse(Runtime.Brain->BindMove({7, 3}, FAIRequestID(40)), TEXT("a move was bound to an unknown handle")));
		ASSERT_THAT(IsTrue(Runtime.Brain->BindMove(FakeA, FAIRequestID(41)), TEXT("the move was not bound to FakeA")));
		ASSERT_THAT(IsFalse(Runtime.Brain->BindMove(FakeB, FAIRequestID(42)), TEXT("FakeB bound a second move")));
		ASSERT_THAT(IsFalse(Runtime.Brain->BindMove(FakeA, FAIRequestID(43)), TEXT("FakeA bound its move twice")));
		ASSERT_THAT(IsNotNull(Runtime.Ops().FindByMove(FAIRequestID(41))));
	}
};

#endif
