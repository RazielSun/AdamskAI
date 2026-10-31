// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Compiler/ADSKCompiler.h"
#include "Support/TempTree.h"
#include "AIController.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Pawn.h"
#include "CQTest.h"

namespace FBrainOwnerSpecPrivate
{
	const TCHAR* SourceFile = TEXT("owner.xml");
	const TCHAR* WaitXml = TEXT("<Agent version=\"1\" id=\"owner\"><Inputs/><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");

	void SetSource(UADSKWorldSubsystem& Subsystem, const FADSKTempTree& Tree, const FString& Xml)
	{
		IFileManager::Get().MakeDirectory(*Tree.Root, true);
		Tree.Write(SourceFile, Xml);
		TArray<FString> Errors;
		Subsystem.SetTestRoot(Tree.Root);
		Subsystem.SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
	}
}

using namespace FBrainOwnerSpecPrivate;

TEST_CLASS_WITH_FLAGS(FBrainOwnerSpec, "AdamskAI.Host.BrainOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(PlainActorInitializesAndTicksWithoutAController)
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		SetSource(*Subsystem, Tree, WaitXml);
		AActor& Actor = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
		Actor.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		Brain->StartLogic();
		Brain->TickBrain(0.25f);
		ASSERT_THAT(IsTrue(Brain->IsADSKRunning(), Brain->GetLastError()));
		ASSERT_THAT(IsNull(Brain->GetAIController()));
		ASSERT_THAT(IsTrue(Brain->GetHostActor() == &Actor, TEXT("the host actor is ") + GetNameSafe(Brain->GetHostActor())));
		Brain->Cleanup();
	}

	TEST_METHOD(ResolvesPlainControllerAndUnpossessedHosts)
	{
		FActorTestSpawner Spawner;
		AActor& Actor = Spawner.SpawnActor<AActor>();
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		UADSKBrainComponent* Plain = NewObject<UADSKBrainComponent>(&Actor);
		UADSKBrainComponent* ControllerBrain = NewObject<UADSKBrainComponent>(&Controller);
		ASSERT_THAT(IsTrue(Plain->GetHostActor() == &Actor, TEXT("the host actor is ") + GetNameSafe(Plain->GetHostActor())));
		ASSERT_THAT(IsNull(ControllerBrain->GetHostActor()));
		Controller.Possess(&Pawn);
		ASSERT_THAT(IsTrue(ControllerBrain->GetHostActor() == &Pawn, TEXT("the host actor is ") + GetNameSafe(ControllerBrain->GetHostActor())));
	}

	TEST_METHOD(ControllerOnlyNodesFailLocallyForPlainActorBrains)
	{
		const FString Xml = TEXT("<Agent version=\"1\" id=\"owner\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"0,0,0\"/></Inputs><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><MoveTo id=\"move\" in-goal=\"Goal\" acceptanceRadius=\"10\"/></Tree></Agent>");
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		SetSource(*Subsystem, Tree, Xml);
		AActor& Actor = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
		Actor.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		ASSERT_THAT(IsTrue(Brain->InitializeFromSource(SourceFile), Brain->GetLastError()));
		Brain->StartLogic();
		Brain->TickBrain(0.1f);
		ASSERT_THAT(IsFalse(Brain->GetActiveMoveRequest().IsValid(), TEXT("a brain without a controller started a move")));
		Brain->Cleanup();
	}
};

#endif
