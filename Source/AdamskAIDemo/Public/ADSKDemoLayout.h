// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"

/**
 * The demo arena, shared by its generator (AdamskAIWorld.Demo.Content) and the scenes, so the map and what the scenes
 * spawn on it cannot disagree. Centimetres; the floor's top is at Z 0.
 */
namespace ADSKDemoLayout
{
	inline const TCHAR* MapDirectory = TEXT("/AdamskAI/Demo/Maps");
	inline const TCHAR* MapName = TEXT("L_AdskDemo");

	/** A square floor, walled on every side. */
	constexpr double FloorHalf = 3000.0;
	constexpr double WallHeight = 200.0;
	constexpr double WallThickness = 50.0;

	/** Cover blocks: centre and size. Low enough to see over from the camera, tall enough to block sight traces. */
	struct FBlock { FVector Center; FVector Size; };
	inline const FBlock Cover[] = {
		{FVector(-800.0, 600.0, 100.0), FVector(400.0, 100.0, 200.0)},
		{FVector(900.0, -500.0, 100.0), FVector(100.0, 500.0, 200.0)},
		{FVector(0.0, 1500.0, 100.0), FVector(600.0, 100.0, 200.0)},
		{FVector(-1600.0, -1200.0, 100.0), FVector(300.0, 300.0, 200.0)},
		{FVector(1700.0, 1400.0, 100.0), FVector(100.0, 400.0, 200.0)},
	};

	/** The guard post and the patrol square around the middle. */
	inline const FVector Post(-2200.0, -2200.0, 0.0);
	inline const FVector PatrolPoints[] = {
		FVector(-1500.0, -1500.0, 0.0), FVector(1500.0, -1500.0, 0.0), FVector(1500.0, 1500.0, 0.0), FVector(-1500.0, 1500.0, 0.0)};

	/** The loop the stand-in for the player walks. */
	inline const FVector TargetLoop[] = {
		FVector(2400.0, 2400.0, 0.0), FVector(2400.0, -600.0, 0.0), FVector(300.0, -2400.0, 0.0), FVector(-2400.0, 300.0, 0.0), FVector(-300.0, 2400.0, 0.0)};

	inline const FVector PlayerStart(0.0, -2600.0, 100.0);
	/** Generous in Z so agents standing on the floor are inside it. */
	inline const FVector NavBoundsSize(2.0 * FloorHalf + 400.0, 2.0 * FloorHalf + 400.0, 800.0);
}
