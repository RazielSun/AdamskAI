// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Runtime/ADSKSnapshot.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FBrainReentrySpecPrivate
{
	enum class EAction : uint8 { None, Cleanup, Reinitialize, Restart, StopThenStartFrom };

	struct FHooks
	{
		EAction OnUpdate = EAction::None;
		EAction OnEnd = EAction::None;
		TSharedPtr<const FADSKDefinition> Definition;
		FADSKSnapshot Snapshot;
		int32 Starts = 0;
		int32 Updates = 0;
		int32 Ends = 0;

		void Act(EAction Action, UADSKBrainComponent* Brain)
		{
			switch (Action)
			{
			case EAction::None: break;
			case EAction::Cleanup: Brain->Cleanup(); break;
			case EAction::Reinitialize: Brain->InitializeDefinition(Definition.ToSharedRef()); break;
			case EAction::Restart: Brain->RestartLogic(); break;
			case EAction::StopThenStartFrom: Brain->StopLogic(TEXT("test")); Brain->StartLogicFrom(Snapshot); break;
			}
		}
	};

	struct FRuntime
	{
		TSharedRef<FHooks> Hooks = MakeShared<FHooks>();
		UADSKBrainComponent* Brain = nullptr;

		FRuntime()
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			FADSKNodeDescriptor Descriptor;
			Descriptor.Name = TEXT("HostAct");
			const TSharedRef<FHooks> H = Hooks;
			Descriptor.OnStart = [H](FADSKNodeContext&) { ++H->Starts; return EADSKNodeStatus::Running; };
			Descriptor.OnUpdate = [H](FADSKNodeContext& Context, float)
			{
				++H->Updates;
				H->Act(H->OnUpdate, Context.GetHost<UADSKBrainComponent>());
				return EADSKNodeStatus::Running;
			};
			Descriptor.OnEnd = [H](FADSKNodeContext& Context, EADSKEndReason)
			{
				++H->Ends;
				H->Act(H->OnEnd, Context.GetHost<UADSKBrainComponent>());
			};
			ADSKTest::Register(*Registry, MoveTemp(Descriptor));
			Registry->Freeze();
			const FString Xml = TEXT("<Agent version=\"1\" id=\"b\"><Inputs/><Locals/><Events/>")
				TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><HostAct id=\"act\"/></Tree></Agent>");
			const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("brain_reentry.xml"), Xml, Registry);
			Hooks->Definition = Definition;
			Brain = NewObject<UADSKBrainComponent>();
			Brain->AddToRoot();
			Brain->InitializeDefinition(Definition, FADSKRecorder::Create(64));
		}

		~FRuntime()
		{
			Brain->Cleanup();
			Brain->RemoveFromRoot();
		}

		FADSKCheck Balanced() const
		{
			return Hooks->Starts == Hooks->Ends ? FADSKCheck::Pass() : FADSKCheck::Fail(FString::Printf(TEXT("%d starts but %d ends"), Hooks->Starts, Hooks->Ends));
		}
	};
}

using namespace FBrainReentrySpecPrivate;

TEST_CLASS_WITH_FLAGS(FBrainReentrySpec, "AdamskAI.Host.BrainReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CleanupFromInsideANodeDefersDestruction)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Hooks->OnUpdate = EAction::Cleanup;
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsNull(Runtime.Brain->GetInstance()));
		ASSERT_THAT(IsFalse(Runtime.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ASSERT_THAT(IsFalse(Runtime.Brain->IsRunning(), TEXT("the brain is still running")));
		ADSK_ASSERT(Runtime.Balanced());
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Updates));
	}

	TEST_METHOD(ReinitializeFromInsideANodeKeepsTheRunningInstanceAlive)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKInstance* Old = Runtime.Brain->GetInstance();
		Runtime.Hooks->OnUpdate = EAction::Reinitialize;
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsNotNull(Runtime.Brain->GetInstance()));
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance() != Old, TEXT("reinitializing kept the old instance")));
		ASSERT_THAT(IsFalse(Runtime.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(CleanupFromOnEndDuringStopLogic)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Hooks->OnEnd = EAction::Cleanup;
		Runtime.Brain->StopLogic(TEXT("test"));
		ASSERT_THAT(IsNull(Runtime.Brain->GetInstance()));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(PauseFreezesTicksAndResumeContinues)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(IsTrue(Runtime.Brain->IsRunning(), Runtime.Brain->GetLastError()));
		Runtime.Brain->PauseLogic(TEXT("test"));
		ASSERT_THAT(IsTrue(Runtime.Brain->IsPaused(), TEXT("PauseLogic did not pause the brain")));
		const uint64 Ticks = Runtime.Brain->GetInstance()->GetTickCount();
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(Ticks, Runtime.Brain->GetInstance()->GetTickCount()));
		ASSERT_THAT(AreEqual(EAILogicResuming::Continue, Runtime.Brain->ResumeLogic(TEXT("test"))));
		ASSERT_THAT(IsFalse(Runtime.Brain->IsPaused(), TEXT("still paused after ResumeLogic")));
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(Ticks + 1, Runtime.Brain->GetInstance()->GetTickCount()));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Updates));
	}

	TEST_METHOD(RestartStartsANewGeneration)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const uint32 Generation = Runtime.Brain->GetInstance()->GetGeneration();
		Runtime.Brain->PauseLogic(TEXT("test"));
		Runtime.Brain->RestartLogic();
		ASSERT_THAT(IsTrue(Runtime.Brain->IsRunning(), Runtime.Brain->GetLastError()));
		ASSERT_THAT(IsFalse(Runtime.Brain->IsPaused(), TEXT("still paused after RestartLogic")));
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->GetGeneration() > Generation, FString::Printf(TEXT("generation %u, was %u"), Runtime.Brain->GetInstance()->GetGeneration(), Generation)));
		ASSERT_THAT(AreEqual(2, Runtime.Hooks->Starts));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Ends));
	}

	TEST_METHOD(RestartFromInsideANodeRunsAfterTheTick)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Hooks->OnUpdate = EAction::Restart;
		Runtime.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(Runtime.Brain->IsRunning(), Runtime.Brain->GetLastError()));
		ASSERT_THAT(AreEqual(2, Runtime.Hooks->Starts));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Ends));
	}

	/** The stop is still draining inside the tick, so the start waits for it, and keeps its snapshot until then. */
	TEST_METHOD(StartLogicFromDuringTheStopsDrainRestoresAfterTheTick)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Brain->TickBrain(5.f);
		ASSERT_THAT(IsTrue(Runtime.Brain->CaptureSnapshot(Runtime.Hooks->Snapshot), TEXT("capture refused")));
		Runtime.Hooks->OnUpdate = EAction::StopThenStartFrom;
		Runtime.Brain->TickBrain(1.f);
		ASSERT_THAT(IsTrue(Runtime.Brain->IsRunning(), Runtime.Brain->GetLastError()));
		ASSERT_THAT(AreEqual(2, Runtime.Hooks->Starts));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Ends));
		ASSERT_THAT(IsTrue(Runtime.Brain->GetRecorder()->Snapshot().ContainsByPredicate([](const FADSKRecord& Record)
			{ return Record.Kind == EADSKRecordKind::InstanceStarted && Record.Reason == EADSKRecordReason::Restored; }), TEXT("no restored start")));
		// The clock is the snapshot's, not the six seconds the stopped run had reached.
		FADSKSnapshot After;
		ASSERT_THAT(IsTrue(Runtime.Brain->CaptureSnapshot(After) && After.Clock == Runtime.Hooks->Snapshot.Clock,
			FString::Printf(TEXT("clock %f, snapshot %f"), After.Clock, Runtime.Hooks->Snapshot.Clock)));
	}
};

#endif
