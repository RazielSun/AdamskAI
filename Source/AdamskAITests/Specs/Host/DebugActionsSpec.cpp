// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Debug/ADSKDebugActions.h"
#include "Program/ADSKDefinition.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "AIController.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Components/ActorTestSpawner.h"
#include "Misc/StringOutputDevice.h"
#include "Support/TempTree.h"

namespace FDebugActionsSpecPrivate
{
	const TCHAR* SourceFile = TEXT("actions.xml");

	FString Xml()
	{
		return TEXT("<Agent version=\"1\" id=\"actions\">")
			TEXT("<Events><Event name=\"go\"><Param name=\"Flag\" type=\"Bool\"/></Event></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"go\"/></State></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"wait\" duration=\"100\"/></Tree></Agent>");
	}

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		AAIController* Controller = &Spawner.SpawnActor<AAIController>();
		APawn* Pawn = &Spawner.SpawnActor<APawn>();
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		UADSKBrainComponent* Brain = nullptr;

		FFixture()
		{
			Controller->Possess(Pawn);
			if (!ADSKTest::Expect(Subsystem != nullptr, TEXT("the test world has no AdamskAI subsystem")))
			{
				return;
			}
			IFileManager::Get().MakeDirectory(*Tree.Root, true);
			Tree.Write(SourceFile, Xml());
			TArray<FString> Errors;
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
			Brain = NewObject<UADSKBrainComponent>(Controller);
			Controller->AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			ADSKTest::Expect(Brain->InitializeFromSource(SourceFile), TEXT("brain did not initialize from ") + FString(SourceFile));
		}

		~FFixture()
		{
			if (Brain) { Subsystem->UnregisterBrain(SourceFile, Brain); Brain->Cleanup(); }
		}

		FStringOutputDevice Event(const FString& Address, const FString& Name, TConstArrayView<FString> Args)
		{
			FStringOutputDevice Output;
			ADSKDebugActions::Event(&Spawner.GetWorld(), Address, Name, Args, Output);
			return Output;
		}
	};
}

using namespace FDebugActionsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FDebugActionsSpec, "AdamskAI.Host.DebugActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(UnknownFullPathUsesExistingTargetMessage)
	{
		FFixture F;
		FStringOutputDevice Output;
		TArray<UADSKBrainComponent*> Brains;
		ASSERT_THAT(IsFalse(ADSKDebugActions::ResolveBrains({F.Subsystem}, TEXT("/Game/Destroyed.Brain"), Brains, Output), Output));
		ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("adsk: unknown target '/Game/Destroyed.Brain'")), Output));
	}

	TEST_METHOD(InvalidEventsQueueNothing)
	{
		FFixture F;
		const FString Address = F.Brain->GetPathName();
		for (const TArray<FString>& Args : {TArray<FString>{TEXT("Flag=bad")}, TArray<FString>{}, TArray<FString>{TEXT("Other=true")}})
		{
			FStringOutputDevice Output = F.Event(Address, TEXT("go"), Args);
			TArray<FString> Queued;
			F.Brain->GetInstance()->GetQueuedEvents(Queued);
			ASSERT_THAT(IsTrue(Queued.IsEmpty(), TEXT("queued: ") + FString::Join(Queued, TEXT(", "))));
			ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("adsk: ")), Output));
		}
		UADSKBrainComponent* Uninitialized = NewObject<UADSKBrainComponent>(F.Controller);
		F.Controller->AddInstanceComponent(Uninitialized);
		Uninitialized->RegisterComponent();
		F.Subsystem->RegisterBrain(SourceFile, Uninitialized);
		FStringOutputDevice Output = F.Event(Uninitialized->GetPathName(), TEXT("go"), {TEXT("Flag=true")});
		ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("adsk: not-running")), Output));
		F.Subsystem->UnregisterBrain(SourceFile, Uninitialized);
		Uninitialized->Cleanup();
	}

	TEST_METHOD(ValidEventQueuesOnce)
	{
		FFixture F;
		F.Brain->StartLogic();
		FStringOutputDevice Output = F.Event(F.Brain->GetPathName(), TEXT("go"), {TEXT("Flag=true")});
		TArray<FString> Queued;
		F.Brain->GetInstance()->GetQueuedEvents(Queued);
		ASSERT_THAT(AreEqual(TArray<FString>{TEXT("go")}, Queued));
		ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("queued for 1 of 1")), Output));
	}

	TEST_METHOD(RestartUsesPublishedDefinitionAndPreservesState)
	{
		FFixture F;
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.0f);
		const FString Hash = F.Brain->GetDefinition()->GetCompositeHashHex();
		FStringOutputDevice Output;
		ADSKDebugActions::Restart(&F.Spawner.GetWorld(), F.Brain->GetPathName(), Output);
		ASSERT_THAT(IsTrue(F.Brain->IsADSKRunning(), F.Brain->GetLastError()));
		ASSERT_THAT(AreEqual(Hash, F.Brain->GetDefinition()->GetCompositeHashHex()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), F.Brain->GetInstance()->GetTickCount()));
		ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("adsk: restarted")), Output));
	}

	TEST_METHOD(RestartStartsAStoppedBrain)
	{
		FFixture F;
		F.Brain->StartLogic();
		F.Brain->StopLogic(TEXT("test"));
		FStringOutputDevice Output;
		ADSKDebugActions::Restart(&F.Spawner.GetWorld(), F.Brain->GetPathName(), Output);
		ASSERT_THAT(IsTrue(F.Brain->IsADSKRunning(), F.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(FString(Output).Contains(TEXT("adsk: restarted")), FString(Output)));
	}
};

#endif
