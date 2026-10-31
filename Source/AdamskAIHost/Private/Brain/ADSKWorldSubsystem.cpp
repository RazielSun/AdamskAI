// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Brain/ADSKWorldSubsystem.h"
#include "Brain/ADSKBrainComponent.h"
#include "Brain/ADSKHostServices.h"
#include "Catalogue/ADSKRegistryAssembly.h"
#include "Settings/ADSKSettings.h"
#include "Sources/ADSKSourcePath.h"
#include "Trace/ADSKTrace.h"
#include "Sources/ADSKValidation.h"
#include "Engine/World.h"
#include "Runtime/ADSKParallelTick.h"
#include "Misc/Paths.h"
#include "ADSKHostModule.h"
#include "Program/ADSKDefinition.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagsManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Tasks/Task.h"
#include "UObject/UObjectHash.h"
#include "Trace/ADSKStats.h"

DECLARE_CYCLE_STAT(TEXT("Acquire"), STAT_ADSKAcquire, STATGROUP_AdamskAI);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Synchronous compiles"), STAT_ADSKSynchronousCompiles, STATGROUP_AdamskAI);

struct UADSKWorldSubsystem::FPrewarm
{
	UE::Tasks::TTask<FADSKSourceReadResult> Compile;
	/** Published by this prewarm; a reload that replaced it meanwhile makes the streamed assets moot. */
	TSharedPtr<const FADSKDefinition> Definition;
	TSharedPtr<FStreamableHandle> Streaming;
};

const TCHAR* LexToString(EADSKReloadResult Result)
{
	switch (Result)
	{
	case EADSKReloadResult::Queued: return TEXT("queued");
	case EADSKReloadResult::Unchanged: return TEXT("unchanged");
	case EADSKReloadResult::Rejected: return TEXT("rejected");
	case EADSKReloadResult::NotLoaded: return TEXT("not-loaded");
	}
	return TEXT("unknown");
}

int32 UADSKWorldSubsystem::LiveCount = 0;
bool UADSKWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UADSKWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	++LiveCount;
	PreActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UADSKWorldSubsystem::OnWorldPreActorTick);
	// World creation precedes the possessions that Acquire, so compiling overlaps the level load. A client runs no
	// server brains; its net mode is already known here (from the URL, or PIE's play settings).
	const UADSKSettings* Settings = GetDefault<UADSKSettings>();
	if (GetWorld()->GetNetMode() == NM_Client && !Settings->bPrewarmOnClients) return;
	for (const FString& File : Settings->PrewarmFiles) Prewarm(File);
}
void UADSKWorldSubsystem::Deinitialize()
{
	if (PreActorTickHandle.IsValid()) FWorldDelegates::OnWorldPreActorTick.Remove(PreActorTickHandle);
	PreActorTickHandle.Reset();
	for (TPair<FString, FSourceRecord>& Pair : Sources)
	{
		if (Pair.Value.Prewarm && Pair.Value.Prewarm->Streaming) Pair.Value.Prewarm->Streaming->CancelHandle();
		// The compile only reads the file and the registry it holds, so it is left to finish unobserved.
	}
	Sources.Reset();
	--LiveCount;
	Super::Deinitialize();
}
FString UADSKWorldSubsystem::GetRoot() const
{
#if WITH_AUTOMATION_TESTS
	if (!TestRoot.IsEmpty()) return TestRoot;
#endif
	return UADSKSettings::GetBehaviorRootDir();
}
TSharedPtr<const FADSKDefinition> UADSKWorldSubsystem::Acquire(FStringView RelativePath, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKWorldSubsystem::Acquire);
	SCOPE_CYCLE_COUNTER(STAT_ADSKAcquire);
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) { OutError = TEXT("source-read"); return nullptr; }
	if (FSourceRecord* Existing = Sources.Find(Path))
	{
		if (Existing->Prewarm) AdvancePrewarm(Path, *Existing, true);
		if (Existing->Active) return Existing->Active;
	}
	TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(OutError);
	if (!Registry) return nullptr;
	FADSKSourceReadResult Read = FADSKValidation::ReadSource(GetRoot(), Path, Registry.ToSharedRef());
	if (!Read.Result.Definition) { OutError = Read.bReadFailed ? TEXT("source-read") : TEXT("compile"); return nullptr; }
	FSourceRecord& Record = Sources.FindOrAdd(Path);
	++SynchronousCompiles;
	INC_DWORD_STAT(STAT_ADSKSynchronousCompiles);
	if (!Record.bWarnedSynchronous)
	{
		Record.bWarnedSynchronous = true;
		UE_LOG(LogAdamskAIHost, Display, TEXT("%s was compiled on the game thread at its first use; list it in PrewarmFiles to compile it on a worker as the world loads"), *Path);
	}
	LoadObjectPaths(Path, *Read.Result.Definition, Record, Record.ActiveRefs);
	Record.Active = Read.Result.Definition; return Read.Result.Definition;
}

void UADSKWorldSubsystem::Prewarm(FStringView RelativePath)
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) { UE_LOG(LogAdamskAIHost, Warning, TEXT("prewarm: invalid behavior path '%.*s'"), RelativePath.Len(), RelativePath.GetData()); return; }
	FSourceRecord& Record = Sources.FindOrAdd(Path);
	if (Record.Active || Record.Prewarm) return;
	FString Error;
	const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(Error);
	if (!Registry) { UE_LOG(LogAdamskAIHost, Warning, TEXT("prewarm %s: %s"), *Path, *Error); return; }
	Record.Prewarm = MakeShared<FPrewarm>();
	Record.Prewarm->Compile = LaunchCompile(Path, Registry.ToSharedRef());
}

UE::Tasks::TTask<FADSKSourceReadResult> UADSKWorldSubsystem::LaunchCompile(const FString& Path, const TSharedRef<const FADSKNodeRegistry>& Registry) const
{
	// Mounts are game-thread state, so the file is located here and only read and compiled on the worker.
	return UE::Tasks::Launch(UE_SOURCE_LOCATION, [Location = FADSKValidation::Locate(GetRoot(), Path), Registry]()
	{
		return FADSKValidation::ReadSource(Location, Registry);
	});
}

void UADSKWorldSubsystem::AdvancePrewarm(const FString& Path, FSourceRecord& Record, bool bWait)
{
	FPrewarm& Prewarm = *Record.Prewarm;
	if (!Prewarm.Definition)
	{
		if (!bWait && !Prewarm.Compile.IsCompleted()) return;
		const TSharedPtr<const FADSKDefinition> Compiled = Prewarm.Compile.GetResult().Result.Definition;
		// A failed prewarm reports nothing itself: the Acquire that follows compiles again and says why.
		if (!Compiled || Record.Active) { Record.Prewarm.Reset(); return; }
		Prewarm.Definition = Compiled;
		Record.Active = Compiled;
		TArray<FSoftObjectPath> Paths;
		for (const FADSKObjectPathField& Field : ADSK::GetObjectPathFields(*Compiled)) if (!Field.Path.IsNull()) Paths.AddUnique(Field.Path);
		if (!Paths.IsEmpty() && UAssetManager::IsInitialized()) Prewarm.Streaming = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths));
	}
	if (Prewarm.Streaming && Prewarm.Streaming->IsLoadingInProgress())
	{
		if (!bWait) return;
		Prewarm.Streaming->WaitUntilComplete();
	}
	// Streamed objects are in memory now, so this only finds and retains them (and warns once for each missing one).
	if (Record.Active == Prewarm.Definition) LoadObjectPaths(Path, *Record.Active, Record, Record.ActiveRefs);
	Record.Prewarm.Reset();
}

void UADSKWorldSubsystem::LoadObjectPaths(const FString& Source, const FADSKDefinition& Definition, FSourceRecord& Record, TArray<TStrongObjectPtr<UObject>>& OutRefs)
{
	OutRefs.Reset();
	TSet<FSoftObjectPath> Seen;
	for (const FADSKObjectPathField& Field : ADSK::GetObjectPathFields(Definition))
	{
		if (Field.Path.IsNull()) continue;
		bool bAlready = false; Seen.Add(Field.Path, &bAlready);
		if (bAlready) continue;
		// A native class is loaded with its module or not at all; asking the loader for a missing one only adds a warning.
		UObject* Object = Field.Path.GetLongPackageName().StartsWith(TEXT("/Script/")) ? Field.Path.ResolveObject() : Field.Path.TryLoad();
		const UClass* Class = Cast<UClass>(Object);
		const bool bFits = Object && (!Field.bClass || (Class && (!Field.MetaClass || Class->IsChildOf(Field.MetaClass))));
		if (Object) { OutRefs.Emplace(Object); }
		if (bFits) continue;
		bool bWarned = false; Record.WarnedMissing.Add(Field.Path.ToString(), &bWarned);
		if (bWarned) continue;
		if (!Object) { UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: could not load object '%s' (node %s)"), *Source, *Field.Path.ToString(), *Field.NodeId.ToString()); }
		else { UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: '%s' is not a %s class (node %s)"), *Source, *Field.Path.ToString(),
			Field.MetaClass ? *Field.MetaClass->GetName() : TEXT("UObject"), *Field.NodeId.ToString()); }
	}
	for (const FADSKTagField& Field : ADSK::GetTagFields(Definition))
	{
		if (UGameplayTagsManager::Get().RequestGameplayTag(Field.Tag, false).IsValid()) continue;
		bool bWarned = false; Record.WarnedMissing.Add(Field.Tag.ToString(), &bWarned);
		if (!bWarned) UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: gameplay tag '%s' is not registered (node %s)"), *Source, *Field.Tag.ToString(), *Field.NodeId.ToString());
	}
	// Assets are resolvable now: a node whose configuration the asset cannot serve is reported once.
	for (const FADSKNodeDefinition& Node : Definition.GetNodes())
	{
		FString Error;
		if (!Node.Descriptor || !Node.Descriptor->ValidateAssets || !Node.Config.IsValid() || Node.Descriptor->ValidateAssets(Node.Config.GetMemory(), Error)) continue;
		bool bWarned = false; Record.WarnedMissing.Add(Node.AuthoredId + TEXT(": ") + Error, &bWarned);
		if (!bWarned) UE_LOG(LogAdamskAIHost, Warning, TEXT("%s:%d: node %s: %s"), *Source, Node.Line, *Node.AuthoredId, *Error);
	}
}

#if WITH_AUTOMATION_TESTS
int32 UADSKWorldSubsystem::GetActiveRetainedCount(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return 0;
	const FSourceRecord* Record = Sources.Find(Path); return Record ? Record->ActiveRefs.Num() : 0;
}

bool UADSKWorldSubsystem::IsReloadCompiling(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return false;
	const FSourceRecord* Record = Sources.Find(Path); return Record && Record->ReloadCompile.IsSet();
}

void UADSKWorldSubsystem::FinishReloadsForTest()
{
	// A superseded compile launches its successor, which is waited for in the next round.
	for (int32 Round = 0; Round < 8; ++Round)
	{
		AdvanceReloads(true);
		bool bRunning = false;
		for (const TPair<FString, FSourceRecord>& Pair : Sources) bRunning = bRunning || Pair.Value.ReloadCompile.IsSet();
		if (!bRunning) return;
	}
}

bool UADSKWorldSubsystem::IsPrewarming(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return false;
	const FSourceRecord* Record = Sources.Find(Path); return Record && Record->Prewarm.IsValid();
}

int32 UADSKWorldSubsystem::GetPendingRetainedCount(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return 0;
	const FSourceRecord* Record = Sources.Find(Path); return Record ? Record->PendingRefs.Num() : 0;
}
#endif

TSharedPtr<const FADSKNodeRegistry> UADSKWorldSubsystem::GetRegistry(FString& OutError) const
{
	TSharedPtr<const FADSKNodeRegistry> Registry;
#if WITH_AUTOMATION_TESTS
	Registry = TestRegistry;
#endif
	if (!Registry)
	{
		TArray<FString> Errors;
		Registry = FADSKRegistryAssembly::TryGet(Errors);
		if (!Registry) OutError = Errors.IsEmpty() ? TEXT("catalogue-setup") : FString::Join(Errors, TEXT("; "));
	}
	return Registry;
}

EADSKReloadResult UADSKWorldSubsystem::RequestReload(FStringView RelativePath, FString& OutDiagnostics)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKWorldSubsystem::RequestReload);
	FString Path;
	if (!FADSKSourcePath::Normalize(RelativePath, Path)) { OutDiagnostics = TEXT("source-read"); return EADSKReloadResult::Rejected; }
	FSourceRecord* Record = Sources.Find(Path);
	if (!Record || !Record->Active) { OutDiagnostics = TEXT("not-loaded"); return EADSKReloadResult::NotLoaded; }
	// A known source always discards the old candidate first. An invalid latest edit must cancel it.
	Record->Pending.Reset();
	Record->PendingRefs.Reset();
	// This reads the file now, so an asynchronous compile still running read an older edit; its result is dropped.
	if (Record->ReloadCompile.IsSet())
	{
		Record->bReloadCompileSuperseded = true;
		Record->bReloadAgain = false;
	}
	FString RegistryError;
	const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(RegistryError);
	if (!Registry) { Record->LastDiagnostics = RegistryError; OutDiagnostics = RegistryError; return EADSKReloadResult::Rejected; }
	return PrepareCandidate(Path, *Record, FADSKValidation::ReadSource(GetRoot(), Path, Registry.ToSharedRef()), OutDiagnostics);
}

EADSKReloadResult UADSKWorldSubsystem::PrepareCandidate(const FString& Path, FSourceRecord& Record, const FADSKSourceReadResult& Read, FString& OutDiagnostics)
{
	if (!Read.Result.Definition)
	{
		Record.LastDiagnostics = Read.bReadFailed ? Read.ReportCode : TEXT("compile");
		OutDiagnostics = Record.LastDiagnostics;
		return EADSKReloadResult::Rejected;
	}
	if (Read.Result.Definition->GetCompositeHash() == Record.Active->GetCompositeHash())
	{
		Record.LastDiagnostics.Reset(); OutDiagnostics.Reset(); return EADSKReloadResult::Unchanged;
	}
	for (UADSKBrainComponent* Brain : BrainRegistry.GetLive(Path))
	{
		FString Error;
		if (!Brain->ValidateBindings(*Read.Result.Definition, Error))
		{
			Record.LastDiagnostics = Error; OutDiagnostics = Error; return EADSKReloadResult::Rejected;
		}
	}
	LoadObjectPaths(Path, *Read.Result.Definition, Record, Record.PendingRefs);
	Record.Pending = Read.Result.Definition;
	Record.LastDiagnostics.Reset(); OutDiagnostics.Reset();
	return EADSKReloadResult::Queued;
}

bool UADSKWorldSubsystem::RequestReloadAsync(FStringView RelativePath)
{
	FString Path;
	if (!FADSKSourcePath::Normalize(RelativePath, Path)) return false;
	FSourceRecord* Record = Sources.Find(Path);
	if (!Record || !Record->Active) return false;
	if (Record->ReloadCompile.IsSet()) { Record->bReloadAgain = true; return true; }
	LaunchReloadCompile(Path, *Record);
	return Record->ReloadCompile.IsSet();
}

void UADSKWorldSubsystem::LaunchReloadCompile(const FString& Path, FSourceRecord& Record)
{
	FString Error;
	const TSharedPtr<const FADSKNodeRegistry> Registry = GetRegistry(Error);
	if (!Registry) { Record.LastDiagnostics = Error; Record.LastAsyncResult = EADSKReloadResult::Rejected; return; }
	Record.bReloadAgain = false;
	Record.bReloadCompileSuperseded = false;
	Record.ReloadCompile = LaunchCompile(Path, Registry.ToSharedRef());
}

void UADSKWorldSubsystem::AdvanceReloads(bool bWait)
{
	TArray<FString> Paths;
	for (const TPair<FString, FSourceRecord>& Pair : Sources) if (Pair.Value.ReloadCompile.IsSet()) Paths.Add(Pair.Key);
	Paths.Sort();
	for (const FString& Path : Paths)
	{
		FSourceRecord* Record = Sources.Find(Path);
		if (!Record || !Record->ReloadCompile.IsSet() || (!bWait && !Record->ReloadCompile->IsCompleted())) continue;
		const FADSKSourceReadResult Read = Record->ReloadCompile->GetResult();
		Record->ReloadCompile.Reset();
		// A newer edit arrived while this compiled: it supersedes this result, like a second RequestReload would.
		if (Record->bReloadAgain) { LaunchReloadCompile(Path, *Record); continue; }
		// A synchronous RequestReload read the file after this compile did, and its candidate stands.
		if (Record->bReloadCompileSuperseded) { Record->bReloadCompileSuperseded = false; continue; }
		if (!Record->Active) continue;
		Record->Pending.Reset();
		Record->PendingRefs.Reset();
		FString Diagnostics;
		Record->LastAsyncResult = PrepareCandidate(Path, *Record, Read, Diagnostics);
		UE_LOG(LogAdamskAIHost, Log, TEXT("AdamskAI reload %s: %s%s%s"), *Path, LexToString(*Record->LastAsyncResult), Diagnostics.IsEmpty() ? TEXT("") : TEXT(" "), *Diagnostics);
		if (!Read.Result.Definition)
		{
			for (const FADSKDiagnostic& Diagnostic : Read.Result.Diagnostics)
			{
				if (Diagnostic.Severity != EADSKSeverity::Error) continue;
				UE_LOG(LogAdamskAIHost, Log, TEXT("  %s:%d: %s %s"), *Path, Diagnostic.Line, LexToString(Diagnostic.Code), *Diagnostic.Message);
			}
		}
	}
}

TOptional<EADSKReloadResult> UADSKWorldSubsystem::GetLastAsyncReloadResult(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return {};
	const FSourceRecord* Record = Sources.Find(Path); return Record ? Record->LastAsyncResult : TOptional<EADSKReloadResult>();
}

void UADSKWorldSubsystem::OnWorldPreActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld()) return;
	for (TPair<FString, FSourceRecord>& Pair : Sources)
	{
		if (Pair.Value.Prewarm) AdvancePrewarm(Pair.Key, Pair.Value, false);
	}
	AdvanceReloads(false);
	CommitPendingReloads();
	// A paused world ticks no actors, so it ticks no managed brains either.
	if (TickType == LEVELTICK_All) TickManagedBrains(DeltaSeconds);
}

void UADSKWorldSubsystem::AddManagedBrain(UADSKBrainComponent* Brain)
{
	ManagedTicker.Add(Brain);
}

void UADSKWorldSubsystem::RemoveManagedBrain(UADSKBrainComponent* Brain)
{
	ManagedTicker.Remove(Brain);
}

IADSKHostServices& UADSKWorldSubsystem::GetHostServices() const
{
	return HostServices ? *HostServices : IADSKHostServices::Engine();
}

void UADSKWorldSubsystem::TickManagedBrains(float Delta)
{
	if (ManagedTicker.Num() == 0) return;
	GetHostServices().GetViewers(GetWorld(), ViewerScratch);
	ManagedTicker.Tick(Delta, ViewerScratch);
}

void UADSKWorldSubsystem::CommitPendingReloads()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UADSKWorldSubsystem::CommitPendingReloads);
	TArray<FString> Paths;
	for (const TPair<FString, FSourceRecord>& Pair : Sources) if (Pair.Value.Pending) Paths.Add(Pair.Key);
	Paths.Sort();
	for (const FString& Path : Paths)
	{
		FSourceRecord* Record = Sources.Find(Path);
		if (!Record || !Record->Pending) continue;
		const TSharedPtr<const FADSKDefinition> Candidate = Record->Pending;
		// Clear before callbacks: a callback request is for the next frame.
		Record->Pending.Reset();
		TArray<TStrongObjectPtr<UObject>> Refs = MoveTemp(Record->PendingRefs);
		Record->PendingRefs.Reset();
		// Deepest first, then by path name; both keys are taken once, not per comparison.
		struct FOrdered { UADSKBrainComponent* Brain; int32 Depth; FString Name; };
		TArray<FOrdered> Ordered;
		for (UADSKBrainComponent* Brain : BrainRegistry.GetLive(Path))
		{
			Ordered.Add({Brain, Brain->GetInstance() ? Brain->GetInstance()->GetActiveStatePath().Num() : 0, Brain->GetPathName()});
		}
		Ordered.Sort([](const FOrdered& A, const FOrdered& B) { return A.Depth != B.Depth ? A.Depth > B.Depth : A.Name < B.Name; });
		TArray<UADSKBrainComponent*> Brains;
		Brains.Reserve(Ordered.Num());
		for (const FOrdered& Entry : Ordered) Brains.Add(Entry.Brain);
		// Every brain is prepared before any stops: one refusal rejects the reload and leaves all of them running.
		TArray<FADSKPreparedReplacement> Prepared;
		Prepared.SetNum(Brains.Num());
		bool bValid = true;
		for (int32 Index = 0; Index < Brains.Num() && bValid; ++Index)
		{
			FString Error;
			if (!Brains[Index]->PrepareReplacement(Candidate.ToSharedRef(), Prepared[Index], Error))
			{
				UE_LOG(LogAdamskAIHost, Warning, TEXT("%s: reload rejected by %s: %s"), *Path, *Brains[Index]->GetPathName(), *Error);
				Record->LastDiagnostics = MoveTemp(Error);
				bValid = false;
			}
		}
		if (!bValid) continue;
		// Stop callbacks are external code and may end other brains, so each one is re-resolved before it is applied.
		TArray<TWeakObjectPtr<UADSKBrainComponent>> WeakBrains; WeakBrains.Reserve(Brains.Num());
		TArray<bool> WasRunning; WasRunning.Reserve(Brains.Num());
		const bool bKeepState = GetDefault<UADSKSettings>()->bKeepStateOnReload;
		for (int32 Index = 0; Index < Brains.Num(); ++Index)
		{
			UADSKBrainComponent* Brain = Brains[Index];
			WeakBrains.Add(Brain);
			WasRunning.Add(Brain->IsADSKRunning());
			FADSKSnapshot Snapshot;
			if (bKeepState && WasRunning.Last() && Brain->CaptureSnapshot(Snapshot)) Prepared[Index].Snapshot = MoveTemp(Snapshot);
		}
		for (int32 Index = 0; Index < Brains.Num(); ++Index)
		{
			UADSKBrainComponent* Brain = WeakBrains[Index].Get();
			if (!Brain || Brain->IsCleanedUp() || !Brain->IsADSKRunning()) continue;
			// Taken again inside the stop, so it holds what the cancelled nodes wrote; the first one stays when this fails.
			FADSKSnapshot AfterCancel;
			if (Prepared[Index].Snapshot.IsSet())
			{
				if (Brain->StopLogicAndCaptureSnapshot(AfterCancel)) Prepared[Index].Snapshot = MoveTemp(AfterCancel);
			}
			else
			{
				Brain->StopLogic(TEXT("definition reloaded"));
			}
		}
		Record->Active = Candidate;
		Record->ActiveRefs = MoveTemp(Refs);
		for (int32 Index = 0; Index < Brains.Num(); ++Index)
		{
			UADSKBrainComponent* Brain = WeakBrains[Index].Get();
			if (!Brain || Brain->IsCleanedUp()) continue;
			Brain->ApplyReplacement(MoveTemp(Prepared[Index]), WasRunning[Index]);
		}
	}
}

TSharedPtr<FADSKRecorder> UADSKWorldSubsystem::CreateBrainRecorder() const
{
	const int32 Capacity = UADSKSettings::GetRecorderCapacity();
	return Capacity > 0 ? FADSKRecorder::Create(Capacity) : nullptr;
}

void UADSKWorldSubsystem::RegisterBrain(FStringView RelativePath, UADSKBrainComponent* Brain)
{
	FString Path;
	if (!Brain || !FADSKSourcePath::Normalize(RelativePath, Path)) return;
	BrainRegistry.Register(Path, Brain);
}

const FString& UADSKWorldSubsystem::GetLastDiagnostics(FStringView RelativePath) const
{
	static const FString Empty;
	FString Path;
	if (!FADSKSourcePath::Normalize(RelativePath, Path)) return Empty;
	const FSourceRecord* Record = Sources.Find(Path);
	return Record ? Record->LastDiagnostics : Empty;
}

void UADSKWorldSubsystem::UnregisterBrain(FStringView RelativePath, UADSKBrainComponent* Brain)
{
	FString Path;
	if (!FADSKSourcePath::Normalize(RelativePath, Path)) return;
	BrainRegistry.Unregister(Path, Brain);
}

TArray<TWeakObjectPtr<UADSKBrainComponent>> UADSKWorldSubsystem::GetBrains(FStringView RelativePath) const
{
	FString Path;
	if (!FADSKSourcePath::Normalize(RelativePath, Path)) return {};
	return BrainRegistry.Get(Path);
}

TArray<UADSKBrainComponent*> UADSKWorldSubsystem::GetAllBrains() const
{
	return BrainRegistry.GetAll();
}

TArray<UADSKBrainComponent*> UADSKWorldSubsystem::GetWorldBrains() const
{
	TArray<UADSKBrainComponent*> Out;
	const UWorld* World = GetWorld();
	ForEachObjectOfClass(UADSKBrainComponent::StaticClass(), [World, &Out](UObject* Object)
	{
		UADSKBrainComponent* Brain = static_cast<UADSKBrainComponent*>(Object);
		if (IsValid(Brain) && !Brain->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Brain->GetWorld() == World) Out.Add(Brain);
	});
	Out.Sort([](const UADSKBrainComponent& A, const UADSKBrainComponent& B) { return A.GetPathName() < B.GetPathName(); });
	return Out;
}

TSharedPtr<const FADSKDefinition> UADSKWorldSubsystem::GetActiveDefinition(FStringView RelativePath) const
{
	FString Path; if (!FADSKSourcePath::Normalize(RelativePath, Path)) return nullptr;
	const FSourceRecord* Record = Sources.Find(Path); return Record ? Record->Active : nullptr;
}
