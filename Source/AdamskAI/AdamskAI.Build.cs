// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

using UnrealBuildTool;

public class AdamskAI : ModuleRules
{
	public AdamskAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// One float result in every build: MSVC compiles games, and editors before 5.8, with /fp:fast otherwise.
		FPSemantics = FPSemanticsMode.Precise;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });

		// Private only: none of these appears in a public header.
		PrivateDependencyModuleNames.AddRange(new string[] { "Expat", "Json", "TraceLog" });

		// The engine links Expat statically; without XML_STATIC Windows links against __imp_XML_*.
		PrivateDefinitions.Add("XML_STATIC=1");
	}
}
