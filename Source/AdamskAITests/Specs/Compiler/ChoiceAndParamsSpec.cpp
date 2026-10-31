// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

using namespace FCompilerSpecSupport;

namespace FChoiceAndParamsSpecPrivate
{
	FString ChoiceXml(const TCHAR* Tree, const TCHAR* ExtraTrees = TEXT(""))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"choice\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\">%s</Tree>%s</Agent>"), Tree, ExtraTrees);
	}

	FString ChoiceLine(const FString& Xml, const TCHAR* Id)
	{
		FADSKTestRuntime Runtime(Xml);
		Runtime.Instance->Start();
		const FString Prefix = FString::Printf(TEXT("Choice:%s:"), Id);
		const FString* Line = Runtime.Log->Lines.FindByPredicate([&Prefix](const FString& L) { return L.StartsWith(Prefix); });
		return Line ? *Line : FString();
	}
}

using namespace FChoiceAndParamsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FChoiceAndParamsSpec, "AdamskAI.Compiler.ChoiceAndParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ChoiceTakesExactEnumeratorNames)
	{
		ASSERT_THAT(AreEqual(FString(TEXT("Choice:c:Calm:")), ChoiceLine(ChoiceXml(TEXT("<Choosy id=\"c\"/>")), TEXT("c"))));
		ASSERT_THAT(AreEqual(FString(TEXT("Choice:c:Angry:")), ChoiceLine(ChoiceXml(TEXT("<Choosy id=\"c\" mood=\"Angry\"/>")), TEXT("c"))));
		ASSERT_THAT(AreEqual(1, Load(ChoiceXml(TEXT("<Choosy id=\"c\" mood=\"angry\"/>"))).Count(EADSKDiagnosticCode::UnknownEnumValue)));
		ASSERT_THAT(AreEqual(1, Load(ChoiceXml(TEXT("<Choosy id=\"c\" mood=\"1\"/>"))).Count(EADSKDiagnosticCode::UnknownEnumValue)));
		ASSERT_THAT(AreEqual(1, Load(ChoiceXml(TEXT("<Choosy id=\"c\" mood=\"EADSKTestMood_MAX\"/>"))).Count(EADSKDiagnosticCode::UnknownEnumValue)));
	}

	TEST_METHOD(ParamsAreConfigurationNotChildren)
	{
		const FString Xml = ChoiceXml(TEXT("<Choosy id=\"c\"><Param name=\"A\" value=\"1.5\"/><Param name=\"Long Name\" value=\"-2\"/></Choosy>"));
		ASSERT_THAT(AreEqual(FString(TEXT("Choice:c:Calm:A=1.5,Long Name=-2")), ChoiceLine(Xml, TEXT("c"))));
		const FADSKCompileResult Loaded = Load(Xml);
		const FString Dump = ADSKTest::DumpNormalizedTables(*Loaded.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("c Choosy parent=-1 children=0+0")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  param A=1.5\n  param Long Name=-2")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  config mood=Calm")), Dump));
		const FString Typed = ChoiceXml(TEXT("<Choosy id=\"c\"><Param name=\"N\" value=\"4\" type=\"Int\"/><Param name=\"B\" value=\"false\" type=\"Bool\"/></Choosy>"));
		ASSERT_THAT(AreEqual(FString(TEXT("Choice:c:Calm:N=4,B=false")), ChoiceLine(Typed, TEXT("c"))));
		const FString TypedDump = ADSKTest::DumpNormalizedTables(*Load(Typed).Definition);
		ASSERT_THAT(IsTrue(TypedDump.Contains(TEXT("  param N=Int:4\n  param B=Bool:false")), TypedDump));
	}

	TEST_METHOD(ParamsSurviveRunExpansion)
	{
		const FString Xml = ChoiceXml(TEXT("<Run id=\"r1\" tree=\"sub\"/>"), TEXT("<Tree id=\"sub\"><Choosy id=\"c\" mood=\"Angry\"><Param name=\"A\" value=\"3\"/></Choosy></Tree>"));
		ASSERT_THAT(AreEqual(FString(TEXT("Choice:r1/c:Angry:A=3")), ChoiceLine(Xml, TEXT("r1/c"))));
	}

	TEST_METHOD(ParamOnANodeWithoutParamsIsAnUnknownElement)
	{
		ASSERT_THAT(AreEqual(1, Load(ChoiceXml(TEXT("<Wait id=\"w\"><Param name=\"A\" value=\"1\"/></Wait>"))).Count(EADSKDiagnosticCode::UnknownElement)));
		// Directly under a Tree, Param declares a tree parameter, which takes a type and no value.
		ASSERT_THAT(AreEqual(1, Load(ChoiceXml(TEXT("<Param name=\"A\" value=\"1\"/>"))).Count(EADSKDiagnosticCode::UnknownAttribute)));
	}

	TEST_METHOD(SchemaListsChoicesAndParams)
	{
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		const FString Schema = FADSKTestNodes::MakeFrozenRegistry(Log)->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"mood\",\"type\":\"Choice\",\"default\":\"Calm\",\"values\":[\"Calm\",\"Angry\"]}")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("\"attributes\":[],\"params\":true}")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"is\",\"required\":false},{\"name\":\"below\",\"required\":false},{\"name\":\"above\",\"required\":false}")), Schema));
	}

	TEST_METHOD(ParamsPropertyMustBeANamedParamArray)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("P");
		Node.ConfigStruct = FADSKTestChoiceConfig::StaticStruct();
		Node.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Node.ParamsProperty = TEXT("Mood");
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidParamsProperty, Registry->Register(Node)));
		Node.ParamsProperty = TEXT("Missing");
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidParamsProperty, Registry->Register(Node)));
		Node.ParamsProperty = TEXT("Params");
		ASSERT_THAT(AreEqual(EADSKRegisterResult::Accepted, Registry->Register(Node)));
	}
};

#endif
