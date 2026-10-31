// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "World/WorldScenario.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Settings/ADSKSettings.h"
#include "Support/FakeHostServices.h"
#include "CQTest.h"

// A benchmark, not a threshold, like AdamskAI.Perf.*: a fleet of managed agents shuttling on the navmesh, in a PIE
// world under -nullrhi. The numbers go to Saved/AdamskAI/perf/AdamskAIWorld.Perf.<test>.json; compare before and after.
namespace FWorldPerfSpecPrivate
{
	constexpr int32 Columns = 20;
	constexpr int32 Rows = 10;
	constexpr uint64 WarmupFrames = 60;
	constexpr uint64 MeasuredFrames = 300;
	const FTimespan Timeout = FTimespan::FromSeconds(120);

	/** The LOD variant's one viewer walks west to east along the fleet's middle row, 7.1 km in the measured frames. */
	const FVector ViewerStart(-4700.0, -1575.0, 200.0);
	const FVector ViewerEnd(2400.0, -1575.0, 200.0);
}

using namespace FWorldPerfSpecPrivate;

TEST_CLASS_WITH_FLAGS(FWorldPerfSpec, "AdamskAIWorld.Perf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKWorldScenario Scenario;
	/** Counts ticks against the baselines measured before sleep, so every brain ticks every frame. Made in BEFORE_EACH: a member would outlive UObjects at exit. */
	TUniquePtr<FADSKScopedSleepSetting> AwakeBrains;
	TArray<FADSKWorldAgent> Fleet;
	uint64 FrameMark = 0;
	double StartSeconds = 0.0;
	uint64 StartTicks = 0;
	bool bSavedParallel = false;
	TArray<FADSKTickLODLevel> SavedLOD;

	BEFORE_EACH()
	{
		AwakeBrains = MakeUnique<FADSKScopedSleepSetting>(false);
		bSavedParallel = GetDefault<UADSKSettings>()->bParallelManagedTick;
		SavedLOD = GetDefault<UADSKSettings>()->TickLOD;
		Scenario.Start(TestRunner);
	}

	AFTER_EACH()
	{
		AwakeBrains.Reset();
		Services.Reset();
		GetMutableDefault<UADSKSettings>()->bParallelManagedTick = bSavedParallel;
		GetMutableDefault<UADSKSettings>()->TickLOD = SavedLOD;
	}

	uint64 FleetTicks() const
	{
		uint64 Ticks = 0;
		for (const FADSKWorldAgent& Agent : Fleet) { Ticks += Agent.Ticks(); }
		return Ticks;
	}

	/** Set only by the LOD variant: the fake viewer it moves every measured frame. */
	TUniquePtr<FADSKScopedFakeHostServices> Services;

	void MoveViewer()
	{
		if (!Services) return;
		const double Alpha = FMath::Clamp(double(GFrameCounter - FrameMark) / double(MeasuredFrames), 0.0, 1.0);
		(*Services)->Viewers = TArray<FVector>{FMath::Lerp(ViewerStart, ViewerEnd, Alpha)};
	}

	/**
	 * Without TickLOD every brain ticks every frame, the most work the pass can do. With it, a viewer crossing the
	 * fleet leaves most brains on the slower levels: the pass's share when distance does its job. With bSleep each
	 * brain sleeps while its MoveTo runs and wakes for the result (bSleepWhenIdle, the default).
	 */
	void MeasureFleet(const TCHAR* Name, bool bTickLOD, bool bSleep = false)
	{
		GetMutableDefault<UADSKSettings>()->bParallelManagedTick = true;
		GetMutableDefault<UADSKSettings>()->bSleepWhenIdle = bSleep;
		GetMutableDefault<UADSKSettings>()->TickLOD = bTickLOD ? TArray<FADSKTickLODLevel>{{1000.f, 0.2f}, {2000.f, 0.5f}} : TArray<FADSKTickLODLevel>();
		const FString Test = FString(TEXT("AdamskAIWorld.Perf.")) + Name;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn the fleet"), [this, bTickLOD]
			{
				if (bTickLOD)
				{
					Services = MakeUnique<FADSKScopedFakeHostServices>(Scenario.GetWorld());
					(*Services)->Viewers = TArray<FVector>{ViewerStart};
				}
				// A 20 x 10 grid south of the wall, each agent walking 8 m east and back.
				for (int32 Row = 0; Row < Rows; ++Row)
				{
					for (int32 Column = 0; Column < Columns; ++Column)
					{
						const FVector A(-2700.0 + 120.0 * Column, -2700.0 + 250.0 * Row, ADSKWorldLayout::StandZ);
						const FVector B = A + FVector(800.0, 0.0, 0.0);
						Fleet.Add(Scenario.SpawnAgent(A, FADSKWorldScenario::Behavior(TEXT("world_shuttle.xml")), {.Inputs = {
							{TEXT("A"), FADSKValue::MakeVector(Scenario.OnNavmesh(A))}, {TEXT("B"), FADSKValue::MakeVector(Scenario.OnNavmesh(B))}},
							.ControllerClass = AADSKWorldManagedController::StaticClass()}));
					}
				}
				FrameMark = GFrameCounter;
			})
			.Until(TEXT("warm up"), [this] { return GFrameCounter - FrameMark >= WarmupFrames; }, Timeout)
			.Then(TEXT("start measuring"), [this]
			{
				FrameMark = GFrameCounter;
				StartSeconds = FPlatformTime::Seconds();
				StartTicks = FleetTicks();
				Scenario.GetWorld().GetSubsystem<UADSKWorldSubsystem>()->ResetManagedTickStats();
			})
			.Until(TEXT("measured frames"), [this]
			{
				MoveViewer();
				return GFrameCounter - FrameMark >= MeasuredFrames;
			}, Timeout)
			.Then(TEXT("write the numbers"), [this, bTickLOD, bSleep, Test]
			{
				const double Seconds = FPlatformTime::Seconds() - StartSeconds;
				const uint64 Frames = GFrameCounter - FrameMark;
				const uint64 Ticks = FleetTicks() - StartTicks;
				int32 Moving = 0;
				for (const FADSKWorldAgent& Agent : Fleet) { Moving += Agent.Pawn->GetVelocity().Size2D() > 10.0 ? 1 : 0; }
				const double FrameMs = 1000.0 * Seconds / double(Frames);
				// The managed pass is the brains' whole share: publication, the instance ticks, MoveTo requests.
				const FADSKManagedTickStats& Pass = Scenario.GetWorld().GetSubsystem<UADSKWorldSubsystem>()->GetManagedTickStats();
				const double PassMs = 1000.0 * Pass.Seconds / double(Frames);
				const double TickShare = double(Ticks) / (double(Fleet.Num()) * double(Frames));
				const FString Json = FString::Printf(TEXT("{\"test\":\"%s\",\"agents\":%d,\"frames\":%llu,\"frame_ms\":%.3f,")
					TEXT("\"brain_pass_ms\":%.3f,\"brain_share_of_frame\":%.3f,\"brain_ticks\":%llu,\"brain_ticks_per_agent_frame\":%.3f,\"parallel_ticks\":%llu,\"serial_ticks\":%llu,")
					TEXT("\"moving_at_end\":%d,\"tick_lod\":%s,\"sleep\":%s,\"parallel_managed_tick_setting\":true,\"rhi\":\"null\"}\n"),
					*Test, Fleet.Num(), Frames, FrameMs, PassMs, FrameMs > 0.0 ? PassMs / FrameMs : 0.0, Ticks, TickShare, Pass.ParallelTicks,
					Pass.BrainTicks - Pass.ParallelTicks, Moving, bTickLOD ? TEXT("true") : TEXT("false"), bSleep ? TEXT("true") : TEXT("false"));
				const FString Path = FPaths::ProjectSavedDir() / TEXT("AdamskAI/perf") / (Test + TEXT(".json"));
				ASSERT_THAT(IsTrue(FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("cannot write ") + Path));
				TestRunner->AddInfo(FString::Printf(TEXT("%d agents: %.2f ms per frame over %llu frames, brain pass %.3f ms, %llu brain ticks (%.2f per agent frame, %llu on workers)"),
					Fleet.Num(), FrameMs, Frames, PassMs, Ticks, TickShare, Pass.ParallelTicks));
				if (bSleep)
				{
					// A brain ticks when its move ends and once more for the completion, not every frame.
					ASSERT_THAT(IsTrue(TickShare < 0.25, FString::Printf(TEXT("%.2f brain ticks per agent frame asleep"), TickShare)));
				}
				else if (bTickLOD)
				{
					// Distance did its job: far brains ticked less, but every one ticked, at least once per slowest interval.
					ASSERT_THAT(IsTrue(TickShare < 0.75, FString::Printf(TEXT("%.2f brain ticks per agent frame with TickLOD"), TickShare)));
					ASSERT_THAT(IsTrue(Ticks >= uint64(Fleet.Num()) * uint64(Frames / 30 / 2), FString::Printf(TEXT("%llu brain ticks over %llu frames for %d agents"), Ticks, Frames, Fleet.Num())));
				}
				else
				{
					// Every brain ticks every frame.
					ASSERT_THAT(IsTrue(Ticks >= uint64(Fleet.Num()) * (Frames - 1), FString::Printf(TEXT("%llu brain ticks over %llu frames for %d agents"), Ticks, Frames, Fleet.Num())));
				}
				ASSERT_THAT(IsTrue(Moving > Fleet.Num() / 2, FString::Printf(TEXT("only %d of %d agents are walking"), Moving, Fleet.Num())));
			});
	}

	TEST_METHOD(ShuttlingFleet)
	{
		if (!Scenario.IsOpen()) return;
		MeasureFleet(TEXT("ShuttlingFleet"), false);
	}

	TEST_METHOD(ShuttlingFleetTickLOD)
	{
		if (!Scenario.IsOpen()) return;
		MeasureFleet(TEXT("ShuttlingFleetTickLOD"), true);
	}

	TEST_METHOD(ShuttlingFleetSleeping)
	{
		if (!Scenario.IsOpen()) return;
		MeasureFleet(TEXT("ShuttlingFleetSleeping"), false, true);
	}
};

#endif
