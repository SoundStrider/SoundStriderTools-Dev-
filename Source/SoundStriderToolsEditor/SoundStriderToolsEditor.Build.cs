// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

using UnrealBuildTool;

public class SoundStriderToolsEditor : ModuleRules
{
    public SoundStriderToolsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivatePCHHeaderFile = "Public/SoundStriderToolsEditor.h";


        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UnrealEd",
            "SoundStriderTools",
            
            "Slate",
            "SlateCore",
            "InputCore",
            "ToolMenus",
            "WorkspaceMenuStructure",
            "PropertyEditor",
            "MessageLog"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "GameplayTags",
            // Trigger event pin on TriggerOutputsFromEvent nodes
            "GraphEditor",
            "BlueprintGraph"
        });
    }
}