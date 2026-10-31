// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FResultQueueSpecPrivate
{
	struct FProbe
	{
		FADSKInstance* Instance = nullptr;
		TArray<FADSKActivationHandle> Handles;
		TArray<FString> Ends;
	};

	/** `Pending` waits for a posted result; `Instant` posts its own result from inside OnStart. */
	FADSKNodeDescriptor MakeLeaf(const TCHAR* Name, const TSharedRef<FProbe>& Probe, bool bPostsFromStart)
	{
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.OnStart = [Probe, bPostsFromStart](FADSKNodeContext& Context)
		{
			Probe->Handles.Add(Context.GetHandle());
			if (bPostsFromStart)
			{
				Probe->Instance->PostActionResult(Context.GetHandle(), EADSKNodeStatus::Succeeded);
			}
			return EADSKNodeStatus::Running;
		};
		Descriptor.OnEnd = [Probe](FADSKNodeContext& Context, EADSKEndReason Reason)
		{
			Probe->Ends.Add(FString::Printf(TEXT("%s:%d"), *Context.GetNodeId().ToString(), static_cast<int32>(Reason)));
		};
		return Descriptor;
	}

	struct FRuntime
	{
		TSharedRef<FProbe> Probe = MakeShared<FProbe>();
		TSharedPtr<FADSKRecorder> Recorder = FADSKRecorder::Create(512);
		TUniquePtr<FADSKInstance> Instance;

		FRuntime()
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			ADSKTest::Register(*Registry, MakeLeaf(TEXT("Pending"), Probe, false));
			ADSKTest::Register(*Registry, MakeLeaf(TEXT("Instant"), Probe, true));
			Registry->Freeze();
			const FString Xml = TEXT("<Agent version=\"1\" id=\"q\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><Sequence id=\"seq\"><Pending id=\"first\"/><Instant id=\"second\"/></Sequence></Tree></Agent>");
			Instance = MakeUnique<FADSKInstance>(ADSKTest::LoadOrStandIn(TEXT("result_queue.xml"), Xml, Registry), Recorder);
			Probe->Instance = Instance.Get();
			ADSKTest::Expect(ADSKTest::Start(*Instance));
		}

		int32 Count(EADSKRecordKind Kind) const { return ADSKTest::CountRecords(Recorder.Get(), Kind); }
	};
}

using namespace FResultQueueSpecPrivate;

TEST_CLASS_WITH_FLAGS(FResultQueueSpec, "AdamskAI.Execution.Lifecycle.ResultQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ResultPostedWhileResultsApplyIsClassifiedNextTick)
	{
		FRuntime Runtime;
		const FADSKActivationHandle First = Runtime.Instance->GetActiveLeafHandle();
		Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded);
		// Accepting `first` starts `second`, which posts its own result during the same ApplyResults pass.
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultAccepted)));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle() == Runtime.Probe->Handles.Last(), TEXT("second is not the running leaf")));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(2, Runtime.Count(EADSKRecordKind::ResultAccepted)));
		ASSERT_THAT(IsTrue(Runtime.Probe->Ends.Contains(FString::Printf(TEXT("second:%d"), static_cast<int32>(EADSKEndReason::Succeeded))), TEXT("ends: ") + FString::Join(Runtime.Probe->Ends, TEXT(", "))));
		ASSERT_THAT(IsFalse(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("a leaf is still running")));
	}

	TEST_METHOD(FullQueueReservesOneSlotForTheRunningLeaf)
	{
		FRuntime Runtime;
		const FADSKActivationHandle First = Runtime.Instance->GetActiveLeafHandle();
		for (int32 Index = 0; Index < ADSK::ActionResultCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult({99, 1u + Index}, EADSKNodeStatus::Succeeded)));
		}
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->PostActionResult({98, 1}, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded)));
		ASSERT_THAT(AreEqual(EADSKInputResult::QueueFull, Runtime.Instance->PostActionResult(First, EADSKNodeStatus::Succeeded)));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::ResultAccepted)));
		ASSERT_THAT(AreEqual(ADSK::ActionResultCapacity, Runtime.Count(EADSKRecordKind::ResultStale)));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle() != First, TEXT("the reserved result did not end the first leaf")));
	}
};

#endif
