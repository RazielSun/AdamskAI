// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FObjectValueSpecPrivate
{
	FString ObjectXml(const FString& Extra = FString())
	{
		return TEXT("<Agent version=\"1\" id=\"o\">\n")
			TEXT("<Inputs><Input name=\"Target\" type=\"Object\" default=\"none\"/></Inputs>\n")
			TEXT("<Locals><Local name=\"Chosen\" type=\"Object\"/><Local name=\"Flag\" type=\"Bool\"/></Locals>\n")
			TEXT("<Events><Event name=\"Seen\"><Param name=\"who\" type=\"Object\"/></Event><Event name=\"Check\"/></Events>\n")
			TEXT("<StateMachine id=\"m\" initial=\"idle\">\n")
			TEXT("<State id=\"idle\"><On event=\"Seen\"><Bind param=\"who\" to=\"Chosen\"/></On>")
			TEXT("<On event=\"Check\" goto=\"idle\" if=\"Target\" is=\"none\"/>")
			TEXT("<On event=\"Check\" goto=\"same\" if=\"Target\" is=\"Chosen\"/>")
			TEXT("<On event=\"Check\" goto=\"other\"/></State>\n")
			TEXT("<State id=\"same\"><On event=\"Check\" goto=\"idle\"/></State><State id=\"other\"><On event=\"Check\" goto=\"idle\"/></State>\n")
			TEXT("</StateMachine>\n") + Extra + TEXT("</Agent>");
	}

	TSharedRef<const FADSKNodeRegistry> ProbeRegistry()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKNodeDescriptor Probe;
		Probe.Name = TEXT("ObjectProbe");
		Probe.Ports.Add({TEXT("value"), EADSKValueType::Object, EADSKPortDirection::In});
		Probe.Ports.Add({TEXT("copy"), EADSKValueType::Object, EADSKPortDirection::Out});
		Probe.OnStart = [](FADSKNodeContext& Context)
		{
			uint64 Id = 0;
			return Context.ReadObject(0, Id) && Context.WritePort(1, FADSKValue::MakeObject(Id)) ? EADSKNodeStatus::Succeeded : EADSKNodeStatus::Failed;
		};
		ADSKTest::Register(*Registry, MoveTemp(Probe));
		Registry->Freeze();
		return Registry;
	}

	FADSKValue ValueOf(const FADSKInstance& Instance, const TCHAR* Slot)
	{
		FADSKValue Value;
		Instance.GetValue(Slot, Value);
		return Value;
	}

	bool HasCode(const FADSKCompileResult& Result, Code Expected)
	{
		return Result.Diagnostics.ContainsByPredicate([Expected](const FADSKDiagnostic& D) { return D.Code == Expected; });
	}
}

using namespace FObjectValueSpecPrivate;

TEST_CLASS_WITH_FLAGS(FObjectParseSpec, "AdamskAI.Object.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NoneIsTheOnlyLiteral)
	{
		FADSKValue Parsed;
		ASSERT_THAT(IsTrue(ADSK::TryParseValue(EADSKValueType::Object, TEXT("none"), Parsed), TEXT("rejected none")));
		ASSERT_THAT(IsTrue(Parsed == FADSKValue::MakeObject(0) && Parsed == FADSKValue::MakeZero(EADSKValueType::Object), LexToString(Parsed)));
		for (const TCHAR* Bad : {TEXT(""), TEXT("None"), TEXT("null"), TEXT("0"), TEXT("1"), TEXT("Object#1")})
		{
			ASSERT_THAT(IsFalse(ADSK::TryParseValue(EADSKValueType::Object, Bad, Parsed), FString::Printf(TEXT("accepted [%s]"), Bad)));
		}
	}

	TEST_METHOD(EqualityIsIdentityAndTyped)
	{
		ASSERT_THAT(IsTrue(FADSKValue::MakeObject(5) == FADSKValue::MakeObject(5), TEXT("Object#5 differs from itself")));
		ASSERT_THAT(IsTrue(FADSKValue::MakeObject(5) != FADSKValue::MakeObject(6), TEXT("Object#5 equals Object#6")));
		ASSERT_THAT(IsTrue(FADSKValue::MakeObject(0) != FADSKValue::MakeInt(0), TEXT("Object none equals Int 0")));
		ASSERT_THAT(IsFalse(ADSK::Compare(FADSKValue::MakeObject(1), EADSKCompareOp::Below, FADSKValue::MakeObject(2)), TEXT("objects compared as below")));
		ASSERT_THAT(AreEqual(FString(TEXT("none")), LexToString(FADSKValue::MakeObject(0))));
		ASSERT_THAT(AreEqual(FString(TEXT("Object#5")), LexToString(FADSKValue::MakeObject(5))));
		ASSERT_THAT(AreEqual(FString(TEXT("Object")), FString(LexToString(EADSKValueType::Object))));
	}
};

TEST_CLASS_WITH_FLAGS(FObjectCompilerSpec, "AdamskAI.Object.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(DeclaresInputLocalEventParameterAndComparisons)
	{
		const FADSKCompileResult Result = Load(ObjectXml());
		ASSERT_THAT(IsTrue(Result.Diagnostics.IsEmpty() && Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		ASSERT_THAT(IsTrue(Definition.GetSlots()[Definition.FindSlot(TEXT("Target"))].Default == FADSKValue::MakeObject(0), LexToString(Definition.GetSlots()[Definition.FindSlot(TEXT("Target"))].Default)));
		ASSERT_THAT(AreEqual(EADSKValueType::Object, Definition.GetSlots()[Definition.FindSlot(TEXT("Chosen"))].Type));
		ASSERT_THAT(AreEqual(EADSKValueType::Object, Definition.GetEventParams()[Definition.GetEvents()[Definition.FindEvent(TEXT("Seen"))].FirstParam].Type));
		const FString Dump = ADSKTest::DumpNormalizedTables(Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("Target Object input required=false default=none")), Dump));
	}

	TEST_METHOD(ARequiredObjectInputLoads)
	{
		const FADSKCompileResult Required = Load(ObjectXml().Replace(TEXT("default=\"none\""), TEXT("required=\"true\"")));
		ASSERT_THAT(IsTrue(Required.IsSuccess(), ADSKTest::FormatDiagnostics(Required)));
	}

	TEST_METHOD(AnyOtherDefaultIsATypeMismatch)
	{
		for (const TCHAR* Bad : {TEXT("None"), TEXT("0"), TEXT("Player")})
		{
			const FADSKCompileResult Result = Load(ObjectXml().Replace(TEXT("default=\"none\""), *FString::Printf(TEXT("default=\"%s\""), Bad)));
			ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::TypeMismatch), FString::Printf(TEXT("[%s]:%s"), Bad, *ADSKTest::FormatDiagnostics(Result))));
		}
	}

	TEST_METHOD(AnObjectConditionNeedsIs)
	{
		const FADSKCompileResult Bare = Load(ObjectXml().Replace(TEXT("if=\"Target\" is=\"none\""), TEXT("if=\"Target\"")));
		ASSERT_THAT(IsTrue(Bare.HasError() && HasCode(Bare, Code::TypeMismatch), ADSKTest::FormatDiagnostics(Bare)));
		const FADSKCompileResult Below = Load(ObjectXml().Replace(TEXT("if=\"Target\" is=\"none\""), TEXT("if=\"Target\" below=\"none\"")));
		ASSERT_THAT(IsTrue(Below.HasError() && HasCode(Below, Code::TypeMismatch), ADSKTest::FormatDiagnostics(Below)));
	}

	TEST_METHOD(AnObjectComparesWithNoneOrAnObjectSlotOnly)
	{
		for (const TCHAR* Bad : {TEXT("Flag"), TEXT("Missing"), TEXT("0")})
		{
			const FADSKCompileResult Result = Load(ObjectXml().Replace(TEXT("is=\"Chosen\""), *FString::Printf(TEXT("is=\"%s\""), Bad)));
			ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::TypeMismatch), FString::Printf(TEXT("[%s]:%s"), Bad, *ADSKTest::FormatDiagnostics(Result))));
		}
	}

	TEST_METHOD(AnObjectParameterBindsOnlyIntoAnObjectLocal)
	{
		const FADSKCompileResult Result = Load(ObjectXml().Replace(TEXT("to=\"Chosen\""), TEXT("to=\"Flag\"")));
		ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::TypeMismatch), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(ObjectIsABuiltinTypeName)
	{
		const FADSKCompileResult Result = Load(ObjectXml().Replace(TEXT("<Inputs>"), TEXT("<Enums><Enum name=\"Object\"><Value name=\"A\"/></Enum></Enums>\n<Inputs>")));
		ASSERT_THAT(IsTrue(Result.HasError() && HasCode(Result, Code::DuplicateId), ADSKTest::FormatDiagnostics(Result)));
	}

	TEST_METHOD(AGuardComparesAnObject)
	{
		const FString Tree = TEXT("<Tree id=\"t\"><Guard id=\"g\" in-condition=\"Target\" is=\"none\"><Wait id=\"w\" duration=\"1\"/></Guard></Tree>\n");
		const FADSKCompileResult Result = Load(ObjectXml(Tree).Replace(TEXT("<State id=\"same\">"), TEXT("<State id=\"same\" tree=\"t\">")));
		ASSERT_THAT(IsTrue(Result.Diagnostics.IsEmpty() && Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
	}
};

TEST_CLASS_WITH_FLAGS(FObjectExecutionSpec, "AdamskAI.Object.Execution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TransitionsCompareWithNoneAndByIdentity)
	{
		FADSKTestRuntime Runtime(ObjectXml());
		ADSK_ASSERT(ADSKTest::Start(*Runtime.Instance));
		Runtime.Instance->EnqueueEvent(TEXT("Check"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("idle")}));

		const FADSKNamedValue Who = ADSKParam(TEXT("who"), FADSKValue::MakeObject(7));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->EnqueueEvent(TEXT("Seen"), MakeArrayView(&Who, 1))));
		Runtime.Instance->Tick(0.f);
		ASSERT_THAT(IsTrue(ValueOf(*Runtime.Instance, TEXT("Chosen")) == FADSKValue::MakeObject(7), LexToString(ValueOf(*Runtime.Instance, TEXT("Chosen")))));

		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Runtime.Instance->SetInput(TEXT("Target"), FADSKValue::MakeObject(7))));
		Runtime.Instance->EnqueueEvent(TEXT("Check"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("same")}));

		Runtime.Instance->EnqueueEvent(TEXT("Check"));
		Runtime.Instance->SetInput(TEXT("Target"), FADSKValue::MakeObject(8));
		Runtime.Instance->Tick(0.f);
		Runtime.Instance->EnqueueEvent(TEXT("Check"));
		Runtime.Instance->Tick(0.f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("other")}));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(AnInputOfAnotherTypeIsRefused)
	{
		FADSKTestRuntime Runtime(ObjectXml());
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->SetInput(TEXT("Target"), FADSKValue::MakeInt(7))));
		const FADSKNamedValue Who = ADSKParam(TEXT("who"), FADSKValue::MakeInt(7));
		Runtime.Instance->Start();
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Runtime.Instance->EnqueueEvent(TEXT("Seen"), MakeArrayView(&Who, 1))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(PortsReadAndWriteObjects)
	{
		const FString Xml = ObjectXml(TEXT("<Tree id=\"t\"><ObjectProbe id=\"p\" in-value=\"Target\" out-copy=\"Chosen\"/></Tree>\n"))
			.Replace(TEXT("<State id=\"idle\">"), TEXT("<State id=\"idle\" tree=\"t\">"));
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("object.xml"), Xml, ProbeRegistry());
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKInstance Instance(Loaded.Definition.ToSharedRef());
		Instance.SetInput(TEXT("Target"), FADSKValue::MakeObject(42));
		ADSK_ASSERT(ADSKTest::Start(Instance));
		ASSERT_THAT(IsTrue(ValueOf(Instance, TEXT("Chosen")) == FADSKValue::MakeObject(42), LexToString(ValueOf(Instance, TEXT("Chosen")))));
		Instance.Stop();
	}

	TEST_METHOD(AnObjectPortRefusesAnotherSlotType)
	{
		const FString Xml = ObjectXml(TEXT("<Tree id=\"t\"><ObjectProbe id=\"p\" in-value=\"Flag\" out-copy=\"Chosen\"/></Tree>\n"))
			.Replace(TEXT("<State id=\"idle\">"), TEXT("<State id=\"idle\" tree=\"t\">"));
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("object.xml"), Xml, ProbeRegistry());
		ASSERT_THAT(IsTrue(Loaded.HasError() && HasCode(Loaded, Code::TypeMismatch), ADSKTest::FormatDiagnostics(Loaded)));
	}
};

#endif
