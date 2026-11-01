// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "ADSKDemoActors.h"
#include "ADSKDemoLayout.h"
#include "ADSKDemoSubsystem.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "Debug/ADSKDebugActions.h"
#include "Misc/StringOutputDevice.h"
#include "Engine/DirectionalLight.h"
#include "Groups/ADSKGroupSubsystem.h"
#include "Settings/ADSKSettings.h"
#include "Engine/SkyLight.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Runtime/ADSKInstance.h"
#include "World/WorldMapBuilder.h"
#include "World/WorldScenario.h"
#include "CQTest.h"

// Generated arena: ADSK_GENERATE_DEMO_MAP=1 rewrites L_AdskDemo; the scene specs run each scene headless.
namespace FDemoSpecPrivate
{
	const FTimespan Timeout = FTimespan::FromSeconds(30);
	const TCHAR* GenerateHint = TEXT("ADSK_GENERATE_DEMO_MAP=1 and the test AdamskAIWorld.Demo.Content");

	/** The leaf state of an agent's brain, or empty. */
	FString Leaf(const AADSKDemoController* Agent)
	{
		const FADSKInstance* Instance = Agent && Agent->Brain ? Agent->Brain->GetInstance() : nullptr;
		const TArray<FString> Path = Instance ? Instance->GetActiveStatePath() : TArray<FString>();
		return Path.IsEmpty() ? FString() : Path.Last();
	}
}

using namespace FDemoSpecPrivate;

TEST_CLASS_WITH_FLAGS(FDemoContentSpec, "AdamskAIWorld.Demo.Content",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(GenerateMap)
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("ADSK_GENERATE_DEMO_MAP")) != TEXT("1"))
		{
			TestRunner->AddInfo(TEXT("skipped: set ADSK_GENERATE_DEMO_MAP=1 to write the demo arena"));
			return;
		}
		using namespace ADSKDemoLayout;
		using ADSKWorldMapBuilder::Block;
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		ASSERT_THAT(IsNotNull(World));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		ASSERT_THAT(IsNotNull(Cube));

		Block(*World, Cube, TEXT("Floor"), FVector(0.0, 0.0, -50.0), FVector(2.0 * FloorHalf, 2.0 * FloorHalf, 100.0));
		const double Edge = FloorHalf + WallThickness / 2.0;
		const double Length = 2.0 * FloorHalf + 2.0 * WallThickness;
		Block(*World, Cube, TEXT("WallNorth"), FVector(0.0, Edge, WallHeight / 2.0), FVector(Length, WallThickness, WallHeight));
		Block(*World, Cube, TEXT("WallSouth"), FVector(0.0, -Edge, WallHeight / 2.0), FVector(Length, WallThickness, WallHeight));
		Block(*World, Cube, TEXT("WallEast"), FVector(Edge, 0.0, WallHeight / 2.0), FVector(WallThickness, Length, WallHeight));
		Block(*World, Cube, TEXT("WallWest"), FVector(-Edge, 0.0, WallHeight / 2.0), FVector(WallThickness, Length, WallHeight));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cover); ++Index)
		{
			Block(*World, Cube, *FString::Printf(TEXT("Cover%d"), Index + 1), Cover[Index].Center, Cover[Index].Size);
		}
		// A flat marker where the guard post is, so it reads on screen; too low to block anything.
		Block(*World, Cube, TEXT("PostMarker"), Post + FVector(0.0, 0.0, 1.0), FVector(200.0, 200.0, 2.0))->SetActorEnableCollision(false);
		World->SpawnActor<APlayerStart>(PlayerStart, FRotator(0.0, 90.0, 0.0))->SetActorLabel(TEXT("PlayerStart"));
		World->SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 1000.0), FRotator(-50.0, 30.0, 0.0))->SetActorLabel(TEXT("Sun"));
		World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 800.0), FRotator::ZeroRotator)->SetActorLabel(TEXT("Sky"));
		// An engine game mode: no project class runs here, and the map references nothing of the demo module.
		World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		ASSERT_THAT(IsNotNull(ADSKWorldMapBuilder::BuildNavigation(*World, FVector(0.0, 0.0, 200.0), NavBoundsSize), TEXT("the navmesh built no tiles")));

		const FString Map = FString(MapDirectory) / MapName;
		ASSERT_THAT(IsTrue(UEditorLoadingAndSavingUtils::SaveMap(World, Map), *FString::Printf(TEXT("cannot save %s"), *Map)));
		TestRunner->AddInfo(FString::Printf(TEXT("wrote %s"), *Map));
	}

	TEST_METHOD(EveryDemoFileCompilesWithThePluginCatalogue)
	{
		const FString Directory = FADSKTestPaths::PluginBase() / TEXT("Demo");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.xml")), true, false);
		ASSERT_THAT(IsTrue(Files.Num() > 0, TEXT("no files in Demo/")));
		for (const FString& File : Files)
		{
			FString Text;
			ASSERT_THAT(IsTrue(FFileHelper::LoadFileToString(Text, *(Directory / File)), *File));
			const FADSKCompileResult Result = FADSKCompiler::Compile(File, Text, ADSKTest::PluginCatalogue());
			ASSERT_THAT(IsTrue(Result.IsSuccess(), File + ADSKTest::FormatDiagnostics(Result)));
		}
	}
};

/** Opens the arena; each scene's spec starts its scene and waits for what the scene promises. */
class FADSKDemoScenario
{
public:
	FADSKWorldScenario World;

	void Start(FAutomationTestBase* TestRunner) { World.Start(TestRunner, ADSKDemoLayout::MapDirectory, ADSKDemoLayout::MapName, GenerateHint); }

	UADSKDemoSubsystem& Demo() { return *UADSKDemoSubsystem::Get(&World.GetWorld()); }

	void StartScene(const TCHAR* Name)
	{
		FString Error;
		ADSKTest::Expect(Demo().StartScene(Name, Error), *FString::Printf(TEXT("adsk.Demo %s: %s"), Name, *Error));
	}

	/** How many of the scene's agents are in a leaf state named State. */
	int32 CountIn(const TCHAR* State)
	{
		int32 Count = 0;
		for (const AADSKDemoController* Agent : Demo().GetAgents()) { Count += Leaf(Agent) == State ? 1 : 0; }
		return Count;
	}
};

TEST_CLASS_WITH_FLAGS(FDemoPatrolSpec, "AdamskAIWorld.Demo.Patrol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	TSet<FString> Seen;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(GuardsWalkEveryLegAndThePostStandsWatch)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene"), [this]
			{
				Scenario.StartScene(TEXT("Patrol"));
				ASSERT_THAT(AreEqual(4, Scenario.Demo().GetAgents().Num()));
			})
			.Until(TEXT("every leg walked"), [this]
			{
				for (const AADSKDemoController* Agent : Scenario.Demo().GetAgents()) { Seen.Add(Leaf(Agent)); }
				return Seen.Contains(TEXT("leg1")) && Seen.Contains(TEXT("leg2")) && Seen.Contains(TEXT("leg3")) && Seen.Contains(TEXT("leg4"));
			}, Timeout)
			.Then(TEXT("the post stands watch, nothing is stuck"), [this]
			{
				ASSERT_THAT(IsTrue(Seen.Contains(TEXT("watch")), FString::Join(Seen.Array(), TEXT(","))));
				for (const AADSKDemoController* Agent : Scenario.Demo().GetAgents())
				{
					ASSERT_THAT(IsTrue(Agent->Brain->IsADSKRunning(), *Agent->Brain->GetLastError()));
				}
				Scenario.Demo().Clear();
				ASSERT_THAT(AreEqual(0, Scenario.Demo().GetAgents().Num()));
			});
	}
};

TEST_CLASS_WITH_FLAGS(FDemoChaseSpec, "AdamskAIWorld.Demo.Chase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(AGuardThatSeesTheTargetChasesItAndSearchesWhenItIsGone)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene"), [this] { Scenario.StartScene(TEXT("Chase")); })
			.Until(TEXT("someone chases or strikes"), [this] { return Scenario.CountIn(TEXT("chase")) + Scenario.CountIn(TEXT("attack")) > 0; }, Timeout)
			.Then(TEXT("the target vanishes far away"), [this]
			{
				AADSKDemoTarget* Target = Scenario.Demo().GetTarget();
				Target->bWalking = false;
				Target->SetActorLocation(FVector(0.0, 0.0, -5000.0));
			})
			.Until(TEXT("the chaser searches"), [this] { return Scenario.CountIn(TEXT("search")) > 0; }, Timeout)
			.Until(TEXT("and gives up"), [this] { return Scenario.CountIn(TEXT("patrol")) == Scenario.Demo().GetAgents().Num(); }, Timeout);
	}
};

TEST_CLASS_WITH_FLAGS(FDemoInterruptSpec, "AdamskAIWorld.Demo.Interrupt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	TSet<FName> Nodes;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	void Observe()
	{
		for (const AADSKDemoController* Agent : Scenario.Demo().GetAgents())
		{
			if (const FADSKInstance* Instance = Agent->Brain->GetInstance()) { Nodes.Add(Instance->GetActiveNodeId()); }
		}
	}

	TEST_METHOD(TheCooldownInterruptsThePatrolAndACloseTargetInterruptsBoth)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene, the target out of the way"), [this]
			{
				Scenario.StartScene(TEXT("Interrupt"));
				AADSKDemoTarget* Target = Scenario.Demo().GetTarget();
				Target->bWalking = false;
				Target->SetActorLocation(FVector(0.0, 0.0, -5000.0));
			})
			.Until(TEXT("patrol, then a shout when the cooldown is ready"), [this]
			{
				Observe();
				return Nodes.Contains(TEXT("shout")) && (Nodes.Contains(TEXT("patrol_to_a")) || Nodes.Contains(TEXT("patrol_to_b")));
			}, Timeout)
			.Then(TEXT("the target stands in front of the first agent"), [this]
			{
				const APawn* Pawn = Scenario.Demo().GetAgents()[0]->GetPawn();
				Scenario.Demo().GetTarget()->SetActorLocation(Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 300.0 + FVector(0.0, 0.0, 0.0));
			})
			.Until(TEXT("it backs off"), [this] { Observe(); return Nodes.Contains(TEXT("danger_back_off")) || Nodes.Contains(TEXT("danger_hold")); }, Timeout);
	}
};

TEST_CLASS_WITH_FLAGS(FDemoReloadSpec, "AdamskAIWorld.Demo.Reload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	TArray<FString> Before;
	TArray<uint32> Generations;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(AReloadKeepsEveryAgentWhereItWas)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene"), [this] { Scenario.StartScene(TEXT("Reload")); })
			.Until(TEXT("someone left patrol"), [this] { return Scenario.CountIn(TEXT("patrol")) < Scenario.Demo().GetAgents().Num(); }, Timeout)
			.Then(TEXT("reload the file"), [this]
			{
				AADSKDemoTarget* Target = Scenario.Demo().GetTarget();
				Target->bWalking = false;
				for (const AADSKDemoController* Agent : Scenario.Demo().GetAgents())
				{
					Before.Add(Leaf(Agent));
					Generations.Add(Agent->Brain->GetInstance()->GetGeneration());
				}
				// What an author does: change the copy the scene runs from, then reload it (the editor's watcher would too).
				const FString Copy = UADSKDemoSubsystem::GetEditDirectory() / TEXT("chase.xml");
				FString Text;
				ASSERT_THAT(IsTrue(FFileHelper::LoadFileToString(Text, *Copy), *Copy));
				ASSERT_THAT(IsTrue(FFileHelper::SaveStringToFile(Text.Replace(TEXT("duration=\"0.2\""), TEXT("duration=\"0.25\"")), *Copy)));
				FStringOutputDevice Output;
				ADSKDebugActions::Reload(&Scenario.World.GetWorld(), FString::Printf(TEXT("@%s/chase.xml"), UADSKDemoSubsystem::EditMount), Output);
				TestRunner->AddInfo(FString(Output));
			})
			.Until(TEXT("the reload committed"), [this]
			{
				const TConstArrayView<TObjectPtr<AADSKDemoController>> Agents = Scenario.Demo().GetAgents();
				for (int32 Index = 0; Index < Agents.Num(); ++Index)
				{
					const FADSKInstance* Instance = Agents[Index]->Brain->GetInstance();
					if (!Instance || Instance->GetGeneration() == Generations[Index]) { return false; }
				}
				return true;
			}, Timeout)
			.Then(TEXT("every agent kept its state"), [this]
			{
				ASSERT_THAT(IsTrue(Scenario.Demo().GetHint().Contains(TEXT("chase.xml"))));
				// A restart would put everyone back in patrol; a kept state leaves the one that had left it outside.
				FString Report;
				bool bKept = false;
				for (int32 Index = 0; Index < Before.Num(); ++Index)
				{
					const FString After = Leaf(Scenario.Demo().GetAgents()[Index]);
					Report += FString::Printf(TEXT(" %s->%s"), *Before[Index], *After);
					bKept |= Before[Index] != TEXT("patrol") && After != TEXT("patrol");
				}
				ASSERT_THAT(IsTrue(bKept, *Report));
			});
	}
};

TEST_CLASS_WITH_FLAGS(FDemoSquadSpec, "AdamskAIWorld.Demo.Squad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	TSet<int32> Investigated;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(OneSightingSendsTheOthersToWhereItWasSeen)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene, the target in front of the first guard only"), [this]
			{
				Scenario.StartScene(TEXT("Squad"));
				AADSKDemoTarget* Target = Scenario.Demo().GetTarget();
				Target->bWalking = false;
				Target->SetActorLocation(FVector(-1300.0, 0.0, AADSKDemoTarget::Height));
			})
			.Until(TEXT("the first guard calls"), [this] { return Scenario.CountIn(TEXT("spotted")) + Scenario.CountIn(TEXT("engage")) > 0; }, Timeout)
			.Until(TEXT("the others investigate"), [this]
			{
				const TConstArrayView<TObjectPtr<AADSKDemoController>> Agents = Scenario.Demo().GetAgents();
				for (int32 Index = 0; Index < Agents.Num(); ++Index)
				{
					const FString State = Leaf(Agents[Index]);
					if (State == TEXT("investigate") || State == TEXT("engage") || State == TEXT("spotted")) { Investigated.Add(Index); }
				}
				return Investigated.Num() == Agents.Num();
			}, Timeout);
	}
};

TEST_CLASS_WITH_FLAGS(FDemoTokensSpec, "AdamskAIWorld.Demo.Tokens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	TSet<int32> Struck;
	int32 MostAtOnce = 0;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	TEST_METHOD(AtMostTwoStrikeAtOnceAndEveryoneGetsATurn)
	{
		if (!Scenario.World.IsOpen()) return;
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("start the scene"), [this] { Scenario.StartScene(TEXT("Tokens")); })
			.Until(TEXT("most attackers have struck"), [this]
			{
				const TConstArrayView<TObjectPtr<AADSKDemoController>> Agents = Scenario.Demo().GetAgents();
				int32 Now = 0;
				for (int32 Index = 0; Index < Agents.Num(); ++Index)
				{
					const FADSKInstance* Instance = Agents[Index]->Brain->GetInstance();
					if (Instance && Instance->GetActiveNodeId() == FName(TEXT("attack_strike"))) { ++Now; Struck.Add(Index); }
				}
				const UADSKGroupSubsystem* Groups = UADSKGroupSubsystem::Get(&Scenario.World.GetWorld());
				MostAtOnce = FMath::Max3(MostAtOnce, Now, Groups ? Groups->CountHolders(TEXT("DemoStrike"), Scenario.Demo().GetTarget()) : 0);
				return Struck.Num() >= 4;
			}, Timeout)
			.Then(TEXT("never more than two at once"), [this]
			{
				ASSERT_THAT(IsTrue(MostAtOnce >= 1 && MostAtOnce <= 2, *FString::Printf(TEXT("%d at once"), MostAtOnce)));
				Scenario.Demo().Clear();
				ASSERT_THAT(IsFalse(GetDefault<UADSKSettings>()->TokenPools.Contains(TEXT("DemoStrike")), TEXT("Clear left the demo's pool size in the settings")));
			});
	}
};

TEST_CLASS_WITH_FLAGS(FDemoFleetSpec, "AdamskAIWorld.Demo.Fleet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	FADSKDemoScenario Scenario;
	uint64 FrameMark = 0;
	double StartSeconds = 0.0;
	static constexpr int32 Brains = 1000;
	static constexpr uint64 WarmupFrames = 30;
	static constexpr uint64 MeasuredFrames = 120;

	BEFORE_EACH()
	{
		Scenario.Start(TestRunner);
	}

	/** Spawns the fleet, lets it settle, measures MeasuredFrames and writes Saved/AdamskAI/perf/<Name>.json. */
	void Measure(const TCHAR* Name, const FADSKDemoFleetOptions& Options)
	{
		TestCommandBuilder
			.StartWhen(TEXT("navmesh ready"), [this] { return Scenario.World.IsNavigationReady(); }, Timeout)
			.Then(TEXT("spawn the fleet"), [this, Options]
			{
				FString Error;
				ASSERT_THAT(IsTrue(Scenario.Demo().StartFleet(Options, Error), *Error));
				ASSERT_THAT(AreEqual(Options.Count, Scenario.Demo().GetDots().Num()));
				FrameMark = GFrameCounter;
			})
			.Until(TEXT("warm up"), [this] { return GFrameCounter - FrameMark >= WarmupFrames; }, Timeout)
			.Then(TEXT("start measuring"), [this]
			{
				Scenario.World.GetWorld().GetSubsystem<UADSKWorldSubsystem>()->ResetManagedTickStats();
				FrameMark = GFrameCounter;
				StartSeconds = FPlatformTime::Seconds();
			})
			.Until(TEXT("measured frames"), [this] { return GFrameCounter - FrameMark >= MeasuredFrames; }, Timeout)
			.Then(TEXT("write the numbers"), [this, Name, Options]
			{
				const double Seconds = FPlatformTime::Seconds() - StartSeconds;
				const double Frames = double(GFrameCounter - FrameMark);
				const FADSKManagedTickStats& Pass = Scenario.World.GetWorld().GetSubsystem<UADSKWorldSubsystem>()->GetManagedTickStats();
				const double TicksPerBrainFrame = double(Pass.BrainTicks) / (Frames * Options.Count);
				const FString Json = FString::Printf(TEXT("{\"test\":\"%s\",\"brains\":%d,\"frames\":%.0f,\"frame_ms\":%.3f,\"brain_pass_ms\":%.3f,")
					TEXT("\"us_per_brain_tick\":%.3f,\"brain_ticks_per_brain_frame\":%.3f,\"parallel_ticks\":%llu,\"recorder\":%s,\"rhi\":\"null\"}\n"),
					Name, Options.Count, Frames, 1000.0 * Seconds / Frames, 1000.0 * Pass.Seconds / Frames,
					Pass.BrainTicks > 0 ? 1.0e6 * Pass.Seconds / double(Pass.BrainTicks) : 0.0, TicksPerBrainFrame, Pass.ParallelTicks,
					Options.bRecorder ? TEXT("true") : TEXT("false"));
				const FString Path = FPaths::ProjectSavedDir() / TEXT("AdamskAI/perf") / FString::Printf(TEXT("%s.json"), Name);
				ASSERT_THAT(IsTrue(FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), *Path));
				TestRunner->AddInfo(Json);
				// Brains sleep through their waits (bSleepWhenIdle), so they tick less than every frame; none stopped.
				ASSERT_THAT(IsTrue(Pass.BrainTicks > 0, *Json));
				for (const AADSKDemoDot* Dot : Scenario.Demo().GetDots()) { ASSERT_THAT(IsTrue(Dot->Brain->IsADSKRunning())); }
				if (Options.bParallel) { ASSERT_THAT(IsTrue(Pass.ParallelTicks > 0, TEXT("a core-only fleet did not tick on workers"))); }
				Scenario.Demo().Clear();
				ASSERT_THAT(IsFalse(GetDefault<UADSKSettings>()->bParallelManagedTick && !Options.bParallel));
			});
	}

	TEST_METHOD(AThousandBrainsWithRecorders)
	{
		if (!Scenario.World.IsOpen()) return;
		FADSKDemoFleetOptions Options;
		Options.Count = Brains;
		Measure(TEXT("AdamskAIWorld.Demo.Fleet"), Options);
	}

	TEST_METHOD(AThousandBrainsOnWorkersWithoutRecorders)
	{
		if (!Scenario.World.IsOpen()) return;
		FADSKDemoFleetOptions Options;
		Options.Count = Brains;
		Options.bParallel = true;
		Options.bRecorder = false;
		Measure(TEXT("AdamskAIWorld.Demo.FleetParallel"), Options);
	}
};

#endif
