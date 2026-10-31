// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Settings/ADSKSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "ADSKDemoSubsystem.generated.h"

class AADSKDemoController;
class AADSKDemoDot;
class AADSKDemoTarget;
struct FADSKNamedValue;

/** The Fleet scene's knobs, from `adsk.Demo.Fleet <count> [lod] [parallel] [norecorder] [color]`. */
struct FADSKDemoFleetOptions
{
	int32 Count = 500;
	/** UADSKSettings::TickLOD: far brains tick less often (from 1000 cm every 0.2 s, from 2000 cm every 0.5 s). */
	bool bTickLOD = false;
	/** UADSKSettings::bParallelManagedTick: the fleet's file is core nodes only, so its brains tick on workers. */
	bool bParallel = false;
	/** A recorder per brain, as outside Shipping by default; off is what Shipping costs. */
	bool bRecorder = true;
	/** Tint every dot by its state: a tenth of them each frame. Costs more than the brains. */
	bool bColor = false;
};

/**
 * The demo scenes: `adsk.Demo <Scene>` clears what the last scene spawned and spawns the next one on the arena
 * (ADSKDemoLayout), with its behavior files from `@AdskDemo`. Writes the scene, its hint and live numbers on screen.
 * Game and PIE worlds, game thread only.
 */
UCLASS()
class ADAMSKAIDEMO_API UADSKDemoSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static UADSKDemoSubsystem* Get(const UObject* WorldContext);

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Every scene name, in the order `adsk.Demo.List` prints them. */
	static TArray<FString> GetSceneNames();
	/** Clears the current scene and starts Name; false with a reason for an unknown scene or a failed brain. */
	bool StartScene(const FString& Name, FString& OutError);
	/** Destroys every actor a scene spawned and restores the settings a scene changed. */
	void Clear();
	/** The Fleet scene with these options; false with a reason. */
	bool StartFleet(const FADSKDemoFleetOptions& Options, FString& OutError);
	/** The last measurement line of the Fleet scene, refreshed twice a second; empty in other scenes. */
	const FString& GetFleetLine() const { return FleetLine; }

	const FString& GetSceneName() const { return Scene; }
	const FString& GetHint() const { return Hint; }
	TConstArrayView<TObjectPtr<AADSKDemoController>> GetAgents() const { return Agents; }
	AADSKDemoTarget* GetTarget() const { return Target; }
	TConstArrayView<TObjectPtr<AADSKDemoDot>> GetDots() const { return Dots; }

	/** The mount the Reload scene runs from: a copy of the Chase file under Saved/, so editing it leaves the plugin alone. */
	static const TCHAR* EditMount;
	/** Where that copy lives. */
	static FString GetEditDirectory();

private:
	/** Spawns a demo agent at Location running Source (`@Mount/file.xml`) with Inputs; null after a failure. */
	AADSKDemoController* SpawnAgent(const FVector& Location, const FString& Source, TConstArrayView<FADSKNamedValue> Inputs, FString& OutError);
	AADSKDemoTarget* SpawnTarget();

	bool SetupPatrol(FString& OutError);
	bool SetupChase(FString& OutError) { return SpawnChase(Demo(TEXT("chase.xml")), OutError); }
	bool SpawnChase(const FString& Source, FString& OutError);
	static FString Demo(const TCHAR* File);
	bool SetupReload(FString& OutError);
	bool SetupInterrupt(FString& OutError);
	bool SetupSquad(FString& OutError);
	bool SetupTokens(FString& OutError);

	bool SetupFleet(FString& OutError);
	void MeasureFleet();

	/** The tick settings the Fleet scene changed, as they were. */
	struct FSavedTickSettings
	{
		TArray<FADSKTickLODLevel> TickLOD;
		bool bParallelManagedTick = false;
		int32 RecorderCapacity = 0;
	};
	TOptional<FSavedTickSettings> SavedTick;
	FADSKDemoFleetOptions FleetOptions;
	FString FleetLine;
	double FleetMeasuredAt = 0.0;
	uint64 FleetFrames = 0;
	int32 NextTint = 0;

	/** The DemoStrike pool size the Tokens scene set, and the one it replaced (restored by Clear). */
	TOptional<TOptional<int32>> SavedStrikePool;

	/** The scene's files read TargetLocation, so Tick publishes the target's position to every agent. */
	bool bPublishTargetLocation = false;

	FString Scene;
	FString Hint;

	UPROPERTY()
	TArray<TObjectPtr<AADSKDemoController>> Agents;

	UPROPERTY()
	TObjectPtr<AADSKDemoTarget> Target;

	UPROPERTY()
	TArray<TObjectPtr<AADSKDemoDot>> Dots;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Spawned;
};
