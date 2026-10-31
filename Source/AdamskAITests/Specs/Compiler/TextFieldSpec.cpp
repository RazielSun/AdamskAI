// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/TestADSKNodes.h"
#include "Program/ADSKDefinition.h"
#include "Compiler/ADSKCompiler.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FTextFieldSpecPrivate
{
	TSharedRef<const FADSKNodeRegistry> MakeRegistry()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKNodeDescriptor Node;
		Node.Name = TEXT("Labelled");
		Node.ConfigStruct = FADSKTestTextConfig::StaticStruct();
		Node.Fields.Add({TEXT("section"), TEXT("Section")});
		Node.Fields.Add({TEXT("note"), TEXT("Note")});
		Node.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		ADSKTest::Register(*Registry, MoveTemp(Node));
		Registry->Freeze();
		return Registry;
	}

	FADSKCompileResult Load(const FString& Leaf)
	{
		return FADSKCompiler::Compile(TEXT("text.xml"), FString::Printf(TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\">")
			TEXT("<State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\">%s</Tree></Agent>"), *Leaf), MakeRegistry());
	}
}

using namespace FTextFieldSpecPrivate;

TEST_CLASS_WITH_FLAGS(FTextFieldSpec, "AdamskAI.Compiler.TextFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(NameAndStringFieldsTakeTheAuthoredText)
	{
		const FADSKCompileResult Result = Load(TEXT("<Labelled id=\"l\" section=\"Attack_2\" note=\"hit, then wait\"/>"));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKTestTextConfig& Config = Result.Definition->GetNodes()[0].Config.Get<FADSKTestTextConfig>();
		ASSERT_THAT(AreEqual(FName(TEXT("Attack_2")), Config.Section));
		ASSERT_THAT(AreEqual(FString(TEXT("hit, then wait")), Config.Note));
	}

	TEST_METHOD(OmittedFieldsKeepTheirDefaultsAndTheSchemaNamesTheKinds)
	{
		const FADSKCompileResult Result = Load(TEXT("<Labelled id=\"l\"/>"));
		ASSERT_THAT(IsTrue(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(AreEqual(FName(TEXT("Start")), Result.Definition->GetNodes()[0].Config.Get<FADSKTestTextConfig>().Section));
		const FString Schema = MakeRegistry()->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"section\",\"type\":\"Name\",\"default\":\"Start\"}")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"note\",\"type\":\"String\",\"default\":\"\"}")), Schema));
	}

	TEST_METHOD(ANameTooLongForAnFNameIsOutOfRange)
	{
		const FADSKCompileResult Result = Load(FString::Printf(TEXT("<Labelled id=\"l\" section=\"%s\"/>"), *FString::ChrN(NAME_SIZE, TEXT('x'))));
		ASSERT_THAT(IsFalse(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		ASSERT_THAT(IsTrue(Result.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Code == EADSKDiagnosticCode::OutOfRange; }), ADSKTest::FormatDiagnostics(Result)));
	}
};

#endif
