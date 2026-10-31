// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIHost : ModuleRules
{
	public AdamskAIHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// GameplayTags is public: the brain's active state tags are an FGameplayTagContainer.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AIModule", "DeveloperSettings", "GameplayTags", "AdamskAI" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "Json" });
	}
}
