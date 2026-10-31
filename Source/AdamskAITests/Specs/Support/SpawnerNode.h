// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "GameFramework/Pawn.h"
#include "Nodes/ADSKBuiltinNodes.h"
#include "Runtime/ADSKNodeRegistry.h"
#include "Support/TestADSKNodes.h"
#include "Support/TestChecks.h"

namespace FADSKSpawnerNode
{
	inline FADSKNodeDescriptor Make()
	{
		FADSKNodeDescriptor Spawner;
		Spawner.Name = TEXT("Spawner");
		Spawner.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Succeeded; };
		Spawner.ConfigStruct = FADSKTestClassTagConfig::StaticStruct();
		FADSKFieldMap& Spawn = Spawner.Fields.Add_GetRef({TEXT("spawn"), TEXT("Spawn")});
		Spawn.MetaClass = APawn::StaticClass();
		Spawner.Fields.Add({TEXT("tag"), TEXT("Tag")});
		return Spawner;
	}

	inline TSharedRef<const FADSKNodeRegistry> MakeRegistry(bool bWithBuiltins = true)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		if (bWithBuiltins)
		{
			FADSKBuiltinNodes::RegisterAll(*Registry);
		}
		ADSKTest::Register(*Registry, Make());
		Registry->Freeze();
		return Registry;
	}
}
