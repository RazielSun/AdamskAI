// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"
#include "EngineDefines.h"

#if WITH_AUTOMATION_TESTS && ENABLE_VISUAL_LOG

#include "Brain/ADSKBrainComponent.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Compiler/ADSKCompiler.h"
#include "AIController.h"
#include "Components/ActorTestSpawner.h"
#include "GameFramework/Pawn.h"
#include "VisualLogger/VisualLogger.h"
#include "CQTest.h"
#include "Support/TestChecks.h"

namespace FVisualLoggerSpecPrivate
{
	struct FLineDevice : public FVisualLogDevice
	{
		TArray<FString> Lines;
		virtual void Serialize(const UObject*, const FName&, const FName&, const FName&, const FVisualLogEntry& Entry) override
		{
			for (const FVisualLogLine& Line : Entry.LogLines) { Lines.Add(Line.Line); }
		}
	};

	struct FControlled
	{
		FActorTestSpawner Spawner;
		AAIController& Controller = Spawner.SpawnActor<AAIController>();
		APawn& Pawn = Spawner.SpawnActor<APawn>();
		UADSKBrainComponent* Brain = NewObject<UADSKBrainComponent>(&Controller);

		FControlled()
		{
			const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"v\"><StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine>")
				TEXT("<Tree id=\"t\"><Wait id=\"w\" duration=\"100\"/></Tree></Agent>");
			const TSharedRef<const FADSKDefinition> Definition = ADSKTest::LoadOrStandIn(TEXT("v.xml"), Xml, ADSKTest::PluginCatalogue());
			Controller.Possess(&Pawn);
			Controller.AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			Brain->InitializeDefinition(Definition, FADSKRecorder::Create(32));
		}
		~FControlled() { Brain->Cleanup(); }
	};
}

using namespace FVisualLoggerSpecPrivate;

TEST_CLASS_WITH_FLAGS(FVisualLoggerSpec, "AdamskAI.Host.VisualLogger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(TheSnapshotCarriesTheDebugLinesUnderAdamskAI)
	{
		FControlled F;
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.1f);
		FVisualLogEntry Entry;
		F.Brain->DescribeSelfToVisLog(&Entry);
		const FVisualLogStatusCategory* Category = Entry.Status.FindByPredicate([](const FVisualLogStatusCategory& Each) { return Each.Category == TEXT("AdamskAI"); });
		ASSERT_THAT(IsNotNull(Category));
		ASSERT_THAT(IsTrue(Category->Data.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("state=m/s")); }), FString::Join(Category->Data, TEXT("\n"))));
		ASSERT_THAT(IsTrue(Category->Data.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("node=w")); }), FString::Join(Category->Data, TEXT("\n"))));
	}

	TEST_METHOD(DebugInfoNamesThePathAndTheNodes)
	{
		FControlled F;
		F.Brain->StartLogic();
		ASSERT_THAT(IsTrue(F.Brain->GetDebugInfoString().EndsWith(TEXT("m/s [w]")), F.Brain->GetDebugInfoString()));
	}

	TEST_METHOD(EveryTransitionIsLoggedOnce)
	{
		FControlled F;
		FLineDevice Device;
		FVisualLogger& Logger = FVisualLogger::Get();
		const bool bWasRecording = FVisualLogger::IsRecording();
		Logger.AddDevice(&Device);
		Logger.SetIsRecording(true);
		F.Brain->StartLogic();
		F.Brain->TickBrain(0.1f);
		F.Brain->TickBrain(0.1f);
		F.Brain->StopLogic();
		Logger.Flush();
		Logger.SetIsRecording(bWasRecording);
		Logger.RemoveDevice(&Device);
		ASSERT_THAT(AreEqual(1, Device.Lines.FilterByPredicate([](const FString& Line) { return Line.Contains(TEXT("(stopped) -> m/s")); }).Num()));
		ASSERT_THAT(AreEqual(1, Device.Lines.FilterByPredicate([](const FString& Line) { return Line.Contains(TEXT("m/s -> (stopped)")); }).Num()));
	}
};

#endif
