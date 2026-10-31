// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

struct FFileChangeData;
class IDirectoryWatcher;

class ADAMSKAIEDITOR_API FADSKLiveReload
{
public:
	void Start();
	void Stop();
	void TickForTest(double Now);
	void InjectChangesForTest(const TArray<FFileChangeData>& Changes, const FString& Prefix, const FString& Root) { OnDirectoryChanged(Changes, Prefix, Root); }
	TArray<FString> GetPendingForTest() const { return Pending.Array(); }
	TArray<TPair<FString, FString>> GetWatchesForTest() const;
	/** The behavior path a changed file is known by: relative to its root, `@Mount/`-prefixed in a mount. */
	static bool MapChange(const FString& Prefix, const FString& Root, const FString& Filename, FString& OutPath);

private:
	struct FWatch
	{
		FString Prefix;
		FString Root;
		FDelegateHandle Handle;
	};

	void Watch(const FString& Prefix, const FString& Root);
	void Unwatch();
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes, FString Prefix, FString Root);
	bool Tick(float DeltaSeconds);
	/** Watches the roots that did not exist yet once they do. */
	void WatchAppeared(double Now);

	TArray<FWatch> Watches;
	/** Roots that did not exist when watched; their handles stay unset. */
	TArray<FWatch> Missing;
	double LastMissingCheck = 0.0;
	TSet<FString> Pending;
	double FirstChange = 0.0;
	double LastChange = 0.0;
	FDelegateHandle MountsHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	IDirectoryWatcher* Watcher = nullptr;
	TSet<FString> Warned;
};
