// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Components/ActorTestSpawner.h"
#include "Settings/ADSKSettings.h"
#include "Support/CompilerSpecSupport.h"
#include "Support/TempTree.h"
#include "CQTest.h"

namespace FReloadKeepStateSpecPrivate
{
	const TCHAR* SourceFile = TEXT("reload_keep.xml");

	/**
	 * Go(V) binds V to Kept and leaves idle for busy, whose wait runs under a cooldown; a cooldown that is not ready
	 * fails busy's tree into cooled. The edit changes only busy's wait.
	 */
	FString MakeXml(const TCHAR* Duration)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"keep\"><Locals><Local name=\"Kept\" type=\"Int\"/></Locals>")
			TEXT("<Events><Event name=\"Go\"><Param name=\"V\" type=\"Int\"/></Event></Events>")
			TEXT("<StateMachine id=\"m\" initial=\"idle\"><State id=\"idle\" tree=\"t\"><On event=\"Go\" goto=\"busy\"><Bind param=\"V\" to=\"Kept\"/></On></State>")
			TEXT("<State id=\"busy\" tree=\"b\"><On event=\"tree.failure\" goto=\"cooled\"/></State><State id=\"cooled\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree>")
			TEXT("<Tree id=\"b\"><Cooldown id=\"cd\" duration=\"100\"><Wait id=\"bw\" duration=\"%s\"/></Cooldown></Tree></Agent>"), Duration);
	}

	struct FFixture
	{
		FADSKTempTree Tree;
		FActorTestSpawner Spawner;
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		bool bSavedKeepState = GetDefault<UADSKSettings>()->bKeepStateOnReload;

		FFixture()
		{
			IFileManager::Get().MakeDirectory(*Tree.Root, true);
			Tree.Write(SourceFile, MakeXml(TEXT("100")));
			Subsystem->SetTestRoot(Tree.Root);
			Subsystem->SetTestRegistry(FCompilerSpecSupport::MakePathProbeRegistry());
		}

		~FFixture()
		{
			GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = bSavedKeepState;
		}

		/** A brain in busy with Kept 4. */
		UADSKBrainComponent* StartBusyBrain()
		{
			AActor& Actor = Spawner.SpawnActor<AActor>();
			UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
			Actor.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			ADSKTest::Expect(Brain->InitializeFromSource(SourceFile), TEXT("brain did not initialize from ") + FString(SourceFile));
			Brain->StartLogic();
			Brain->EnqueueEvent(TEXT("Go"), {{TEXT("V"), FADSKValue::MakeInt(4)}});
			Brain->TickBrain(0.f);
			return Brain;
		}

		void Reload(const FString& Xml = MakeXml(TEXT("50")))
		{
			Tree.Write(SourceFile, Xml);
			FString Error;
			const EADSKReloadResult Result = Subsystem->RequestReload(SourceFile, Error);
			ADSKTest::Expect(Result == EADSKReloadResult::Queued, FString::Printf(TEXT("reload not queued (%d): %s"), static_cast<int32>(Result), *Error));
			Subsystem->CommitPendingReloads();
		}
	};

	FString Leaf(const UADSKBrainComponent& Brain)
	{
		const TArray<FString> Path = Brain.GetInstance()->GetActiveStatePath();
		return Path.IsEmpty() ? FString() : Path.Last();
	}
}

using namespace FReloadKeepStateSpecPrivate;

TEST_CLASS_WITH_FLAGS(FReloadKeepStateSpec, "AdamskAI.Host.ReloadKeepState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AReloadRestartsFromWhereTheBrainWas)
	{
		FFixture F;
		GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = true;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		ASSERT_THAT(AreEqual(FString(TEXT("busy")), Leaf(*Brain)));
		const FADSKDefinition* Old = &Brain->GetDefinition().Get();
		F.Reload();
		ASSERT_THAT(IsTrue(&Brain->GetDefinition().Get() != Old && Brain->IsRunning(), Brain->GetLastError()));
		ASSERT_THAT(AreEqual(FString(TEXT("busy")), Leaf(*Brain)));
		FADSKValue Kept;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Kept"), Kept) && Kept == FADSKValue::MakeInt(4), LexToString(Kept)));
		Brain->Cleanup();
	}

	TEST_METHOD(ACooldownRunningAtTheReloadStillCoolsDown)
	{
		FFixture F;
		GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = true;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		Brain->TickBrain(1.f);
		ASSERT_THAT(AreEqual(FString(TEXT("busy")), Leaf(*Brain)));
		F.Reload();
		// The reload cancelled the cooldown's child, which started the cooldown: the restarted tree may not run it again.
		Brain->TickBrain(0.f);
		Brain->TickBrain(0.f);
		ASSERT_THAT(AreEqual(FString(TEXT("cooled")), Leaf(*Brain)));
		Brain->Cleanup();
	}

	TEST_METHOD(WithTheSettingOffAReloadStartsClean)
	{
		FFixture F;
		GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = false;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		F.Reload();
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Leaf(*Brain)));
		FADSKValue Kept;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Kept"), Kept) && Kept == FADSKValue::MakeInt(0), LexToString(Kept)));
		Brain->Cleanup();
	}

	TEST_METHOD(AReloadThatDropsTheLeafStateEntersItsNearestAncestor)
	{
		FFixture F;
		GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = true;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		// busy is gone: the brain restarts at the machine's entry, idle, and keeps the Locals that still match.
		F.Reload(MakeXml(TEXT("50")).Replace(TEXT("goto=\"busy\""), TEXT("goto=\"cooled\""))
			.Replace(TEXT("<State id=\"busy\" tree=\"b\"><On event=\"tree.failure\" goto=\"cooled\"/></State>"), TEXT(""))
			.Replace(TEXT("<Tree id=\"b\"><Cooldown id=\"cd\" duration=\"100\"><Wait id=\"bw\" duration=\"50\"/></Cooldown></Tree>"), TEXT("")));
		ASSERT_THAT(IsTrue(Brain->IsRunning() && Brain->GetInstance()->GetDefinition()->FindState(TEXT("busy")) == INDEX_NONE, Brain->GetLastError()));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Leaf(*Brain)));
		FADSKValue Kept;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Kept"), Kept) && Kept == FADSKValue::MakeInt(4), LexToString(Kept)));
		Brain->Cleanup();
	}

	TEST_METHOD(StartLogicFromARunningBrainIsIgnoredAndForgotten)
	{
		FFixture F;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		FADSKSnapshot Busy;
		ASSERT_THAT(IsTrue(Brain->CaptureSnapshot(Busy), TEXT("capture refused")));
		Brain->RestartLogic();
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Leaf(*Brain)));
		// Already running: the snapshot changes nothing, and is not kept for the next start either.
		Brain->StartLogicFrom(Busy);
		ASSERT_THAT(IsTrue(Brain->IsRunning(), Brain->GetLastError()));
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Leaf(*Brain)));
		Brain->RestartLogic();
		ASSERT_THAT(AreEqual(FString(TEXT("idle")), Leaf(*Brain)));
		FADSKValue Kept;
		ASSERT_THAT(IsTrue(Brain->GetInstance()->GetValue(TEXT("Kept"), Kept) && Kept == FADSKValue::MakeInt(0), LexToString(Kept)));
		Brain->Cleanup();
	}

	TEST_METHOD(APausedBrainStaysPausedWithOrWithoutKeepingState)
	{
		for (const bool bKeep : {true, false})
		{
			FFixture F;
			GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = bKeep;
			UADSKBrainComponent* Brain = F.StartBusyBrain();
			Brain->PauseLogic(TEXT("test"));
			F.Reload();
			ASSERT_THAT(IsTrue(Brain->IsPaused(), FString::Printf(TEXT("keep state %d: the reload resumed the brain"), bKeep ? 1 : 0)));
			const FString Before = Leaf(*Brain);
			Brain->TickBrain(1000.f);
			ASSERT_THAT(AreEqual(Before, Leaf(*Brain)));
			ASSERT_THAT(IsTrue(Brain->IsPaused(), TEXT("a tick resumed the brain")));
			Brain->ResumeLogic(TEXT("test"));
			ASSERT_THAT(IsTrue(Brain->IsRunning() && !Brain->IsPaused(), Brain->GetLastError()));
			Brain->Cleanup();
		}
	}

	TEST_METHOD(AStoppedBrainStaysStopped)
	{
		FFixture F;
		GetMutableDefault<UADSKSettings>()->bKeepStateOnReload = true;
		UADSKBrainComponent* Brain = F.StartBusyBrain();
		Brain->StopLogic();
		FADSKSnapshot Snapshot;
		ASSERT_THAT(IsFalse(Brain->CaptureSnapshot(Snapshot), TEXT("a stopped brain captured")));
		F.Reload();
		ASSERT_THAT(IsFalse(Brain->IsRunning(), TEXT("a reload started a stopped brain")));
		Brain->Cleanup();
	}
};

#endif
