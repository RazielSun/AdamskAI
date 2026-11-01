// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAITests : ModuleRules
{
	public AdamskAITests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Unity off on purpose: every spec's `namespace F<File>Private` + `using namespace` collides once concatenated.
		bUseUnity = false;

		// Specs include their shared helpers as "Support/<Header>.h".
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Specs"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			// CQTest.h includes Engine/Engine.h, so any module that includes it needs Engine on its path.
			"Engine",
			"AdamskAI",
			"AdamskAIHost",
			"AdamskAIDemo",
			// The world specs drive Act and HoldToken through test performers; the demo specs run the demo scenes.
			"AIModule",
			// The AdamskAIWorld.* specs wait for the test map's navmesh and lock AI logic with a gameplay task.
			"NavigationSystem",
			"GameplayTasks",
			"CQTest",
			"Projects",
			"Json",
			"GameplayTags",
			// The Insights smoke test toggles AdamskAI's trace channel.
			"TraceLog"
		});

		// The commandlet and its report writer exist only in editor builds; the game target never links them.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AdamskAIEditor");
			// FFileChangeData, which the live-reload specs feed in.
			PrivateDependencyModuleNames.Add("DirectoryWatcher");
			// The test content generator: a new map, a brush for its nav bounds, saving it and the EQS assets.
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("AssetRegistry");
			// CQTest's PIE network component replicates through Iris in its header templates.
			PrivateDependencyModuleNames.Add("IrisCore");
			// The dedicated-server spec reads the AdamskAI category's lines from the client's replicator.
			if (Target.bUseGameplayDebugger)
			{
				PrivateDependencyModuleNames.Add("GameplayDebugger");
			}
		}
	}
}
