// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/ADSKSettings.h"
#include "Tasks/AITask_LockLogic.h"
#include "CQTest.h"

// How often brains tick in a live world: managed LOD from the real player view (no fake viewers), pause, time
// dilation, AI resource locks through a gameplay task, and the parallel managed pass.
namespace FTickSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(30);
	const TCHAR* WaitFile = TEXT("world_wait.xml");
	const TCHAR* PureFile = TEXT("world_pure.xml");
	constexpr int32 PureAgents = 20;
	constexpr uint64 PureFrames = 120;

	/** Restores the settings a test changes. */
	struct FSettingsGuard
	{
		TArray<FADSKTickLODLevel> TickLOD = GetDefault<UADSKSettings>()->TickLOD;
		bool bParallel = GetDefault<UADSKSettings>()->bParallelManagedTick;
		~FSettingsGuard()
		{
			GetMutableDefault<UADSKSettings>()->TickLOD = TickLOD;
			GetMutableDefault<UADSKSettings>()->bParallelManagedTick = bParallel;
		}
	};

	/** What a pure agent did: its state, its last roll and its trace, which hold every time and seeded choice. */
	FString Snapshot(const FADSKWorldAgent& Agent)
	{
		return FString::Printf(TEXT("%s Pick=%d ticks=%llu%s"), *Agent.Path(), Agent.Value(TEXT("Pick")).Int, Agent.Ticks(), *Agent.Trace(64));
	}
}

using namespace FTickSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldTickSpec, "AdamskAIWorld.Tick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	/** Counts ticks, so every brain ticks every frame. Made in BEFORE_EACH: a member would outlive UObjects at exit. */
	TUniquePtr<FADSKScopedSleepSetting> AwakeBrains;
	TUniquePtr<FSettingsGuard> Settings;
	FADSKWorldAgent Near, Mid, Far, Auto, Managed;
	TArray<FADSKWorldAgent> Batch;
	FADSKWorldAgent Mover;
	TArray<FString> Serial;
	TStrongObjectPtr<UAITask_LockLogic> Lock;
	uint64 NearTicks = 0, MidTicks = 0, FarTicks = 0, AutoTicks = 0, ManagedTicks = 0;
	uint64 FrameMark = 0;
	double Mark = 0.0;

	BEFORE_EACH()
	{
		AwakeBrains = MakeUnique<FADSKScopedSleepSetting>(false);
		Settings = MakeUnique<FSettingsGuard>();
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
		AwakeBrains.Reset();
		Lock.Reset();
		Settings.Reset();
	}

	void WaitSeconds(const TCHAR* Description, double Seconds)
	{
		TestCommandBuilder
			.Then([this] { Mark = Scenario.Now(); })
			.Until(Description, [this, Seconds] { return Scenario.Now() - Mark >= Seconds; }, Timeout);
	}

	FADSKWorldAgent SpawnWaiter(const FVector& Location, UClass* Controller)
	{
		return Scenario.SpawnAgent(Location, FADSKWorldScenario::Behavior(WaitFile), {.ControllerClass = Controller});
	}

	/**
	 * Levels at 10 m (5 Hz) and 30 m (2 Hz) from the player, who starts at PlayerStart; agents 3 m, 15 m and 52 m away.
	 * Then the player walks over to the far one, which speeds up while the near one slows down.
	 */
	TEST_METHOD(ManagedLodFollowsThePlayersView)
	{
		if (!Scenario.IsOpen()) return;
		GetMutableDefault<UADSKSettings>()->TickLOD = {{1000.f, 0.2f}, {3000.f, 0.5f}};
		TestCommandBuilder
			.StartWhen(TEXT("player pawn"), [this] { return Scenario.GetWorld().GetFirstPlayerController() && Scenario.GetWorld().GetFirstPlayerController()->GetPawn(); }, Timeout)
			.Then(TEXT("spawn three managed agents"), [this]
			{
				const FVector Player = Scenario.GetWorld().GetFirstPlayerController()->GetPawn()->GetActorLocation();
				UClass* Class = AADSKWorldManagedController::StaticClass();
				Near = SpawnWaiter(Player + FVector(300.0, 0.0, 0.0), Class);
				Mid = SpawnWaiter(Player + FVector(1500.0, 0.0, 0.0), Class);
				Far = SpawnWaiter(FVector(2000.0, 0.0, ADSKWorldLayout::StandZ), Class);
			});
		WaitSeconds(TEXT("three seconds"), 3.0);
		TestCommandBuilder.Then(TEXT("rates by distance, Waits whole"), [this]
		{
			ASSERT_THAT(IsTrue(Near.Ticks() >= 170, FString::Printf(TEXT("near ticked %llu times in 3 s, expected every frame"), Near.Ticks())));
			ASSERT_THAT(IsTrue(Mid.Ticks() >= 12 && Mid.Ticks() <= 18, FString::Printf(TEXT("mid ticked %llu times in 3 s, expected about 15"), Mid.Ticks())));
			ASSERT_THAT(IsTrue(Far.Ticks() >= 4 && Far.Ticks() <= 8, FString::Printf(TEXT("far ticked %llu times in 3 s, expected about 6"), Far.Ticks())));
			// The accumulated delta keeps the one-second Wait one second long at any rate.
			ADSK_ASSERT(Far.PathIs({TEXT("m"), TEXT("done")}));
			APawn* Player = Scenario.GetWorld().GetFirstPlayerController()->GetPawn();
			Player->SetActorLocation(Far.Location() + FVector(-200.0, 0.0, 100.0), false, nullptr, ETeleportType::TeleportPhysics);
			NearTicks = Near.Ticks();
			FarTicks = Far.Ticks();
		});
		WaitSeconds(TEXT("one second at the far agent"), 1.0);
		TestCommandBuilder.Then(TEXT("rates swapped"), [this]
		{
			ASSERT_THAT(IsTrue(Far.Ticks() - FarTicks >= 55, FString::Printf(TEXT("far ticked %llu times in 1 s next to the player"), Far.Ticks() - FarTicks)));
			ASSERT_THAT(IsTrue(Near.Ticks() - NearTicks <= 4, FString::Printf(TEXT("near ticked %llu times in 1 s, now 50 m away"), Near.Ticks() - NearTicks)));
		});
	}

	/** A player controller with no pawn and no camera of its own (joining, dead) is no viewer, wherever it stands. */
	TEST_METHOD(AControllerWithoutAPawnAddsNoViewer)
	{
		if (!Scenario.IsOpen()) return;
		GetMutableDefault<UADSKSettings>()->TickLOD = {{1000.f, 0.5f}};
		TestCommandBuilder
			.StartWhen(TEXT("player pawn"), [this] { return Scenario.GetWorld().GetFirstPlayerController() && Scenario.GetWorld().GetFirstPlayerController()->GetPawn(); }, Timeout)
			.Then(TEXT("a far agent with a bare controller beside it"), [this]
			{
				const FVector Spot(2000.0, -2000.0, ADSKWorldLayout::StandZ);
				Far = SpawnWaiter(Spot, AADSKWorldManagedController::StaticClass());
				APlayerController* Bare = Scenario.GetWorld().SpawnActor<APlayerController>(Spot + FVector(100.0, 0.0, 0.0), FRotator::ZeroRotator);
				ASSERT_THAT(IsTrue(Bare && !Bare->GetPawn(), TEXT("no bare player controller")));
				FarTicks = Far.Ticks();
			});
		WaitSeconds(TEXT("two seconds"), 2.0);
		TestCommandBuilder.Then(TEXT("still at the far rate"), [this]
		{
			const uint64 Ticked = Far.Ticks() - FarTicks;
			ASSERT_THAT(IsTrue(Ticked >= 2 && Ticked <= 6, FString::Printf(TEXT("the far agent ticked %llu times in 2 s, expected about 4"), Ticked)));
		});
	}

	TEST_METHOD(PauseFreezesBothTicksAndDilationSlowsWaits)
	{
		if (!Scenario.IsOpen()) return;
		// The managed waiter must tick every frame, whatever TickLOD the host project configures.
		GetMutableDefault<UADSKSettings>()->TickLOD.Reset();
		TestCommandBuilder
			.StartWhen(TEXT("player"), [this] { return Scenario.GetWorld().GetFirstPlayerController() != nullptr; }, Timeout)
			.Then(TEXT("spawn and pause"), [this]
			{
				Auto = SpawnWaiter(FVector(-1000.0, -1000.0, ADSKWorldLayout::StandZ), AADSKWorldTestController::StaticClass());
				Managed = SpawnWaiter(FVector(-1000.0, 1000.0, ADSKWorldLayout::StandZ), AADSKWorldManagedController::StaticClass());
			});
		WaitSeconds(TEXT("a quarter second"), 0.25);
		TestCommandBuilder.Then(TEXT("pause"), [this]
		{
			ASSERT_THAT(IsTrue(Auto.Ticks() > 0 && Managed.Ticks() > 0, TEXT("not ticking before the pause")));
			ASSERT_THAT(IsTrue(UGameplayStatics::SetGamePaused(&Scenario.GetWorld(), true), TEXT("the game did not pause")));
			AutoTicks = Auto.Ticks();
			ManagedTicks = Managed.Ticks();
		});
		WaitSeconds(TEXT("half a second paused"), 0.5);
		TestCommandBuilder.Then(TEXT("frozen, then unpause"), [this]
		{
			ASSERT_THAT(IsTrue(Auto.Ticks() == AutoTicks && Managed.Ticks() == ManagedTicks, FString::Printf(TEXT("ticked while paused: auto %llu, managed %llu"), Auto.Ticks() - AutoTicks, Managed.Ticks() - ManagedTicks)));
			UGameplayStatics::SetGamePaused(&Scenario.GetWorld(), false);
			// Half speed: a one-second Wait now takes two.
			UGameplayStatics::SetGlobalTimeDilation(&Scenario.GetWorld(), 0.5f);
			Auto = SpawnWaiter(FVector(1000.0, -1000.0, ADSKWorldLayout::StandZ), AADSKWorldTestController::StaticClass());
			Managed = SpawnWaiter(FVector(1000.0, 1000.0, ADSKWorldLayout::StandZ), AADSKWorldManagedController::StaticClass());
		});
		WaitSeconds(TEXT("1.5 s at half speed"), 1.5);
		TestCommandBuilder.Then(TEXT("still waiting"), [this]
		{
			ADSK_ASSERT(Auto.PathIs({TEXT("m"), TEXT("waiting")}));
			ADSK_ASSERT(Managed.PathIs({TEXT("m"), TEXT("waiting")}));
		});
		WaitSeconds(TEXT("another second"), 1.0);
		TestCommandBuilder.Then(TEXT("done after two"), [this]
		{
			ADSK_ASSERT(Auto.PathIs({TEXT("m"), TEXT("done")}));
			ADSK_ASSERT(Managed.PathIs({TEXT("m"), TEXT("done")}));
		});
	}

	/** I22: UAITask_LockLogic claims the controller's Logic resource; the brain pauses and resumes without catching up. */
	TEST_METHOD(AGameplayTaskLockingLogicPausesTheBrain)
	{
		if (!Scenario.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("player"), [this] { return Scenario.GetWorld().GetFirstPlayerController() != nullptr; }, Timeout)
			.Then(TEXT("spawn"), [this] { Auto = SpawnWaiter(FVector(-1000.0, -1000.0, ADSKWorldLayout::StandZ), AADSKWorldTestController::StaticClass()); });
		WaitSeconds(TEXT("a quarter second"), 0.25);
		TestCommandBuilder.Then(TEXT("lock"), [this]
		{
			AADSKWorldTestController& Controller = *Auto.Controller;
			Lock.Reset(UAITask::NewAITask<UAITask_LockLogic>(Controller, Controller, EAITaskPriority::High));
			ASSERT_THAT(IsTrue(Lock.IsValid(), TEXT("no lock task")));
			// The task claims nothing by itself; this adds UAIResource_Logic to what it claims.
			Lock->RequestAILogicLocking();
			Lock->ReadyForActivation();
		})
		// The tasks component may activate it from its own queue rather than inside ReadyForActivation.
		.Until(TEXT("paused"), [this] { return Auto.Brain->IsPaused(); }, FTimespan::FromSeconds(5))
		.Then(TEXT("note the ticks"), [this]
		{
			ASSERT_THAT(IsTrue(Lock->IsActive(), TEXT("the lock task is not active")));
			AutoTicks = Auto.Ticks();
		});
		WaitSeconds(TEXT("a second locked"), 1.0);
		TestCommandBuilder.Then(TEXT("frozen, then release"), [this]
		{
			ASSERT_THAT(IsTrue(Auto.Ticks() == AutoTicks, TEXT("ticked while locked")));
			ADSK_ASSERT(Auto.PathIs({TEXT("m"), TEXT("waiting")}));
			Lock->ExternalCancel();
			ASSERT_THAT(IsFalse(Auto.Brain->IsPaused(), TEXT("releasing the lock did not resume the brain")));
		});
		WaitSeconds(TEXT("half a second"), 0.5);
		TestCommandBuilder.Then(TEXT("ticking, the locked second not counted"), [this]
		{
			ASSERT_THAT(IsTrue(Auto.Ticks() > AutoTicks, FString::Printf(TEXT("%llu ticks, %llu when locked"), Auto.Ticks(), AutoTicks)));
			// 0.25 s before the lock and 0.5 s after: the one-second Wait has not finished.
			ADSK_ASSERT(Auto.PathIs({TEXT("m"), TEXT("waiting")}));
		});
	}

	/**
	 * I23: twenty seeded pure agents tick serially for 120 frames, then twenty more with the same seeds tick in the
	 * parallel managed pass beside an agent with host nodes; each pair must end in exactly the same state and trace.
	 */
	TEST_METHOD(TheParallelManagedPassMatchesTheSerialOne)
	{
		if (!Scenario.IsOpen()) return;
		GetMutableDefault<UADSKSettings>()->TickLOD.Reset();
		GetMutableDefault<UADSKSettings>()->bParallelManagedTick = false;
		auto SpawnBatch = [this](double Y)
		{
			Batch.Reset();
			for (int32 Seed = 1; Seed <= PureAgents; ++Seed)
			{
				Batch.Add(Scenario.SpawnAgent(FVector(-2800.0 + 250.0 * Seed, Y, ADSKWorldLayout::StandZ), FADSKWorldScenario::Behavior(PureFile),
					{.ControllerClass = AADSKWorldManagedController::StaticClass(), .RandomSeed = Seed}));
			}
			FrameMark = GFrameCounter;
		};
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("serial batch"), [SpawnBatch] { SpawnBatch(-2800.0); })
			.Until(TEXT("120 frames"), [this] { return GFrameCounter - FrameMark >= PureFrames; }, Timeout)
			.Then(TEXT("snapshot, then the parallel batch"), [this, SpawnBatch]
			{
				Serial.Reset();
				for (const FADSKWorldAgent& Agent : Batch)
				{
					Serial.Add(Snapshot(Agent));
					Agent.Controller->Destroy();
					Agent.Pawn->Destroy();
				}
				GetMutableDefault<UADSKSettings>()->bParallelManagedTick = true;
				const FVector Goal = Scenario.OnNavmesh(FVector(1500.0, 1000.0, 0.0));
				Mover = Scenario.SpawnAgent(FVector(-1500.0, 1000.0, ADSKWorldLayout::StandZ), FADSKWorldScenario::Behavior(TEXT("world_move_to.xml")),
					{.Inputs = {{TEXT("Goal"), FADSKValue::MakeVector(Goal)}}, .ControllerClass = AADSKWorldManagedController::StaticClass()});
				SpawnBatch(-2400.0);
			})
			.Until(TEXT("120 frames"), [this] { return GFrameCounter - FrameMark >= PureFrames; }, Timeout)
			.Then(TEXT("the same, pair by pair"), [this]
			{
				ASSERT_THAT(AreEqual(Serial.Num(), Batch.Num()));
				for (int32 Index = 0; Index < Batch.Num(); ++Index)
				{
					const FString Parallel = Snapshot(Batch[Index]);
					ASSERT_THAT(IsTrue(Parallel == Serial[Index], FString::Printf(TEXT("seed %d differs.\nserial: %s\nparallel: %s"), Index + 1, *Serial[Index], *Parallel)));
				}
				ASSERT_THAT(IsTrue(Mover.Distance2D(FVector(-1500.0, 1000.0, 0.0)) > 300.0, TEXT("the host-node agent did not move in the parallel pass")));
			});
	}
};

#endif
