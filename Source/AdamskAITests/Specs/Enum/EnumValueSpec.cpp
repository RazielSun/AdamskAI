// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Nodes/ADSKBuiltinNodes.h"
#include "Program/ADSKNormalizedDump.h"
#include "Runtime/ADSKInstance.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;
using Code = EADSKDiagnosticCode;

namespace FEnumValueSpecPrivate
{
	FString Agent(const FString& Enums, const FString& Inputs = FString(), const FString& Locals = FString(),
		const FString& Events = FString(), const FString& State = TEXT("<State id=\"s\" tree=\"t\"/>"),
		const FString& Tree = TEXT("<Wait id=\"w\" duration=\"100\"/>"))
	{
		return TEXT("<Agent version=\"1\" id=\"enum\"><Enums>") + Enums + TEXT("</Enums><Inputs>") + Inputs
			+ TEXT("</Inputs><Locals>") + Locals + TEXT("</Locals><Events>") + Events
			+ TEXT("</Events><StateMachine id=\"m\" initial=\"s\">") + State
			+ TEXT("</StateMachine><Tree id=\"t\">") + Tree + TEXT("</Tree></Agent>");
	}

	const FString Intent = TEXT("<Enum name=\"Intent\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>");
}

using namespace FEnumValueSpecPrivate;

TEST_CLASS_WITH_FLAGS(FEnumCompilerSpec, "AdamskAI.Enum.Compiler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(LinksDefinitionScopedTablesAndDefaults)
	{
		const FADSKCompileResult Result = Load(Agent(Intent,
			TEXT("<Input name=\"Mode\" type=\"Intent\"/><Input name=\"RequiredMode\" type=\"Intent\" required=\"true\"/>")));
		ASSERT_THAT(IsTrue(Result.Diagnostics.IsEmpty() && Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKDefinition& Definition = *Result.Definition;
		ASSERT_THAT(AreEqual(1, Definition.GetEnums().Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent")), Definition.GetEnums()[0].Name));
		ASSERT_THAT(AreEqual(2, Definition.GetEnums()[0].ValueCount));
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("None"), TEXT("Engage")}, TArray<FString>(Definition.GetEnumValueNames())));
		const FADSKSlotDefinition& Mode = Definition.GetSlots()[Definition.FindSlot(TEXT("Mode"))];
		const FADSKSlotDefinition& Required = Definition.GetSlots()[Definition.FindSlot(TEXT("RequiredMode"))];
		ASSERT_THAT(AreEqual(EADSKValueType::Enum, Mode.Type, FString::Printf(TEXT("Mode is %s"), LexToString(Mode.Type))));
		ASSERT_THAT(AreEqual(0, Mode.Enum));
		ASSERT_THAT(IsTrue(Mode.Default == FADSKValue::MakeEnum(0, 0), LexToString(Mode.Default)));
		ASSERT_THAT(AreEqual(EADSKValueType::Enum, Required.Type, FString::Printf(TEXT("RequiredMode is %s"), LexToString(Required.Type))));
		ASSERT_THAT(IsTrue(Required.bRequired, TEXT("RequiredMode is not required")));
		ASSERT_THAT(AreEqual(0, Definition.FindEnum(TEXT("Intent"))));
		ASSERT_THAT(AreEqual(INDEX_NONE, Definition.FindEnum(TEXT("intent"))));
		ASSERT_THAT(IsTrue(Definition.MakeEnumValue(TEXT("Intent"), TEXT("Engage")) == TOptional<FADSKValue>(FADSKValue::MakeEnum(0, 1)),
			LexToString(Definition.MakeEnumValue(TEXT("Intent"), TEXT("Engage")).Get(FADSKValue()))));
		ASSERT_THAT(IsFalse(Definition.MakeEnumValue(TEXT("Intent"), TEXT("engage")).IsSet(), TEXT("enum value names matched ignoring case")));
	}

	TEST_METHOD(RejectsNamesCollisionsPlacementAndUnknownDefaults)
	{
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<Enum name=\"Bool\"><Value name=\"A\"/></Enum>"))).Count(Code::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<Enum name=\"Bad_Name\"><Value name=\"A\"/></Enum>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<Enum name=\"Mode\"><Value name=\"A\"/><Value name=\"A\"/></Enum>"))).Count(Code::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Load(Agent(TEXT("<Enum name=\"Mode\"/>"))).Count(Code::BadArity)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Intent, TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"engage\"/>"))).Count(Code::UnknownEnumValue)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Intent, FString(), TEXT("<Local name=\"Mode\" type=\"Missing\"/>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Intent, FString(), FString(), TEXT("<Event name=\"E\"><Param name=\"Mode\" type=\"Missing\"/></Event>"))).Count(Code::TypeMismatch)));
		ASSERT_THAT(AreEqual(1, Load(Agent(Intent, FString(), TEXT("<Local name=\"Mode\" type=\"Intent\" default=\"Engage\"/>"))).Count(Code::UnknownAttribute)));
	}

	TEST_METHOD(LocalsAndEventParamsTakeEnumsAndBindOnlyTheSameEnum)
	{
		const FString Phase = TEXT("<Enum name=\"Phase\"><Value name=\"Idle\"/><Value name=\"Busy\"/></Enum>");
		const FString Events = TEXT("<Event name=\"Order\"><Param name=\"Mode\" type=\"Intent\"/></Event>");
		const FADSKCompileResult Loaded = Load(Agent(Intent, FString(), TEXT("<Local name=\"Mode\" type=\"Intent\"/>"), Events,
			TEXT("<State id=\"s\" tree=\"t\"><On event=\"Order\"><Bind param=\"Mode\" to=\"Mode\"/></On></State>")));
		ASSERT_THAT(IsTrue(Loaded.Diagnostics.IsEmpty() && Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		const FADSKDefinition& Definition = *Loaded.Definition;
		const FADSKSlotDefinition& Mode = Definition.GetSlots()[Definition.FindSlot(TEXT("Mode"))];
		ASSERT_THAT(AreEqual(EADSKValueType::Enum, Mode.Type, FString::Printf(TEXT("Mode is %s"), LexToString(Mode.Type))));
		ASSERT_THAT(IsFalse(Mode.bIsInput, TEXT("Mode is an input")));
		ASSERT_THAT(IsTrue(Mode.Default == FADSKValue::MakeEnum(0, 0), LexToString(Mode.Default)));
		ASSERT_THAT(AreEqual(EADSKValueType::Enum, Definition.GetEventParams()[0].Type, FString::Printf(TEXT("the param is %s"), LexToString(Definition.GetEventParams()[0].Type))));
		ASSERT_THAT(AreEqual(0, Definition.GetEventParams()[0].Enum));
		ASSERT_THAT(IsTrue(ADSKTest::DumpNormalizedTables(Definition).Contains(TEXT("[params]\n0 Mode Enum enum=0\n")), ADSKTest::DumpNormalizedTables(Definition)));

		const FADSKCompileResult Crossed = Load(Agent(Intent + Phase, FString(), TEXT("<Local name=\"Mode\" type=\"Phase\"/>"), Events,
			TEXT("<State id=\"s\" tree=\"t\"><On event=\"Order\"><Bind param=\"Mode\" to=\"Mode\"/></On></State>")));
		ASSERT_THAT(AreEqual(1, Crossed.Count(Code::TypeMismatch)));
	}

	TEST_METHOD(AnEnumEventParamReachesItsLocalAndGuardsATransition)
	{
		const FString Events = TEXT("<Event name=\"Order\"><Param name=\"Mode\" type=\"Intent\"/></Event><Event name=\"Go\"/>");
		const FString States = TEXT("<State id=\"s\" tree=\"t\"><On event=\"Order\"><Bind param=\"Mode\" to=\"Mode\"/></On>"
			"<On event=\"Go\" goto=\"engaged\" if=\"Mode\" is=\"Engage\"/></State><State id=\"engaged\" tree=\"t\"/>");
		const FADSKCompileResult Loaded = Load(Agent(Intent, FString(), TEXT("<Local name=\"Mode\" type=\"Intent\"/>"), Events, States));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKInstance Instance(Loaded.Definition.ToSharedRef());
		ADSK_ASSERT(ADSKTest::Start(Instance));
		const FADSKNamedValue Raw[] = {{TEXT("Mode"), FADSKValue::MakeInt(1)}};
		const FADSKNamedValue OutOfRange[] = {{TEXT("Mode"), FADSKValue::MakeEnum(0, 2)}};
		const FADSKNamedValue OtherEnum[] = {{TEXT("Mode"), FADSKValue::MakeEnum(1, 1)}};
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.EnqueueEvent(TEXT("Order"), Raw)));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.EnqueueEvent(TEXT("Order"), OutOfRange)));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.EnqueueEvent(TEXT("Order"), OtherEnum)));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Instance.EnqueueEvent(TEXT("Go"))));
		Instance.Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("s")), Instance.GetActiveStatePath().Last()));

		const FADSKNamedValue Engage[] = {{TEXT("Mode"), *Loaded.Definition->MakeEnumValue(TEXT("Intent"), TEXT("Engage"))}};
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Instance.EnqueueEvent(TEXT("Order"), Engage)));
		Instance.Tick(0.1f);
		FADSKValue Mode;
		ASSERT_THAT(IsTrue(Instance.GetValue(TEXT("Mode"), Mode), TEXT("no slot Mode")));
		ASSERT_THAT(IsTrue(Mode == FADSKValue::MakeEnum(0, 1), LexToString(Mode)));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Instance.EnqueueEvent(TEXT("Go"))));
		Instance.Tick(0.1f);
		ASSERT_THAT(AreEqual(FString(TEXT("engaged")), Instance.GetActiveStatePath().Last()));
	}

	TEST_METHOD(EnforcesDeclarationAndValueLimitsWithoutCascades)
	{
		FString Enums;
		for (int32 Enum = 0; Enum < 17; ++Enum)
		{
			Enums += FString::Printf(TEXT("<Enum name=\"E%d\"><Value name=\"V\"/></Enum>"), Enum);
		}
		const FADSKCompileResult TooManyEnums = Load(Agent(Enums));
		ASSERT_THAT(AreEqual(1, TooManyEnums.Count(Code::LimitExceeded)));

		FString Values;
		for (int32 Value = 0; Value < 33; ++Value)
		{
			Values += FString::Printf(TEXT("<Value name=\"V%d\"/>"), Value);
		}
		const FADSKCompileResult TooManyValues = Load(Agent(TEXT("<Enum name=\"Mode\">") + Values + TEXT("</Enum>")));
		ASSERT_THAT(AreEqual(1, TooManyValues.Count(Code::LimitExceeded)));
	}
};

TEST_CLASS_WITH_FLAGS(FEnumInputSpec, "AdamskAI.Enum.Input",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SymbolicAndRawWritesAreDefinitionSafe)
	{
		const FADSKCompileResult Loaded = Load(Agent(
			Intent + TEXT("<Enum name=\"Phase\"><Value name=\"None\"/><Value name=\"Engage\"/></Enum>"),
			TEXT("<Input name=\"Mode\" type=\"Intent\" required=\"true\"/><Input name=\"Count\" type=\"Int\" default=\"0\"/>")));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FADSKInstance Instance(Loaded.Definition.ToSharedRef());
		ASSERT_THAT(AreEqual(EADSKInputResult::UnknownName, Instance.SetEnumInput(TEXT("Missing"), TEXT("Engage"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.SetEnumInput(TEXT("Count"), TEXT("Engage"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.SetEnumInput(TEXT("Mode"), TEXT("engage"))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.SetInput(TEXT("Mode"), FADSKValue::MakeEnum(1, 1))));
		ASSERT_THAT(AreEqual(EADSKInputResult::TypeMismatch, Instance.SetInput(TEXT("Mode"), FADSKValue::MakeEnum(0, 2))));
		ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Instance.SetEnumInput(TEXT("Mode"), TEXT("Engage"))));
		ADSK_ASSERT(ADSKTest::Start(Instance));
		FADSKValue Mode;
		ASSERT_THAT(IsTrue(Instance.GetValue(TEXT("Mode"), Mode), TEXT("no slot Mode")));
		ASSERT_THAT(IsTrue(Mode == FADSKValue::MakeEnum(0, 1), LexToString(Mode)));
	}

	TEST_METHOD(FormattingIsNamedAndBoundsChecked)
	{
		const FADSKCompileResult Loaded = Load(Agent(Intent, TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"Engage\"/>")));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		ASSERT_THAT(AreEqual(FString(TEXT("Intent:Engage")), ADSK::FormatValue(*Loaded.Definition, FADSKValue::MakeEnum(0, 1))));
		ASSERT_THAT(AreEqual(FString(TEXT("Enum#0:7")), ADSK::FormatValue(*Loaded.Definition, FADSKValue::MakeEnum(0, 7))));
		ASSERT_THAT(AreEqual(FString(TEXT("Enum#7:0")), ADSK::FormatValue(*Loaded.Definition, FADSKValue::MakeEnum(7, 0))));
	}
};

TEST_CLASS_WITH_FLAGS(FEnumRegistrySpec, "AdamskAI.Enum.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CompilerNamesAreReservedAndEnumPortsAreRejected)
	{
		for (const TCHAR* Name : {TEXT("Enums"), TEXT("Enum"), TEXT("Value"), TEXT("Run")})
		{
			const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
			FADSKNodeDescriptor Descriptor;
			Descriptor.Name = Name;
			Descriptor.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
			const EADSKRegisterResult Result = Registry->Register(MoveTemp(Descriptor));
			ASSERT_THAT(AreEqual(EADSKRegisterResult::ReservedName, Result, FString::Printf(TEXT("%s: %s"), Name, LexToString(Result))));
		}
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Descriptor;
		Descriptor.Name = TEXT("EnumProbe");
		Descriptor.Ports.Add({TEXT("mode"), EADSKValueType::Enum, EADSKPortDirection::In});
		Descriptor.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		const EADSKRegisterResult Registered = Registry->Register(MoveTemp(Descriptor));
		ASSERT_THAT(AreEqual(EADSKRegisterResult::UnsupportedPortType, Registered, LexToString(Registered)));
		ASSERT_THAT(AreEqual(FString(TEXT("UnsupportedPortType")), FString(LexToString(EADSKRegisterResult::UnsupportedPortType))));
	}
};

TEST_CLASS_WITH_FLAGS(FBlockBNormalizedDumpSpec, "AdamskAI.Enum.NormalizedDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(EmitsEnumsConditionsComparisonsAndEmitOnlyWhenPresent)
	{
		const FString State = TEXT("<State id=\"s\" tree=\"t\"><On event=\"Go\" if=\"Mode\" is=\"Engage\"/></State>");
		const FString Tree = TEXT("<Guard id=\"g\" in-condition=\"Mode\" is=\"Engage\">"
			"<Emit id=\"e\" event=\"Handoff\"/></Guard>");
		const FADSKCompileResult Loaded = Load(Agent(Intent, TEXT("<Input name=\"Mode\" type=\"Intent\" default=\"Engage\"/>"),
			FString(), TEXT("<Event name=\"Go\"/><Event name=\"Handoff\"/>"), State, Tree));
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Loaded.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("[enums]\n0 Intent values=0+2\n")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("Mode Enum input required=false default=Intent:Engage enum=0")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("condition=0 negated=false value=Intent:Engage")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  compare=Intent:Engage")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  emit event=3")), Dump));
	}
};

#endif
