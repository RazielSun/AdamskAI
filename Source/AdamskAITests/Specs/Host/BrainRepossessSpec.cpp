// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Sources/ADSKValidation.h"
#include "Support/TempTree.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"

// One brain living several lives on one component, the way a controller that possesses, unpossesses
// and possesses again drives it: AAIController calls Cleanup() on every unpossess.
namespace FBrainRepossessSpecPrivate
{
	const TCHAR* GuardFile = TEXT("lifecycle_guard.xml");
	const TCHAR* IdleFile = TEXT("lifecycle_idle.xml");
	const TCHAR* GuardXml = TEXT("<Agent version=\"1\" id=\"guard\"><Inputs><Input name=\"Goal\" type=\"Vector\" required=\"true\"/></Inputs><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
	const TCHAR* IdleXml = TEXT("<Agent version=\"1\" id=\"idle\"><Inputs/><Locals/><Events/><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");

	/** Tree is declared first so it outlives the world. */
	struct FRepossessWorld
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = nullptr;
		UADSKBrainComponent* Brain = nullptr;

		FRepossessWorld()
		{
			Tree.Write(GuardFile, GuardXml);
			Tree.Write(IdleFile, IdleXml);
			Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
			TArray<FString> Errors;
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FADSKRegistryAssembly::BuildPluginCatalogue(Errors));
			AActor& Actor = Spawner.SpawnActor<AActor>();
			Brain = NewObject<UADSKBrainComponent>(&Actor);
			Actor.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
		}

		bool IsRegistered() const { return Subsystem->GetAllBrains().Contains(Brain); }

		bool StartGuard()
		{
			if (!Brain->InitializeFromSource(GuardFile)) { return false; }
			Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector(100.0, 0.0, 0.0)));
			Brain->StartLogic();
			return Brain->IsADSKRunning();
		}
	};
}

using namespace FBrainRepossessSpecPrivate;

TEST_CLASS_WITH_FLAGS(FBrainRepossessSpec, "AdamskAI.Host.BrainRepossess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(CleanupStopsEveryLifeNotOnlyTheFirst)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.StartGuard(), World.Brain->GetLastError()));
		World.Brain->Cleanup();
		ASSERT_THAT(IsFalse(World.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ASSERT_THAT(IsFalse(World.IsRegistered(), TEXT("the brain is still registered")));
		ASSERT_THAT(IsTrue(World.StartGuard(), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.IsRegistered(), TEXT("the brain is not registered")));
		World.Brain->Cleanup();
		ASSERT_THAT(IsFalse(World.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ASSERT_THAT(IsFalse(World.IsRegistered(), TEXT("the brain is still registered")));
	}

	TEST_METHOD(ReinitializationForgetsThePreviousLifesInputs)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.StartGuard(), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.Brain->GetCachedInputs().Contains(TEXT("Goal")), TEXT("the Goal input is not cached")));
		ASSERT_THAT(IsTrue(World.Brain->InitializeFromSource(IdleFile), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.Brain->GetCachedInputs().IsEmpty(), FString::Printf(TEXT("%d cached inputs"), World.Brain->GetCachedInputs().Num())));
		// A stale Goal would reject every reload of a file that has no such input.
		World.Tree.Write(IdleFile, FString(IdleXml).Replace(TEXT("duration=\"100\""), TEXT("duration=\"50\"")));
		FString Diagnostics;
		ASSERT_THAT(AreEqual(EADSKReloadResult::Queued, World.Subsystem->RequestReload(IdleFile, Diagnostics)));
	}

	TEST_METHOD(FailedReinitializationKeepsTheRunningBrainRegistered)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.StartGuard(), World.Brain->GetLastError()));
		ASSERT_THAT(IsFalse(World.Brain->InitializeFromSource(TEXT("missing.xml")), TEXT("a missing file initialized")));
		ASSERT_THAT(IsFalse(World.Brain->GetLastError().IsEmpty(), TEXT("no error for the missing file")));
		ASSERT_THAT(IsTrue(World.Brain->IsADSKRunning(), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.IsRegistered(), TEXT("the brain is not registered")));
		ASSERT_THAT(AreEqual(FString(GuardFile), World.Brain->GetSourcePath()));
		ASSERT_THAT(IsTrue(World.Brain->GetCachedInputs().Contains(TEXT("Goal")), TEXT("the Goal input is not cached")));
	}

	TEST_METHOD(DirectDefinitionDetachesFromTheSourceFile)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.StartGuard(), World.Brain->GetLastError()));
		FString Error;
		const TSharedPtr<const FADSKDefinition> Idle = World.Subsystem->Acquire(IdleFile, Error);
		ASSERT_THAT(IsTrue(Idle.IsValid(), Error));
		World.Brain->InitializeDefinition(Idle.ToSharedRef());
		ASSERT_THAT(IsTrue(World.Brain->GetSourcePath().IsEmpty(), World.Brain->GetSourcePath()));
		ASSERT_THAT(IsFalse(World.IsRegistered(), TEXT("the brain is still registered")));
	}

	TEST_METHOD(StartWithoutRequiredInputSaysWhichOne)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.Brain->InitializeFromSource(GuardFile), World.Brain->GetLastError()));
		World.Brain->StartLogic();
		ASSERT_THAT(IsFalse(World.Brain->IsADSKRunning(), TEXT("the brain is still running")));
		ASSERT_THAT(IsTrue(World.Brain->GetLastError().Contains(TEXT("Goal"), ESearchCase::CaseSensitive), World.Brain->GetLastError()));
		World.Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(FVector::ZeroVector));
		World.Brain->StartLogic();
		ASSERT_THAT(IsTrue(World.Brain->IsADSKRunning(), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.Brain->GetLastError().IsEmpty(), World.Brain->GetLastError()));
	}

	TEST_METHOD(StartUnderAHeldResourceLockStaysPaused)
	{
		FRepossessWorld World;
		ASSERT_THAT(IsTrue(World.Brain->InitializeFromSource(IdleFile), World.Brain->GetLastError()));
		World.Brain->LockResource(EAIRequestPriority::Logic);
		World.Brain->StartLogic();
		ASSERT_THAT(IsTrue(World.Brain->IsADSKRunning(), World.Brain->GetLastError()));
		ASSERT_THAT(IsTrue(World.Brain->IsPaused(), TEXT("the resource lock did not pause the brain")));
		World.Brain->ClearResourceLock(EAIRequestPriority::Logic);
		ASSERT_THAT(IsFalse(World.Brain->IsPaused(), TEXT("still paused after the lock was cleared")));
		ASSERT_THAT(IsTrue(World.Brain->IsADSKRunning(), World.Brain->GetLastError()));
	}

	TEST_METHOD(UppercaseExtensionIsReportedNotSkipped)
	{
		FADSKTempTree Tree;
		Tree.Write(TEXT("Guard.XML"), IdleXml);
		TArray<FString> Errors;
		const TSharedRef<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors).ToSharedRef();
		const FADSKSourceReadResult Result = FADSKValidation::ReadSource(Tree.Root, TEXT("Guard.XML"), Registry);
		ASSERT_THAT(IsTrue(Result.bReadFailed, TEXT("Guard.XML was read")));
		ASSERT_THAT(AreEqual(1, Result.Result.Diagnostics.Num()));
		ASSERT_THAT(IsTrue(Result.Result.Diagnostics[0].Message.Contains(TEXT("lowercase .xml")), Result.Result.Diagnostics[0].Message));
#if PLATFORM_WINDOWS
		// Windows wildcards match case-insensitively, so the scan finds the file; it must surface, not vanish.
		const TArray<FADSKSourceReadResult> Found = FADSKValidation::ValidateFiles(Tree.Root, Registry);
		ASSERT_THAT(AreEqual(1, Found.Num()));
		ASSERT_THAT(IsTrue(Found[0].bReadFailed, TEXT("Guard.XML was read")));
#endif
	}
};

#endif
