// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FExecutionSelectorSpecPrivate
{
	FString SelectorXml(const FString& Body)
	{
		return TEXT("<Agent version=\"1\" id=\"selector\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"1,2,3\"/>")
			TEXT("<Input name=\"Ok\" type=\"Bool\" default=\"true\"/><Input name=\"Off\" type=\"Bool\" default=\"false\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"ok\"/>")
			TEXT("<On event=\"tree.failure\" goto=\"bad\"/></State><State id=\"ok\"/><State id=\"bad\"/></StateMachine>")
			TEXT("<Tree id=\"t\">") + Body + TEXT("</Tree></Agent>");
	}

	FString Fail(const TCHAR* Id) { return FString::Printf(TEXT("<AcquireResource id=\"%s\"/>"), Id); }
	FString Move(const TCHAR* Id) { return FString::Printf(TEXT("<MoveTo id=\"%s\" in-goal=\"Goal\" acceptanceRadius=\"1\"/>"), Id); }
	FString Pass(const TCHAR* Id) { return FString::Printf(TEXT("<Wait id=\"%s\" duration=\"0\"/>"), Id); }

	bool InState(const FADSKTestRuntime& Runtime, const TCHAR* Id)
	{
		const TArray<FString> Path = Runtime.Instance->GetActiveStatePath();
		return !Path.IsEmpty() && Path.Last() == Id;
	}

	void Settle(FADSKTestRuntime& Runtime)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Runtime.Instance->Tick(0.f);
		}
	}
}

using namespace FExecutionSelectorSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionSelectorSpec, "AdamskAI.Execution.Selector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(FallbackToSecond)
	{
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\">") + Fail(TEXT("a")) + Move(TEXT("b")) + TEXT("</Selector>")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:AcquireResource:a:Failed")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:b")));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("no leaf is running in ") + ADSKTest::FormatPath(*Runtime.Instance)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(AllFailedFails)
	{
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\">") + Fail(TEXT("a")) + Fail(TEXT("b")) + TEXT("</Selector>")));
		Runtime.Instance->Start();
		Settle(Runtime);
		ASSERT_THAT(IsTrue(InState(Runtime, TEXT("bad")), TEXT("not in bad: ") + ADSKTest::FormatPath(*Runtime.Instance)));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:AcquireResource:b")));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(FirstSuccessShortCircuits)
	{
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\">") + Pass(TEXT("a")) + Move(TEXT("b")) + TEXT("</Selector>")));
		Runtime.Instance->Start();
		Settle(Runtime);
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo:b")));
		ASSERT_THAT(IsTrue(InState(Runtime, TEXT("ok")), TEXT("not in ok: ") + ADSKTest::FormatPath(*Runtime.Instance)));
	}

	TEST_METHOD(BudgetDeferral)
	{
		FString Children;
		for (int32 Index = 0; Index < 70; ++Index)
		{
			Children += FString::Printf(TEXT("<AcquireResource id=\"f%d\"/>"), Index);
		}
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\">") + Children + Pass(TEXT("last")) + TEXT("</Selector>")), 4096);
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(ADSK::ProcedureOperationBudget, Runtime.Count(EADSKRecordKind::NodeStarted)));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::BudgetDeferred)));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:AcquireResource:f63")));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:AcquireResource:f69")));
		Settle(Runtime);
		ASSERT_THAT(IsTrue(InState(Runtime, TEXT("ok")), TEXT("not in ok: ") + ADSKTest::FormatPath(*Runtime.Instance)));
	}

	TEST_METHOD(ADeferralMadeWhileADeferredStartIsDroppedSurvives)
	{
		const auto Failing = [](const TCHAR* Prefix)
		{
			FString Children;
			for (int32 Index = 0; Index < 70; ++Index)
			{
				Children += FString::Printf(TEXT("<AcquireResource id=\"%s%d\"/>"), Prefix, Index);
			}
			return Children;
		};
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\"><Guard id=\"g\" in-condition=\"Ok\"><Selector id=\"first\">")
			+ Failing(TEXT("a")) + Move(TEXT("am")) + TEXT("</Selector></Guard><Selector id=\"fallback\">") + Failing(TEXT("b"))
			+ Move(TEXT("bm")) + TEXT("</Selector></Selector>")), 8192);
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::BudgetDeferred)));
		// The guard fails while a start is deferred under it; the fallback's own starts run out of budget again.
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:AcquireResource:b0")));
		ASSERT_THAT(AreEqual(2, Runtime.Count(EADSKRecordKind::BudgetDeferred), Runtime.Trace()));
		Settle(Runtime);
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:bm")));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetActiveLeafHandle().IsValid(), TEXT("stalled at ") + ADSKTest::FormatPath(*Runtime.Instance)));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(OptionGuardFalseStartsNext)
	{
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Selector id=\"sel\"><Guard id=\"g\" in-condition=\"Off\">") + Move(TEXT("a"))
			+ TEXT("</Guard>") + Move(TEXT("b")) + TEXT("</Selector>")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo:a")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:b")));
		ASSERT_THAT(AreEqual(1, Runtime.Count(EADSKRecordKind::GuardValue)));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(OuterGuardFalseCancels)
	{
		FADSKTestRuntime Runtime(SelectorXml(TEXT("<Guard id=\"g\" in-condition=\"Ok\"><Selector id=\"sel\">") + Fail(TEXT("a"))
			+ Move(TEXT("b")) + Move(TEXT("c")) + TEXT("</Selector></Guard>")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:b")));
		Runtime.Instance->SetInput(TEXT("Ok"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		Settle(Runtime);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:b:Cancelled")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo:c")));
		ASSERT_THAT(IsTrue(InState(Runtime, TEXT("bad")), TEXT("not in bad: ") + ADSKTest::FormatPath(*Runtime.Instance)));
		ADSK_ASSERT(Runtime.Balanced());
	}
};

#endif
