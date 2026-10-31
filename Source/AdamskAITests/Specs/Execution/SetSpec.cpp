// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FSetSpecPrivate
{
	/** Slots of every kind; `Flagged` watches Flag, and the tree under test runs in `s`, then waits in `done`. */
	FString SetXml(const TCHAR* Tree, const TCHAR* ExtraTrees = TEXT(""))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"set\">")
			TEXT("<Enums><Enum name=\"Mode\"><Value name=\"Calm\"/><Value name=\"Alert\"/></Enum></Enums>")
			TEXT("<Inputs><Input name=\"Speed\" type=\"Float\" default=\"2.5\"/></Inputs>")
			TEXT("<Locals><Local name=\"Count\" type=\"Int\"/><Local name=\"Copy\" type=\"Float\"/><Local name=\"Flag\" type=\"Bool\"/>")
			TEXT("<Local name=\"Mood\" type=\"Mode\"/><Local name=\"Who\" type=\"Object\"/><Local name=\"At\" type=\"Vector\"/></Locals>")
			TEXT("<Events><Event name=\"Flagged\" change=\"Flag\"/></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"Flagged\" goto=\"done\"/></State><State id=\"done\" tree=\"w\"/></StateMachine>")
			TEXT("<Tree id=\"t\">%s</Tree><Tree id=\"w\"><Wait id=\"wait\" duration=\"100\"/></Tree>%s</Agent>"), Tree, ExtraTrees);
	}

	FADSKValue Value(const FADSKTestRuntime& Runtime, const TCHAR* Slot)
	{
		FADSKValue Out;
		Runtime.Instance->GetValue(Slot, Out);
		return Out;
	}
}

using namespace FSetSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSetSpec, "AdamskAI.Execution.Set",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(WritesLiteralsOfEveryTypeAndCopiesSlots)
	{
		FADSKTestRuntime Runtime(SetXml(TEXT("<Sequence id=\"q\">")
			TEXT("<Set id=\"a\" to=\"Count\" value=\"3\"/><Set id=\"b\" to=\"Copy\" from=\"Speed\"/><Set id=\"c\" to=\"Mood\" value=\"Alert\"/>")
			TEXT("<Set id=\"d\" to=\"At\" value=\"1,2,3\"/><Set id=\"e\" to=\"Who\" value=\"none\"/><Wait id=\"hold\" duration=\"100\"/></Sequence>")));
		ASSERT_THAT(IsTrue(Runtime.Instance->Start().IsSuccess()));
		ASSERT_THAT(IsTrue(Value(Runtime, TEXT("Count")) == FADSKValue::MakeInt(3)));
		ASSERT_THAT(IsTrue(Value(Runtime, TEXT("Copy")) == FADSKValue::MakeFloat(2.5f)));
		ASSERT_THAT(IsTrue(Value(Runtime, TEXT("At")) == FADSKValue::MakeVector(FVector(1.0, 2.0, 3.0))));
		FString Type, Name;
		ASSERT_THAT(IsTrue(Runtime.Definition->ResolveEnumValue(Value(Runtime, TEXT("Mood")), Type, Name) && Name == TEXT("Alert")));
		ASSERT_THAT(AreEqual(FString(TEXT("hold")), Runtime.Instance->GetActiveNodeId().ToString()));
		ASSERT_THAT(IsTrue(Runtime.Count(EADSKRecordKind::SlotWritten) >= 3, Runtime.Trace()));
	}

	TEST_METHOD(AChangeEventOnTheLocalFiresOnTheNextTick)
	{
		FADSKTestRuntime Runtime(SetXml(TEXT("<Sequence id=\"q\"><Set id=\"f\" to=\"Flag\" value=\"true\"/><Wait id=\"hold\" duration=\"100\"/></Sequence>")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("s")}));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("done")}));
	}

	TEST_METHOD(WritingTheSameValueRecordsNothing)
	{
		FADSKTestRuntime Runtime(SetXml(TEXT("<Sequence id=\"q\"><Set id=\"f\" to=\"Flag\" value=\"false\"/><Wait id=\"hold\" duration=\"100\"/></Sequence>")));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		ASSERT_THAT(AreEqual(0, Runtime.Count(EADSKRecordKind::SlotWritten)));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("s")}));
	}

	TEST_METHOD(TheCompilerTypesItFromTheTarget)
	{
		const auto Errors = [](const TCHAR* Tree, Code Expected) { return Load(SetXml(Tree)).Count(Expected) == 1; };
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" value=\"1\"/>"), Code::MissingRequired)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Count\"/>"), Code::MissingRequired)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Count\" value=\"1\" from=\"Count\"/>"), Code::BadArity)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Speed\" value=\"1\"/>"), Code::TypeMismatch)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Nope\" value=\"1\"/>"), Code::UnresolvedReference)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Count\" value=\"1.5\"/>"), Code::TypeMismatch)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Mood\" value=\"alert\"/>"), Code::UnknownEnumValue)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Count\" from=\"Speed\"/>"), Code::TypeMismatch)));
		ASSERT_THAT(IsTrue(Errors(TEXT("<Set id=\"x\" to=\"Count\" value=\"1\" amount=\"2\"/>"), Code::UnknownAttribute)));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Load(SetXml(TEXT("<Set id=\"x\" to=\"Copy\" from=\"Speed\"/>"))).Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  set slot=")) && Dump.Contains(TEXT(" from=")), Dump));
	}

	TEST_METHOD(ATreeParameterBoundToAnInputCannotBeSet)
	{
		const FString Sub = TEXT("<Tree id=\"sub\"><Param name=\"v\" type=\"Float\"/><Set id=\"x\" to=\"v\" value=\"1\"/></Tree>");
		ASSERT_THAT(AreEqual(1, Load(SetXml(TEXT("<Run id=\"r\" tree=\"sub\"><Param name=\"v\" from=\"Speed\"/></Run>"), *Sub)).Count(Code::TypeMismatch)));
		FADSKTestRuntime Runtime(SetXml(TEXT("<Sequence id=\"q\"><Run id=\"r\" tree=\"sub\"><Param name=\"v\" from=\"Copy\"/></Run><Wait id=\"hold\" duration=\"100\"/></Sequence>"), *Sub));
		Runtime.Instance->Start();
		ASSERT_THAT(IsTrue(Value(Runtime, TEXT("Copy")) == FADSKValue::MakeFloat(1.f)));
	}
};

#endif
