// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Compiler/ADSKCompiler.h"
#include "Components/ActorTestSpawner.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

TEST_CLASS_WITH_FLAGS(FParallelHostSpec, "AdamskAI.Host.Parallel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheSnapshotListsTheBackgroundNode)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"par\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><SimpleParallel id=\"both\"><Wait id=\"front\" duration=\"100\"/><Wait id=\"back\" duration=\"100\"/></SimpleParallel></Tree></Agent>");
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("par.xml"), Xml, Registry.ToSharedRef());
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		FActorTestSpawner Spawner;
		AActor& Actor = Spawner.SpawnActor<AActor>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Actor);
		Actor.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		Subsystem->RegisterBrain(TEXT("debug/par.xml"), Brain);
		Brain->StartLogic();
		Brain->TickBrain(0.1f);
		const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(&Spawner.GetWorld());
		Subsystem->UnregisterBrain(TEXT("debug/par.xml"), Brain);
		Brain->Cleanup();
		ASSERT_THAT(AreEqual(1, Snapshot.Rows.Num()));
		ASSERT_THAT(AreEqual(FString(TEXT("front")), Snapshot.Rows[0].ActiveNodeId.ToString()));
		ASSERT_THAT(AreEqual(TArray<FName>({TEXT("back")}), Snapshot.Rows[0].BackgroundNodeIds));
		ASSERT_THAT(IsTrue(Snapshot.ToLines().ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("node=front")) && Line.EndsWith(TEXT(" background=[back]")); }), FString::Join(Snapshot.ToLines(), TEXT("\n"))));
	}
};

#endif
