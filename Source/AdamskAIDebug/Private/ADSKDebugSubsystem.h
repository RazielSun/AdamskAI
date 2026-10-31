// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "ADSKDebugSubsystem.generated.h"

/** Draws `adsk.Labels`: one `state / node` label over every brain's pawn, each frame the variable is on. */
UCLASS()
class ADAMSKAIDEBUG_API UADSKDebugSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
};
