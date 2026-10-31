// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "CQTest.h"

// The perception bridge on real AI sight, with no InjectStimulusForTest or fake clock: the target is seen in
// front of the agent, then teleported behind the map's wall.
namespace FPerceptionSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	/** South of the wall, looking north (+Y) at it. */
	const FVector Watcher(0.0, 700.0, ADSKWorldLayout::StandZ);
	/** 5 m away, 37 degrees off the view axis. */
	const FVector InSight(300.0, 1100.0, ADSKWorldLayout::StandZ);
	/** 15 m away, straight ahead, but behind the wall. */
	const FVector BehindWall(0.0, 2200.0, ADSKWorldLayout::StandZ);

	int32 CountConsumed(const FADSKWorldAgent& Agent, const TCHAR* Event)
	{
		const FADSKDefinition& Definition = *Agent.Brain->GetInstance()->GetDefinition();
		const int32 EventIndex = Definition.FindEvent(Event);
		int32 Found = 0;
		for (const FADSKRecord& Record : Agent.Brain->GetRecorder()->Snapshot())
		{
			Found += Record.Kind == EADSKRecordKind::EventConsumed && Record.Event == EventIndex ? 1 : 0;
		}
		return Found;
	}
}

using namespace FPerceptionSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldPerceptionSpec, "AdamskAIWorld.Perception",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	TWeakObjectPtr<AADSKWorldTestPawn> Target;
	double HiddenAt = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(SightTracksRemembersAndForgetsATarget)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn the target and the watcher"), [this]
			{
				Target = &Scenario.SpawnPawn(InSight);
				Agent = Scenario.SpawnAgent(Watcher, FADSKWorldScenario::Behavior(TEXT("world_sight.xml")),
					{.ControllerClass = AADSKWorldSightController::StaticClass(), .Rotation = FRotator(0.0, 90.0, 0.0)});
			})
			.Until(TEXT("seen"), [this] { return Agent.IsIn(TEXT("m/tracking")); }, Timeout)
			.Then(TEXT("the bridge publishes it"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("Sensed")).bBool && Agent.Value(TEXT("HasTarget")).bBool, TEXT("seen but not sensed") + Agent.Trace()));
				ASSERT_THAT(IsTrue(FADSKObjectHandles::Resolve(Agent.Value(TEXT("Target"))) == Target.Get(), TEXT("the Target input is not the target")));
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("TargetLocation")).Vector.Equals(Target->GetActorLocation(), 50.0),
					FString::Printf(TEXT("TargetLocation %s, the target is at %s"), *Agent.Value(TEXT("TargetLocation")).Vector.ToString(), *Target->GetActorLocation().ToString())));
			})
			.Until(TEXT("alerted"), [this] { return Agent.Value(TEXT("Alert")).Float >= 1.f; }, Timeout)
			.Then(TEXT("hide behind the wall"), [this]
			{
				Target->SetActorLocation(BehindWall, false, nullptr, ETeleportType::TeleportPhysics);
			})
			.Until(TEXT("no longer sensed"), [this] { return !Agent.Value(TEXT("Sensed")).bBool; }, Timeout)
			.Then(TEXT("remembered"), [this]
			{
				HiddenAt = Scenario.Now();
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("tracking")}));
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("HasTarget")).bBool, TEXT("forgotten at once despite ForgetAfter")));
				ASSERT_THAT(IsTrue(FADSKObjectHandles::Resolve(Agent.Value(TEXT("Target"))) == Target.Get(), TEXT("the remembered Target is not the target")));
				// The remembered location is where it was last seen, not where it went.
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("TargetLocation")).Vector.Equals(InSight, 100.0), TEXT("the memory follows the hidden target")));
			})
			.Until(TEXT("forgotten"), [this] { return Agent.IsIn(TEXT("m/lost")); }, Timeout)
			.Then(TEXT("after ForgetAfter, alerted once"), [this]
			{
				const double Remembered = Scenario.Now() - HiddenAt;
				ASSERT_THAT(IsTrue(Remembered >= AADSKWorldSightController::ForgetAfter - 0.2 && Remembered <= AADSKWorldSightController::ForgetAfter + 1.0,
					FString::Printf(TEXT("remembered for %.2f s, ForgetAfter is %.1f"), Remembered, AADSKWorldSightController::ForgetAfter)));
				ASSERT_THAT(IsFalse(Agent.Value(TEXT("HasTarget")).bBool, TEXT("still has a target after ForgetAfter")));
				ASSERT_THAT(AreEqual(1, CountConsumed(Agent, TEXT("Alerted"))));
				ASSERT_THAT(AreEqual(1, CountConsumed(Agent, TEXT("TargetSeen"))));
			});
	}
};

#endif
