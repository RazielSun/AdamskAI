// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "Debug/ADSKDebugSnapshot.h"
#include "GameFramework/PlayerController.h"
// Before 5.7 FStringOutputDevice lives in Containers/UnrealString.h.
#if __has_include("Misc/StringOutputDevice.h")
#include "Misc/StringOutputDevice.h"
#endif
#include "VisualLogger/VisualLogger.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "CQTest.h"

// Visual Logger, adsk.* commands and the Gameplay Debugger category on real controllers in PIE.
namespace FDebugSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(30);
	const FVector SpawnPoint(-1500.0, -1500.0, ADSKWorldLayout::StandZ);

#if ENABLE_VISUAL_LOG
	/** Every log line and shape description the Visual Logger records, of any owner. */
	struct FLineDevice : FVisualLogDevice
	{
		TArray<FString> Lines;
		TArray<FString> Shapes;
		virtual void Serialize(const UObject*, const FName&, const FName&, const FName&, const FVisualLogEntry& Entry) override
		{
			for (const FVisualLogLine& Line : Entry.LogLines) { Lines.Add(Line.Line); }
			for (const FVisualLogShapeElement& Shape : Entry.ElementsToDraw) { Shapes.Add(Shape.Description); }
		}
	};
#endif
}

using namespace FDebugSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldDebugSpec, "AdamskAIWorld.Debug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
#if ENABLE_VISUAL_LOG
	TUniquePtr<FLineDevice> Device;
	bool bWasRecording = false;
#endif

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
#if ENABLE_VISUAL_LOG
		if (Device)
		{
			FVisualLogger::Get().RemoveDevice(Device.Get());
			FVisualLogger::Get().SetIsRecording(bWasRecording);
			Device.Reset();
		}
#endif
	}

	void SpawnMover()
	{
		const FVector Goal = Scenario.OnNavmesh(FVector(0.0, -1500.0, 0.0));
		Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(TEXT("world_move_to.xml")), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
	}

	FString Run(const FString& Command)
	{
		FStringOutputDevice Output;
		ADSKTest::Expect(GEngine->Exec(&Scenario.GetWorld(), *Command, Output), TEXT("no console command handled: ") + Command);
		return Output;
	}

#if ENABLE_VISUAL_LOG
	/** I15: one `from -> to` line per transition, recorded from the live brain, under AdamskAI's log category. */
	TEST_METHOD(TheVisualLoggerRecordsEveryTransition)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("record, then walk"), [this]
			{
				Device = MakeUnique<FLineDevice>();
				bWasRecording = FVisualLogger::Get().IsRecording();
				FVisualLogger::Get().AddDevice(Device.Get());
				FVisualLogger::Get().SetIsRecording(true);
				SpawnMover();
			})
			.Until(TEXT("arrived"), [this] { return Agent.IsIn(TEXT("m/arrived")) || Agent.IsIn(TEXT("m/failed")); }, Timeout)
			.Then(TEXT("stop, flush"), [this]
			{
				Agent.Brain->StopLogic(TEXT("test"));
				FVisualLogger::Get().Flush();
			})
			.Then(TEXT("the transitions are in the log"), [this]
			{
				ASSERT_THAT(IsTrue(Device->Lines.Contains(TEXT("m/moving -> m/arrived")), TEXT("no 'm/moving -> m/arrived' line in:\n") + FString::Join(Device->Lines, TEXT("\n"))));
				ASSERT_THAT(IsTrue(Device->Lines.Contains(TEXT("m/arrived -> (stopped)")), TEXT("no 'm/arrived -> (stopped)' line in:\n") + FString::Join(Device->Lines, TEXT("\n"))));
				// MoveTo draws its goal with the acceptance radius.
				ASSERT_THAT(IsTrue(Device->Shapes.Contains(TEXT("move_goal goal")), TEXT("no 'move_goal goal' shape in:\n") + FString::Join(Device->Shapes, TEXT("\n"))));
			});
	}
#endif

	/** I24: the console commands against the PIE world, the way a designer types them during play. */
	TEST_METHOD(ConsoleCommandsReachThePlayWorldsBrains)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("walk"), [this] { SpawnMover(); })
			.Then(TEXT("status, then stop by event"), [this]
			{
				const FString Status = Run(TEXT("adsk.Status"));
				ASSERT_THAT(IsTrue(Status.Contains(TEXT("world_move_to.xml")) && Status.Contains(TEXT("m/moving")), TEXT("adsk.Status:\n") + Status));
				const FString Event = Run(TEXT("adsk.Event all Stop"));
				ASSERT_THAT(IsTrue(Event.Contains(TEXT("Stop queued for 1 of 1 brain(s)")), TEXT("adsk.Event:\n") + Event));
			})
			.Until(TEXT("stopped"), [this] { return Agent.IsIn(TEXT("m/stopped")); }, Timeout)
			.Then(TEXT("restart, reload"), [this]
			{
				const FString Restart = Run(TEXT("adsk.Restart all"));
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("moving")}));
				const FString Reload = Run(TEXT("adsk.Reload ") + FADSKWorldScenario::Behavior(TEXT("world_move_to.xml")));
				ASSERT_THAT(IsTrue(Reload.Contains(TEXT("unchanged")), TEXT("adsk.Reload of an unchanged file:\n") + Reload + TEXT("\nadsk.Restart:\n") + Restart));
			});
	}

	/** I25: what the AdamskAI Gameplay Debugger category draws for the pawn a player would select. */
	TEST_METHOD(GameplayDebuggerLinesDescribeTheSelectedPawn)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("walk"), [this] { SpawnMover(); })
			.Then(TEXT("lines for the pawn and for the controller"), [this]
			{
				const TArray<FString> Lines = FADSKDebugSnapshot::GameplayDebuggerLines(Agent.Pawn.Get());
				const FString Text = FString::Join(Lines, TEXT("\n"));
				ASSERT_THAT(IsTrue(Text.Contains(TEXT("m/moving")) && Text.Contains(TEXT("move_goal")), TEXT("Gameplay Debugger lines:\n") + Text));
				ASSERT_THAT(AreEqual(Lines, FADSKDebugSnapshot::GameplayDebuggerLines(Agent.Controller.Get())));
				// The player's own pawn has no brain.
				const TArray<FString> Player = FADSKDebugSnapshot::GameplayDebuggerLines(Scenario.GetWorld().GetFirstPlayerController()->GetPawn());
				ASSERT_THAT(IsTrue(Player.Num() == 1 && Player[0].Contains(TEXT("no AdamskAI brain")), TEXT("player lines:\n") + FString::Join(Player, TEXT("\n"))));
			});
	}
};

#endif
