// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Components/ActorTestSpawner.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

// Without authority a brain never compiles, loads or ticks. Roles set by hand; NetworkSpec does it networked.
namespace FAuthoritySpecPrivate
{
	const TCHAR* SourceFile = TEXT("authority.xml");
	const TCHAR* WaitXml = TEXT("<Agent version=\"1\" id=\"authority\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
	const TCHAR* Refusal = TEXT("refused on a copy without authority");

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = nullptr;

		FFixture()
		{
			Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
			Tree.Write(SourceFile, WaitXml);
			TArray<FString> Errors;
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
		}

		/** A brain on a fresh actor with Role, configured by Configure before it registers and begins play. */
		UADSKBrainComponent* Brain(ENetRole Role, TFunctionRef<void(UADSKBrainComponent&)> Configure)
		{
			AActor& Actor = Spawner.SpawnActor<AActor>();
			Actor.SetRole(Role);
			UADSKBrainComponent* Result = NewObject<UADSKBrainComponent>(&Actor);
			Configure(*Result);
			Actor.AddInstanceComponent(Result);
			Result->RegisterComponent();
			if (!Result->HasBegunPlay()) Result->BeginPlay();
			return Result;
		}

		UADSKBrainComponent* Brain(ENetRole Role) { return Brain(Role, [](UADSKBrainComponent&) {}); }
	};
}

using namespace FAuthoritySpecPrivate;

TEST_CLASS_WITH_FLAGS(FAuthoritySpec, "AdamskAI.Host.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(InitializeFromSourceIsRefusedBeforeAnythingLoads)
	{
		FFixture F;
		TestRunner->AddExpectedError(Refusal, EAutomationExpectedErrorFlags::Contains, 1);
		UADSKBrainComponent* Brain = F.Brain(ROLE_SimulatedProxy);
		ASSERT_THAT(IsFalse(Brain->InitializeFromSource(SourceFile), TEXT("a simulated proxy initialized")));
		ASSERT_THAT(AreEqual(FString(TEXT("not-authority")), Brain->GetLastError()));
		ASSERT_THAT(IsFalse(F.Subsystem->GetActiveDefinition(SourceFile).IsValid(), TEXT("the refused brain still made the world compile the file")));
		// One warning per brain, however often the host retries.
		ASSERT_THAT(IsFalse(Brain->InitializeFromSource(SourceFile)));
		Brain->Cleanup();
	}

	TEST_METHOD(InitializeDefinitionAndStartLogicAreRefused)
	{
		FFixture F;
		TestRunner->AddExpectedError(Refusal, EAutomationExpectedErrorFlags::Contains, 1);
		UADSKBrainComponent* Brain = F.Brain(ROLE_SimulatedProxy);
		Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(SourceFile, WaitXml, ADSKTest::PluginCatalogue()));
		ASSERT_THAT(IsNull(Brain->GetInstance(), TEXT("InitializeDefinition made an instance on a simulated proxy")));
		Brain->StartLogic();
		ASSERT_THAT(IsFalse(Brain->IsADSKRunning()));
		ASSERT_THAT(AreEqual(FString(TEXT("not-authority")), Brain->GetLastError()));
		Brain->Cleanup();
	}

	/** A brain initialized while it had authority cannot start after its owner lost it. */
	TEST_METHOD(StartLogicChecksTheRoleAtTheStart)
	{
		FFixture F;
		TestRunner->AddExpectedError(Refusal, EAutomationExpectedErrorFlags::Contains, 1);
		UADSKBrainComponent* Brain = F.Brain(ROLE_Authority);
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		Brain->GetOwner()->SetRole(ROLE_SimulatedProxy);
		Brain->StartLogic();
		ASSERT_THAT(IsFalse(Brain->IsADSKRunning(), TEXT("started after losing authority")));
		Brain->Cleanup();
	}

	TEST_METHOD(NeitherTickIsEnabledWithoutAuthority)
	{
		FFixture F;
		UADSKBrainComponent* Auto = F.Brain(ROLE_SimulatedProxy, [](UADSKBrainComponent& Brain) { Brain.bAutoTick = true; });
		UADSKBrainComponent* Managed = F.Brain(ROLE_SimulatedProxy, [](UADSKBrainComponent& Brain) { Brain.bManagedTick = true; });
		ASSERT_THAT(IsFalse(Auto->IsComponentTickEnabled(), TEXT("bAutoTick enabled the tick on a simulated proxy")));
		ASSERT_THAT(AreEqual(0, F.Subsystem->GetManagedBrainCount(), TEXT("managed brains")));
		Auto->Cleanup();
		Managed->Cleanup();
	}

	/** A client-local actor (not replicated) has authority on that client, so cosmetic brains keep working. */
	TEST_METHOD(AnActorWithAuthorityRuns)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.Brain(ROLE_Authority, [](UADSKBrainComponent& B) { B.bManagedTick = true; });
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		Brain->StartLogic();
		ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), Brain->GetLastError()));
		ASSERT_THAT(AreEqual(1, F.Subsystem->GetManagedBrainCount(), TEXT("managed brains")));
		Brain->Cleanup();
	}
};

#endif
