// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "GameFramework/Pawn.h"
#include "NativeGameplayTags.h"
#include "Program/ADSKDefinition.h"
#include "Compiler/ADSKCompiler.h"
#include "Program/ADSKNormalizedDump.h"
#include "Support/SpawnerNode.h"
#include "CQTest.h"
#if WITH_EDITOR
#include "Validate/ADSKAssetValidation.h"
#endif

namespace FClassAndTagFieldSpecPrivate
{
	/** Native tags must come from a runtime module, so the tag is filed under the plugin's runtime host module. */
	struct FScopedTag
	{
		FNativeGameplayTag Tag{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.Registered"), TEXT("AdamskAI test tag"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};
	};

	using Code = EADSKDiagnosticCode;

	TSharedRef<const FADSKNodeRegistry> MakeRegistry()
	{
		return FADSKSpawnerNode::MakeRegistry(false);
	}

	FADSKCompileResult LoadNode(const FString& Node)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"a\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\">")
			+ Node + TEXT("</Tree></Agent>");
		return FADSKCompiler::Compile(TEXT("spawn.xml"), Xml, MakeRegistry());
	}
}

using namespace FClassAndTagFieldSpecPrivate;

TEST_CLASS_WITH_FLAGS(FClassAndTagFieldSpec, "AdamskAI.Compiler.ClassAndTagField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(RegistryClassifiesClassPathAndTagAndKeepsMetaClassForClassesOnly)
	{
		const TSharedRef<const FADSKNodeRegistry> Registry = MakeRegistry();
		const FADSKNodeDescriptor* Node = Registry->Find(TEXT("Spawner"));
		ASSERT_THAT(AreEqual(EADSKFieldKind::ClassPath, Node->FindField(TEXT("spawn"))->Kind));
		ASSERT_THAT(AreEqual(EADSKFieldKind::Tag, Node->FindField(TEXT("tag"))->Kind));
		const FString Schema = Registry->ExportSchemaJson();
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"spawn\",\"type\":\"ClassPath\",\"default\":\"\",\"metaClass\":\"/Script/Engine.Pawn\"}")), Schema));
		ASSERT_THAT(IsTrue(Schema.Contains(TEXT("{\"name\":\"tag\",\"type\":\"Tag\",\"default\":\"\"}")), Schema));

		const TSharedRef<FADSKNodeRegistry> Fresh = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Wrong;
		Wrong.Name = TEXT("Wrong");
		Wrong.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Wrong.ConfigStruct = FADSKTestClassTagConfig::StaticStruct();
		FADSKFieldMap& Tag = Wrong.Fields.Add_GetRef({TEXT("tag"), TEXT("Tag")});
		Tag.MetaClass = APawn::StaticClass();
		ASSERT_THAT(AreEqual(EADSKRegisterResult::UnsupportedFieldType, Fresh->Register(MoveTemp(Wrong))));
	}

	TEST_METHOD(AuthoredClassAndTagReachTheConfigAndTheDump)
	{
		const FADSKCompileResult Result = LoadNode(TEXT("<Spawner id=\"s1\" spawn=\"/Script/Engine.Character\" tag=\"AdamskAI.Test.Registered\"/>"));
		ASSERT_THAT(IsTrue(Result.IsSuccess() && Result.Diagnostics.IsEmpty(), ADSKTest::FormatDiagnostics(Result)));
		const FADSKTestClassTagConfig& Config = *Result.Definition->GetNodes()[0].Config.GetPtr<FADSKTestClassTagConfig>();
		ASSERT_THAT(AreEqual(FString(TEXT("/Script/Engine.Character")), Config.Spawn.ToString()));
		ASSERT_THAT(AreEqual(FName(TEXT("AdamskAI.Test.Registered")), Config.Tag.GetTagName()));
		const FString Dump = ADSKTest::DumpNormalizedTables(*Result.Definition);
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  config spawn=/Script/Engine.Character")), Dump));
		ASSERT_THAT(IsTrue(Dump.Contains(TEXT("  config tag=AdamskAI.Test.Registered")), Dump));

		const TArray<FADSKObjectPathField> Paths = ADSK::GetObjectPathFields(*Result.Definition);
		ASSERT_THAT(AreEqual(1, Paths.Num()));
		ASSERT_THAT(IsTrue(Paths[0].bClass, TEXT("the spawn path is not a class path")));
		ASSERT_THAT(IsTrue(Paths[0].MetaClass == APawn::StaticClass(), TEXT("the meta class is ") + GetNameSafe(Paths[0].MetaClass)));
		const TArray<FADSKTagField> Tags = ADSK::GetTagFields(*Result.Definition);
		ASSERT_THAT(AreEqual(1, Tags.Num()));
		ASSERT_THAT(AreEqual(FName(TEXT("AdamskAI.Test.Registered")), Tags[0].Tag));
		ASSERT_THAT(AreEqual(FName(TEXT("s1")), Tags[0].NodeId));
		// An unauthored tag is no tag, so nothing asks the validator about it.
		ASSERT_THAT(AreEqual(0, ADSK::GetTagFields(*LoadNode(TEXT("<Spawner id=\"s1\"/>")).Definition).Num()));
	}

	TEST_METHOD(MalformedClassesAndTagsAreTypeMismatch)
	{
		for (const TCHAR* Bad : {TEXT("spawn=\"\""), TEXT("spawn=\"not a path\""), TEXT("tag=\"\""), TEXT("tag=\"A..B\""), TEXT("tag=\".A\""),
			TEXT("tag=\"A.\""), TEXT("tag=\"A B\""), TEXT("tag=\"A,B\"")})
		{
			ASSERT_THAT(AreEqual(1, LoadNode(FString::Printf(TEXT("<Spawner id=\"s1\" %s/>"), Bad)).Count(Code::TypeMismatch)));
		}
	}
};

#if WITH_EDITOR
TEST_CLASS_WITH_FLAGS(FClassAndTagValidationSpec, "AdamskAI.Compiler.ClassAndTagValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ValidatorWarnsForMissingOrForeignClassesAndUnregisteredTags)
	{
		const FScopedTag Registered;
		FADSKCompileResult Clean = LoadNode(TEXT("<Spawner id=\"s1\" spawn=\"/Script/Engine.Character\" tag=\"AdamskAI.Test.Registered\"/>"));
		FADSKAssetValidation::AppendMissingAssets(*Clean.Definition, Clean);
		ASSERT_THAT(AreEqual(0, Clean.Count(Code::MissingAsset)));

		FADSKCompileResult Foreign = LoadNode(TEXT("<Spawner id=\"s1\" spawn=\"/Script/Engine.Actor\"/>"));
		FADSKAssetValidation::AppendMissingAssets(*Foreign.Definition, Foreign);
		ASSERT_THAT(AreEqual(1, Foreign.Count(Code::MissingAsset)));
		ASSERT_THAT(AreEqual(FString(TEXT("class '/Script/Engine.Actor' is not a Pawn")), Foreign.FindFirst(Code::MissingAsset)->Message));

		FADSKCompileResult Missing = LoadNode(TEXT("<Spawner id=\"s1\" spawn=\"/Script/Engine.NoSuchClass\" tag=\"AdamskAI.Test.Unregistered\"/>"));
		FADSKAssetValidation::AppendMissingAssets(*Missing.Definition, Missing);
		ASSERT_THAT(AreEqual(2, Missing.Count(Code::MissingAsset)));
		ASSERT_THAT(IsTrue(Missing.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Message == TEXT("class '/Script/Engine.NoSuchClass' not found"); }),
			ADSKTest::FormatDiagnostics(Missing)));
		ASSERT_THAT(IsTrue(Missing.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& D) { return D.Message == TEXT("gameplay tag 'AdamskAI.Test.Unregistered' is not registered") && D.Line == 1; }),
			ADSKTest::FormatDiagnostics(Missing)));
	}
};
#endif

#endif
