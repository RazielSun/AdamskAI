// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAIBench : ModuleRules
{
	public AdamskAIBench(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			// CQTest.h includes Engine/Engine.h.
			"Engine",
			"AdamskAI",
			"AdamskAIHost",
			// Behavior Trees and AAIController.
			"AIModule",
			"GameplayTags",
			"GameplayTasks",
			// StateTree runtime, its component, and the compiler that builds a tree from code (editor only).
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayStateTreeModule",
			"PropertyBindingUtils",
			"CQTest",
			"Projects",
			"Json"
		});
		// The StateTree side uses the engine's own Delay task, which is exported but lives in a private header.
		PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins", "Runtime", "StateTree", "Source", "StateTreeModule", "Private"));
	}
}
