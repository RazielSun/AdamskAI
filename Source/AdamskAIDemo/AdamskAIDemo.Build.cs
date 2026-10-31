// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIDemo : ModuleRules
{
	public AdamskAIDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Engine and AI only: the demo draws with engine basic shapes and debug text, so it needs no animation, input,
		// UI or tag module.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AIModule", "AdamskAI", "AdamskAIHost" });
		PrivateDependencyModuleNames.AddRange(new string[] { "NavigationSystem", "Projects" });
	}
}
