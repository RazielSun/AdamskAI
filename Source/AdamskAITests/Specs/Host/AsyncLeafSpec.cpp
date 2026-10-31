// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Nodes/ADSKAsyncLeaf.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FAsyncLeafSpecPrivate
{
	struct FHooks
	{
		TOptional<FADSKAsyncTicket> Ticket;
		EADSKNodeStatus StartStatus = EADSKNodeStatus::Running;
		int32 Cancels = 0;
		int32 BeforeEnds = 0;
		int32 CancelsBeforeBeforeEnd = INDEX_NONE;
	};

	struct FRuntime
	{
		TSharedRef<FHooks> Hooks = MakeShared<FHooks>();
		UADSKBrainComponent* Brain = nullptr;
		TSharedPtr<const FADSKDefinition> Definition;

		FRuntime()
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKBuiltinNodes::RegisterAll(*Registry);
			const TSharedRef<FHooks> H = Hooks;
			FADSKNodeDescriptor Fetch;
			Fetch.Name = TEXT("Fetch");
			Fetch.Ports.Add({TEXT("value"), EADSKValueType::Int, EADSKPortDirection::Out});
			ADSK::MakeAsyncLeaf(Fetch, TEXT("Fetch"), [H](FADSKNodeContext&, const FADSKAsyncTicket& Ticket)
			{
				H->Ticket = Ticket;
				Ticket.SetCancel([H] { ++H->Cancels; });
				return H->StartStatus;
			}, 0, [H](FADSKNodeContext&, EADSKEndReason)
			{
				H->CancelsBeforeBeforeEnd = H->Cancels;
				++H->BeforeEnds;
			});
			ADSKTest::Register(*Registry, MoveTemp(Fetch));
			Registry->Freeze();
			Definition = ADSKTest::LoadOrStandIn(TEXT("async_leaf.xml"),
				TEXT("<Agent version=\"1\" id=\"a\"><Locals><Local name=\"L\" type=\"Int\"/></Locals><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><Sequence id=\"q\"><Fetch id=\"f\" out-value=\"L\"/><Wait id=\"w\" duration=\"100\"/></Sequence></Tree></Agent>"), Registry);
			Brain = NewObject<UADSKBrainComponent>();
			Brain->AddToRoot();
			Brain->InitializeDefinition(Definition.ToSharedRef());
		}

		~FRuntime()
		{
			Brain->Cleanup();
			Brain->RemoveFromRoot();
		}
	};
}

using namespace FAsyncLeafSpecPrivate;

TEST_CLASS_WITH_FLAGS(FAsyncLeafSpec, "AdamskAI.Host.AsyncLeaf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CompletionWritesThePayloadOnTheNextTick)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Accepted, Runtime.Hooks->Ticket->Complete(EADSKNodeStatus::Succeeded, FADSKValue::MakeInt(7))));
		Runtime.Brain->TickBrain(0.1f);
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->GetValue(TEXT("L"), Value), TEXT("no slot L")));
		ASSERT_THAT(AreEqual(7, Value.Int));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->BeforeEnds));
	}

	TEST_METHOD(EndingFirstRunsBeforeEndThenTheCancelOnce)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		Runtime.Brain->StopLogic();
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->BeforeEnds));
		ASSERT_THAT(AreEqual(0, Runtime.Hooks->CancelsBeforeBeforeEnd));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Runtime.Hooks->Ticket->Complete(EADSKNodeStatus::Succeeded)));
	}

	TEST_METHOD(ATicketFromBeforeCleanupCannotCompleteTheNextLifetimesLeaf)
	{
		FRuntime Runtime;
		Runtime.Brain->StartLogic();
		const FADSKAsyncTicket Old = Runtime.Hooks->Ticket.GetValue();
		Runtime.Brain->Cleanup();
		Runtime.Brain->InitializeDefinition(Runtime.Definition.ToSharedRef());
		Runtime.Brain->StartLogic();
		ASSERT_THAT(IsTrue(Runtime.Brain->IsRunning(), Runtime.Brain->GetLastError()));
		const int32 CancelsBefore = Runtime.Hooks->Cancels;
		ASSERT_THAT(AreEqual(EADSKAsyncCompletion::Stale, Old.Complete(EADSKNodeStatus::Succeeded, FADSKValue::MakeInt(7))));
		Runtime.Brain->TickBrain(0.1f);
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->GetValue(TEXT("L"), Value), TEXT("no slot L")));
		ASSERT_THAT(AreEqual(0, Value.Int));
		ASSERT_THAT(AreEqual(CancelsBefore, Runtime.Hooks->Cancels));
	}

	TEST_METHOD(AnUnhandledCompletionWarnsOncePerHold)
	{
		FRuntime Runtime;
		Runtime.Hooks->StartStatus = EADSKNodeStatus::Failed;
		Runtime.Brain->StartLogic();
		TestRunner->AddExpectedMessagePlain(TEXT("holding until an event"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		for (int32 Tick = 0; Tick < 4; ++Tick) { Runtime.Brain->TickBrain(0.1f); }
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->IsHolding(), TEXT("not holding in ") + ADSKTest::FormatPath(*Runtime.Brain->GetInstance())));
	}

	TEST_METHOD(AnImmediateFailureClosesTheEntry)
	{
		FRuntime Runtime;
		Runtime.Hooks->StartStatus = EADSKNodeStatus::Failed;
		Runtime.Brain->StartLogic();
		ASSERT_THAT(AreEqual(0, Runtime.Brain->GetAsyncOps().GetOps().Num()));
		ASSERT_THAT(AreEqual(1, Runtime.Hooks->Cancels));
	}
};

#endif
