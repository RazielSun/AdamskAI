// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CQTest.h"

// What the host scope and leaf nodes do to a real controller and CharacterMovement, and that each is undone.
namespace FPawnEffectsSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	const FVector SpawnPoint(-1500.0, -1500.0, ADSKWorldLayout::StandZ);
	const FVector LookPoint(-1500.0, 0.0, ADSKWorldLayout::StandZ);
	const FVector TargetPoint(0.0, 0.0, ADSKWorldLayout::StandZ);
	const FVector GoalPoint(0.0, -1500.0, 0.0);
}

using namespace FPawnEffectsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldPawnEffectsSpec, "AdamskAIWorld.PawnEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	FADSKWorldAgent Agent;
	TWeakObjectPtr<AADSKWorldTestPawn> Target;
	float SpeedBefore = 0.f;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	UCharacterMovementComponent& Movement() const { return *Agent.Pawn->GetCharacterMovement(); }

	TEST_METHOD(LookAtFocusActorAndMoveSpeedApplyAndUndo)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn"), [this]
			{
				Target = &Scenario.SpawnPawn(TargetPoint);
				const FVector Goal = Scenario.OnNavmesh(GoalPoint);
				Agent = Scenario.SpawnAgent(SpawnPoint, FADSKWorldScenario::Behavior(TEXT("world_focus.xml")), {.Inputs = {
					{TEXT("Goal"), FADSKValue::MakeVector(Goal)}, {TEXT("LookPoint"), FADSKValue::MakeVector(LookPoint)},
					{TEXT("Target"), FADSKObjectHandles::Make(Target.Get())}}});
				SpeedBefore = Movement().MaxWalkSpeed;
				Mark = Scenario.Now();
			})
			.Until(TEXT("a quarter second"), [this] { return Scenario.Now() - Mark >= 0.25; }, Timeout)
			.Then(TEXT("LookAt holds the focal point"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.Brain->GetInstance()->GetActiveNodeId() == FName(TEXT("act_look")), TEXT("not looking: ") + Agent.Path()));
				ASSERT_THAT(IsTrue(Agent.Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay).Equals(LookPoint, 1.0), TEXT("focal point ") + Agent.Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay).ToString()));
			})
			.Until(TEXT("walking"), [this] { return Agent.Brain->GetInstance()->GetActiveNodeId() == FName(TEXT("act_walk")); }, Timeout)
			.Then(TEXT("mark"), [this] { Mark = Scenario.Now(); })
			.Until(TEXT("half a second of walking"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("FocusActor and MoveSpeed hold"), [this]
			{
				ASSERT_THAT(IsTrue(Agent.Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay) == Target.Get(), TEXT("not facing the target")));
				ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Movement().MaxWalkSpeed, 200.f), FString::Printf(TEXT("MaxWalkSpeed %.0f, expected 200"), Movement().MaxWalkSpeed)));
				ASSERT_THAT(IsTrue(Agent.Pawn->GetVelocity().Size2D() <= 210.0, FString::Printf(TEXT("walking at %.0f cm/s"), Agent.Pawn->GetVelocity().Size2D())));
			})
			.Until(TEXT("done"), [this] { return Agent.IsIn(TEXT("m/done")) || Agent.IsIn(TEXT("m/failed")); }, Timeout)
			.Then(TEXT("every effect undone"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("done")}));
				ASSERT_THAT(IsNull(Agent.Controller->GetFocusActorForPriority(EAIFocusPriority::Gameplay)));
				ASSERT_THAT(IsFalse(FAISystem::IsValidLocation(Agent.Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay)), TEXT("a focal point is left behind")));
				ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(Movement().MaxWalkSpeed, SpeedBefore), FString::Printf(TEXT("MaxWalkSpeed %.0f, was %.0f"), Movement().MaxWalkSpeed, SpeedBefore)));
			});
	}
};

#endif
