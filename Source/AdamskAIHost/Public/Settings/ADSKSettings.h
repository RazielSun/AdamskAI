// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "ADSKSettings.generated.h"

/** Brains at least Distance from the nearest player view tick every Interval seconds (managed tick only). */
USTRUCT()
struct FADSKTickLODLevel
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Tick", meta = (ClampMin = "0"))
	float Distance = 0.f;

	UPROPERTY(Config, EditAnywhere, Category = "Tick", meta = (ClampMin = "0"))
	float Interval = 0.f;
};

/** Project settings for AdamskAI, stored in DefaultGame.ini under [/Script/AdamskAIHost.ADSKSettings]. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "AdamskAI"))
class ADAMSKAIHOST_API UADSKSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** Behavior files directory, relative to Content. Packaged builds must stage it: +DirectoriesToAlwaysStageAsUFS=(Path="<BehaviorRoot>"). */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	FString BehaviorRoot = TEXT("AI");

	/** Extra behavior directories by mount name (`@<Mount>/<file>.xml`), relative to the project directory; each needs staging like BehaviorRoot. */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	TMap<FString, FString> BehaviorMounts;

	/** Behavior files, relative to BehaviorRoot, compiled on a worker as each Game/PIE world is created. */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	TArray<FString> PrewarmFiles;

	/** A live reload restarts each running brain from a snapshot of it (state path, Locals, cooldowns) instead of its initial state; `adsk.Restart` still starts clean. */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	bool bKeepStateOnReload = true;

	/** Prewarm in network client worlds too. Only for games that run brains on client-local actors; brains on replicated actors run on the server. */
	UPROPERTY(Config, EditAnywhere, Category = "Behavior")
	bool bPrewarmOnClients = false;

	/** Managed-tick rates: a brain takes the farthest level it is at or beyond; nearer than all, or with no player view, it ticks every frame. */
	UPROPERTY(Config, EditAnywhere, Category = "Tick")
	TArray<FADSKTickLODLevel> TickLOD;

	/** Thread-safe managed brains (FADSKDefinition::IsThreadSafe) tick on worker threads; host facts stay on the game thread. */
	UPROPERTY(Config, EditAnywhere, Category = "Tick")
	bool bParallelManagedTick = false;

	/** Brains skip ticks while nothing is due and get the skipped time on waking; off ticks every brain every frame. */
	UPROPERTY(Config, EditAnywhere, Category = "Tick")
	bool bSleepWhenIdle = true;

	static float GetTickInterval(double Distance);

	/** Tokens per target of each `HoldToken` pool, by pool name: at most this many agents hold one on the same target. A pool not named here has 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Groups")
	TMap<FName, int32> TokenPools;

	/** Records kept per brain for traces and the debug panel. 0 disables recording; Shipping never records. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = 0))
	int32 RecorderCapacity = 256;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** BehaviorRoot with `\` folded to `/`; the default "AI" when it is empty, absolute or contains `.`/`..` segments. */
	static FString GetBehaviorRoot();

	static FString GetBehaviorRootDir();

	/** RecorderCapacity clamped at zero; always zero in Shipping. */
	static int32 GetRecorderCapacity();

};
