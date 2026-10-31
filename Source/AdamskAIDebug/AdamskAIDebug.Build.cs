// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIDebug : ModuleRules
{
	public AdamskAIDebug(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AdamskAI", "AdamskAIHost" });
		// The `AdamskAI` Gameplay Debugger category; the module defines WITH_GAMEPLAY_DEBUGGER_MENU for us.
		if (Target.bUseGameplayDebugger)
		{
			PrivateDependencyModuleNames.Add("GameplayDebugger");
		}
		else
		{
			PrivateDefinitions.Add("WITH_GAMEPLAY_DEBUGGER_MENU=0");
		}
	}
}
