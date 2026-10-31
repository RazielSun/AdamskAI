// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Misc/AutomationTest.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Sources/ADSKValidation.h"
#include "Brain/ADSKBrainRegistry.h"
#include "Core/ADSKTypes.h"
#include "Brain/ADSKManagedTicker.h"
#include "Tasks/Task.h"
#include "ADSKWorldSubsystem.generated.h"

class FADSKDefinition;
class FADSKNodeRegistry;
class FADSKRecorder;
class IADSKHostServices;
class UADSKBrainComponent;

enum class EADSKReloadResult : uint8
{
	Queued,
	Unchanged,
	Rejected,
	NotLoaded,
};

ADAMSKAIHOST_API const TCHAR* LexToString(EADSKReloadResult Result);

UCLASS()
class ADAMSKAIHOST_API UADSKWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	/**
	 * The first successful Acquire synchronously loads and retains the definition's ObjectPath assets (can hitch)
	 * until deinitialize or a reload replaces them. InitializeDefinition on a brain bypasses this preload.
	 */
	TSharedPtr<const FADSKDefinition> Acquire(FStringView RelativePath, FString& OutError);
	/** Compiles on a worker and streams assets so the first Acquire does not hitch; an earlier Acquire waits for it. */
	void Prewarm(FStringView RelativePath);
	/** Prepares a replacement. Publishing only happens from the pre-actor-tick callback. */
	EADSKReloadResult RequestReload(FStringView RelativePath, FString& OutDiagnostics);
	/**
	 * RequestReload with the compile on a worker; prepared in a later pre-actor tick. A request during a compile
	 * compiles again after it, so the latest edit wins. False when the path is invalid or the file was never loaded.
	 */
	bool RequestReloadAsync(FStringView RelativePath);
	/** The outcome of the last finished asynchronous reload of the file, as RequestReload would have returned it. */
	TOptional<EADSKReloadResult> GetLastAsyncReloadResult(FStringView RelativePath) const;
	/** Test hook and delegate target. Never publishes a candidate prepared during this call. */
	void CommitPendingReloads();
	TSharedPtr<FADSKRecorder> CreateBrainRecorder() const;
	void RegisterBrain(FStringView RelativePath, UADSKBrainComponent* Brain);
	void UnregisterBrain(FStringView RelativePath, UADSKBrainComponent* Brain);
	TArray<TWeakObjectPtr<UADSKBrainComponent>> GetBrains(FStringView RelativePath) const;
	/** Brains registered to a behavior file, which its reload replaces. */
	TArray<UADSKBrainComponent*> GetAllBrains() const;
	/** Every live brain in this world, also one given a definition directly, by path name: what debug views list. */
	TArray<UADSKBrainComponent*> GetWorldBrains() const;
	TSharedPtr<const FADSKDefinition> GetActiveDefinition(FStringView RelativePath) const;
	const FString& GetLastDiagnostics(FStringView RelativePath) const;
	static int32 GetLiveCount() { return LiveCount; }
	/** Files Acquire compiled on the game thread because no prewarm had them; each is also logged once. */
	int32 GetSynchronousCompileCount() const { return SynchronousCompiles; }

	/** Managed tick (UADSKBrainComponent::bManagedTick): brains this subsystem ticks before the world's actors. */
	void AddManagedBrain(UADSKBrainComponent* Brain);
	void RemoveManagedBrain(UADSKBrainComponent* Brain);
	int32 GetManagedBrainCount() const { return ManagedTicker.Num(); }
	const FADSKManagedTickStats& GetManagedTickStats() const { return ManagedTicker.GetStats(); }
	void ResetManagedTickStats() { ManagedTicker.ResetStats(); }
	/** One managed pass: every brain accumulates Delta and ticks once its interval has passed. */
	void TickManagedBrains(float Delta);

	/** Replaces the engine services in this world; null restores them. See IADSKHostServices. */
	void SetHostServices(TSharedPtr<IADSKHostServices> Services) { HostServices = MoveTemp(Services); }
	IADSKHostServices& GetHostServices() const;
#if WITH_AUTOMATION_TESTS
	void SetTestRoot(const FString& InRoot) { TestRoot = InRoot; }
	void SetTestRegistry(const TSharedPtr<const FADSKNodeRegistry>& InRegistry) { TestRegistry = InRegistry; }
	int32 GetActiveRetainedCount(FStringView RelativePath) const;
	int32 GetPendingRetainedCount(FStringView RelativePath) const;
	bool IsPrewarming(FStringView RelativePath) const;
	bool IsReloadCompiling(FStringView RelativePath) const;
	void FinishReloadsForTest();
#endif
private:
	struct FPrewarm;
	struct FSourceRecord
	{
		TSharedPtr<const FADSKDefinition> Active;
		TSharedPtr<const FADSKDefinition> Pending;
		FString LastDiagnostics;
		/** Objects the Active / Pending definition references through ObjectPath fields. */
		TArray<TStrongObjectPtr<UObject>> ActiveRefs;
		TArray<TStrongObjectPtr<UObject>> PendingRefs;
		FADSKExactStringSet WarnedMissing;
		TSharedPtr<FPrewarm> Prewarm;
		/**
		 * An asynchronous reload's compile; whether another asynchronous request arrived while it ran (it compiles
		 * again); whether a synchronous request did (its result is dropped: the synchronous one read a newer edit).
		 */
		TOptional<UE::Tasks::TTask<FADSKSourceReadResult>> ReloadCompile;
		bool bReloadAgain = false;
		bool bReloadCompileSuperseded = false;
		TOptional<EADSKReloadResult> LastAsyncResult;
		bool bWarnedSynchronous = false;
	};
	EADSKReloadResult PrepareCandidate(const FString& Path, FSourceRecord& Record, const FADSKSourceReadResult& Read, FString& OutDiagnostics);
	void LaunchReloadCompile(const FString& Path, FSourceRecord& Record);
	/** Locates the file on the game thread, then reads and compiles it on a worker. */
	UE::Tasks::TTask<FADSKSourceReadResult> LaunchCompile(const FString& Path, const TSharedRef<const FADSKNodeRegistry>& Registry) const;
	void AdvanceReloads(bool bWait);
	void AdvancePrewarm(const FString& Path, FSourceRecord& Record, bool bWait);
	static void LoadObjectPaths(const FString& Source, const FADSKDefinition& Definition, FSourceRecord& Record, TArray<TStrongObjectPtr<UObject>>& OutRefs);
	FString GetRoot() const;
	TSharedPtr<const FADSKNodeRegistry> GetRegistry(FString& OutError) const;
	void OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/** By exact-case path: on Linux `Guard.xml` and `guard.xml` are two files. */
	TADSKExactMap<FSourceRecord> Sources;
	FADSKBrainRegistry BrainRegistry;
	FADSKManagedTicker ManagedTicker;
	TSharedPtr<IADSKHostServices> HostServices;
	/** Kept between passes, so gathering the viewers allocates nothing. */
	TArray<FVector> ViewerScratch;
	FDelegateHandle PreActorTickHandle;
	int32 SynchronousCompiles = 0;
	static int32 LiveCount;
#if WITH_AUTOMATION_TESTS
	FString TestRoot;
	TSharedPtr<const FADSKNodeRegistry> TestRegistry;
#endif
};
