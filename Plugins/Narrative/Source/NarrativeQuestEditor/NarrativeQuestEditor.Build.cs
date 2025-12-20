// Copyright Narrative Tools 2022. 

using UnrealBuildTool;

public class NarrativeQuestEditor : ModuleRules
{
	public NarrativeQuestEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        bLegacyPublicIncludePaths = false;
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {});
        PrivateIncludePaths.AddRange(new string[] { "NarrativeQuestEditor/Private" });

        //Required for registering quest assets
        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetRegistry",
                "AssetTools",
                "PropertyEditor",
                "ContentBrowser"
            }
            );

        PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "ApplicationCore",
                "Engine",
                "RenderCore",
                "InputCore",
                "Slate",
                "SlateCore",
                "EditorStyle",
                "UnrealEd",
                "AudioEditor",
                "MessageLog",
                "GraphEditor",
                "Projects",
                "Kismet",
				"KismetCompiler",
                "KismetWidgets",
                "PropertyEditor",
                "AnimGraph",
                "BlueprintGraph",
                "ClassViewer",
                "Narrative",
                "AIGraph",
                "AssetTools",
                "UMG"
            }
			);
		

	}
}
