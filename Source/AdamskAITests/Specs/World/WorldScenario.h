// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Brain/ADSKBrainComponent.h"
#include "Components/MapTestSpawner.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NavigationSystem.h"
#include "Objects/ADSKObjectHandles.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Support/TempTree.h"
#include "Support/TestChecks.h"
#include "World/WorldTestActors.h"
#include <initializer_list>

/** L_AdskWorldTest positions in cm, shared by the map generator and the specs. */
namespace ADSKWorldLayout
{
	inline const TCHAR* MapDirectory = TEXT("/AdamskAI/Tests/Maps");
	inline const TCHAR* MapName = TEXT("L_AdskWorldTest");
	/** The main floor: 60 x 60 m, its top at z = 0, centered on the origin. */
	inline constexpr double FloorHalf = 3000.0;
	/** A 10 x 10 m island across a 20 m gap east of the floor: on the navmesh, but no path leads there. */
	inline const FVector IslandCenter(5500.0, 0.0, 0.0);
	inline constexpr double IslandHalf = 500.0;
	/** A wall along X north of the origin, 20 m long, 1 m thick, 3 m tall: it blocks sight. */
	inline const FVector WallCenter(0.0, 1500.0, 150.0);
	inline const FVector WallSize(2000.0, 100.0, 300.0);
	inline const FVector PlayerStart(-2500.0, -2500.0, 100.0);
	/** The nav bounds hold the floor and the island. */
	inline const FVector NavBoundsCenter(1500.0, 0.0, 200.0);
	inline const FVector NavBoundsSize(10000.0, 7000.0, 1000.0);
	/** Where a pawn's capsule center stands on the floor. */
	inline constexpr double StandZ = 100.0;
}

/** One agent: its body, its controller and the controller's brain. The pawn may be destroyed by the test. */
struct FADSKWorldAgent
{
	TWeakObjectPtr<AADSKWorldTestPawn> Pawn;
	TWeakObjectPtr<AADSKWorldTestController> Controller;
	TWeakObjectPtr<UADSKBrainComponent> Brain;

	bool HasInstance() const { return Brain.IsValid() && Brain->GetInstance(); }

	/** The active state path joined by '/', or a note when the brain has no instance. */
	FString Path() const { return HasInstance() ? ADSKTest::FormatPath(*Brain->GetInstance()) : FString(TEXT("(no brain)")); }

	bool IsIn(const TCHAR* InPath) const { return Path().Equals(InPath, ESearchCase::CaseSensitive); }

	FADSKCheck PathIs(std::initializer_list<const TCHAR*> Expected) const
	{
		if (!HasInstance())
		{
			return FADSKCheck::Fail(TEXT("the agent has no brain instance"));
		}
		return ADSKTest::PathIs(*Brain->GetInstance(), Expected, Brain->GetRecorder().Get());
	}

	/** The slot's current value; a zero Bool when there is no instance or no such slot. */
	FADSKValue Value(const TCHAR* Slot) const
	{
		FADSKValue Out;
		if (HasInstance())
		{
			Brain->GetInstance()->GetValue(Slot, Out);
		}
		return Out;
	}

	uint64 Ticks() const { return HasInstance() ? Brain->GetInstance()->GetTickCount() : 0; }

	FString Trace(int32 Tail = 40) const
	{
		return HasInstance() ? ADSKTest::FormatTrace(Brain->GetRecorder().Get(), *Brain->GetInstance()->GetDefinition(), Tail) : FString();
	}

	FVector Location() const { return Pawn.IsValid() ? Pawn->GetActorLocation() : FVector(TNumericLimits<double>::Max()); }

	double Distance2D(const FVector& Point) const { return FVector::Dist2D(Location(), Point); }
};

/** At namespace scope: clang rejects a nested struct with member initializers as a default argument. */
struct FADSKWorldAgentOptions
{
	std::initializer_list<FADSKNamedValue> Inputs;
	UClass* ControllerClass = nullptr;
	FRotator Rotation = FRotator::ZeroRotator;
	int32 RandomSeed = 0;
	/** False: initialize only, the test sets more and calls StartLogic. */
	bool bStart = true;
};

/** Pins bSleepWhenIdle for a test's life: tick-counting tests must not see brains sleep. */
struct FADSKScopedSleepSetting
{
	bool bSaved = GetDefault<UADSKSettings>()->bSleepWhenIdle;
	explicit FADSKScopedSleepSetting(bool bSleep) { GetMutableDefault<UADSKSettings>()->bSleepWhenIdle = bSleep; }
	~FADSKScopedSleepSetting() { GetMutableDefault<UADSKSettings>()->bSleepWhenIdle = bSaved; }
	FADSKScopedSleepSetting(const FADSKScopedSleepSetting&) = delete;
	FADSKScopedSleepSetting& operator=(const FADSKScopedSleepSetting&) = delete;
};

/**
 * The test map in PIE with `@AdskWorld/` and a scratch `@AdskWorldTemp/` mounted and a fixed time step. Start in
 * BEFORE_EACH; restores both on destruction.
 */
class FADSKWorldScenario
{
public:
	static constexpr const TCHAR* Mount = TEXT("AdskWorld");
	static constexpr const TCHAR* TempMount = TEXT("AdskWorldTemp");
	/** Simulated seconds per frame while a scenario runs. */
	static constexpr double FixedDelta = 1.0 / 60.0;

	FADSKWorldScenario() = default;
	FADSKWorldScenario(const FADSKWorldScenario&) = delete;
	FADSKWorldScenario& operator=(const FADSKWorldScenario&) = delete;

	~FADSKWorldScenario()
	{
		if (bStarted)
		{
			FApp::SetUseFixedTimeStep(bSavedFixedTimeStep);
			FApp::SetFixedDeltaTime(SavedFixedDelta);
		}
		if (bMounted)
		{
			FADSKBehaviorRoots::Unregister(Mount);
		}
		if (Temp)
		{
			FADSKBehaviorRoots::Unregister(TempMount);
		}
	}

	/** `@AdskWorld/<File>`: a file of the plugin's Fixtures/world. */
	static FString Behavior(const FString& File) { return FString::Printf(TEXT("@%s/%s"), Mount, *File); }

	/** `@AdskWorldTemp/<File>`: a file this scenario wrote. */
	static FString TempBehavior(const FString& File) { return FString::Printf(TEXT("@%s/%s"), TempMount, *File); }

	/** Without the map it fails the test instead of letting FMapTestSpawner assert; methods then check IsOpen. */
	void Start(FAutomationTestBase* TestRunner, const TCHAR* MapDirectory = ADSKWorldLayout::MapDirectory, const TCHAR* MapName = ADSKWorldLayout::MapName,
		const TCHAR* GenerateHint = TEXT("ADSK_GENERATE_WORLD_MAP=1 and the test AdamskAIWorld.Content"))
	{
		const FString Map = FString(MapDirectory) / MapName;
		if (!FPackageName::DoesPackageExist(Map))
		{
			ADSKTest::Fail(FString::Printf(TEXT("%s is missing; generate it with %s"), *Map, GenerateHint));
			return;
		}
		FString Existing;
		if (!FADSKBehaviorRoots::Find(Mount, Existing))
		{
			bMounted = FADSKBehaviorRoots::Register(Mount, FADSKTestPaths::PluginBase() / TEXT("Fixtures/world"));
			ADSKTest::Expect(bMounted, TEXT("cannot mount Fixtures/world as @AdskWorld"));
		}
		bSavedFixedTimeStep = FApp::UseFixedTimeStep();
		SavedFixedDelta = FApp::GetFixedDeltaTime();
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(FixedDelta);
		bStarted = true;
		Spawner = MakeUnique<FMapTestSpawner>(MapDirectory, MapName);
		Spawner->AddWaitUntilLoadedCommand(TestRunner);
	}

	/** Start found the map and queued opening it. */
	bool IsOpen() const { return Spawner.IsValid(); }

	UWorld& GetWorld() { return Spawner->GetWorld(); }

	/** Simulated seconds since PIE started, undilated and counting pauses: FixedDelta per frame. */
	double Now() { return GetWorld().GetRealTimeSeconds(); }

	UNavigationSystemV1* GetNavigation() { return FNavigationSystem::GetCurrent<UNavigationSystemV1>(&GetWorld()); }

	/** The navmesh exists, is not rebuilding, and the floor's center is on it. */
	bool IsNavigationReady()
	{
		UNavigationSystemV1* Navigation = GetNavigation();
		FNavLocation Probe;
		return Navigation && Navigation->GetDefaultNavDataInstance() && !Navigation->IsNavigationBuildInProgress()
			&& Navigation->ProjectPointToNavigation(FVector::ZeroVector, Probe);
	}

	/** Point moved onto the navmesh; unchanged, with a test error, when there is none near it. */
	FVector OnNavmesh(const FVector& Point)
	{
		FNavLocation Projected;
		UNavigationSystemV1* Navigation = GetNavigation();
		if (Navigation && Navigation->ProjectPointToNavigation(Point, Projected, FVector(100.0, 100.0, 300.0)))
		{
			return Projected.Location;
		}
		ADSKTest::Fail(FString::Printf(TEXT("no navmesh near %s"), *Point.ToCompactString()));
		return Point;
	}

	/** Writes `@AdskWorldTemp/<File>`; the mount registers at once, so it works before PIE. */
	FString WriteBehavior(const FString& File, const FString& Xml)
	{
		if (!Temp)
		{
			Temp = MakeUnique<FADSKTempTree>();
			ADSKTest::Expect(FADSKBehaviorRoots::Register(TempMount, Temp->Root), TEXT("cannot mount the scratch directory as @AdskWorldTemp"));
		}
		Temp->Write(File, Xml);
		return TempBehavior(File);
	}

	/** A pawn nobody controls: a target, an obstacle with a body. */
	AADSKWorldTestPawn& SpawnPawn(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		return Spawner->SpawnActorAt<AADSKWorldTestPawn>(Location, Rotation, Parameters);
	}

	using FAgentOptions = FADSKWorldAgentOptions;

	/** A pawn at Location under a test controller running Source; a brain that cannot start fails the test. */
	FADSKWorldAgent SpawnAgent(const FVector& Location, const FString& Source, const FAgentOptions& Options = FAgentOptions())
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		FADSKWorldAgent Agent;
		AADSKWorldTestPawn& Pawn = SpawnPawn(Location, Options.Rotation);
		UClass* ControllerClass = Options.ControllerClass ? Options.ControllerClass : AADSKWorldTestController::StaticClass();
		AADSKWorldTestController& Controller = Spawner->SpawnActorAt<AADSKWorldTestController>(Location, Options.Rotation, Parameters, ControllerClass);
		Controller.Possess(&Pawn);
		Agent.Pawn = &Pawn;
		Agent.Controller = &Controller;
		Agent.Brain = Controller.Brain;
		Controller.Brain->RandomSeed = Options.RandomSeed;
		if (!Controller.Brain->InitializeFromSource(Source))
		{
			ADSKTest::Fail(FString::Printf(TEXT("%s did not initialize: %s"), *Source, *Controller.Brain->GetLastError()));
			return Agent;
		}
		for (const FADSKNamedValue& Input : Options.Inputs)
		{
			ADSKTest::Expect(Controller.Brain->SetInput(Input.Name, Input.Value) == EADSKInputResult::Accepted,
				FString::Printf(TEXT("%s refused input %s"), *Source, *Input.Name));
		}
		if (Options.bStart)
		{
			Controller.Brain->StartLogic();
			ADSKTest::Expect(Controller.Brain->IsADSKRunning(), FString::Printf(TEXT("%s did not start: %s"), *Source, *Controller.Brain->GetLastError()));
		}
		return Agent;
	}

private:
	/** Declared before Spawner so the scratch files outlive the actors that may still read them. */
	TUniquePtr<FADSKTempTree> Temp;
	TUniquePtr<FMapTestSpawner> Spawner;
	bool bMounted = false;
	bool bStarted = false;
	bool bSavedFixedTimeStep = false;
	double SavedFixedDelta = 0.0;
};

#endif
