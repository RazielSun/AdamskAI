// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Compiler/ADSKCompiler.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Settings/ADSKSettings.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "CQTest.h"
#include "Support/TestChecks.h"
#include "Support/FakeHostServices.h"
#include "Engine/World.h"

namespace FManagedTickSpecPrivate
{
	const TCHAR* Xml = TEXT("<Agent version=\"1\" id=\"managed\"><StateMachine id=\"m\" initial=\"wait\">"
		"<State id=\"wait\" tree=\"t\"><On event=\"tree.success\" goto=\"done\"/></State><State id=\"done\"/></StateMachine>"
		"<Tree id=\"t\"><Wait id=\"w\" duration=\"1\"/></Tree></Agent>");

	TSharedRef<const FADSKDefinition> LoadDefinition()
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKBuiltinNodes::RegisterAll(*Registry);
		Registry->Freeze();
		return ADSKTest::LoadOrStandIn(TEXT("managed.xml"), Xml, Registry);
	}

	struct FManagedBrain
	{
		AActor* Host = nullptr;
		UADSKBrainComponent* Brain = nullptr;
		int32 Ticks = 0;

		FManagedBrain(FActorTestSpawner& Spawner, float Interval, const FVector& Location = FVector::ZeroVector, bool bAutoTick = false)
		{
			Host = &Spawner.SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Host);
			Host->AddInstanceComponent(Root);
			Root->RegisterComponent();
			Host->SetRootComponent(Root);
			Host->SetActorLocation(Location);
			Brain = NewObject<UADSKBrainComponent>(Host);
			Brain->bManagedTick = true;
			Brain->bAutoTick = bAutoTick;
			Brain->ManagedTickInterval = Interval;
			Host->AddInstanceComponent(Brain);
			Brain->RegisterComponent();
			if (!Brain->HasBegunPlay()) Brain->BeginPlay();
			Brain->InitializeDefinition(LoadDefinition());
			Brain->OnPreTick.AddLambda([this]() { ++Ticks; });
			Brain->StartLogic();
		}

		FString State() const { return Brain->GetInstance()->GetActiveStatePath().Last(); }
	};

	UADSKWorldSubsystem& Subsystem(FActorTestSpawner& Spawner)
	{
		return *Spawner.GetWorld().GetSubsystem<UADSKWorldSubsystem>();
	}

	void Frames(UADSKWorldSubsystem& Subsystem, int32 Count, float Delta = 0.1f)
	{
		for (int32 Frame = 0; Frame < Count; ++Frame) Subsystem.TickManagedBrains(Delta);
	}
}

using namespace FManagedTickSpecPrivate;

TEST_CLASS_WITH_FLAGS(FManagedTickSpec, "AdamskAI.Host.ManagedTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(AFixedIntervalTicksLessOftenWithTheTimeAccumulated)
	{
		FActorTestSpawner Spawner;
		FManagedBrain Agent(Spawner, 0.5f);
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		ASSERT_THAT(AreEqual(1, Managed.GetManagedBrainCount()));
		ASSERT_THAT(IsFalse(Agent.Brain->IsComponentTickEnabled(), TEXT("a managed brain ticks itself")));
		// The first brain added has no phase, so it ticks at once, then every 0.5 s with the 0.5 s it accumulated.
		Frames(Managed, 10);
		ASSERT_THAT(AreEqual(2, Agent.Ticks));
		ASSERT_THAT(AreEqual(FString(TEXT("wait")), Agent.State()));
		// 0.1 + 0.5 + 0.5 s reaches Wait's one second on the third tick, as eleven 0.1 s frames would.
		Frames(Managed, 6);
		ASSERT_THAT(AreEqual(4, Agent.Ticks));
		ASSERT_THAT(AreEqual(FString(TEXT("done")), Agent.State()));
	}

	TEST_METHOD(DistanceToTheNearestViewPicksTheRate)
	{
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		const TArray<FADSKTickLODLevel> Saved = Settings->TickLOD;
		Settings->TickLOD = {{3000.f, 0.2f}, {8000.f, 0.5f}};
		ON_SCOPE_EXIT { GetMutableDefault<UADSKSettings>()->TickLOD = Saved; };
		ASSERT_THAT(IsTrue(UADSKSettings::GetTickInterval(100.0) == 0.f, FString::Printf(TEXT("interval %f"), UADSKSettings::GetTickInterval(100.0))));
		ASSERT_THAT(IsTrue(UADSKSettings::GetTickInterval(3000.0) == 0.2f, FString::Printf(TEXT("interval %f"), UADSKSettings::GetTickInterval(3000.0))));
		ASSERT_THAT(IsTrue(UADSKSettings::GetTickInterval(9000.0) == 0.5f, FString::Printf(TEXT("interval %f"), UADSKSettings::GetTickInterval(9000.0))));

		FActorTestSpawner Spawner;
		FManagedBrain Near(Spawner, 0.f, FVector(100, 0, 0));
		FManagedBrain Far(Spawner, 0.f, FVector(5000, 0, 0));
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		FADSKScopedFakeHostServices Services(Spawner.GetWorld());
		Services->Viewers = TArray<FVector>{FVector(9000, 0, 0), FVector::ZeroVector};
		Frames(Managed, 10);
		ASSERT_THAT(AreEqual(10, Near.Ticks));
		ASSERT_THAT(IsTrue(Far.Ticks >= 4 && Far.Ticks <= 5, FString::Printf(TEXT("%d far ticks"), Far.Ticks)));
		// Without a player view everything ticks every frame.
		Services->Viewers = TArray<FVector>();
		const int32 FarBefore = Far.Ticks;
		Frames(Managed, 3);
		ASSERT_THAT(AreEqual(FarBefore + 3, Far.Ticks));
	}

	TEST_METHOD(EqualRatesAreSpreadOverTheInterval)
	{
		FActorTestSpawner Spawner;
		FManagedBrain First(Spawner, 1.f);
		FManagedBrain Second(Spawner, 1.f);
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		int32 SameFrame = 0;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			const int32 A = First.Ticks, B = Second.Ticks;
			Frames(Managed, 1);
			SameFrame += First.Ticks != A && Second.Ticks != B ? 1 : 0;
		}
		ASSERT_THAT(AreEqual(0, SameFrame));
		ASSERT_THAT(AreEqual(3, First.Ticks));
		ASSERT_THAT(AreEqual(3, Second.Ticks));
	}

	TEST_METHOD(AParallelManagedPassBehavesLikeTheSerialOne)
	{
		UADSKSettings* Settings = GetMutableDefault<UADSKSettings>();
		const bool bSaved = Settings->bParallelManagedTick;
		Settings->bParallelManagedTick = true;
		ON_SCOPE_EXIT { GetMutableDefault<UADSKSettings>()->bParallelManagedTick = bSaved; };
		FActorTestSpawner Spawner;
		TArray<TUniquePtr<FManagedBrain>> Agents;
		for (int32 Index = 0; Index < 16; ++Index) Agents.Add(MakeUnique<FManagedBrain>(Spawner, 0.f));
		ASSERT_THAT(IsTrue(Agents[0]->Brain->GetDefinition()->IsThreadSafe(), TEXT("the managed fixture is not thread-safe")));
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		Frames(Managed, 9);
		ASSERT_THAT(IsFalse(Agents.ContainsByPredicate([](const TUniquePtr<FManagedBrain>& Agent) { return Agent->State() != TEXT("wait"); }), TEXT("not every agent is waiting")));
		Frames(Managed, 3);
		// Host facts are still published on the game thread before every instance tick.
		ASSERT_THAT(IsFalse(Agents.ContainsByPredicate([](const TUniquePtr<FManagedBrain>& Agent) { return Agent->State() != TEXT("done") || Agent->Ticks != 12; }), TEXT("not every agent is done after 12 ticks")));
	}

	TEST_METHOD(EndPlayAndDestructionLeaveTheManagedSet)
	{
		FActorTestSpawner Spawner;
		FManagedBrain Kept(Spawner, 0.f);
		FManagedBrain Ended(Spawner, 0.f);
		FManagedBrain Destroyed(Spawner, 0.f);
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		ASSERT_THAT(AreEqual(3, Managed.GetManagedBrainCount()));
		Ended.Brain->EndPlay(EEndPlayReason::RemovedFromWorld);
		Destroyed.Host->Destroy();
		Frames(Managed, 1);
		ASSERT_THAT(AreEqual(1, Managed.GetManagedBrainCount()));
		ASSERT_THAT(AreEqual(1, Kept.Ticks));
	}

	TEST_METHOD(ABrainEndedByAnotherDuringThePassIsNotTicked)
	{
		FActorTestSpawner Spawner;
		FManagedBrain First(Spawner, 0.f);
		FManagedBrain Second(Spawner, 0.f);
		bool bEnded = false;
		First.Brain->OnPreTick.AddLambda([&Second, &bEnded]()
		{
			if (!bEnded) Second.Brain->EndPlay(EEndPlayReason::RemovedFromWorld);
			bEnded = true;
		});
		UADSKWorldSubsystem& Managed = Subsystem(Spawner);
		Frames(Managed, 1);
		ASSERT_THAT(AreEqual(1, First.Ticks));
		ASSERT_THAT(AreEqual(0, Second.Ticks));
		Frames(Managed, 1);
		ASSERT_THAT(AreEqual(1, Managed.GetManagedBrainCount()));
		ASSERT_THAT(AreEqual(2, First.Ticks));
	}

	TEST_METHOD(TheWorldsPreActorTickDrivesItsOwnManagedBrainsUnlessPaused)
	{
		FActorTestSpawner Spawner;
		FManagedBrain Agent(Spawner, 0.f);
		UWorld& World = Spawner.GetWorld();
		FWorldDelegates::OnWorldPreActorTick.Broadcast(&World, LEVELTICK_All, 0.1f);
		ASSERT_THAT(AreEqual(1, Agent.Ticks));
		FWorldDelegates::OnWorldPreActorTick.Broadcast(&World, LEVELTICK_PauseTick, 0.1f);
		FWorldDelegates::OnWorldPreActorTick.Broadcast(&World, LEVELTICK_TimeOnly, 0.1f);
		ASSERT_THAT(AreEqual(1, Agent.Ticks));
		FActorTestSpawner Other;
		FWorldDelegates::OnWorldPreActorTick.Broadcast(&Other.GetWorld(), LEVELTICK_All, 0.1f);
		ASSERT_THAT(AreEqual(1, Agent.Ticks));
		FWorldDelegates::OnWorldPreActorTick.Broadcast(&World, LEVELTICK_All, 0.1f);
		ASSERT_THAT(AreEqual(2, Agent.Ticks));
	}

	TEST_METHOD(BothTickFlagsWarnAndTheManagedTickWins)
	{
		TestRunner->AddExpectedError(TEXT("bManagedTick and bAutoTick are both set; the managed tick wins"), EAutomationExpectedErrorFlags::Contains, 1);
		FActorTestSpawner Spawner;
		FManagedBrain Agent(Spawner, 0.f, FVector::ZeroVector, true);
		ASSERT_THAT(IsFalse(Agent.Brain->IsComponentTickEnabled(), TEXT("a managed brain ticks itself")));
		ASSERT_THAT(AreEqual(1, Subsystem(Spawner).GetManagedBrainCount()));
	}
};

#endif
