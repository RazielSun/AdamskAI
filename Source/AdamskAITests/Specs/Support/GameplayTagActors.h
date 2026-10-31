// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagActors.generated.h"

UCLASS(NotBlueprintable, Transient)
class AADSKTestTaggedActor : public AActor, public IGameplayTagAssetInterface
{
	GENERATED_BODY()
public:
	FGameplayTagContainer OwnedTags;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& Out) const override { Out = OwnedTags; }
};

UCLASS(NotBlueprintable, Transient)
class AADSKTestTaggedPawn : public APawn, public IGameplayTagAssetInterface
{
	GENERATED_BODY()
public:
	FGameplayTagContainer OwnedTags;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& Out) const override { Out = OwnedTags; }
};
