// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "CQTest.h"

// MoveTo and MoveToActor on the real navmesh, finished by path following.
namespace FNavigationSpecPrivate
{
	const TCHAR* MoveFile = TEXT("world_move_to.xml");
	const TCHAR* ChaseFile = TEXT("world_chase.xml");
	const FVector SpawnPoint(-1500.0, -1500.0, ADSKWorldLayout::StandZ);
	/** Simulated time at 600 cm/s covers the floor's diagonal in under 15 s; real time with -nullrhi is far less. */
	const FTimespan Timeout = FTimespan::FromSeconds(30);

	bool HasFinished(const FADSKWorldAgent& Agent)
	{
		return !Agent.HasInstance() || Agent.IsIn(TEXT("m/arrived")) || Agent.IsIn(TEXT("m/failed")) || Agent.IsIn(TEXT("m/caught"));
	}

	/** MoveToLocation counts the agent radius in its reach test, so the pawn stops within radius + capsule. */
	double Reach(const FADSKWorldAgent& Agent, double AcceptanceRadius)
	{
		return AcceptanceRadius + Agent.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10.0;
	}

	int32 CountStarts(const FADSKWorldAgent& Agent, const TCHAR* NodeId)
	{
		int32 Starts = 0;
		const FADSKDefinition& Definition = *Agent.Brain->GetInstance()->GetDefinition();
		for (const FADSKRecord& Record : Agent.Brain->GetRecorder()->Snapshot())
		{
			Starts += Record.Kind == EADSKRecordKind::NodeStarted && Definition.GetNodes()[Record.Node].Id == FName(NodeId) ? 1 : 0;
		}
		return Starts;
	}
}

using namespace FNavigationSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldNavigationSpec, "AdamskAIWorld.Navigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	FADSKWorldAgent Runner;
	TWeakObjectPtr<AADSKWorldTestPawn> Target;
	FVector Goal = FVector::ZeroVector;
	FVector Stop = FVector::ZeroVector;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	void WaitForNavmesh()
	{
		TestCommandBuilder.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout);
	}

	/** MakeGoal runs once PIE is up: projecting onto the navmesh needs the world. */
	void MoveTo(TFunction<FVector()> MakeGoal, const TCHAR* File = MoveFile)
	{
		WaitForNavmesh();
		TestCommandBuilder
			.Then(TEXT("spawn the agent"), [this, MakeGoal, File]
			{
				Goal = MakeGoal();
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(File), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
			})
			.Until(TEXT("the move ends"), [this] { return HasFinished(Agent); }, Timeout);
	}

	TEST_METHOD(MoveToReachesANavmeshPoint)
	{
		if (!Scenario.IsOpen()) return;
		MoveTo([this] { return Scenario.OnNavmesh(FVector(1500.0, -1500.0, 0.0)); });
		TestCommandBuilder.Then(TEXT("arrived"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("arrived")}));
			ASSERT_THAT(IsTrue(Agent.Distance2D(Goal) <= Reach(Agent, 50.0), FString::Printf(TEXT("stopped %.0f cm from the goal, reach %.0f"), Agent.Distance2D(Goal), Reach(Agent, 50.0))));
			ASSERT_THAT(AreEqual(0u, Agent.Brain->GetStaleAsyncCompletions()));
		});
	}

	/** Unreachable island: with allowPartialPath (default) the pawn stops at the floor's edge and the move succeeds. */
	TEST_METHOD(MoveToAnUnreachablePointEndsAtTheClosestPoint)
	{
		if (!Scenario.IsOpen()) return;
		MoveTo([this] { return Scenario.OnNavmesh(ADSKWorldLayout::IslandCenter); });
		TestCommandBuilder.Then(TEXT("arrived at the edge"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("arrived")}));
			const FVector At = Agent.Location();
			ASSERT_THAT(IsTrue(At.X > ADSKWorldLayout::FloorHalf - 300.0 && At.X < ADSKWorldLayout::FloorHalf && At.Z > 0.0,
				FString::Printf(TEXT("the pawn ended at %s, not on the floor's east edge"), *At.ToCompactString())));
		});
	}

	/** The same goal with allowPartialPath="false" (the default allows partial paths): no full path, so the move fails where it starts. */
	TEST_METHOD(AStrictMoveToAnUnreachablePointFails)
	{
		if (!Scenario.IsOpen()) return;
		MoveTo([this] { return Scenario.OnNavmesh(ADSKWorldLayout::IslandCenter); }, TEXT("world_move_strict.xml"));
		TestCommandBuilder.Then(TEXT("failed"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("failed")}));
			ASSERT_THAT(IsTrue(Agent.Distance2D(SpawnPoint) < 300.0, FString::Printf(TEXT("the pawn walked to %s"), *Agent.Location().ToCompactString())));
		});
	}

	TEST_METHOD(MoveToAPointOffTheNavmeshFails)
	{
		if (!Scenario.IsOpen()) return;
		// Below the floor and outside the nav bounds' height.
		MoveTo([] { return FVector(0.0, -1500.0, -2000.0); });
		TestCommandBuilder.Then(TEXT("failed"), [this]
		{
			ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("failed")}));
		});
	}

	/** I3: the Goal slot moves more than repathDistance mid-move; the same MoveTo re-paths and ends at the new goal. */
	TEST_METHOD(MoveToRepathsWhenTheGoalMoves)
	{
		if (!Scenario.IsOpen()) return;
		WaitForNavmesh();
		TestCommandBuilder
			.Then(TEXT("spawn and walk east"), [this]
			{
				Goal = Scenario.OnNavmesh(FVector(1500.0, -1500.0, 0.0));
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(MoveFile), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
				Mark = Scenario.Now();
			})
			.Until(TEXT("a second of walking"), [this] { return Scenario.Now() - Mark >= 1.0; }, Timeout)
			.Then(TEXT("move the goal north"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.IsIn(TEXT("m/moving")), TEXT("the agent stopped before the goal moved: ") + Agent.Path()));
				Goal = Scenario.OnNavmesh(FVector(0.0, 1000.0, 0.0));
				ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Agent.Brain->SetInput(TEXT("Goal"), FADSKValue::MakeVector(Goal))));
			})
			.Until(TEXT("the move ends"), [this] { return HasFinished(Agent); }, Timeout)
			.Then(TEXT("arrived at the new goal"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("arrived")}));
				ASSERT_THAT(IsTrue(Agent.Distance2D(Goal) <= Reach(Agent, 50.0), FString::Printf(TEXT("stopped %.0f cm from the moved goal"), Agent.Distance2D(Goal))));
				ASSERT_THAT(AreEqual(1, CountStarts(Agent, TEXT("move_goal"))));
				ASSERT_THAT(AreEqual(0u, Agent.Brain->GetStaleAsyncCompletions()));
			});
	}

	/** I3: MoveToActor after a target that walks away: the chaser catches it once, without restarting the node. */
	TEST_METHOD(MoveToActorFollowsAMovingTarget)
	{
		if (!Scenario.IsOpen()) return;
		WaitForNavmesh();
		TestCommandBuilder
			.Then(TEXT("spawn the runner and the chaser"), [this]
			{
				Goal = Scenario.OnNavmesh(FVector(1500.0, 1000.0, 0.0));
				Runner = Scenario.SpawnAgent(FVector(-500.0, -1500.0, ADSKWorldLayout::StandZ), FADSKWorldScenario::Behavior(MoveFile), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(ChaseFile), {.Inputs = {{TEXT("Target"), FADSKObjectHandles::Make(Runner.Pawn.Get())}}});
			})
			.Until(TEXT("the chase ends"), [this] { return HasFinished(Agent) && HasFinished(Runner); }, Timeout)
			.Then(TEXT("caught"), [this]
			{
				ADSK_ASSERT(Runner.PathIs({TEXT("m"), TEXT("arrived")}));
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("caught")}));
				// MoveToActor's reach counts both capsules.
				const double Reach = 50.0 + 2.0 * Agent.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10.0;
				ASSERT_THAT(IsTrue(Agent.Distance2D(Runner.Location()) <= Reach, FString::Printf(TEXT("caught %.0f cm from the runner, reach %.0f"), Agent.Distance2D(Runner.Location()), Reach)));
				ASSERT_THAT(AreEqual(1, CountStarts(Agent, TEXT("chase_target"))));
			});
	}

	/** I4: leaving the state mid-move stops the pawn; the aborted request's completion changes nothing. */
	TEST_METHOD(LeavingTheStateStopsThePawn)
	{
		if (!Scenario.IsOpen()) return;
		WaitForNavmesh();
		TestCommandBuilder
			.Then(TEXT("spawn and walk"), [this]
			{
				Goal = Scenario.OnNavmesh(FVector(1500.0, -1500.0, 0.0));
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(MoveFile), {.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}});
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second of walking"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("stop"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.IsIn(TEXT("m/moving")), TEXT("the agent stopped on its own: ") + Agent.Path()));
				ASSERT_THAT(AreEqual(EADSKInputResult::Accepted, Agent.Brain->EnqueueEvent(TEXT("Stop"))));
			})
			.Until(TEXT("stopped"), [this] { return Agent.IsIn(TEXT("m/stopped")); }, Timeout)
			.Then(TEXT("note where"), [this] { Stop = Agent.Location(); Mark = Scenario.Now(); })
			.Until(TEXT("a second later"), [this] { return Scenario.Now() - Mark >= 1.0; }, Timeout)
			.Then(TEXT("still there"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("stopped")}));
				// Braking from 600 cm/s takes a few centimetres, not the 2 m it would have walked on.
				ASSERT_THAT(IsTrue(Agent.Distance2D(Stop) < 50.0, FString::Printf(TEXT("the pawn moved %.0f cm after the stop"), Agent.Distance2D(Stop))));
				ASSERT_THAT(IsTrue(Agent.Controller->GetMoveStatus() == EPathFollowingStatus::Idle, TEXT("path following is still active")));
				ASSERT_THAT(IsFalse(Agent.Brain->GetActiveMoveRequest().IsValid(), TEXT("a move request is still active")));
				ASSERT_THAT(IsTrue(Agent.Distance2D(Goal) > 1000.0, TEXT("the pawn reached the goal it was stopped short of")));
			});
	}

	/** I16: the chased target is destroyed: MoveToActor fails, the old id resolves to nothing, a new target gets a new id. */
	TEST_METHOD(ADestroyedTargetFailsTheChaseAndANewOneHasANewId)
	{
		if (!Scenario.IsOpen()) return;
		WaitForNavmesh();
		TestCommandBuilder
			.Then(TEXT("spawn a target far away and the chaser"), [this]
			{
				Target = &Scenario.SpawnPawn(FVector(1500.0, -1500.0, ADSKWorldLayout::StandZ));
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(ChaseFile), {.Inputs = {{TEXT("Target"), FADSKObjectHandles::Make(Target.Get())}}});
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second of chasing"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("destroy the target"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.IsIn(TEXT("m/chasing")), TEXT("the chase ended early: ") + Agent.Path()));
				Target->Destroy();
			})
			.Until(TEXT("the chase ends"), [this] { return HasFinished(Agent); }, Timeout)
			.Then(TEXT("failed, and a respawned target is someone else"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("failed")}));
				const FADSKValue Old = Agent.Value(TEXT("Target"));
				ASSERT_THAT(IsNull(FADSKObjectHandles::Resolve(Old)));
				AADSKWorldTestPawn& Respawned = Scenario.SpawnPawn(FVector(1500.0, -1500.0, ADSKWorldLayout::StandZ));
				const FADSKValue New = FADSKObjectHandles::Make(&Respawned);
				ASSERT_THAT(IsTrue(New.ObjectId != 0 && New.ObjectId != Old.ObjectId, FString::Printf(TEXT("new id %llu, old id %llu"), New.ObjectId, Old.ObjectId)));
				ASSERT_THAT(IsTrue(FADSKObjectHandles::Resolve(New) == &Respawned, TEXT("the new handle does not resolve to the respawned pawn")));
			});
	}
};

#endif
