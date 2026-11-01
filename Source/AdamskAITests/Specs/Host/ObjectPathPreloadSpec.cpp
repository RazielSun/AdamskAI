// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Components/ActorTestSpawner.h"
#include "Settings/ADSKSettings.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/SpawnerNode.h"
#include "Support/TempTree.h"
#include "CQTest.h"
#if WITH_EDITOR
#include "Cook/ADSKCookReferences.h"
#endif

namespace FObjectPathPreloadSpecPrivate
{
	const TCHAR* SourceFile = TEXT("preload.xml");
	const TCHAR* Texture = TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture");
	const TCHAR* Missing = TEXT("/Game/Missing/Preload_Nope.Preload_Nope");

	FString MakeXml(const FString& First, const FString& Second, const TCHAR* Wait = TEXT("100"))
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"preload\"><Inputs/><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Sequence id=\"q\"><PathProbe id=\"p1\" query=\"%s\"/><PathProbe id=\"p2\" query=\"%s\"/><Wait id=\"w\" duration=\"%s\"/></Sequence></Tree></Agent>"),
			*First, *Second, Wait);
	}

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();

		FFixture(const FString& Xml)
		{
			IFileManager::Get().MakeDirectory(*Tree.Root, true);
			Tree.Write(SourceFile, Xml);
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FCompilerSpecSupport::MakePathProbeRegistry());
		}
	};
}

using namespace FObjectPathPreloadSpecPrivate;

TEST_CLASS_WITH_FLAGS(FObjectPathPreloadSpec, "AdamskAI.Host.ObjectPathPreload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AcquireRetainsDeduplicatedObjects)
	{
		FFixture F(MakeXml(Texture, Texture));
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error).IsValid(), Error));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetPendingRetainedCount(SourceFile)));
	}

	TEST_METHOD(MissingPathWarnsOncePerSource)
	{
		TestRunner->AddExpectedError(TEXT("could not load object"), EAutomationExpectedErrorFlags::Contains, 1);
		FFixture F(MakeXml(Texture, Missing));
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error).IsValid(), Error));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		F.Tree.Write(SourceFile, MakeXml(Texture, Missing, TEXT("50")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetPendingRetainedCount(SourceFile)));
	}

	TEST_METHOD(PendingPromotesToActiveOnCommit)
	{
		FFixture F(MakeXml(Missing, Missing));
		TestRunner->AddExpectedError(TEXT("could not load object"), EAutomationExpectedErrorFlags::Contains, 1);
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error).IsValid(), Error));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		F.Tree.Write(SourceFile, MakeXml(Texture, Texture));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
	}

	TEST_METHOD(InvalidAndUnchangedCandidatesClearPending)
	{
		FFixture F(MakeXml(Texture, Texture));
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error).IsValid(), Error));
		F.Tree.Write(SourceFile, MakeXml(Texture, Texture, TEXT("50")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		F.Tree.Write(SourceFile, TEXT("<Agent"));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Rejected, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		F.Tree.Write(SourceFile, MakeXml(Texture, Texture, TEXT("50")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
		F.Tree.Write(SourceFile, MakeXml(Texture, Texture));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Unchanged, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
	}

	TEST_METHOD(AcquireWaitsForAPrewarmAndRetainsItsObjects)
	{
		FFixture F(MakeXml(Texture, Texture));
		F.Subsystem->Prewarm(SourceFile);
		ASSERT_THAT(IsTrue(F.Subsystem->IsPrewarming(SourceFile), TEXT("the file is not prewarming")));
		FString Error;
		const TSharedPtr<const FADSKDefinition> Definition = F.Subsystem->Acquire(SourceFile, Error);
		ASSERT_THAT(IsTrue(Definition.IsValid(), Error));
		ASSERT_THAT(IsFalse(F.Subsystem->IsPrewarming(SourceFile), TEXT("still prewarming after Acquire")));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error) == Definition, TEXT("Acquire did not return the prewarmed definition")));
	}

	TEST_METHOD(AWorldWithoutAGameInstanceIsNotPrewarmed)
	{
		// The shape of a standalone game's placeholder world before its first LoadMap: created with no game instance.
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		const TArray<FString> Saved = Settings->PrewarmFiles;
		Settings->PrewarmFiles = {SourceFile};
		FActorTestSpawner Spawner;
		const bool bPrewarming = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>()->IsPrewarming(SourceFile);
		Settings->PrewarmFiles = Saved;
		ASSERT_THAT(IsFalse(bPrewarming, TEXT("a world with no game instance prewarmed")));
	}

	TEST_METHOD(AFailedPrewarmLeavesTheDiagnosisToAcquire)
	{
		FFixture F(TEXT("<Agent"));
		F.Subsystem->Prewarm(SourceFile);
		FString Error;
		ASSERT_THAT(IsFalse(F.Subsystem->Acquire(SourceFile, Error).IsValid(), TEXT("a broken file was acquired")));
		ASSERT_THAT(IsTrue(Error.StartsWith(TEXT("compile\n  ")) && Error.Contains(FString(SourceFile) + TEXT(":")), Error));
		ASSERT_THAT(IsFalse(F.Subsystem->IsPrewarming(SourceFile), TEXT("still prewarming after Acquire")));
	}

#if WITH_EDITOR
	TEST_METHOD(CookCollectsEveryNamedPackageOnceAndSkipsBrokenFiles)
	{
		FFixture F(MakeXml(Texture, Missing));
		F.Tree.Write(TEXT("broken.xml"), TEXT("<Agent"));
		TArray<FString> Skipped;
		const TArray<FName> Packages = FADSKCookReferences::CollectPackages(F.Tree.Root, FCompilerSpecSupport::MakePathProbeRegistry(), Skipped);
		ASSERT_THAT(AreEqual(2, Packages.Num()));
		ASSERT_THAT(IsTrue(Packages.Contains(FName(TEXT("/Engine/EngineResources/DefaultTexture"))), TEXT("packages: ") + FString::JoinBy(Packages, TEXT(", "), [](const FName& Package) { return Package.ToString(); })));
		ASSERT_THAT(IsTrue(Packages.Contains(FName(TEXT("/Game/Missing/Preload_Nope"))), TEXT("packages: ") + FString::JoinBy(Packages, TEXT(", "), [](const FName& Package) { return Package.ToString(); })));
		ASSERT_THAT(AreEqual(1, Skipped.Num()));
	}
#endif

	TEST_METHOD(CommitTimeRejectionKeepsActiveRefs)
	{
		FFixture F(MakeXml(Texture, Texture));
		AActor& Actor = F.Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
		Actor.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		const TSharedRef<const FADSKDefinition> Active = Brain->GetDefinition();
		bool bReject = false;
		FString Error;
		UObject* Owner = &Actor;
		ASSERT_THAT(IsTrue(Brain->AddBindingValidator(Owner, [&bReject](const FADSKDefinition&, FString& Out) { if (!bReject) return true; Out = TEXT("late"); return false; }, Error), Error));
		F.Tree.Write(SourceFile, MakeXml(Texture, Texture, TEXT("50")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		bReject = true;
		TestRunner->AddExpectedMessagePlain(TEXT("reload rejected by"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		F.Subsystem->CommitPendingReloads();
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetPendingRetainedCount(SourceFile)));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() == &Active.Get(), TEXT("the rejected reload replaced the definition")));
		Brain->Cleanup();
	}

	TEST_METHOD(UnregisteredTagsAndForeignOrMissingClassesWarnOncePerSource)
	{
		TestRunner->AddExpectedError(TEXT("gameplay tag 'AdamskAI.Test.Nowhere' is not registered"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("'/Script/Engine.Actor' is not a Pawn class"), EAutomationExpectedErrorFlags::Contains, 1);
		TestRunner->AddExpectedError(TEXT("could not load object '/Script/Engine.NoSuchClass'"), EAutomationExpectedErrorFlags::Contains, 1);
		const auto Xml = [](const TCHAR* Wait)
		{
			return FString::Printf(TEXT("<Agent version=\"1\" id=\"spawn\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><Sequence id=\"q\"><Spawner id=\"a\" tag=\"AdamskAI.Test.Nowhere\"/><Spawner id=\"b\" spawn=\"/Script/Engine.Actor\"/>")
				TEXT("<Spawner id=\"c\" spawn=\"/Script/Engine.NoSuchClass\"/><Spawner id=\"d\" spawn=\"/Script/Engine.Character\"/><Wait id=\"w\" duration=\"%s\"/></Sequence></Tree></Agent>"), Wait);
		};
		FFixture F(Xml(TEXT("100")));
		F.Subsystem->SetTestRegistry(FADSKSpawnerNode::MakeRegistry());
		FString Error;
		ASSERT_THAT(IsTrue(F.Subsystem->Acquire(SourceFile, Error).IsValid(), Error));
		// Character and Actor load and are retained; the missing class is not.
		ASSERT_THAT(AreEqual(2, F.Subsystem->GetActiveRetainedCount(SourceFile)));
		F.Tree.Write(SourceFile, Xml(TEXT("50")));
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, F.Subsystem->RequestReload(SourceFile, Error)));
	}
};

#endif
