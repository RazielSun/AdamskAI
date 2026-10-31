// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Reload/ADSKLiveReload.h"

#include "Settings/ADSKSettings.h"
#include "Sources/ADSKBehaviorRoots.h"
#include "Sources/ADSKSourcePath.h"
#include "Brain/ADSKWorldSubsystem.h"
#include "DirectoryWatcherModule.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdamskAIReload, Log, All);

namespace ADSKLiveReloadPrivate
{
	/** Quiet time after the last change, so one save that touches a file several times reloads it once. */
	constexpr double SettleSeconds = 0.15;
	/** Longest wait after the first change, so a steady stream of edits still reloads. */
	constexpr double MaxWaitSeconds = 1.0;
}

void FADSKLiveReload::Start()
{
	if (TickerHandle.IsValid() || IsRunningCommandlet()) return;
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	Watcher = Module.Get();
	if (!Watcher) { UE_LOG(LogAdamskAIReload, Warning, TEXT("AdamskAI live reload disabled: no directory watcher")); return; }
	Watch(FString(), UADSKSettings::GetBehaviorRootDir());
	for (const TPair<FString, FString>& Mount : FADSKBehaviorRoots::GetAll()) Watch(TEXT("@") + Mount.Key + TEXT("/"), Mount.Value);
	// A Game Feature mounting or unmounting its directory changes what is watched; pending changes are kept.
	MountsHandle = FADSKBehaviorRoots::OnChanged().AddLambda([this]()
	{
		Unwatch();
		Watch(FString(), UADSKSettings::GetBehaviorRootDir());
		for (const TPair<FString, FString>& Mount : FADSKBehaviorRoots::GetAll()) Watch(TEXT("@") + Mount.Key + TEXT("/"), Mount.Value);
	});
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FADSKLiveReload::Tick));
}

void FADSKLiveReload::Watch(const FString& Prefix, const FString& Root)
{
	FWatch Entry{Prefix, Root, FDelegateHandle()};
	bool bAlready = false;
	if (!IFileManager::Get().DirectoryExists(*Root))
	{
		Missing.Add(MoveTemp(Entry));
		Warned.Add(Root, &bAlready);
		if (!bAlready) UE_LOG(LogAdamskAIReload, Display, TEXT("AdamskAI live reload will watch %s once it exists"), *Root);
		return;
	}
	if (Watcher->RegisterDirectoryChangedCallback_Handle(Root, IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FADSKLiveReload::OnDirectoryChanged, Prefix, Root), Entry.Handle, 0))
	{
		Watches.Add(MoveTemp(Entry));
		return;
	}
	Warned.Add(Root, &bAlready);
	if (!bAlready) UE_LOG(LogAdamskAIReload, Warning, TEXT("AdamskAI live reload does not watch %s: the directory watcher refused it"), *Root);
}

void FADSKLiveReload::WatchAppeared(double Now)
{
	// a directory check per root, at most every two seconds
	if (Missing.IsEmpty() || Now - LastMissingCheck < 2.0) return;
	LastMissingCheck = Now;
	TArray<FWatch> Waiting = MoveTemp(Missing);
	for (FWatch& Entry : Waiting)
	{
		if (!IFileManager::Get().DirectoryExists(*Entry.Root)) { Missing.Add(MoveTemp(Entry)); continue; }
		Warned.Remove(Entry.Root);
		Watch(Entry.Prefix, Entry.Root);
		if (Watches.ContainsByPredicate([&Entry](const FWatch& W) { return W.Root == Entry.Root; }))
		{
			UE_LOG(LogAdamskAIReload, Display, TEXT("AdamskAI live reload now watches %s"), *Entry.Root);
		}
	}
}

TArray<TPair<FString, FString>> FADSKLiveReload::GetWatchesForTest() const
{
	TArray<TPair<FString, FString>> Out;
	for (const FWatch& Entry : Watches) Out.Add({Entry.Prefix, Entry.Root});
	return Out;
}

void FADSKLiveReload::Unwatch()
{
	for (FWatch& Entry : Watches)
	{
		if (Watcher && Entry.Handle.IsValid()) Watcher->UnregisterDirectoryChangedCallback_Handle(Entry.Root, Entry.Handle);
	}
	Watches.Reset();
	Missing.Reset();
}

void FADSKLiveReload::Stop()
{
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (MountsHandle.IsValid()) FADSKBehaviorRoots::OnChanged().Remove(MountsHandle);
	MountsHandle.Reset();
	Unwatch();
	Watcher = nullptr; Pending.Reset(); FirstChange = LastChange = 0.0;
}

bool FADSKLiveReload::MapChange(const FString& Prefix, const FString& Root, const FString& Filename, FString& OutPath)
{
	FString Relative = Filename;
	if (!FPaths::MakePathRelativeTo(Relative, *(Root / TEXT("")))) return false;
	return FADSKSourcePath::Normalize(Prefix + Relative, OutPath);
}

void FADSKLiveReload::OnDirectoryChanged(const TArray<FFileChangeData>& Changes, FString Prefix, FString Root)
{
	const double Now = FPlatformTime::Seconds();
	for (const FFileChangeData& Change : Changes)
	{
		if (Change.Action != FFileChangeData::FCA_Added && Change.Action != FFileChangeData::FCA_Modified) continue;
		FString Path;
		if (MapChange(Prefix, Root, Change.Filename, Path)) Pending.Add(MoveTemp(Path));
	}
	if (!Pending.IsEmpty()) { if (FirstChange == 0.0) FirstChange = Now; LastChange = Now; }
}

bool FADSKLiveReload::Tick(float)
{
	const double Now = FPlatformTime::Seconds();
	WatchAppeared(Now);
	TickForTest(Now);
	return true;
}

void FADSKLiveReload::TickForTest(double Now)
{
	using namespace ADSKLiveReloadPrivate;
	if (Pending.IsEmpty() || (Now - LastChange < SettleSeconds && Now - FirstChange < MaxWaitSeconds)) return;
	TArray<FString> Paths = Pending.Array(); Paths.Sort(); Pending.Reset(); FirstChange = LastChange = 0.0;
	if (!GEngine) return;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World(); if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)) continue;
		UADSKWorldSubsystem* Subsystem = World->GetSubsystem<UADSKWorldSubsystem>(); if (!Subsystem) continue;
		// Compiled on a worker; the subsystem logs each outcome once its pre-actor tick has prepared the candidate.
		for (const FString& Path : Paths) Subsystem->RequestReloadAsync(Path);
	}
}
