// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "Debug/ADSKTraceExport.h"
#include "Compiler/ADSKCompiler.h"
#include "Support/TempTree.h"
#include "AIController.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Pawn.h"

namespace FIntHostSpecPrivate
{
	const TCHAR* IntSource = TEXT("debug/int.xml");

	const TCHAR* IntXml = TEXT("<Agent version=\"1\" id=\"int\"><Inputs><Input name=\"Level\" type=\"Int\" default=\"2\"/></Inputs>")
		TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
		TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
}

using namespace FIntHostSpecPrivate;

TEST_CLASS_WITH_FLAGS(FIntHostSpec, "AdamskAI.Host.Int",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(SnapshotDisplaysAndTraceWritesNumericIntValues)
	{
		FActorTestSpawner Spawner;
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		Controller.Possess(&Pawn);
		TArray<FString> Errors;
		const TSharedPtr<const FADSKNodeRegistry> Registry = FADSKRegistryAssembly::BuildPluginCatalogue(Errors);
		const FADSKCompileResult Loaded = FADSKCompiler::Compile(TEXT("int.xml"), IntXml, Registry.ToSharedRef());
		ASSERT_THAT(IsTrue(Loaded.IsSuccess(), ADSKTest::FormatDiagnostics(Loaded)));
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);
		Controller.AddInstanceComponent(Brain);
		Brain->RegisterComponent();
		Brain->InitializeDefinition(Loaded.Definition.ToSharedRef(), FADSKRecorder::Create(64));
		UADSKWorldSubsystem* Subsystem = Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
		Subsystem->RegisterBrain(IntSource, Brain);
		Brain->SetInput(TEXT("Level"), FADSKValue::MakeInt(7));
		Brain->StartLogic();
		Brain->TickBrain(0.f);

		const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(&Spawner.GetWorld());
		bool bDisplayed = false;
		if (Snapshot.Rows.Num() == 1)
		{
			const FADSKDebugNamedValue& Input = Snapshot.Rows[0].Inputs[0];
			bDisplayed = Input.Type == TEXT("Int") && Input.Value == TEXT("7");
		}

		const FADSKTempTree Tree;
		const FADSKTraceExportResult Export = FADSKTraceExport::Write(*Brain, TEXT("IntWorld"), Tree.Root);
		FString Jsonl;
		if (Export.IsSuccess())
		{
			FFileHelper::LoadFileToString(Jsonl, *Export.Path);
		}
		const bool bNumeric = Jsonl.Contains(TEXT("\"value\":{\"type\":\"Int\",\"value\":7}"));

		Subsystem->UnregisterBrain(IntSource, Brain);
		Brain->Cleanup();
		ASSERT_THAT(IsTrue(bDisplayed, TEXT("the debug snapshot does not show Level as Int 7")));
		ASSERT_THAT(IsTrue(Export.IsSuccess(), Export.Error));
		ASSERT_THAT(IsTrue(bNumeric, TEXT("the trace export has no numeric Int value:\n") + Jsonl));
	}
};

#endif
