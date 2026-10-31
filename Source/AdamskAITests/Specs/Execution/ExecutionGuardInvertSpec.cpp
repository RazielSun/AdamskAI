// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"

namespace FExecutionGuardInvertSpecPrivate
{
	FString InvertXml()
	{
		return TEXT("<Agent version=\"1\" id=\"invert\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"1,2,3\"/>")
			TEXT("<Input name=\"Flag\" type=\"Bool\" default=\"false\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Guard id=\"g\" in-condition=\"Flag\" invert=\"true\">")
			TEXT("<MoveTo id=\"move\" in-goal=\"Goal\" acceptanceRadius=\"1\"/></Guard></Tree></Agent>");
	}

	TArray<bool> GuardValues(const FADSKTestRuntime& Runtime)
	{
		TArray<bool> Values;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::GuardValue)
			{
				Values.Add(Record.Value.bBool);
			}
		}
		return Values;
	}
}

using namespace FExecutionGuardInvertSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionGuardInvertSpec, "AdamskAI.Execution.Guard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(InvertStartFalsePasses)
	{
		FADSKTestRuntime Runtime(InvertXml());
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("Start:MoveTo:move")));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(InvertRecheckCancelsWhenTrue)
	{
		FADSKTestRuntime Runtime(InvertXml());
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Flag"), FADSKValue::MakeBool(true));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:move:Cancelled")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Update:MoveTo:move")));
		Runtime.Instance->Stop();
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(InvertRecordsRawValue)
	{
		FADSKTestRuntime Runtime(InvertXml());
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Flag"), FADSKValue::MakeBool(true));
		Runtime.Instance->Tick(0.1f);
		const TArray<bool> Values = GuardValues(Runtime);
		ASSERT_THAT(AreEqual(2, Values.Num()));
		ASSERT_THAT(IsFalse(Values[0], TEXT("the first guard value is true")));
		ASSERT_THAT(IsTrue(Values[1], TEXT("the second guard value is false")));
		Runtime.Instance->Stop();
	}
};

#endif
