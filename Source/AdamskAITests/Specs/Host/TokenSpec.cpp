// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Objects/ADSKObjectHandles.h"
#include "Compiler/ADSKCompiler.h"
#include "AIController.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

// Attack tokens: a pool's tokens on a target, taken by brains, held through nested and cancelled HoldToken scopes.
namespace FTokenSpecPrivate
{
	const FName Pool = TEXT("TokenSpec");

	/** An outer HoldToken whose child holds the same token again briefly, then waits long inside the outer scope only. */
	const TCHAR* NestedXml = TEXT(
		"<Agent version=\"1\" id=\"nested\"><Inputs><Input name=\"Target\" type=\"Object\" default=\"none\"/></Inputs>"
		"<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>"
		"<Tree id=\"t\"><HoldToken id=\"outer\" pool=\"TokenSpec\" in-target=\"Target\"><Sequence id=\"steps\">"
		"<HoldToken id=\"inner\" pool=\"TokenSpec\" in-target=\"Target\"><Wait id=\"short\" duration=\"0\"/></HoldToken>"
		"<Wait id=\"long\" duration=\"100\"/>"
		"</Sequence></HoldToken></Tree></Agent>");

	struct FFixture
	{
		FActorTestSpawner Spawner;
		AActor& Target = Spawner.SpawnActor<AActor>();
		UADSKGroupSubsystem* Groups = Spawner.GetWorld().GetSubsystem<UADSKGroupSubsystem>();

		UADSKBrainComponent* StartBrain(const TCHAR* Xml)
		{
			AAIController& Controller = Spawner.SpawnActor<AAIController>();
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);
			Controller.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			Brain->InitializeDefinition(ADSKTest::LoadOrStandIn(TEXT("nested.xml"), Xml, ADSKTest::PluginCatalogue()));
			Brain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(&Target));
			Brain->StartLogic();
			return Brain;
		}
	};
}

using namespace FTokenSpecPrivate;

TEST_CLASS_WITH_FLAGS(FTokenSpec, "AdamskAI.Host.Tokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AHolderKeepsItsTokenUntilItReturnedItAsOftenAsItTookIt)
	{
		FFixture F;
		const UObject* First = &F.Spawner.SpawnActor<AActor>();
		const UObject* Second = &F.Spawner.SpawnActor<AActor>();
		ASSERT_THAT(IsTrue(F.Groups->TakeToken(Pool, &F.Target, *First)));
		ASSERT_THAT(IsTrue(F.Groups->TakeToken(Pool, &F.Target, *First), TEXT("a holder could not take its own token again")));
		F.Groups->ReturnToken(Pool, &F.Target, *First);
		ASSERT_THAT(AreEqual(1, F.Groups->CountHolders(Pool, &F.Target)));
		ASSERT_THAT(IsFalse(F.Groups->TakeToken(Pool, &F.Target, *Second), TEXT("another holder took a token still held")));
		F.Groups->ReturnToken(Pool, &F.Target, *First);
		ASSERT_THAT(AreEqual(0, F.Groups->CountHolders(Pool, &F.Target)));
		ASSERT_THAT(IsTrue(F.Groups->TakeToken(Pool, &F.Target, *Second), TEXT("the token was not freed")));
	}

	TEST_METHOD(ANestedScopeEndingLeavesTheOuterScopesTokenHeld)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBrain(NestedXml);
		ASSERT_THAT(IsTrue(Brain->IsRunning(), Brain->GetLastError()));
		// The inner scope ends with its zero wait; the outer one now runs the long wait.
		for (int32 Frame = 0; Frame < 3; ++Frame) Brain->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, F.Groups->CountHolders(Pool, &F.Target)));
		// The pool has one token, so a second brain's outer scope fails to take it.
		UADSKBrainComponent* Other = F.StartBrain(NestedXml);
		Other->TickBrain(0.1f);
		ASSERT_THAT(AreEqual(1, F.Groups->CountHolders(Pool, &F.Target)));
		// Cancelling the outer scope returns it.
		Brain->StopLogic(TEXT("test"));
		ASSERT_THAT(AreEqual(0, F.Groups->CountHolders(Pool, &F.Target)));
		Other->Cleanup();
		Brain->Cleanup();
	}
};

#endif
