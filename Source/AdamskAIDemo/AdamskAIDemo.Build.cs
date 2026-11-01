// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIDemo : ModuleRules
{
	public AdamskAIDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Engine and AI only: the demo draws with basic shapes and debug text.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AIModule", "AdamskAI", "AdamskAIHost" });
		PrivateDependencyModuleNames.AddRange(new string[] { "NavigationSystem", "Projects" });
	}
}
