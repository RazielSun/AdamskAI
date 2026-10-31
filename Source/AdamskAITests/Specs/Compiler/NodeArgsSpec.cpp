// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Runtime/ADSKInstance.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FNodeArgsSpecPrivate
{
	/** A registry with the core nodes and `Pass`, a leaf that takes args and keeps what it read last. */
	TSharedRef<const FADSKNodeRegistry> MakeRegistry(const TSharedRef<TArray<FADSKNamedValue>>& Read)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKNodeDescriptor Pass;
		Pass.Name = TEXT("Pass");
		Pass.bTakesArgs = true;
		Pass.OnStart = [Read](FADSKNodeContext& Context)
		{
			*Read = Context.ReadArgs();
			return EADSKNodeStatus::Succeeded;
		};
		ADSKTest::Register(*Registry, Pass);
		Registry->Freeze();
		return Registry;
	}

	FString Agent(const TCHAR* Declarations, const TCHAR* Tree, const TCHAR* ExtraTrees = TEXT(""))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"args\">%s<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\">%s</Tree>%s</Agent>"), Declarations, Tree, ExtraTrees);
	}

	const TCHAR* Slots = TEXT("<Enums><Enum name=\"Mood\"><Value name=\"Calm\"/></Enum></Enums>")
		TEXT("<Inputs><Input name=\"Speed\" type=\"Float\" default=\"2.5\"/><Input name=\"Mood\" type=\"Mood\" default=\"Calm\"/></Inputs>")
		TEXT("<Locals><Local name=\"Where\" type=\"Vector\"/></Locals>");

	FADSKCompileResult Compile(const FString& Xml)
	{
		return FADSKCompiler::Compile(TEXT("args.xml"), Xml, MakeRegistry(MakeShared<TArray<FADSKNamedValue>>()));
	}

	/** Starts the file and returns what Pass read. */
	TArray<FADSKNamedValue> Run(const FString& Xml)
	{
		const TSharedRef<TArray<FADSKNamedValue>> Read = MakeShared<TArray<FADSKNamedValue>>();
		const FADSKCompileResult Result = FADSKCompiler::Compile(TEXT("args.xml"), Xml, MakeRegistry(Read));
		if (Result.IsSuccess())
		{
			FADSKInstance Instance(Result.Definition.ToSharedRef());
			Instance.Start();
		}
		return *Read;
	}
}

using namespace FNodeArgsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FNodeArgsSpec, "AdamskAI.Compiler.NodeArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ArgsReadTheirSlotsInDocumentOrder)
	{
		const TArray<FADSKNamedValue> Read = Run(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"speed\" from=\"Speed\"/><Param name=\"where\" from=\"Where\"/></Pass>")));
		ASSERT_THAT(AreEqual(2, Read.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("speed")), Read[0].Name));
		ASSERT_THAT(IsTrue(Read[0].Value == FADSKValue::MakeFloat(2.5f)));
		ASSERT_THAT(AreEqual(FString(TEXT("where")), Read[1].Name));
		ASSERT_THAT(IsTrue(Read[1].Value == FADSKValue::MakeVector(FVector::ZeroVector)));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Compile(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"speed\" from=\"Speed\"/></Pass>"))).Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  arg speed slot=")), Dump));
	}

	TEST_METHOD(ArgsSurviveRunExpansion)
	{
		const TArray<FADSKNamedValue> Read = Run(Agent(Slots, TEXT("<Run id=\"r\" tree=\"sub\"><Param name=\"v\" from=\"Speed\"/></Run>"),
			TEXT("<Tree id=\"sub\"><Param name=\"v\" type=\"Float\"/><Pass id=\"p\"><Param name=\"x\" from=\"v\"/></Pass></Tree>")));
		ASSERT_THAT(AreEqual(1, Read.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("x")), Read[0].Name));
		ASSERT_THAT(IsTrue(Read[0].Value == FADSKValue::MakeFloat(2.5f)));
	}

	TEST_METHOD(ArgsNameASlotOnceAndNeverAnEnum)
	{
		ASSERT_THAT(IsTrue(Compile(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"a\" value=\"1\"/></Pass>"))).HasError()));
		ASSERT_THAT(AreEqual(1, Compile(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"a\" from=\"Nope\"/></Pass>"))).Count(EADSKDiagnosticCode::UnresolvedReference)));
		ASSERT_THAT(AreEqual(1, Compile(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"a\" from=\"Speed\"/><Param name=\"a\" from=\"Where\"/></Pass>"))).Count(EADSKDiagnosticCode::DuplicateId)));
		ASSERT_THAT(AreEqual(1, Compile(Agent(Slots, TEXT("<Pass id=\"p\"><Param name=\"a\" from=\"Mood\"/></Pass>"))).Count(EADSKDiagnosticCode::TypeMismatch)));
	}

	TEST_METHOD(OnlyCallbackKindsWithoutParamsPropertyTakeArgs)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("P");
		Node.bTakesArgs = true;
		Node.ConfigStruct = FADSKTestChoiceConfig::StaticStruct();
		Node.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Node.ParamsProperty = TEXT("Params");
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidParamsProperty, Registry->Register(Node)));
		Node.ParamsProperty = NAME_None;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::Accepted, Registry->Register(Node)));
		Registry->Freeze();
		ASSERT_THAT(IsTrue(Registry->ExportSchemaJson().Contains(TEXT("\"args\":true"))));
	}
};

#endif
