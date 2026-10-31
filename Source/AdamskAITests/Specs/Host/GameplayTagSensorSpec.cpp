// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Compiler/ADSKCompiler.h"
#include "Components/ActorTestSpawner.h"
#include "NativeGameplayTags.h"
#include "Objects/ADSKObjectHandles.h"
#include "Support/GameplayTagActors.h"
#include "Support/TestChecks.h"
#include "CQTest.h"
#if WITH_EDITOR
#include "Validate/ADSKAssetValidation.h"
#endif

namespace FGameplayTagSensorSpecPrivate
{
	struct FScopedTags
	{
		FNativeGameplayTag Disabled{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.Sensor.Disabled"),
			TEXT("Sensor test parent"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};
		FNativeGameplayTag Stunned{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.Sensor.Disabled.Stunned"),
			TEXT("Sensor test child"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};
	};

	FString Xml(const TCHAR* Tag = TEXT("AdamskAI.Test.Sensor.Disabled"))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"tag_sensor\"><Inputs>")
			TEXT("<Input name=\"Target\" type=\"Object\" default=\"none\"/><Input name=\"Matches\" type=\"Bool\" default=\"false\"/></Inputs>")
			TEXT("<Sensors><HasGameplayTag id=\"tag\" tag=\"%s\" in-target=\"Target\" out-value=\"Matches\"/></Sensors>")
			TEXT("<Events><Event name=\"Changed\" change=\"Matches\"/></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"wait\">")
			TEXT("<On event=\"Changed\" goto=\"matched\" if=\"Matches\"/></State>")
			TEXT("<State id=\"matched\" tree=\"wait\"><On event=\"Changed\" goto=\"idle\"/></State></StateMachine>")
			TEXT("<Tree id=\"wait\"><Wait id=\"wait_node\" duration=\"100\"/></Tree></Agent>"), Tag);
	}

	TSharedRef<const FADSKNodeRegistry> Registry()
	{
		TArray<FString> Errors;
		return FADSKRegistryAssembly::BuildPluginCatalogue(Errors).ToSharedRef();
	}

	struct FRuntime
	{
		FScopedTags Tags;
		FActorTestSpawner Spawner;
		AActor& Host = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Host);

		FRuntime()
		{
			Host.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("tag_sensor.xml"), Xml(), Registry()));
			Brain->StartLogic();
		}
		~FRuntime() { Brain->Cleanup(); }

		bool Sample(AActor* Target)
		{
			ADSKTest::Expect(Brain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(Target)) == EADSKInputResult::Accepted, TEXT("target write refused"));
			Brain->TickBrain(0.1f);
			FADSKValue Value;
			ADSKTest::Expect(Brain->GetInstance()->GetValue(TEXT("Matches"), Value), TEXT("missing Matches"));
			return Value.bBool;
		}
	};
}

using namespace FGameplayTagSensorSpecPrivate;

TEST_CLASS_WITH_FLAGS(FGameplayTagSensorSpec, "AdamskAI.Host.GameplayTagSensor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(ExactAndChildTagsWorkOnDifferentActorClasses)
	{
		FRuntime Runtime;
		AADSKTestTaggedActor& Actor = Runtime.Spawner.SpawnActor<AADSKTestTaggedActor>();
		AADSKTestTaggedPawn& Pawn = Runtime.Spawner.SpawnActor<AADSKTestTaggedPawn>();
		Actor.OwnedTags.AddTag(Runtime.Tags.Disabled);
		Pawn.OwnedTags.AddTag(Runtime.Tags.Stunned);
		ASSERT_THAT(IsTrue(Runtime.Sample(&Actor)));
		ASSERT_THAT(IsTrue(Runtime.Sample(&Pawn)));
		ASSERT_THAT(AreEqual(FString(TEXT("matched")), Runtime.Brain->GetInstance()->GetActiveStatePath().Last()));
		Pawn.OwnedTags.Reset();
		ASSERT_THAT(IsFalse(Runtime.Sample(&Pawn)));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Runtime.Brain->GetInstance()->GetActiveStatePath().Last()));
	}

	TEST_METHOD(NoInterfaceNoTagNoneAndDestroyedTargetAreFalse)
	{
		FRuntime Runtime;
		AActor& Plain = Runtime.Spawner.SpawnActor<AActor>();
		Plain.Tags.Add(TEXT("AdamskAI.Test.Sensor.Disabled"));
		AADSKTestTaggedActor& Tagged = Runtime.Spawner.SpawnActor<AADSKTestTaggedActor>();
		ASSERT_THAT(IsFalse(Runtime.Sample(&Plain), TEXT("Actor Tags are not GameplayTags")));
		ASSERT_THAT(IsFalse(Runtime.Sample(&Tagged)));
		ASSERT_THAT(IsFalse(Runtime.Sample(nullptr)));
		Tagged.OwnedTags.AddTag(Runtime.Tags.Stunned);
		ASSERT_THAT(IsTrue(Runtime.Sample(&Tagged)));
		Tagged.Destroy();
		Runtime.Brain->TickBrain(0.1f);
		FADSKValue Value;
		ASSERT_THAT(IsTrue(Runtime.Brain->GetInstance()->GetValue(TEXT("Matches"), Value)));
		ASSERT_THAT(IsFalse(Value.bBool));
	}

	TEST_METHOD(InvalidBindingsAreRejectedAndUnregisteredTagsAreDiagnosed)
	{
		FScopedTags Tags;
		const TSharedRef<const FADSKNodeRegistry> Nodes = Registry();
		const FADSKCompileResult Empty = FADSKCompiler::Compile(TEXT("tag.xml"), Xml(TEXT("")), Nodes);
		ASSERT_THAT(IsFalse(Empty.IsSuccess()));
		const FString Wrong = Xml().Replace(TEXT("name=\"Matches\" type=\"Bool\""), TEXT("name=\"Matches\" type=\"Float\""));
		ASSERT_THAT(IsFalse(FADSKCompiler::Compile(TEXT("tag.xml"), Wrong, Nodes).IsSuccess()));
#if WITH_EDITOR
		FADSKCompileResult Unknown = FADSKCompiler::Compile(TEXT("tag.xml"), Xml(TEXT("AdamskAI.Test.Sensor.Unknown")), Nodes);
		ASSERT_THAT(IsTrue(Unknown.IsSuccess(), ADSKTest::FormatDiagnostics(Unknown)));
		FADSKAssetValidation::AppendMissingAssets(*Unknown.Definition, Unknown);
		ASSERT_THAT(AreEqual(1, Unknown.Count(EADSKDiagnosticCode::MissingAsset)));
		ASSERT_THAT(IsTrue(Unknown.Diagnostics.ContainsByPredicate([](const FADSKDiagnostic& Diagnostic)
		{
			return Diagnostic.Message == TEXT("gameplay tag 'AdamskAI.Test.Sensor.Unknown' is not registered");
		}), ADSKTest::FormatDiagnostics(Unknown)));
#endif
	}
};

#endif
