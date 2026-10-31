// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FIntValueSpecPrivate
{
	FString ProbeXml(const FString& InputDefault, const FString& Attr)
	{
		return TEXT("<Agent version=\"1\" id=\"i\">\n<Inputs>\n<Input name=\"N\" type=\"Int\" ") + InputDefault + TEXT("/>\n</Inputs>\n")
			TEXT("<Locals>\n<Local name=\"R\" type=\"Int\"/><Local name=\"F\" type=\"Float\"/>\n</Locals>\n")
			TEXT("<Events>\n<Event name=\"Set\"><Param name=\"V\" type=\"Int\"/></Event>\n</Events>\n")
			TEXT("<StateMachine id=\"m\" initial=\"s\">\n<State id=\"s\" tree=\"t\"><On event=\"Set\"><Bind param=\"V\" to=\"R\"/></On></State>\n</StateMachine>\n")
			TEXT("<Tree id=\"t\">\n<IntProbe id=\"p\" in-value=\"N\" out-result=\"R\" ") + Attr + TEXT("/>\n</Tree>\n</Agent>");
	}

	int32 ValueOf(const FADSKInstance& Instance, const TCHAR* Slot)
	{
		FADSKValue Value;
		return Instance.GetValue(Slot, Value) && Value.Type == EADSKValueType::Int ? Value.Int : MIN_int32;
	}
}

using namespace FIntValueSpecPrivate;

TEST_CLASS_WITH_FLAGS(FIntParseSpec, "AdamskAI.Int.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AcceptsExactlyMinusOptionalDigitsInRange)
	{
		int32 Value = 0;
		ASSERT_THAT(IsTrue(ADSK::TryParseInt(TEXT("0"), Value), TEXT("rejected 0")));
		ASSERT_THAT(AreEqual(0, Value));
		ASSERT_THAT(IsTrue(ADSK::TryParseInt(TEXT("-7"), Value), TEXT("rejected -7")));
		ASSERT_THAT(AreEqual(-7, Value));
		ASSERT_THAT(IsTrue(ADSK::TryParseInt(TEXT("007"), Value), TEXT("rejected 007")));
		ASSERT_THAT(AreEqual(7, Value));
		ASSERT_THAT(IsTrue(ADSK::TryParseInt(TEXT("2147483647"), Value), TEXT("rejected 2147483647")));
		ASSERT_THAT(AreEqual(MAX_int32, Value));
		ASSERT_THAT(IsTrue(ADSK::TryParseInt(TEXT("-2147483648"), Value), TEXT("rejected -2147483648")));
		ASSERT_THAT(AreEqual(MIN_int32, Value));
	}

	TEST_METHOD(RejectsEverythingElse)
	{
		int32 Value = 0;
		for (const TCHAR* Bad : {TEXT(""), TEXT("-"), TEXT("+1"), TEXT(" 1"), TEXT("1 "), TEXT("1.0"), TEXT("1e3"), TEXT("0x10"),
				 TEXT("--1"), TEXT("1_0"), TEXT("2147483648"), TEXT("-2147483649"), TEXT("99999999999999999999")})
		{
			ASSERT_THAT(IsFalse(ADSK::TryParseInt(Bad, Value), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(ValueTypeRoundTrips)
	{
		FADSKValue Parsed;
		ASSERT_THAT(IsTrue(ADSK::TryParseValue(EADSKValueType::Int, TEXT("-42"), Parsed), TEXT("rejected -42")));
		ASSERT_THAT(IsTrue(Parsed == FADSKValue::MakeInt(-42) && Parsed != FADSKValue::MakeInt(42), LexToString(Parsed)));
		ASSERT_THAT(IsTrue(FADSKValue::MakeZero(EADSKValueType::Int) == FADSKValue::MakeInt(0), LexToString(FADSKValue::MakeZero(EADSKValueType::Int))));
		ASSERT_THAT(IsTrue(FADSKValue::MakeInt(0) != FADSKValue::MakeFloat(0.f), TEXT("Int 0 equals Float 0")));
		ASSERT_THAT(AreEqual(FString(TEXT("-42")), LexToString(Parsed)));
		ASSERT_THAT(AreEqual(FString(TEXT("Int")), FString(LexToString(EADSKValueType::Int))));
	}
};

TEST_CLASS_WITH_FLAGS(FIntCompilerSpec, "AdamskAI.Int.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(DeclaresInputLocalAndEventParameter)
	{
		const FADSKCompileResult Result = Load(ProbeXml(TEXT("default=\"-5\""), TEXT("")));
		ASSERT_THAT(IsTrue(Result.Diagnostics.IsEmpty() && Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		const FADSKSlotDefinition& Input = Definition.GetSlots()[Definition.FindSlot(TEXT("N"))];
		ASSERT_THAT(AreEqual(EADSKValueType::Int, Input.Type, FString::Printf(TEXT("N is %s"), LexToString(Input.Type))));
		ASSERT_THAT(IsTrue(Input.Default == FADSKValue::MakeInt(-5), LexToString(Input.Default)));
		ASSERT_THAT(IsTrue(Definition.GetSlots()[Definition.FindSlot(TEXT("R"))].Default == FADSKValue::MakeInt(0), LexToString(Definition.GetSlots()[Definition.FindSlot(TEXT("R"))].Default)));
		ASSERT_THAT(AreEqual(EADSKValueType::Int, Definition.GetEventParams()[Definition.GetEvents()[Definition.FindEvent(TEXT("Set"))].FirstParam].Type));
	}

	TEST_METHOD(MalformedAndOverflowDefaultsAreTypeMismatches)
	{
		for (const TCHAR* Bad : {TEXT("1.5"), TEXT("+1"), TEXT(" 1"), TEXT("0x1"), TEXT("1e2"), TEXT("2147483648"), TEXT("-2147483649")})
		{
			const FADSKCompileResult Result = Load(ProbeXml(FString::Printf(TEXT("default=\"%s\""), Bad), TEXT("")));
			ADSK_ASSERT(Has(Result, Code::TypeMismatch, EADSKSeverity::Error, 3));
		}
	}

	TEST_METHOD(IntPortRejectsAFloatSlot)
	{
		const FADSKCompileResult Result = Load(ProbeXml(TEXT("default=\"1\""), TEXT("")).Replace(TEXT("out-result=\"R\""), TEXT("out-result=\"F\"")));
		ASSERT_THAT(IsTrue(Result.HasError(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == Code::TypeMismatch; }), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(ReflectedIntFieldConvertsStrictly)
	{
		const FADSKCompileResult Good = Load(ProbeXml(TEXT("default=\"1\""), TEXT("amount=\"-12\"")));
		ASSERT_THAT(IsTrue(Good.Diagnostics.IsEmpty() && Good.IsSuccess(), ADSKTest::FormatDiagnostics(Good)));
		ASSERT_THAT(AreEqual(-12, Good.Definition->GetNodes()[0].Config.Get<FADSKTestIntConfig>().Amount));
		for (const TCHAR* Bad : {TEXT("1.5"), TEXT("+1"), TEXT(" 1"), TEXT("0x1"), TEXT("1e2"), TEXT("2147483648"), TEXT(""), TEXT("one")})
		{
			const FADSKCompileResult Result = Load(ProbeXml(TEXT("default=\"1\""), FString::Printf(TEXT("amount=\"%s\""), Bad)));
			ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == Code::TypeMismatch; }),
				FString::Printf(TEXT("[%s]:%s"), Bad, *ADSKTest::FormatDiagnostics(Result))));
		}
	}

	TEST_METHOD(EventParameterBindsIntoAnIntLocalButNotAFloatLocal)
	{
		const FADSKCompileResult Bound = Load(ProbeXml(TEXT("default=\"1\""), TEXT("")));
		ASSERT_THAT(IsTrue(Bound.IsSuccess(), ADSKTest::FormatDiagnostics(Bound)));
		const FADSKCompileResult Mismatch = Load(ProbeXml(TEXT("default=\"1\""), TEXT("")).Replace(TEXT("to=\"R\""), TEXT("to=\"F\"")));
		ASSERT_THAT(IsTrue(Mismatch.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == Code::TypeMismatch; }), ADSKTest::FormatDiagnostics(Mismatch)));
	}
};

TEST_CLASS_WITH_FLAGS(FIntRegistrySpec, "AdamskAI.Int.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SchemaExportsIntFieldAndPortTypes)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const FString Schema = FADSKTestNodes::MakeFrozenRegistry(Log)->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"amount\",\"type\":\"Int\",\"default\":\"3\"}")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"value\",\"type\":\"Int\",\"direction\":\"in\"}")), Schema));
	}
};

TEST_CLASS_WITH_FLAGS(FIntExecutionSpec, "AdamskAI.Int.Execution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(PortReadsInputAndWritesLocal)
	{
		FADSKTestRuntime Runtime(ProbeXml(TEXT("default=\"4\""), TEXT("amount=\"3\"")));
		ASSERT_THAT(AreEqual(4, ValueOf(*Runtime.Instance, TEXT("N"))));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(7, ValueOf(*Runtime.Instance, TEXT("R"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(InputOfTheWrongTypeIsRejected)
	{
		FADSKTestRuntime Runtime(ProbeXml(TEXT("default=\"4\""), TEXT("")));
		ASSERT_THAT(AreNotEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("N"), FADSKValue::MakeFloat(1.f))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("N"), FADSKValue::MakeInt(-9))));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(-6, ValueOf(*Runtime.Instance, TEXT("R"))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(EventParameterBindsIntoLocal)
	{
		FADSKTestRuntime Runtime(ProbeXml(TEXT("default=\"4\""), TEXT("")));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Set"), {ADSKParam(TEXT("V"), FADSKValue::MakeInt(123))})));
		ASSERT_THAT(AreNotEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Set"), {ADSKParam(TEXT("V"), FADSKValue::MakeFloat(1.f))})));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(AreEqual(123, ValueOf(*Runtime.Instance, TEXT("R"))));
		Runtime.Instance->Stop();
	}
};

#endif
