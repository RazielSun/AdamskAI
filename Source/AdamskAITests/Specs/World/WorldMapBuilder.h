// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "ActorFactories/ActorFactory.h"
#include "AssetCompilingManager.h"
#include "Builders/CubeBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"

/** What the map generators (AdamskAIWorld.Content, AdamskAIWorld.Demo.Content) share: blocks and a built navmesh. */
namespace ADSKWorldMapBuilder
{
	/** A cube mesh actor centred at Center, Size in cm (the engine cube is 100 cm). */
	inline AStaticMeshActor* Block(UWorld& World, UStaticMesh* Cube, const TCHAR* Label, const FVector& Center, const FVector& Size)
	{
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Actor->SetActorScale3D(Size / 100.0);
		Actor->SetActorLabel(Label);
		return Actor;
	}

	inline ARecastNavMesh* FindNavMesh(UWorld& World)
	{
		TActorIterator<ARecastNavMesh> It(&World);
		return It ? *It : nullptr;
	}

	/** RuntimeGeneration has no setter; the generator writes it as the details panel would. */
	inline void MakeDynamic(ARecastNavMesh& NavMesh)
	{
		const FEnumProperty* Property = CastField<FEnumProperty>(ANavigationData::StaticClass()->FindPropertyByName(TEXT("RuntimeGeneration")));
		Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(&NavMesh), static_cast<int64>(ERuntimeGenerationType::Dynamic));
	}

	/**
	 * Nav bounds of Size at Center and a dynamic navmesh built inside them, in a new editor world; null when nothing
	 * was built. Dynamic, so obstacles a scene or test spawns cut the navmesh, whatever the project default is.
	 */
	inline ARecastNavMesh* BuildNavigation(UWorld& World, const FVector& Center, const FVector& Size)
	{
		ANavMeshBoundsVolume* Bounds = World.SpawnActor<ANavMeshBoundsVolume>(Center, FRotator::ZeroRotator);
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Size.X;
		Builder->Y = Size.Y;
		Builder->Z = Size.Z;
		UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
		Bounds->SetActorLabel(TEXT("NavBounds"));
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!Navigation)
		{
			return nullptr;
		}
		// A new editor world holds AsyncLoadLock until a ticker sees asset compilation done; this runs before any
		// tick, so finish compiling and lift the lock the way that ticker would, without its rebuild.
		FAssetCompilingManager::Get().FinishAllCompilation();
		Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		Navigation->OnNavigationBoundsUpdated(Bounds);
		// Spawned here rather than left to Build, which spawns none in a world this new.
		ARecastNavMesh* NavMesh = World.SpawnActor<ARecastNavMesh>();
		if (!NavMesh)
		{
			return nullptr;
		}
		MakeDynamic(*NavMesh);
		// The bounds update and the registration are queued for the navigation tick; apply them before building.
		Navigation->Tick(0.f);
		Navigation->Build();
		return FindNavMesh(World) == NavMesh && NavMesh->GetNumActiveTiles() > 0 ? NavMesh : nullptr;
	}
}

#endif
