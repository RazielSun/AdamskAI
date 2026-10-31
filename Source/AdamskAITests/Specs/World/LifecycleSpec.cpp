// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldQueries.h"
#include "World/WorldScenario.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Editor.h"
#include "CQTest.h"

// The host contract in a live world: pawns die mid-action, controllers unpossess, PIE ends. Any error a brain logs
// fails the test through the automation framework.
namespace FLifecycleSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	const FVector SpawnPoint(-1500.0, -1500.0, ADSKWorldLayout::StandZ);

	FString QueryXml()
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"world_eqs\"><Locals><Local name=\"Point\" type=\"Vector\"/></Locals>"
			"<StateMachine id=\"m\" initial=\"querying\"><State id=\"querying\" tree=\"query\"><On event=\"tree.success\" goto=\"found\"/>"
			"<On event=\"tree.failure\" goto=\"failed\"/></State><State id=\"found\"/><State id=\"failed\"/></StateMachine>"
			"<Tree id=\"query\"><RunEQS id=\"query_run\" query=\"%s\" out-point=\"Point\"><Param name=\"Radius\" value=\"600\"/>"
			"<Param name=\"Rings\" value=\"2\" type=\"Int\"/><Param name=\"Reachable\" value=\"true\" type=\"Bool\"/></RunEQS></Tree></Agent>"),
			*ADSKWorldQueries::AssetPath(ADSKWorldQueries::RingsName));
	}
}

using namespace FLifecycleSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldLifecycleSpec, "AdamskAIWorld.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Mover;
	FADSKWorldAgent Querier;
	FADSKWorldAgent Waiter;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	void SpawnMover()
	{
		const FVector Goal = Scenario.OnNavmesh(FVector(1500.0, -1500.0, 0.0));
		Mover = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(TEXT("world_move_to.xml")), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
	}

	TEST_METHOD(APawnDestroyedMidMoveStopsItsBrain)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("walk"), [this] { SpawnMover(); Mark = Scenario.Now(); })
			.Until(TEXT("half a second"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("destroy the pawn"), [this]
			{
				ADSK_ASSERT(Mover.PathIs({TEXT("m"), TEXT("moving")}));
				Mover.Pawn->Destroy();
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second later"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("ended, nothing pending"), [this]
			{
				// An AI controller destroys itself with its pawn (AController::PawnPendingDestroy), brain included; the
				// brain's EndPlay must have stopped it cleanly, which any error it logged would contradict.
				ASSERT_THAT(IsFalse(Mover.Pawn.IsValid(), TEXT("the pawn survived Destroy")));
				ASSERT_THAT(IsTrue(!Mover.Brain.IsValid() || !Mover.Brain->IsADSKRunning(), TEXT("the brain still runs without a pawn")));
				ASSERT_THAT(IsTrue(!Mover.Brain.IsValid() || !Mover.Brain->GetActiveMoveRequest().IsValid(), TEXT("a move request outlived the pawn")));
			});
	}

	/** The querier dies before the EQS manager runs the query: the result, if any, must reach nobody. */
	TEST_METHOD(APawnDestroyedMidQueryStopsItsBrain)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("query and die"), [this]
			{
				Querier = Scenario.SpawnAgent(SpawnPoint, Scenario.WriteBehavior(TEXT("eqs.xml"), QueryXml()));
				ADSK_ASSERT(Querier.PathIs({TEXT("m"), TEXT("querying")}));
				Querier.Pawn->Destroy();
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second later"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("ended, slot untouched"), [this]
			{
				// As above, the controller and its brain usually go with the pawn; one that survives must be stopped.
				ASSERT_THAT(IsTrue(!Querier.Brain.IsValid() || !Querier.Brain->IsADSKRunning(), TEXT("the brain still runs without a pawn")));
				ASSERT_THAT(IsTrue(Querier.Value(TEXT("Point")).Vector.IsZero(), TEXT("a dead querier's result was written")));
				ASSERT_THAT(IsTrue(!Querier.Brain.IsValid() || Querier.Brain->GetStaleAsyncCompletions() == 0u, TEXT("a completion arrived after the querier died")));
			});
	}

	TEST_METHOD(UnpossessStopsTheBrainAndRepossessStartsANewLife)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("walk"), [this] { SpawnMover(); Mark = Scenario.Now(); })
			.Until(TEXT("half a second"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("unpossess, then possess again"), [this]
			{
				AADSKWorldTestPawn* Pawn = Mover.Pawn.Get();
				Mover.Controller->UnPossess();
				ASSERT_THAT(IsFalse(Mover.Brain->IsADSKRunning(), TEXT("unpossess did not stop the brain")));
				ASSERT_THAT(IsTrue(Mover.Brain->IsCleanedUp(), TEXT("AAIController's unpossess did not clean the brain up")));
				Mover.Controller->Possess(Pawn);
				ASSERT_THAT(IsTrue(Mover.Brain->InitializeFromSource(FADSKWorldScenario::Behavior(TEXT("world_move_to.xml"))), Mover.Brain->GetLastError()));
				const FVector Goal = Scenario.OnNavmesh(FVector(-1500.0, 1000.0, 0.0));
				ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Mover.Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(Goal))));
				Mover.Brain->StartLogic();
				ASSERT_THAT(IsTrue(Mover.Brain->IsADSKRunning(), Mover.Brain->GetLastError()));
			})
			.Until(TEXT("the second life arrives"), [this] { return Mover.IsIn(TEXT("m/arrived")) || Mover.IsIn(TEXT("m/failed")); }, Timeout)
			.Then(TEXT("arrived"), [this]
			{
				ADSK_ASSERT(Mover.PathIs({TEXT("m"), TEXT("arrived")}));
			});
	}

	/** Ending PIE with agents mid-move, mid-query and mid-wait leaves no world subsystem behind. */
	TEST_METHOD(EndingPlayReleasesEveryBrain)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("busy agents"), [this]
			{
				SpawnMover();
				Querier = Scenario.SpawnAgent(FVector(0.0, -2000.0, ADSKWorldLayout::StandZ), Scenario.WriteBehavior(TEXT("eqs.xml"), QueryXml()));
				Waiter = Scenario.SpawnAgent(FVector(-2000.0, 0.0, ADSKWorldLayout::StandZ), FADSKWorldScenario::Behavior(TEXT("world_wait.xml")));
				ASSERT_THAT(IsTrue(UADSKWorldSubsystem::GetLiveCount() >= 1, TEXT("no world subsystem is live")));
				GEditor->RequestEndPlayMap();
			})
			.Until(TEXT("PIE ended"), [] { return GEditor->PlayWorld == nullptr; }, Timeout)
			.Then(TEXT("no subsystem left"), [this]
			{
				ASSERT_THAT(AreEqual(0, UADSKWorldSubsystem::GetLiveCount()));
				ASSERT_THAT(IsFalse(Mover.Brain.IsValid() && Mover.Brain->IsADSKRunning(), TEXT("a brain still runs after PIE ended")));
			});
	}
};

#endif
