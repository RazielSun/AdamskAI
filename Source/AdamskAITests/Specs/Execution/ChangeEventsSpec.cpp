// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FChangeEventsSpecPrivate
{
	/** GroundedChanged watches an Input, PlanChanged a Local; Poke is a lower-priority host event, Noise unhandled. */
	FString ChangeXml(const FString& Extra = FString())
	{
		return TEXT("<Agent version=\"1\" id=\"c\">\n")
			TEXT("<Inputs><Input name=\"Grounded\" type=\"Bool\" default=\"true\"/></Inputs>\n")
			TEXT("<Locals><Local name=\"Plan\" type=\"Int\"/></Locals>\n")
			TEXT("<Events><Event name=\"GroundedChanged\" priority=\"90\" change=\"Grounded\"/><Event name=\"PlanChanged\" change=\"Plan\"/>")
			TEXT("<Event name=\"Poke\" priority=\"50\"/><Event name=\"Noise\"/><Event name=\"Set\"><Param name=\"v\" type=\"Int\"/></Event></Events>\n")
			TEXT("<StateMachine id=\"m\" initial=\"ground\">\n")
			TEXT("<State id=\"ground\" tree=\"w\"><On event=\"GroundedChanged\" goto=\"air\" unless=\"Grounded\"/><On event=\"Poke\" goto=\"poked\"/>")
			TEXT("<On event=\"Set\"><Bind param=\"v\" to=\"Plan\"/></On><On event=\"PlanChanged\" goto=\"planned\"/></State>\n")
			TEXT("<State id=\"air\" tree=\"w\"><On event=\"GroundedChanged\" goto=\"ground\" if=\"Grounded\"/></State>\n")
			TEXT("<State id=\"poked\" tree=\"w\"/><State id=\"planned\" tree=\"w\"/>\n")
			TEXT("</StateMachine>\n<Tree id=\"w\"><Wait id=\"wait\" duration=\"100\"/></Tree>\n") + Extra + TEXT("</Agent>");
	}

	bool HasCode(const FADSKCompileResult& Result, Code Expected)
	{
		return Result.Diagnostics.ContainsByPredicate([Expected](const FADSKDiagnostic& D) { return D.Code == Expected; });
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

	FString Leaf(const FADSKTestRuntime& Runtime)
	{
		return Runtime.Instance->GetActiveStatePath().Last();
	}
}

using namespace FChangeEventsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FChangeEventsCompilerSpec, "AdamskAI.Execution.ChangeEvents.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ACompiledChangeEventNamesItsSlot)
	{
		const FADSKCompileResult Result = Load(ChangeXml());
		ASSERT_THAT(IsTrue(Result.Diagnostics.IsEmpty() && Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		const FADSKEventDefinition& Event = Definition.GetEvents()[Definition.FindEvent(TEXT("GroundedChanged"))];
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("Grounded")), Event.ChangeSlot));
		ASSERT_THAT(IsTrue(Event.IsRaisedByCore() && !Event.bReserved, TEXT("GroundedChanged is not a core-raised, unreserved event")));
		ASSERT_THAT(IsFalse(Definition.GetEvents()[Definition.FindEvent(TEXT("Poke"))].IsRaisedByCore(), TEXT("Poke is raised by the core")));
		ASSERT_THAT(IsTrue(ADSKTest::DumpNormalizedTables(Definition).Contains(TEXT("GroundedChanged priority=90 reserved=false params=0+0 change=0")), ADSKTest::DumpNormalizedTables(Definition)));
	}

	TEST_METHOD(AnUnknownSlotIsUnresolved)
	{
		const FADSKCompileResult Result = Load(ChangeXml().Replace(TEXT("change=\"Grounded\""), TEXT("change=\"Flying\"")));
		ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::UnresolvedReference), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(AChangeEventTakesNoParameters)
	{
		const FADSKCompileResult Result = Load(ChangeXml().Replace(TEXT("<Event name=\"PlanChanged\" change=\"Plan\"/>"),
			TEXT("<Event name=\"PlanChanged\" change=\"Plan\"><Param name=\"p\" type=\"Int\"/></Event>")));
		ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::BadArity), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(EmitCannotRaiseAChangeEvent)
	{
		const FADSKCompileResult Result = Load(ChangeXml(TEXT("<Tree id=\"e\"><Emit id=\"emit\" event=\"GroundedChanged\"/></Tree>\n"))
			.Replace(TEXT("<State id=\"planned\" tree=\"w\"/>"), TEXT("<State id=\"planned\" tree=\"e\"/>")));
		ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::TypeMismatch), ADSKTest::FormatDiagnostics(Result)));
	}
};

TEST_CLASS_WITH_FLAGS(FChangeEventsExecutionSpec, "AdamskAI.Execution.ChangeEvents.Execution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RaisedOnceForEachChangeAndConditionsReadTheNewValue)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("ground")), Leaf(Runtime)));
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("air")), Leaf(Runtime)));
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(1, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(true));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("ground")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(2, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(TheStartValuesAreTheBaseline)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("ground")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(0, Enqueued(Runtime, TEXT("GroundedChanged"))));

		// A restart takes a new baseline, so a change made while stopped raises nothing either.
		Runtime.Instance->Stop();
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(true));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(0, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(AFlickerBetweenTwoTicksIsNoChange)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->Start();
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(true));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(0, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(PriorityOrdersItAmongHostEvents)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->Start();
		Runtime.Instance->EnqueueEvent(TEXT("Poke"));
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		// GroundedChanged (90) goes first and leaves ground; air ignores Poke (50).
		ASSERT_THAT(AreEqual(FString(TEXT("air")), Leaf(Runtime)));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(ALocalWrittenWhileTickingRaisesItOnTheNextTick)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->Start();
		const FADSKNamedValue Value = ADSKParam(TEXT("v"), FADSKValue::MakeInt(3));
		Runtime.Instance->EnqueueEvent(TEXT("Set"), MakeArrayView(&Value, 1));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("ground")), Leaf(Runtime)));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("planned")), Leaf(Runtime)));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(AChangeAFullQueueRefusesIsRaisedOnTheNextTick)
	{
		FADSKTestRuntime Runtime(ChangeXml(), 1024);
		Runtime.Instance->Start();
		for (int32 Index = 0; Index < ADSK::ExternalEventCapacity; ++Index)
		{
			ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Noise"))));
		}
		Runtime.Instance->SetInput(TEXT("Grounded"), FADSKValue::MakeBool(false));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("ground")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(0, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("air")), Leaf(Runtime)));
		ASSERT_THAT(AreEqual(1, Enqueued(Runtime, TEXT("GroundedChanged"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(HostsCannotRaiseAChangeEvent)
	{
		FADSKTestRuntime Runtime(ChangeXml());
		Runtime.Instance->Start();
		ASSERT_THAT(IsFalse(Runtime.Instance->FindEvent(TEXT("GroundedChanged")).IsValid(), TEXT("a change event resolved as a host event")));
		ASSERT_THAT(AreEqual(EADSKInputResult::Reserved, Runtime.Instance->EnqueueEvent(TEXT("GroundedChanged"))));
		Runtime.Instance->Stop();
	}
};

#endif
