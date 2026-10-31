// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "ADSKDebugSubsystem.h"

#include "Debug/ADSKDebugSnapshot.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

namespace ADSKDebugSubsystemPrivate
{
	TAutoConsoleVariable<bool> LabelsEnabled(TEXT("adsk.Labels"), false, TEXT("Show AdamskAI execution labels."));
}

bool UADSKDebugSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UADSKDebugSubsystem::Tick(float)
{
	UWorld* World = GetWorld();
	if (!World || !ADSKDebugSubsystemPrivate::LabelsEnabled.GetValueOnGameThread()) return;
	// Labels need no slots, events or records.
	const FADSKDebugSnapshot Snapshot = FADSKDebugSnapshot::Collect(World, EADSKDebugDetail::Labels);
	for (const FADSKBrainDebugRow& Row : Snapshot.Rows)
	{
		if (!Row.bHasLabelLocation) continue;
		const FString State = Row.ActiveStatePath.IsEmpty() ? TEXT("stopped") : Row.ActiveStatePath.Last();
		FString Node = Row.ActiveNodeId.IsNone() ? TEXT("none") : Row.ActiveNodeId.ToString();
		for (const FName Background : Row.BackgroundNodeIds) { Node += TEXT(" + ") + Background.ToString(); }
		DrawDebugString(World, Row.LabelLocation, State + TEXT(" / ") + Node, nullptr, FColor::Cyan, 0.0f, false);
	}
}

TStatId UADSKDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UADSKDebugSubsystem, STATGROUP_Tickables);
}
