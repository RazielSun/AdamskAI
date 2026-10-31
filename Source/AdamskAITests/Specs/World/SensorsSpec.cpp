// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "CQTest.h"

// The host sensors against real geometry: the wall north of the origin blocks sight, the floor holds the pawn.
namespace FSensorsSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	const FVector AgentPoint(0.0, 500.0, ADSKWorldLayout::StandZ);
	/** South of the agent, nothing between. */
	const FVector OpenPoint(0.0, -1000.0, ADSKWorldLayout::StandZ);
	/** North of the agent, behind the wall. */
	const FVector HiddenPoint(0.0, 2500.0, ADSKWorldLayout::StandZ);
}

using namespace FSensorsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldSensorsSpec, "AdamskAIWorld.Sensors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	TWeakObjectPtr<AADSKWorldTestPawn> Open;
	TWeakObjectPtr<AADSKWorldTestPawn> Hidden;
	uint64 Mark = 0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	/** The host's write reaches the slot on the brain tick after the one that sampled it. */
	bool TwoTicksSince(uint64 Ticks) const { return Agent.Ticks() >= Ticks + 2; }

	void SetTarget(const AActor* Target)
	{
		ASSERT_THAT(IsTrue(Agent.Brain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(Target)) == EADSKInputResult::Accepted));
		Mark = Agent.Ticks();
	}

	TEST_METHOD(SightDistanceAndGroundFollowTheWorld)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn, facing all around"), [this]
			{
				Open = &Scenario.SpawnPawn(OpenPoint);
				Hidden = &Scenario.SpawnPawn(HiddenPoint);
				Agent = Scenario.SpawnAgent(AgentPoint, FADSKWorldScenario::Behavior(TEXT("world_sensors.xml")));
				SetTarget(Open.Get());
			})
			.Until(TEXT("landed and sampled"), [this] { return TwoTicksSince(Mark) && Agent.Value(TEXT("Grounded")).bBool; }, Timeout)
			.Then(TEXT("an open target is seen at its distance"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("Sees")).bBool, TEXT("the open target is not seen")));
				const float Range = Agent.Value(TEXT("Range")).Float;
				const double Expected = FVector::Dist(Agent.Location(), Open->GetActorLocation());
				ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Range, Expected, 1.0), FString::Printf(TEXT("Range %.1f, expected %.1f"), Range, Expected)));
				SetTarget(Hidden.Get());
			})
			.Until(TEXT("sampled"), [this] { return TwoTicksSince(Mark); }, Timeout)
			.Then(TEXT("the wall hides the other"), [this]
			{
				ASSERT_THAT(IsFalse(Agent.Value(TEXT("Sees")).bBool, TEXT("the target behind the wall is seen")));
				SetTarget(nullptr);
			})
			.Until(TEXT("sampled"), [this] { return TwoTicksSince(Mark); }, Timeout)
			.Then(TEXT("no target: unseen and unreachable"), [this]
			{
				ASSERT_THAT(IsFalse(Agent.Value(TEXT("Sees")).bBool));
				ASSERT_THAT(IsTrue(Agent.Value(TEXT("Range")).Float == TNumericLimits<float>::Max(), FString::Printf(TEXT("Range %g"), Agent.Value(TEXT("Range")).Float)));
			});
	}
};

#endif
