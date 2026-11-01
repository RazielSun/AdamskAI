// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FMemoryArenaSpecPrivate
{
	FString Agent(const FString& Root)
	{
		return TEXT("<Agent version=\"1\" id=\"arena\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"tree.success\" goto=\"s\"/></State></StateMachine><Tree id=\"t\">") + Root + TEXT("</Tree></Agent>");
	}
}

using namespace FMemoryArenaSpecPrivate;

TEST_CLASS_WITH_FLAGS(FMemoryArenaSpec, "AdamskAI.Execution.MemoryArena",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheArenaHoldsOneSlicePerNodeWithMemory)
	{
		const int32 WaitBytes = FADSKWaitMemory::StaticStruct()->GetStructureSize();
		FADSKTestRuntime Two(Agent(TEXT("<Sequence id=\"q\"><Wait id=\"a\"/><Wait id=\"b\"/></Sequence>")));
		ASSERT_THAT(IsTrue(Two.Instance->GetNodeMemoryBytes() >= 2 * WaitBytes, FString::Printf(TEXT("%lld bytes for two waits of %d"), static_cast<int64>(Two.Instance->GetNodeMemoryBytes()), WaitBytes)));
		ASSERT_THAT(IsTrue(Two.Instance->GetNodeMemoryBytes() <= 2 * WaitBytes + FADSKWaitMemory::StaticStruct()->GetMinAlignment(), FString::Printf(TEXT("%lld bytes for two waits of %d"), static_cast<int64>(Two.Instance->GetNodeMemoryBytes()), WaitBytes)));
		FADSKTestRuntime None(Agent(TEXT("<Sequence id=\"q\"><Emit id=\"e\" event=\"tree.success\"/></Sequence>")).Replace(TEXT("<Emit id=\"e\" event=\"tree.success\"/>"), TEXT("<Roll id=\"r\" out-value=\"N\"/>"))
			.Replace(TEXT("<StateMachine"), TEXT("<Locals><Local name=\"N\" type=\"Int\"/></Locals><StateMachine")));
		ASSERT_THAT(IsTrue(None.Instance->GetNodeMemoryBytes() < WaitBytes, FString::Printf(TEXT("%lld bytes without a wait"), static_cast<int64>(None.Instance->GetNodeMemoryBytes()))));
	}

	TEST_METHOD(EveryActivationStartsFromFreshMemoryAcrossRestarts)
	{
		// A stale or undestroyed memory slice would show in the Wait's timing or leak the selector's array.
		FADSKTestRuntime Runtime(Agent(TEXT("<Sequence id=\"q\"><RandomSelector id=\"r\"><Chance id=\"c\" probability=\"0\"/><Wait id=\"w\" duration=\"0.3\"/></RandomSelector></Sequence>")));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		int32 WaitStarts = 0;
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			const FName Before = Runtime.Instance->GetActiveNodeId();
			Runtime.Instance->Tick(0.1f);
			WaitStarts += Before != FName(TEXT("w")) && Runtime.Instance->GetActiveNodeId() == FName(TEXT("w")) ? 1 : 0;
		}
		// 0.3 s waits back to back over 4 s, with the loop's own ticks in between: about ten, never a stuck one.
		ASSERT_THAT(IsTrue(WaitStarts >= 8 && WaitStarts <= 14, FString::Printf(TEXT("%d wait starts"), WaitStarts)));
	}
};

#endif
