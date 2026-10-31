// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FConditionalTransitionsSpecPrivate
{
	FString Agent(const FString& Inputs, const FString& Events, const FString& Machine,
		const FString& Trees, const FString& Enums = FString(), const FString& Locals = FString())
	{
		return TEXT("<Agent version=\"1\" id=\"conditions\">")
			+ (Enums.IsEmpty() ? FString() : TEXT("<Enums>") + Enums + TEXT("</Enums>"))
			+ TEXT("<Inputs>") + Inputs + TEXT("</Inputs><Locals>") + Locals + TEXT("</Locals><Events>")
			+ Events + TEXT("</Events>") + Machine + Trees + TEXT("</Agent>");
	}

	const FString BoolInputs = TEXT("<Input name=\"A\" type=\"Bool\" default=\"false\"/>"
		"<Input name=\"B\" type=\"Bool\" default=\"true\"/>"
		"<Input name=\"Count\" type=\"Int\" default=\"2\"/>");
	const FString GoEvent = TEXT("<Event name=\"Go\"/>");
	const FString WaitTree = TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>");

	TArray<FString> Consumed(const FADSKTestRuntime& Runtime)
	{
		TArray<FString> Result;
		for (const FADSKRecord& Record : Runtime.Recorder->Snapshot())
		{
			if (Record.Kind == EADSKRecordKind::EventConsumed)
			{
				Result.Add(Runtime.Definition->GetEvents()[Record.Event].Name);
			}
		}
		return Result;
	}
}

using namespace FConditionalTransitionsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FConditionalCompilerSpec, "AdamskAI.Execution.ConditionalTransitions.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ValidatesDependentConditionTypesAndLiterals)
	{
		const FString BaseMachine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">%s</State></StateMachine>");
		auto WithOn = [&](const TCHAR* On)
		{
			return Agent(BoolInputs, GoEvent, BaseMachine.Replace(TEXT("%s"), On), WaitTree);
		};
		const FADSKCompileResult Both = Load(WithOn(TEXT("<On event=\"Go\" if=\"A\" unless=\"B\"/>")));
		ASSERT_THAT(AreEqual(1, Both.Count(Code::BadArity)));
		ASSERT_THAT(AreEqual(0, Both.Count(Code::UnreachableTransition)));
		ASSERT_THAT(AreEqual(1, Load(WithOn(TEXT("<On event=\"Go\" is=\"2\"/>"))).Count(Code::MissingRequired)));
		ASSERT_THAT(AreEqual(1, Load(WithOn(TEXT("<On event=\"Go\" if=\"Missing\"/>"))).Count(Code::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Load(WithOn(TEXT("<On event=\"Go\" if=\"Count\"/>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(WithOn(TEXT("<On event=\"Go\" if=\"A\" is=\"true\"/>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(WithOn(TEXT("<On event=\"Go\" if=\"Count\" is=\"2x\"/>"))).Count(Code::TypeMismatch)));

		const FString Enums = TEXT("<Enum name=\"Intent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>");
		const FString EnumInput = TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"None\"/>");
		const FString EnumMachine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"Go\" if=\"Mode\" is=\"engage\"/></State></StateMachine>");
		ASSERT_THAT(AreEqual(1, Load(Agent(EnumInput, GoEvent, EnumMachine, WaitTree, Enums)).Count(Code::UnknownEnumValue)));
	}

	TEST_METHOD(DuplicateIdentityAndUnreachableWarningUseFullConditionKey)
	{
		const FString DuplicateMachine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"Go\" if=\"A\"/><On event=\"Go\" if=\"A\"/></State></StateMachine>");
		const FADSKCompileResult Duplicate = Load(Agent(BoolInputs, GoEvent, DuplicateMachine, WaitTree));
		ASSERT_THAT(AreEqual(1, Duplicate.Count(Code::DuplicateTransition)));
		ASSERT_THAT(AreEqual(0, Duplicate.Count(Code::UnreachableTransition)));

		const FString OppositeMachine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"Go\" if=\"A\"/><On event=\"Go\" unless=\"A\"/></State></StateMachine>");
		const FADSKCompileResult Opposite = Load(Agent(BoolInputs, GoEvent, OppositeMachine, WaitTree));
		ASSERT_THAT(IsTrue(Opposite.IsSuccess() && Opposite.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Opposite)));
		ASSERT_THAT(AreEqual(2, Opposite.Definition->GetTransitions().Num()));

		const FString UnreachableMachine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"Go\"/><On event=\"Go\" if=\"A\"/></State></StateMachine>");
		const FADSKCompileResult Unreachable = Load(Agent(BoolInputs, GoEvent, UnreachableMachine, WaitTree));
		ASSERT_THAT(IsTrue(Unreachable.IsSuccess(), ADSKTest::FormatDiagnostics(Unreachable)));
		ASSERT_THAT(AreEqual(1, Unreachable.Count(Code::UnreachableTransition)));
		ASSERT_THAT(AreEqual(EADSKSeverity::Warning, Unreachable.FindFirst(Code::UnreachableTransition)->Severity));
	}
};

TEST_CLASS_WITH_FLAGS(FConditionalExecutionSpec, "AdamskAI.Execution.ConditionalTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(UsesFirstPassingHandlerInDocumentOrder)
	{
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\">"
			"<State id=\"s\" tree=\"t\"><On event=\"Go\" if=\"A\" goto=\"bad\"/>"
			"<On event=\"Go\" if=\"B\" goto=\"good\"/><On event=\"Go\" goto=\"fallback\"/></State>"
			"<State id=\"bad\"/><State id=\"good\"/><State id=\"fallback\"/></StateMachine>");
		FADSKTestRuntime Runtime(Agent(BoolInputs, GoEvent, Machine, WaitTree));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("good")}));
	}

	TEST_METHOD(AllFalseContinuesAtParentAndConditionsRunBeforeBind)
	{
		const FString Events = TEXT("<Event name=\"Go\"><Param name=\"V\" type=\"Bool\"/></Event>");
		const FString Locals = TEXT("<Local name=\"Gate\" type=\"Bool\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"parent\">"
			"<State id=\"parent\" initial=\"child\"><On event=\"Go\" goto=\"good\"/>"
			"<State id=\"child\" tree=\"t\"><On event=\"Go\" if=\"Gate\" goto=\"bad\"><Bind param=\"V\" to=\"Gate\"/></On></State>"
			"<State id=\"bad\"/></State><State id=\"good\"/></StateMachine>");
		FADSKTestRuntime Runtime(Agent(FString(), Events, Machine, WaitTree, FString(), Locals));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Go"), {ADSKParam(TEXT("V"), FADSKValue::MakeBool(true))});
		Runtime.Instance->Tick(0.f);
		FADSKValue Gate;
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("good")}));
		ASSERT_THAT(IsTrue(Runtime.Instance->GetValue(TEXT("Gate"), Gate), TEXT("no slot Gate")));
		ASSERT_THAT(IsFalse(Gate.bBool, TEXT("Gate is true")));
	}

	TEST_METHOD(InternalUnlessAndConditionalCompletionAreSupported)
	{
		const FString Inputs = TEXT("<Input name=\"Ready\" type=\"Bool\" default=\"true\"/>"
			"<Input name=\"Block\" type=\"Bool\" default=\"false\"/>");
		const FString Events = TEXT("<Event name=\"Go\" priority=\"1\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"instant\">"
			"<On event=\"Go\" unless=\"Block\"/><On event=\"tree.success\" if=\"Ready\" goto=\"done\"/>"
			"</State><State id=\"done\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"instant\"><Wait id=\"instant-wait\" duration=\"0\"/></Tree>");
		FADSKTestRuntime Runtime(Agent(Inputs, Events, Machine, Trees));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Go"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("done")}));
		const TArray<FString> Order = Consumed(Runtime);
		ASSERT_THAT(AreEqual(TArray<FString>({TEXT("Go"), TEXT("tree.success")}), Order));
	}

	TEST_METHOD(EnumUnlessMeansNotEqual)
	{
		const FString Enums = TEXT("<Enum name=\"Intent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>");
		const FString Events = TEXT("<Event name=\"Go\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"Go\" unless=\"Mode\" is=\"Engage\" goto=\"not-engage\"/>"
			"<On event=\"Go\" if=\"Mode\" is=\"Engage\" goto=\"engage\"/></State>"
			"<State id=\"not-engage\"/><State id=\"engage\"/></StateMachine>");
		auto Run = [&](const TCHAR* Default)
		{
			const FString Inputs = FString::Printf(TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"%s\"/>"), Default);
			FADSKTestRuntime Runtime(Agent(Inputs, Events, Machine, WaitTree, Enums));
			Runtime.Instance->Start();
			Runtime.Instance->EnqueueEvent(TEXT("Go"));
			Runtime.Instance->Tick(0.f);
			return Runtime.Instance->GetActiveStatePath().Last();
		};
		ASSERT_THAT(AreEqual(FString(TEXT("engage")), Run(TEXT("Engage"))));
		ASSERT_THAT(AreEqual(FString(TEXT("not-engage")), Run(TEXT("None"))));
	}
};

TEST_CLASS_WITH_FLAGS(FGuardCompareSpec, "AdamskAI.Execution.GuardCompare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(IntComparisonIsOrderIndependentInvertedAndRechecked)
	{
		const FString Inputs = TEXT("<Input name=\"Count\" type=\"Int\" default=\"2\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\">"
			"<On event=\"tree.failure\" goto=\"failed\"/></State><State id=\"failed\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"t\"><Guard id=\"g\" is=\"2\" in-condition=\"Count\">"
			"<Wait id=\"w\" duration=\"100\"/></Guard></Tree>");
		FADSKTestRuntime Runtime(Agent(Inputs, FString(), Machine, Trees));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FName(TEXT("w")), Runtime.Instance->GetActiveNodeId()));
		Runtime.Instance->SetInput(TEXT("Count"), FADSKValue::MakeInt(3));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("failed")}));
		ASSERT_THAT(IsTrue(Runtime.Recorder->Snapshot().ContainsByPredicate([](const FADSKRecord& Record)
		{
			return Record.Kind == EADSKRecordKind::GuardValue && Record.bHasValue
				&& Record.Value == FADSKValue::MakeInt(3);
		}), TEXT("no guard-value record of 3") + Runtime.Trace()));

		const FString InvertedTrees = Trees.Replace(TEXT("is=\"2\""), TEXT("is=\"2\" invert=\"true\""));
		FADSKTestRuntime Inverted(Agent(TEXT("<Input name=\"Count\" type=\"Int\" default=\"3\"/>"), FString(), Machine, InvertedTrees));
		ADSK_ASSERT(ADSKTest::Start(*Inverted.Instance));
		ASSERT_THAT(AreEqual(FName(TEXT("w")), Inverted.Instance->GetActiveNodeId()));
	}

	TEST_METHOD(EnumComparisonUsesExactDefinitionValue)
	{
		const FString Enums = TEXT("<Enum name=\"Intent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>");
		const FString Inputs = TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"Engage\"/>");
		const FString Machine = TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>");
		const FString Trees = TEXT("<Tree id=\"t\"><Guard id=\"g\" in-condition=\"Mode\" is=\"Engage\">"
			"<Wait id=\"w\" duration=\"100\"/></Guard></Tree>");
		FADSKTestRuntime Runtime(Agent(Inputs, FString(), Machine, Trees, Enums));
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		ASSERT_THAT(AreEqual(FName(TEXT("w")), Runtime.Instance->GetActiveNodeId()));
	}
};

#endif
