// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FSubtreeParamsSpecPrivate
{
	const FString Go = TEXT("<Tree id=\"go\"><Param name=\"Goal\" type=\"Vector\"/><MoveTo id=\"m\" in-goal=\"Goal\"/></Tree>");

	FString Agent(const FString& Trees, const FString& Inputs = TEXT("<Input name=\"A\" type=\"Vector\" default=\"1,0,0\"/><Input name=\"N\" type=\"Int\" default=\"0\"/>"),
		const FString& Locals = TEXT("<Local name=\"B\" type=\"Vector\"/><Local name=\"Count\" type=\"Int\"/>"),
		const FString& Events = FString(), const FString& States = TEXT("<State id=\"s\" tree=\"t\"/>"))
	{
		return TEXT("<Agent version=\"1\" id=\"params\"><Inputs>") + Inputs + TEXT("</Inputs><Locals>") + Locals + TEXT("</Locals><Events>")
			+ Events + TEXT("</Events><StateMachine id=\"m\" initial=\"s\">") + States + TEXT("</StateMachine>") + Trees + TEXT("</Agent>");
	}

	FString Main(const FString& Root)
	{
		return TEXT("<Tree id=\"t\">") + Root + TEXT("</Tree>");
	}

	int32 PortSlot(const FADSKDefinition& Definition, const TCHAR* NodeId, int32 Port = 0)
	{
		const int32 Node = Definition.FindNode(NodeId);
		return Node == INDEX_NONE ? INDEX_NONE : Definition.GetPortBindings()[Definition.GetNodes()[Node].FirstPortBinding + Port];
	}
}

using namespace FSubtreeParamsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FRunParamsSpec, "AdamskAI.Compiler.RunParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EachCallSiteBindsTheParameterToItsOwnSlotAndTheTemplateIsDropped)
	{
		const FADSKCompileResult Result = Load(Agent(Main(TEXT("<Sequence id=\"s\"><Run id=\"a\" tree=\"go\"><Param name=\"Goal\" from=\"A\"/></Run>"
			"<Run id=\"b\" tree=\"go\"><Param name=\"Goal\" from=\"B\"/></Run></Sequence>")) + Go));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("A")), PortSlot(Definition, TEXT("a/m"))));
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("B")), PortSlot(Definition, TEXT("b/m"))));
		ASSERT_THAT(AreEqual(INDEX_NONE, Definition.FindNode(TEXT("m"))));
		ASSERT_THAT(AreEqual(3, Definition.GetNodes().Num()));
		ASSERT_THAT(AreEqual(4, Definition.GetSlots().Num()));
		ASSERT_THAT(AreEqual(INDEX_NONE, Definition.FindSlot(TEXT("Goal"))));
		ASSERT_THAT(AreEqual(FString(TEXT("go")), Definition.GetTrees()[1].Id));
		ASSERT_THAT(AreEqual(INDEX_NONE, Definition.GetTrees()[1].RootNode));
	}

	TEST_METHOD(ANestedCallComposesTheBindings)
	{
		const FString Outer = TEXT("<Tree id=\"outer\"><Param name=\"Where\" type=\"Vector\"/><Sequence id=\"q\">"
			"<Run id=\"i\" tree=\"go\"><Param name=\"Goal\" from=\"Where\"/></Run><LookAtPoint id=\"l\" in-target=\"Where\"/></Sequence></Tree>");
		const FADSKCompileResult Result = Load(Agent(Main(TEXT("<Run id=\"o\" tree=\"outer\"><Param name=\"Where\" from=\"B\"/></Run>")) + Outer + Go));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const int32 B = Result.Definition->FindSlot(TEXT("B"));
		ASSERT_THAT(AreEqual(B, PortSlot(*Result.Definition, TEXT("o/i/m"))));
		ASSERT_THAT(AreEqual(B, PortSlot(*Result.Definition, TEXT("o/l"))));
		ASSERT_THAT(AreEqual(3, Result.Definition->GetNodes().Num()));
	}

	TEST_METHOD(GuardOperandsAndEnumParametersAreSubstituted)
	{
		const FString Enums = TEXT("<Enums><Enum name=\"Mode\"><Value name=\"Calm\"/><Value name=\"Angry\"/></Enum></Enums>");
		const FString Gate = TEXT("<Tree id=\"gate\"><Param name=\"Limit\" type=\"Int\"/><Param name=\"Mood\" type=\"Mode\"/>"
			"<Guard id=\"g\" in-condition=\"N\" below=\"Limit\"><Guard id=\"h\" in-condition=\"Mood\" is=\"Angry\"><Wait id=\"w\"/></Guard></Guard></Tree>");
		const FString Xml = Agent(Main(TEXT("<Run id=\"c\" tree=\"gate\"><Param name=\"Limit\" from=\"Count\"/><Param name=\"Mood\" from=\"Feel\"/></Run>")) + Gate,
			TEXT("<Input name=\"N\" type=\"Int\" default=\"0\"/><Input name=\"Feel\" type=\"Mode\"/>"), TEXT("<Local name=\"Count\" type=\"Int\"/>"));
		const FADSKCompileResult Result = Load(Xml.Replace(TEXT("<Inputs>"), *(Enums + TEXT("<Inputs>"))));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		const FADSKNodeDefinition& G = Definition.GetNodes()[Definition.FindNode(TEXT("c/g"))];
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("Count")), G.Condition.OperandSlot));
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("Feel")), PortSlot(Definition, TEXT("c/h"))));
	}

	TEST_METHOD(GuardWhenTermsAreSubstituted)
	{
		const FString Gate = TEXT("<Tree id=\"gate\"><Param name=\"Ready\" type=\"Bool\"/><Param name=\"Limit\" type=\"Int\"/>"
			"<Guard id=\"g\" in-condition=\"Open\"><When if=\"Ready\"/><When if=\"N\" below=\"Limit\"/><Wait id=\"w\"/></Guard></Tree>");
		const FADSKCompileResult Result = Load(Agent(Main(TEXT("<Run id=\"c\" tree=\"gate\"><Param name=\"Ready\" from=\"Armed\"/><Param name=\"Limit\" from=\"Count\"/></Run>")) + Gate,
			TEXT("<Input name=\"N\" type=\"Int\" default=\"0\"/><Input name=\"Open\" type=\"Bool\" default=\"true\"/>"),
			TEXT("<Local name=\"Count\" type=\"Int\"/><Local name=\"Armed\" type=\"Bool\"/>")));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		const FADSKNodeDefinition& G = Definition.GetNodes()[Definition.FindNode(TEXT("c/g"))];
		ASSERT_THAT(AreEqual(2, G.TermCount));
		const TConstArrayView<FADSKCondition> Terms = Definition.GetConditionTerms().Slice(G.FirstTerm, G.TermCount);
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("Armed")), Terms[0].Slot));
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("N")), Terms[1].Slot));
		ASSERT_THAT(AreEqual(Definition.FindSlot(TEXT("Count")), Terms[1].OperandSlot));
	}

	TEST_METHOD(BindingErrorsAreExact)
	{
		const auto Calls = [](const FString& Run, const FString& Extra = FString()) { return Load(Agent(Main(Run) + Go + Extra)); };
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"/>")).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Param name=\"Goal\" from=\"A\"/><Param name=\"Speed\" from=\"A\"/></Run>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Param name=\"Goal\" from=\"N\"/></Run>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Param name=\"Goal\" from=\"Nowhere\"/></Run>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Param name=\"Goal\" from=\"A\"/><Param name=\"Goal\" from=\"B\"/></Run>")).Count(Code::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Param name=\"Goal\" value=\"1,2,3\"/></Run>")).Count(Code::UnknownAttribute)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<Run id=\"r\" tree=\"go\"><Wait id=\"w\"/></Run>")).Count(Code::BadArity)));
		// A template is reachable only through Run, and its parameters only inside it.
		ASSERT_THAT(AreEqual(1, Load(Agent(Main(TEXT("<Wait id=\"w\"/>")) + Go, FString(), FString(), FString(),
			TEXT("<State id=\"s\" tree=\"go\"/>"))).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, Calls(TEXT("<MoveTo id=\"x\" in-goal=\"Goal\"/>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Main(TEXT("<Wait id=\"w\"/>")) + TEXT("<Tree id=\"bad\"><Param name=\"B\" type=\"Vector\"/><Wait id=\"x\"/></Tree>"))).Count(Code::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Main(TEXT("<Wait id=\"w\"/>")) + TEXT("<Tree id=\"bad\"><Param name=\"P\" type=\"Name\"/><Wait id=\"x\"/></Tree>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Main(TEXT("<Wait id=\"w\"/>")) + TEXT("<Tree id=\"bad\"><Param name=\"P\" type=\"Int\"/></Tree>"))).Count(Code::BadArity)));
	}

	TEST_METHOD(AnOutPortOnAParameterNeedsALocalAtTheCallSite)
	{
		const FString Sense = TEXT("<Tree id=\"look\"><Param name=\"Into\" type=\"Vector\"/><Sense id=\"see\" out-point=\"Into\"/></Tree>");
		const FADSKCompileResult ToLocal = Load(Agent(Main(TEXT("<Run id=\"r\" tree=\"look\"><Param name=\"Into\" from=\"B\"/></Run>")) + Sense));
		ASSERT_THAT(IsTrue(ToLocal.IsSuccess(), ADSKTest::FormatDiagnostics(ToLocal)));
		const FADSKCompileResult ToInput = Load(Agent(Main(TEXT("<Run id=\"r\" tree=\"look\"><Param name=\"Into\" from=\"A\"/></Run>")) + Sense));
		ASSERT_THAT(AreEqual(1, ToInput.Count(Code::TypeMismatch)));
		ASSERT_THAT(IsFalse(ToInput.IsSuccess(), ADSKTest::FormatDiagnostics(ToInput)));
	}

	TEST_METHOD(TheBoundSlotIsWhatRuns)
	{
		FADSKTestRuntime Runtime(Agent(Main(TEXT("<Run id=\"a\" tree=\"go\"><Param name=\"Goal\" from=\"A\"/></Run>")) + Go));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("A"), FADSKValue::MakeVector(FVector(4, 5, 6)))));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ADSK_ASSERT(Runtime.HasLine(*FString::Printf(TEXT("Goal:a/m:%s"), *FVector(4, 5, 6).ToString())));
	}
};

TEST_CLASS_WITH_FLAGS(FEmitArgsSpec, "AdamskAI.Compiler.EmitArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	const FString Enums = TEXT("<Enums><Enum name=\"Mode\"><Value name=\"Calm\"/><Value name=\"Angry\"/></Enum></Enums>");
	const FString Hit = TEXT("<Event name=\"Hit\"><Param name=\"Where\" type=\"Vector\"/><Param name=\"Loud\" type=\"Bool\"/><Param name=\"Mood\" type=\"Mode\"/></Event>");
	const FString Received = TEXT("<State id=\"s\" tree=\"t\"><On event=\"Hit\"><Bind param=\"Where\" to=\"B\"/><Bind param=\"Loud\" to=\"L\"/><Bind param=\"Mood\" to=\"M\"/></On></State>");
	const FString Locals = TEXT("<Local name=\"B\" type=\"Vector\"/><Local name=\"L\" type=\"Bool\"/><Local name=\"M\" type=\"Mode\"/>");

	FString EmitAgent(const FString& Emit, const FString& Extra = FString())
	{
		const FString Xml = Agent(Main(TEXT("<Sequence id=\"q\">") + Emit + TEXT("<Wait id=\"w\" duration=\"100\"/></Sequence>")) + Extra,
			TEXT("<Input name=\"A\" type=\"Vector\" default=\"1,0,0\"/>"), Locals, Hit, Received);
		return Xml.Replace(TEXT("<Inputs>"), *(Enums + TEXT("<Inputs>")));
	}

	TEST_METHOD(SlotsAreReadWhenTheEmitRunsAndConstantsArrive)
	{
		FADSKTestRuntime Runtime(EmitAgent(TEXT("<Emit id=\"e\" event=\"Hit\"><Param name=\"Mood\" value=\"Angry\"/><Param name=\"Where\" from=\"A\"/><Param name=\"Loud\" value=\"true\"/></Emit>")));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("A"), FADSKValue::MakeVector(FVector(7, 8, 9)))));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		// Written after the emit ran: the queued event already holds the old value.
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("A"), FADSKValue::MakeVector(FVector(0, 0, 1)))));
		Runtime.Instance->Tick(0.1f);
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("B"), Value), TEXT("no slot B")));
		ASSERT_THAT(IsTrue(Value == FADSKValue::MakeVector(FVector(7, 8, 9)), LexToString(Value)));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("L"), Value), TEXT("no slot L")));
		ASSERT_THAT(IsTrue(Value == FADSKValue::MakeBool(true), LexToString(Value)));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("M"), Value), TEXT("no slot M")));
		ASSERT_THAT(IsTrue(Value == FADSKValue::MakeEnum(0, 1), LexToString(Value)));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Runtime.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(FString::Printf(TEXT("  emit-arg 0 slot=%d\n  emit-arg 1 value=true\n  emit-arg 2 value=Mode:Angry\n"),
			Runtime.Definition->FindSlot(TEXT("A")))), Dump));
	}

	TEST_METHOD(AnEmitInsideATemplateReadsTheBoundSlot)
	{
		const FString Tell = TEXT("<Tree id=\"tell\"><Param name=\"P\" type=\"Vector\"/><Emit id=\"e\" event=\"Hit\"><Param name=\"Where\" from=\"P\"/>"
			"<Param name=\"Loud\" value=\"false\"/><Param name=\"Mood\" value=\"Calm\"/></Emit></Tree>");
		FADSKTestRuntime Runtime(EmitAgent(TEXT("<Run id=\"r\" tree=\"tell\"><Param name=\"P\" from=\"A\"/></Run>"), Tell));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->Tick(0.1f);
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("B"), Value), TEXT("no slot B")));
		ASSERT_THAT(IsTrue(Value == FADSKValue::MakeVector(FVector(1, 0, 0)), LexToString(Value)));
	}

	TEST_METHOD(ArgumentErrorsAreExact)
	{
		const auto Emit = [this](const TCHAR* Params) { return Load(EmitAgent(FString::Printf(TEXT("<Emit id=\"e\" event=\"Hit\">%s</Emit>"), Params))); };
		const FString Rest = TEXT("<Param name=\"Loud\" value=\"true\"/><Param name=\"Mood\" value=\"Calm\"/>");
		const auto With = [&](const TCHAR* Where) { return Emit(*(FString(Where) + Rest)); };
		const FADSKCompileResult Valid = With(TEXT("<Param name=\"Where\" value=\"1,2,3\"/>"));
		ASSERT_THAT(IsTrue(Valid.IsSuccess(), ADSKTest::FormatDiagnostics(Valid)));
		ASSERT_THAT(AreEqual(1, Emit(*Rest).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" value=\"1,2,3\"/><Param name=\"Extra\" value=\"1\"/>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" value=\"1,2,3\" from=\"A\"/>")).Count(Code::BadArity)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\"/>")).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" value=\"near\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" from=\"L\"/>")).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" from=\"Nowhere\"/>")).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, With(TEXT("<Param name=\"Where\" value=\"1,2,3\"/><Param name=\"Where\" from=\"A\"/>")).Count(Code::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Emit(TEXT("<Param name=\"Where\" value=\"1,2,3\"/><Param name=\"Loud\" value=\"true\"/><Param name=\"Mood\" value=\"calm\"/>")).Count(Code::UnknownEnumValue)));
	}
};

#endif
