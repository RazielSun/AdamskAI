// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAI : ModuleRules
{
	public AdamskAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });

		// Expat is the strict XML reader; Json writes the schema export; TraceLog holds the Insights
		// channel. None appears in a public header, so none leaks to dependents.
		PrivateDependencyModuleNames.AddRange(new string[] { "Expat", "Json", "TraceLog" });

		// The engine links Expat statically. Without XML_STATIC, expat_external.h declares every
		// XML_* function __declspec(dllimport) on Windows and the link fails with __imp_XML_*.
		PrivateDefinitions.Add("XML_STATIC=1");
	}
}
