// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "CQTest.h"
#include <limits>

namespace FHostBoundarySpecPrivate
{
	FString BoundaryXml()
	{
		return TEXT("<Agent version=\"1\" id=\"b\">")
			TEXT("<Inputs><Input name=\"Grounded\" type=\"Bool\" default=\"true\"/><Input name=\"Speed\" type=\"Float\" default=\"0\"/>")
			TEXT("<Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs>")
			TEXT("<Locals><Local name=\"Plan\" type=\"Int\"/></Locals>")
			TEXT("<Events><Event name=\"GroundedChanged\" change=\"Grounded\"/><Event name=\"SpeedChanged\" change=\"Speed\"/>")
			TEXT("<Event name=\"Hit\"><Param name=\"amount\" type=\"Float\"/></Event></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"w\"/></StateMachine>")
			TEXT("<Tree id=\"w\"><Wait id=\"wait\" duration=\"100\"/></Tree></Agent>");
	}

	int32 Enqueued(const FADSKTestRuntime& Runtime, const TCHAR* Event)
	{
		const int32 Index = Runtime.Definition->FindEvent(Event);
		int32 Found = 0;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			Found += Record.Kind == EADSKRecordKind::EventEnqueued && Record.Event == Index;
		}
		return Found;
	}
}

using namespace FHostBoundarySpecPrivate;

TEST_CLASS_WITH_FLAGS(FHostBoundarySpec, "AdamskAI.Execution.HostBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AnInputIdOutsideTheInputsIsRefused)
	{
		FADSKTestRuntime Runtime(BoundaryXml());
		Runtime.Instance->Start();
		const uint64 Serial = Runtime.Definition->GetSerial();
		const int32 Local = Runtime.Definition->FindSlot(TEXT("Plan"));
		ASSERT_THAT(AreNotEqual(INDEX_NONE, Local));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->SetInput(FADSKInputId{Serial, Local}, FADSKValue::MakeInt(3))));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->SetInput(FADSKInputId{Serial, 999}, FADSKValue::MakeBool(true))));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->SetInput(FADSKInputId{Serial, INDEX_NONE}, FADSKValue::MakeBool(true))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(Runtime.Instance->FindInput(TEXT("Grounded")), FADSKValue::MakeBool(false))));
	}

	TEST_METHOD(AnEventIdTheCoreRaisesIsRefused)
	{
		FADSKTestRuntime Runtime(BoundaryXml());
		Runtime.Instance->Start();
		const uint64 Serial = Runtime.Definition->GetSerial();
		const int32 Success = Runtime.Definition->FindEvent(TEXT("tree.success"));
		ASSERT_THAT(AreNotEqual(INDEX_NONE, Success));
		ASSERT_THAT(AreEqual(EADSKInputResult::Reserved, Runtime.Instance->EnqueueEvent(FADSKEventId{Serial, Success})));
		const int32 Change = Runtime.Definition->FindEvent(TEXT("GroundedChanged"));
		ASSERT_THAT(AreEqual(EADSKInputResult::Reserved, Runtime.Instance->EnqueueEvent(FADSKEventId{Serial, Change})));
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Runtime.Instance->EnqueueEvent(FADSKEventId{Serial, 999})));
		ASSERT_THAT(AreEqual(EADSKInputResult::Reserved, Runtime.Instance->EnqueueEvent(TEXT("GroundedChanged"))));
	}

	TEST_METHOD(ANonFiniteValueIsRefused)
	{
		FADSKTestRuntime Runtime(BoundaryXml());
		Runtime.Instance->Start();
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(NaN))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(std::numeric_limits<float>::infinity()))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(0, NaN, 0)))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->EnqueueEvent(TEXT("Hit"), {{TEXT("amount"), FADSKValue::MakeFloat(NaN)}})));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(2.f))));
	}

	TEST_METHOD(ANaNEqualsANaNSoAChangeIsRaisedOnce)
	{
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		ASSERT_THAT(IsTrue(FADSKValue::MakeFloat(NaN) == FADSKValue::MakeFloat(NaN), TEXT("NaN != NaN")));
		ASSERT_THAT(IsTrue(FADSKValue::MakeVector(FVector(1, NaN, 2)) == FADSKValue::MakeVector(FVector(1, NaN, 2)), TEXT("NaN vector != itself")));
		ASSERT_THAT(IsFalse(FADSKValue::MakeFloat(NaN) == FADSKValue::MakeFloat(0.f), TEXT("NaN == 0")));
		ASSERT_THAT(IsFalse(FADSKValue::MakeFloat(NaN).IsFinite(), TEXT("NaN is finite")));
		FADSKTestRuntime Runtime(BoundaryXml());
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Speed"), FADSKValue::MakeFloat(5.f));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Runtime.Instance->Tick(0.1f);
		}
		ASSERT_THAT(AreEqual(1, Enqueued(Runtime, TEXT("SpeedChanged")), Runtime.Trace()));
	}
};

#endif
