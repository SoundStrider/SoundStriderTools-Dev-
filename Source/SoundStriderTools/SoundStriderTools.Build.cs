// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

using UnrealBuildTool;

public class SoundStriderTools : ModuleRules
{
	public SoundStriderTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Public/SST_LOG.h";

		PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { });

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Core",
			"GameplayTags"

		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			
		});

		DynamicallyLoadedModuleNames.AddRange(new string[] { });
	}
}