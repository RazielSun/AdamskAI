// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIEditor : ModuleRules
{
	public AdamskAIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AdamskAI", "AdamskAIHost" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects", "Json", "DirectoryWatcher", "AssetRegistry", "GameplayTags" });
	}
}
