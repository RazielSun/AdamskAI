// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Program/ADSKNormalizedDump.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FObjectPathFieldSpecPrivate
{
	using Code = EADSKDiagnosticCode;
	const TCHAR* const Texture = TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture");

	FADSKCompileResult LoadNode(const FString& Node)
	{
		TArray<FString> Lines = FCompilerSpecSupport::BaseLines();
		Lines[14] = Node;
		return FCompilerSpecSupport::LoadPathProbe(FString::Join(Lines, TEXT("\n")));
	}
}

using namespace FObjectPathFieldSpecPrivate;

TEST_CLASS_WITH_FLAGS(FObjectPathFieldSpec, "AdamskAI.Compiler.ObjectPathField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RegistryClassifiesSoftObjectPathOnly)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = FCompilerSpecSupport::MakePathProbeRegistry();
		const FADSKNodeDescriptor* Node = Registry->Find(TEXT("PathProbe"));
		ASSERT_THAT(IsNotNull(Node, TEXT("PathProbe is not registered")));
		ASSERT_THAT(AreEqual(EADSKFieldKind::ObjectPath, Node->FindField(TEXT("query"))->Kind));
		ASSERT_THAT(AreEqual(EADSKFieldKind::Value, Node->FindField(TEXT("scale"))->Kind));
		const TSharedRef<FADSKNodeRegistry> Fresh = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Other;
		Other.Name = TEXT("Other");
		Other.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Other.ConfigStruct = FADSKTestFieldConfig::StaticStruct();
		Other.Fields.Add({TEXT("spin"), TEXT("Spin")});
		ASSERT_THAT(AreEqual(EADSKRegisterResult::UnsupportedFieldType, Fresh->Register(Other)));
	}

	TEST_METHOD(EmptyAndMalformedPathsAreTypeMismatch)
	{
		for (const TCHAR* Bad : {TEXT(""), TEXT("not a path"), TEXT("/Game/Bad*Name.Bad")})
		{
			const FADSKCompileResult Result = LoadNode(FString::Printf(TEXT("<PathProbe id=\"w\" query=\"%s\"/>"), Bad));
			ADSK_ASSERT(FCompilerSpecSupport::Has(Result, Code::TypeMismatch, EADSKSeverity::Error, 15, "w"));
		}
	}

	TEST_METHOD(MissingAttributeReachesValidateConfig)
	{
		const FADSKCompileResult Result = LoadNode(TEXT("<PathProbe id=\"w\"/>"));
		ADSK_ASSERT(FCompilerSpecSupport::Has(Result, Code::OutOfRange, EADSKSeverity::Error, 15, "w"));
	}

	TEST_METHOD(TraversalAndDumpUseTheExactPath)
	{
		const FADSKCompileResult Result = LoadNode(FString::Printf(TEXT("<PathProbe id=\"w\" query=\"%s\"/>"), Texture));
		ASSERT_THAT(IsTrue(Result.IsSuccess(), ADSKTest::FormatDiagnostics(Result)));
		const TArray<FADSKObjectPathField> Fields = ADSK::GetObjectPathFields(*Result.Definition);
		ASSERT_THAT(AreEqual(1, Fields.Num()));
		ASSERT_THAT(AreEqual(FName(TEXT("w")), Fields[0].NodeId));
		ASSERT_THAT(AreEqual(15, Fields[0].Line));
		ASSERT_THAT(AreEqual(FString(Texture), Fields[0].Path.ToString()));
		ASSERT_THAT(AreEqual(Fields[0].NodeId, Result.Definition->GetNodes()[Fields[0].NodeIndex].Id));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Result.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(FString::Printf(TEXT("config query=%s"), Texture)), Dump));
	}

	TEST_METHOD(SchemaRendersObjectPathType)
	{
		const FString Schema = FCompilerSpecSupport::MakePathProbeRegistry()->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"query\",\"type\":\"ObjectPath\",\"default\":\"\"}")), Schema));
	}
};

#endif
