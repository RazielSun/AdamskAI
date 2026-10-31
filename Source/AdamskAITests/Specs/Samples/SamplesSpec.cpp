// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Character.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Samples/SampleController.h"
#include "Samples/SampleNodes.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "CQTest.h"

// The compiled samples the recipes point at: they must build, register and run as documented.
namespace FSamplesSpecPrivate
{
	/** What a sample project would keep in Content/AI/sample_watch.xml. */
	const TCHAR* WatchXml = TEXT(
		"<Agent version=\"1\" id=\"sample_watch\">"
		"<Inputs><Input name=\"NoisePoint\" type=\"Vector\" required=\"true\"/></Inputs>"
		"<StateMachine id=\"mode\" initial=\"watch\"><State id=\"watch\" tree=\"watch\"><On event=\"tree.success\" goto=\"watch\"/></State></StateMachine>"
		"<Tree id=\"watch\"><Sequence id=\"watch_steps\"><SampleTurnToward id=\"watch_turn\" in-point=\"NoisePoint\"/>"
		"<Wait id=\"watch_wait\" duration=\"0.1\"/></Sequence></Tree></Agent>");

	TSharedRef<const FADSKNodeRegistry> SampleCatalogue(TArray<FString>& OutErrors)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		FADSKSampleNodeProvider().RegisterNodes(*Registry, OutErrors);
		Registry->Freeze();
		return Registry;
	}
}

using namespace FSamplesSpecPrivate;

TEST_CLASS_WITH_FLAGS(FSamplesSpec, "AdamskAI.Samples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheSampleProviderRegistersItsNode)
	{
		TArray<FString> Errors;
		const TSharedRef<const FADSKNodeRegistry> Registry = SampleCatalogue(Errors);
		ASSERT_THAT(IsTrue(Errors.IsEmpty(), FString::Join(Errors, TEXT("; "))));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("SampleTurnToward")), TEXT("SampleTurnToward is not in the catalogue")));
		ASSERT_THAT(IsNotNull(Registry->Find(TEXT("SampleSendTo")), TEXT("SampleSendTo is not in the catalogue")));
	}

	/** The controller loads the file from the behavior root, publishes NoisePoint by id and the node turns its pawn. */
	TEST_METHOD(TheSampleControllerTurnsItsPawnTowardANoise)
	{
		TArray<FString> Errors;
		const TSharedRef<const FADSKNodeRegistry> Registry = SampleCatalogue(Errors);
		FActorTestSpawner Spawner;
		FADSKTempTree Root;
		Root.Write(TEXT("sample_watch.xml"), WatchXml);
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Subsystem));
		Subsystem->SetTestRoot(Root.Root);
		Subsystem->SetTestRegistry(Registry);

		ACharacter& Pawn = Spawner.SpawnActor<ACharacter>();
		AADSKSampleController& Controller = Spawner.SpawnActor<AADSKSampleController>();
		Controller.Possess(&Pawn);
		ASSERT_THAT(IsTrue(Controller.Brain->IsADSKRunning(), TEXT("the sample brain did not start: ") + Controller.Brain->GetLastError()));

		Controller.ReportNoise(Pawn.GetActorLocation() + FVector(0.0, 500.0, 0.0));
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Controller.Tick(0.1f);
		}
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Pawn.GetActorRotation().Yaw, 90.0, 0.5), FString::Printf(TEXT("yaw %.2f, expected 90"), Pawn.GetActorRotation().Yaw)));

		// A reload brings a new definition; the FADSKInputRef resolves NoisePoint again by itself.
		const uint64 OldSerial = Controller.Brain->GetDefinition()->GetSerial();
		Root.Write(TEXT("sample_watch.xml"), FString(WatchXml).Replace(TEXT("duration=\"0.1\""), TEXT("duration=\"0.2\"")));
		FString Diagnostics;
		ASSERT_THAT(IsTrue(Subsystem->RequestReload(TEXT("sample_watch.xml"), Diagnostics) == EADSKReloadResult::Queued, TEXT("reload not queued: ") + Diagnostics));
		Subsystem->CommitPendingReloads();
		ASSERT_THAT(IsTrue(Controller.Brain->GetDefinition()->GetSerial() != OldSerial, TEXT("the reload kept the old definition")));
		Controller.ReportNoise(Pawn.GetActorLocation() + FVector(-500.0, 0.0, 0.0));
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			Controller.Tick(0.1f);
		}
		ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(FMath::Abs(Pawn.GetActorRotation().Yaw), 180.0, 0.5), FString::Printf(TEXT("yaw %.2f after the reload, expected 180"), Pawn.GetActorRotation().Yaw)));
		Controller.UnPossess();
		ASSERT_THAT(IsFalse(Controller.Brain->IsADSKRunning(), TEXT("unpossess did not stop the brain")));
	}
};

#endif
