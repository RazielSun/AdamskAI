// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FRandomNodesSpecPrivate
{
	FString RandomXml(const TCHAR* Tree)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"random\"><Locals><Local name=\"Plan\" type=\"Int\"/></Locals>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/></State>")
			TEXT("<State id=\"won\"/><State id=\"lost\"/></StateMachine><Tree id=\"t\">%s</Tree></Agent>"), Tree);
	}

	const TCHAR* RollThenHold = TEXT("<Sequence id=\"q\"><Roll id=\"r\" count=\"1000\" out-value=\"Plan\"/><Wait id=\"w\" duration=\"100\"/></Sequence>");

	int32 Plan(const FADSKTestRuntime& Runtime)
	{
		FADSKValue Value;
		Runtime.Instance->GetValue(TEXT("Plan"), Value);
		return Value.Int;
	}

	int32 RollWithSeed(const TCHAR* Tree, int32 Seed)
	{
		FADSKTestRuntime Runtime(RandomXml(Tree));
		Runtime.Instance->SetRandomSeed(Seed);
		Runtime.Instance->Start();
		return Plan(Runtime);
	}

	/** Ticks of Delta until the tree leaves `s`; MAX_int32 when it never does within Limit ticks. */
	int32 TicksToFinish(FADSKTestRuntime& Runtime, float Delta, int32 Limit)
	{
		for (int32 Tick = 1; Tick <= Limit; ++Tick)
		{
			Runtime.Instance->Tick(Delta);
			if (Runtime.Instance->GetActiveStatePath().Last() != TEXT("s"))
			{
				return Tick;
			}
		}
		return MAX_int32;
	}
}

using namespace FRandomNodesSpecPrivate;

TEST_CLASS_WITH_FLAGS(FRandomNodesSpec, "AdamskAI.Execution.Random",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RollStaysInRangeAndVariesWithTheSeed)
	{
		TSet<int32> Seen;
		for (int32 Seed = 1; Seed <= 16; ++Seed)
		{
			const int32 Value = RollWithSeed(TEXT("<Sequence id=\"q\"><Roll id=\"r\" count=\"3\" out-value=\"Plan\"/><Wait id=\"w\" duration=\"100\"/></Sequence>"), Seed);
			ASSERT_THAT(IsTrue(Value >= 0 && Value < 3, FString::Printf(TEXT("seed %d rolled %d"), Seed, Value)));
			Seen.Add(Value);
		}
		ASSERT_THAT(IsTrue(Seen.Num() > 1, TEXT("every seed rolled the same value")));
		ASSERT_THAT(AreEqual(0, RollWithSeed(TEXT("<Sequence id=\"q\"><Roll id=\"r\" count=\"1\" out-value=\"Plan\"/><Wait id=\"w\" duration=\"100\"/></Sequence>"), 9)));
	}

	TEST_METHOD(TheSameSeedReplaysAcrossInstancesAndRestarts)
	{
		ASSERT_THAT(AreEqual(RollWithSeed(RollThenHold, 42), RollWithSeed(RollThenHold, 42)));
		FADSKTestRuntime Runtime(RandomXml(RollThenHold));
		Runtime.Instance->SetRandomSeed(42);
		ASSERT_THAT(AreEqual(42, Runtime.Instance->GetRandomSeed()));
		Runtime.Instance->Start();
		const int32 First = Plan(Runtime);
		Runtime.Instance->Stop();
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(First, Plan(Runtime)));
	}

	TEST_METHOD(ChanceAtTheBoundsIsCertain)
	{
		for (int32 Seed = 1; Seed <= 8; ++Seed)
		{
			FADSKTestRuntime Never(RandomXml(TEXT("<Chance id=\"c\" probability=\"0\"/>")));
			Never.Instance->SetRandomSeed(Seed);
			Never.Instance->Start();
			Never.Instance->Tick(0.1f);
			ADSK_ASSERT(Never.PathIs({TEXT("m"), TEXT("lost")}));
			FADSKTestRuntime Always(RandomXml(TEXT("<Chance id=\"c\" probability=\"1\"/>")));
			Always.Instance->SetRandomSeed(Seed);
			Always.Instance->Start();
			Always.Instance->Tick(0.1f);
			ADSK_ASSERT(Always.PathIs({TEXT("m"), TEXT("won")}));
		}
	}

	TEST_METHOD(WaitDeviationStaysWithinItsBand)
	{
		TSet<int32> Seen;
		for (int32 Seed = 1; Seed <= 16; ++Seed)
		{
			FADSKTestRuntime Runtime(RandomXml(TEXT("<Wait id=\"w\" duration=\"1\" deviation=\"0.5\"/>")));
			Runtime.Instance->SetRandomSeed(Seed);
			Runtime.Instance->Start();
			// 0.5..1.5 s at 0.1 s per tick, plus one tick for the completion to dispatch.
			const int32 Ticks = TicksToFinish(Runtime, 0.1f, 40);
			ASSERT_THAT(IsTrue(Ticks >= 6 && Ticks <= 17, FString::Printf(TEXT("seed %d finished after %d ticks"), Seed, Ticks)));
			Seen.Add(Ticks);
		}
		ASSERT_THAT(IsTrue(Seen.Num() > 1, TEXT("every seed waited the same time")));
	}

	TEST_METHOD(AFixedWaitDrawsNothing)
	{
		const int32 Direct = RollWithSeed(RollThenHold, 5);
		const int32 AfterWait = RollWithSeed(TEXT("<Sequence id=\"q\"><Wait id=\"z\" duration=\"0\"/><Roll id=\"r\" count=\"1000\" out-value=\"Plan\"/><Wait id=\"w\" duration=\"100\"/></Sequence>"), 5);
		ASSERT_THAT(AreEqual(Direct, AfterWait));
	}

	TEST_METHOD(ConfigurationIsRangeChecked)
	{
		using namespace FCompilerSpecSupport;
		ASSERT_THAT(AreEqual(1, Load(RandomXml(TEXT("<Roll id=\"r\" count=\"0\" out-value=\"Plan\"/>"))).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(RandomXml(TEXT("<Chance id=\"c\" probability=\"1.5\"/>"))).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(RandomXml(TEXT("<Wait id=\"w\" deviation=\"-1\"/>"))).Count(EADSKDiagnosticCode::OutOfRange)));
		ASSERT_THAT(AreEqual(1, Load(RandomXml(TEXT("<Roll id=\"r\"/>"))).Count(EADSKDiagnosticCode::MissingRequired)));
	}

	TEST_METHOD(ARandomSelectorHoldsItsOrderInlineUpToItsChildLimit)
	{
		const auto Selector = [](int32 Children)
		{
			FString Tree = TEXT("<RandomSelector id=\"r\">");
			for (int32 Child = 0; Child < Children; ++Child) Tree += FString::Printf(TEXT("<Wait id=\"w%d\" duration=\"1\"/>"), Child);
			return RandomXml(*(Tree + TEXT("</RandomSelector>")));
		};
		const FADSKCompileResult Largest = FCompilerSpecSupport::Load(Selector(FADSKRandomSelectorMemory::MaxChildren));
		ASSERT_THAT(IsTrue(Largest.IsSuccess(), ADSKTest::FormatDiagnostics(Largest)));
		ASSERT_THAT(AreEqual(1, FCompilerSpecSupport::Load(Selector(FADSKRandomSelectorMemory::MaxChildren + 1)).Count(EADSKDiagnosticCode::BadArity)));
		FADSKTestRuntime Runtime(Selector(FADSKRandomSelectorMemory::MaxChildren));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveNodeId().ToString().StartsWith(TEXT("w")), TEXT("active node ") + Runtime.Instance->GetActiveNodeId().ToString()));
	}
};

#endif
