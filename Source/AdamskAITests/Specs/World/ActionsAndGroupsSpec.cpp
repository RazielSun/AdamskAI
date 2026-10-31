// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "CQTest.h"

// `Act` and the group nodes against a real controller and world: Act through IADSKActionPerformer, HoldToken through the group service.
namespace FActionsAndGroupsSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(20);
	const FVector TargetPoint(0.0, -1000.0, ADSKWorldLayout::StandZ);
	const FVector AgentPoints[] = {FVector(-600.0, 0.0, ADSKWorldLayout::StandZ), FVector(0.0, 0.0, ADSKWorldLayout::StandZ), FVector(600.0, 0.0, ADSKWorldLayout::StandZ)};
}

using namespace FActionsAndGroupsSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldActSpec, "AdamskAIWorld.Act",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	/** Waits for ticks, so every brain ticks every frame. Made in BEFORE_EACH: a member would outlive UObjects at exit. */
	TUniquePtr<FADSKScopedSleepSetting> AwakeBrains;
	FADSKWorldAgent Agent;
	TWeakObjectPtr<AADSKWorldTestPawn> Target;
	TWeakObjectPtr<AADSKWorldTestPawn> Other;

	BEFORE_EACH()
	{
		AwakeBrains = MakeUnique<FADSKScopedSleepSetting>(false);
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
		AwakeBrains.Reset();
	}

	AADSKWorldActingController& Performer() const { return *CastChecked<AADSKWorldActingController>(Agent.Controller.Get()); }

	void Spawn()
	{
		Target = &Scenario.SpawnPawn(TargetPoint);
		Other = &Scenario.SpawnPawn(TargetPoint + FVector(400.0, 0.0, 0.0));
		Agent = Scenario.SpawnAgent(AgentPoints[1], FADSKWorldScenario::Behavior(TEXT("world_act.xml")),
			{.Inputs = {{TEXT("Target"), FADSKObjectHandles::Make(Target.Get())}}, .ControllerClass = AADSKWorldActingController::StaticClass()});
	}

	TEST_METHOD(EndsWhenThePerformerSaysSoAndFollowsTheTarget)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn"), [this] { Spawn(); })
			.Until(TEXT("started"), [this] { return !Performer().Calls.IsEmpty() && Agent.Ticks() >= 2; }, Timeout)
			.Then(TEXT("retarget"), [this]
			{
				ASSERT_THAT(AreEqual(Performer().Calls[0], TEXT("Start Wave ") + Target->GetName()));
				ASSERT_THAT(IsTrue(Agent.Brain->SetInput(TEXT("Target"), FADSKObjectHandles::Make(Other.Get())) == EADSKInputResult::Accepted));
			})
			.Until(TEXT("retargeted"), [this] { return Performer().Calls.Num() >= 2; }, Timeout)
			.Then(TEXT("finish"), [this]
			{
				ASSERT_THAT(AreEqual(Performer().Calls[1], TEXT("Retarget Wave ") + Other->GetName()));
				ASSERT_THAT(IsTrue(Agent.IsIn(TEXT("m/acting")), Agent.Path()));
				Performer().Finish(true);
			})
			.Until(TEXT("done"), [this] { return !Agent.IsIn(TEXT("m/acting")); }, Timeout)
			.Then(TEXT("succeeded, never stopped"), [this]
			{
				ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("done")}));
				ASSERT_THAT(AreEqual(Performer().Calls.Num(), 2, FString::Join(Performer().Calls, TEXT("; "))));
			});
	}

	TEST_METHOD(LeavingStopsItAndARefusedActionFails)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn"), [this] { Spawn(); })
			.Until(TEXT("started"), [this] { return !Performer().Calls.IsEmpty(); }, Timeout)
			.Then(TEXT("refuse"), [this] { ASSERT_THAT(IsTrue(Agent.Brain->EnqueueEvent(TEXT("Refuse")) == EADSKInputResult::Accepted)); })
			.Until(TEXT("failed"), [this] { return Agent.IsIn(TEXT("m/failed")); }, Timeout)
			.Then(TEXT("stopped, then refused"), [this]
			{
				const FString Calls = FString::Join(Performer().Calls, TEXT("; "));
				ASSERT_THAT(AreEqual(Calls, FString::Printf(TEXT("Start Wave %s; Stop Wave; Start Refused %s"), *Target->GetName(), *Target->GetName())));
				// a late report from the stopped action is stale
				Performer().Finish(true);
			})
			.Until(TEXT("a few ticks"), [this] { return Agent.Ticks() >= 10; }, Timeout)
			.Then(TEXT("still failed"), [this] { ADSK_ASSERT(Agent.PathIs({TEXT("m"), TEXT("failed")})); });
	}
};

TEST_CLASS_WITH_FLAGS(FWorldGroupNodesSpec, "AdamskAIWorld.GroupNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	TArray<FADSKWorldAgent> Agents;
	TWeakObjectPtr<AADSKWorldTestPawn> Target;
	TSet<int32> Held;
	UADSKGroupSubsystem* Groups = nullptr;
	int32 MostHolders = 0;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	/** Counts this frame's holders, seen through the nodes and through the service, and who held. */
	void Observe()
	{
		int32 Holding = 0;
		for (int32 Index = 0; Index < Agents.Num(); ++Index)
		{
			if (Agents[Index].HasInstance() && Agents[Index].Brain->GetInstance()->GetActiveNodeId() == FName(TEXT("hold")))
			{
				++Holding;
				Held.Add(Index);
			}
		}
		Groups = UADSKGroupSubsystem::Get(&Scenario.GetWorld());
		MostHolders = FMath::Max3(MostHolders, Holding, Groups ? Groups->CountHolders(TEXT("WorldTest"), Target.Get()) : 0);
	}

	TEST_METHOD(OneTokenIsTakenInTurnsNeverTwice)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn three"), [this]
			{
				Target = &Scenario.SpawnPawn(TargetPoint);
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(AgentPoints); ++Index)
				{
					Agents.Add(Scenario.SpawnAgent(AgentPoints[Index], FADSKWorldScenario::Behavior(TEXT("world_tokens.xml")),
						{.Inputs = {{TEXT("Target"), FADSKObjectHandles::Make(Target.Get())}}, .RandomSeed = Index + 1}));
				}
				Mark = Scenario.Now();
			})
			.Until(TEXT("four seconds"), [this] { Observe(); return Scenario.Now() - Mark >= 4.0; }, Timeout)
			.Then(TEXT("at most one held it, and each had a turn"), [this]
			{
				ASSERT_THAT(AreEqual(MostHolders, 1, TEXT("holders at once")));
				ASSERT_THAT(AreEqual(Held.Num(), 3, TEXT("agents that held the token")));
			})
			.Then(TEXT("a stopped brain gives its token back"), [this]
			{
				for (FADSKWorldAgent& Agent : Agents) { Agent.Brain->StopLogic(TEXT("test")); }
				ASSERT_THAT(AreEqual(Groups->CountHolders(TEXT("WorldTest"), Target.Get()), 0));
			});
	}

	static inline const FVector CallerSaw = FVector(100.0, 200.0, 0.0);

	void SpawnRadioSquad()
	{
		Groups = UADSKGroupSubsystem::Get(&Scenario.GetWorld());
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AgentPoints); ++Index)
		{
			Agents.Add(Scenario.SpawnAgent(AgentPoints[Index], FADSKWorldScenario::Behavior(TEXT("world_radio.xml")),
				{.Inputs = {{TEXT("Caller"), FADSKValue::MakeBool(Index == 0)}, {TEXT("SeenAt"), FADSKValue::MakeVector(Index == 0 ? CallerSaw : FVector::ZeroVector)}}}));
			Groups->Join(*Agents.Last().Pawn, TEXT("Radio"));
		}
		Mark = Scenario.Now();
	}

	TEST_METHOD(ARadioCallArrivesAfterItsDelay)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn a caller and two listeners"), [this] { SpawnRadioSquad(); })
			.Until(TEXT("the caller called"), [this] { return Agents[0].IsIn(TEXT("m/called")); }, Timeout)
			.Then(TEXT("not heard yet"), [this]
			{
				ADSK_ASSERT(Agents[1].PathIs({TEXT("m"), TEXT("quiet")}));
				ADSK_ASSERT(Agents[2].PathIs({TEXT("m"), TEXT("quiet")}));
			})
			.Until(TEXT("heard"), [this] { return Agents[1].IsIn(TEXT("m/heard")) && Agents[2].IsIn(TEXT("m/heard")); }, Timeout)
			.Then(TEXT("about a second after the call"), [this]
			{
				const double Elapsed = Scenario.Now() - Mark;
				ASSERT_THAT(IsTrue(Elapsed >= 1.15 && Elapsed <= 1.8, FString::Printf(TEXT("heard %.2f s after spawning"), Elapsed)));
			})
			.Then(TEXT("and where the caller saw it"), [this]
			{
				for (int32 Index = 1; Index < Agents.Num(); ++Index)
				{
					FADSKValue HeardAt;
					ASSERT_THAT(IsTrue(Agents[Index].Brain->GetInstance()->GetValue(TEXT("HeardAt"), HeardAt)));
					ASSERT_THAT(IsTrue(HeardAt.Type == EADSKValueType::Vector && HeardAt.Vector.Equals(CallerSaw), HeardAt.Vector.ToString()));
				}
			});
	}

	TEST_METHOD(KillingTheCallerFirstKeepsTheSquadQuiet)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn a caller and two listeners"), [this] { SpawnRadioSquad(); })
			.Until(TEXT("the caller called"), [this] { return Agents[0].IsIn(TEXT("m/called")); }, Timeout)
			.Then(TEXT("the caller dies"), [this] { Agents[0].Pawn->Destroy(); Mark = Scenario.Now(); })
			.Until(TEXT("two seconds"), [this] { return Scenario.Now() - Mark >= 2.0; }, Timeout)
			.Then(TEXT("nobody heard"), [this]
			{
				ADSK_ASSERT(Agents[1].PathIs({TEXT("m"), TEXT("quiet")}));
				ADSK_ASSERT(Agents[2].PathIs({TEXT("m"), TEXT("quiet")}));
			});
	}

	TEST_METHOD(OneSightingIsTheWholeGroupsTarget)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("two in a group, one outside"), [this]
			{
				Groups = UADSKGroupSubsystem::Get(&Scenario.GetWorld());
				ASSERT_THAT(IsNotNull(Groups));
				Target = &Scenario.SpawnPawn(TargetPoint);
				AADSKWorldTestPawn& A = Scenario.SpawnPawn(AgentPoints[0]);
				AADSKWorldTestPawn& B = Scenario.SpawnPawn(AgentPoints[1]);
				AADSKWorldTestPawn& Outsider = Scenario.SpawnPawn(AgentPoints[2]);
				Groups->Join(A, TEXT("Squad"));
				Groups->Join(B, TEXT("Squad"));
				ASSERT_THAT(IsTrue(Groups->GetLeader(TEXT("Squad")) == &A));
				ASSERT_THAT(IsNull(Groups->GetGroupTarget(B, 10.0)));
				Groups->ReportTarget(A, Target.Get());
				ASSERT_THAT(IsTrue(Groups->GetGroupTarget(B, 10.0) == Target.Get()));
				ASSERT_THAT(IsNull(Groups->GetGroupTarget(Outsider, 10.0)));
				Groups->Leave(A);
				ASSERT_THAT(IsTrue(Groups->GetLeader(TEXT("Squad")) == &B));
				Mark = Scenario.Now();
			})
			.Until(TEXT("half a second"), [this] { return Scenario.Now() - Mark >= 0.5; }, Timeout)
			.Then(TEXT("the sighting fades"), [this]
			{
				const TArray<AActor*> Members = Groups->GetMembers(TEXT("Squad"));
				ASSERT_THAT(AreEqual(Members.Num(), 1));
				ASSERT_THAT(IsNotNull(Groups->GetGroupTarget(*Members[0], 10.0)));
				ASSERT_THAT(IsNull(Groups->GetGroupTarget(*Members[0], 0.25)));
			});
	}
};

#endif
