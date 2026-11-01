// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Components/ActorTestSpawner.h"
#include "NativeGameplayTags.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

// Active state tags become gameplay tags, one broadcast per change; unregistered tags are refused.
namespace FStateTagsHostSpecPrivate
{
	const TCHAR* SourceFile = TEXT("tags.xml");

	/** Native tags must come from a runtime module, so they are filed under the plugin's runtime host module. */
	struct FScopedTags
	{
		FNativeGameplayTag Combat{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.Combat"), TEXT("AdamskAI test tag"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};
		FNativeGameplayTag Attack{TEXT("AdamskAI"), TEXT("AdamskAIHost"), TEXT("AdamskAI.Test.Attack"), TEXT("AdamskAI test tag"), ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD};
	};

	FString Xml(const TCHAR* AttackTags)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"tags\"><Events><Event name=\"Go\"/><Event name=\"Back\"/></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"combat\"><State id=\"combat\" initial=\"search\" tags=\"AdamskAI.Test.Combat\">")
			TEXT("<State id=\"search\" tree=\"t\"><On event=\"Go\" goto=\"attack\"/></State>")
			TEXT("<State id=\"attack\" tree=\"t\" tags=\"%s\"><On event=\"Back\" goto=\"search\"/></State></State>")
			TEXT("</StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>"), AttackTags);
	}

	struct FFixture
	{
		FScopedTags Tags;
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = nullptr;
		UADSKBrainComponent* Brain = nullptr;
		TArray<FString> Broadcasts;

		explicit FFixture(const FString& Source)
		{
			Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
			Tree.Write(SourceFile, Source);
			TArray<FString> Errors;
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
			AActor& Actor = Spawner.SpawnActor<AActor>();
			Brain = NewObject<UADSKBrainComponent>(&Actor);
			Actor.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			Brain->OnActiveTagsChanged.AddLambda([this](const FGameplayTagContainer& Active) { Broadcasts.Add(Active.ToStringSimple()); });
		}

		~FFixture() { Brain->Cleanup(); }

		FString Active() const { return Brain->GetActiveTags().ToStringSimple(); }
	};
}

using namespace FStateTagsHostSpecPrivate;

TEST_CLASS_WITH_FLAGS(FStateTagsHostSpec, "AdamskAI.Host.StateTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(OneBroadcastPerChangeAndNoneForTheSameSet)
	{
		FFixture F(Xml(TEXT("AdamskAI.Test.Attack")));
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
		F.Brain->StartLogic();
		ASSERT_THAT(AreEqual(FString(TEXT("AdamskAI.Test.Combat")), F.Active()));
		ASSERT_THAT(AreEqual(1, F.Broadcasts.Num(), TEXT("broadcasts after the start")));
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, F.Broadcasts.Num(), TEXT("a tick with no transition broadcast")));
		F.Brain->EnqueueEvent(TEXT("Go"));
		F.Brain->TickBrain(0.1f);
		ASSERT_THAT(IsTrue(F.Brain->GetActiveTags().HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("AdamskAI.Test.Attack"))), F.Active()));
		ASSERT_THAT(AreEqual(2, F.Broadcasts.Num(), TEXT("broadcasts after entering attack")));
		F.Brain->StopLogic(TEXT("test"));
		ASSERT_THAT(AreEqual(FString(), F.Active(), TEXT("tags after the stop")));
		ASSERT_THAT(AreEqual(3, F.Broadcasts.Num(), TEXT("broadcasts after the stop")));
	}

	TEST_METHOD(AnUnknownTagIsRefusedAtInitialization)
	{
		FFixture F(Xml(TEXT("AdamskAI.Test.Typo")));
		ASSERT_THAT(IsFalse(F.Brain->InitializeFromSource(SourceFile), TEXT("a file with an unregistered tag initialized")));
		ASSERT_THAT(IsTrue(F.Brain->GetLastError().StartsWith(TEXT("unknown-tag: AdamskAI.Test.Typo")), F.Brain->GetLastError()));
	}

	/** A reload restarts the brain: the stop and the new start each broadcast, in the same frame, the new set last. */
	TEST_METHOD(AReloadThatChangesTheTagsEndsOnTheNewSet)
	{
		FFixture F(Xml(TEXT("AdamskAI.Test.Attack")));
		ASSERT_THAT(IsTrue(F.Brain->InitializeFromSource(SourceFile), F.Brain->GetLastError()));
		F.Brain->StartLogic();
		const int32 Before = F.Broadcasts.Num();
		F.Tree.Write(SourceFile, Xml(TEXT("AdamskAI.Test.Attack")).Replace(TEXT("tags=\"AdamskAI.Test.Combat\""), TEXT("tags=\"AdamskAI.Test.Attack\"")));
		FString Diagnostics;
		ASSERT_THAT(IsTrue(F.Subsystem->RequestReload(SourceFile, Diagnostics) == EADSKReloadResult::Queued, TEXT("not queued: ") + Diagnostics));
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(AreEqual(FString(TEXT("AdamskAI.Test.Attack")), F.Active()));
		ASSERT_THAT(AreEqual(Before + 2, F.Broadcasts.Num(), FString::Join(F.Broadcasts, TEXT(" | "))));
		ASSERT_THAT(AreEqual(FString(TEXT("AdamskAI.Test.Attack")), F.Broadcasts.Last()));
	}
};

#endif
