// Copyright Narrative Tools 2022. 

using UnrealBuildTool;

public class NarrativeDialogueEditor : ModuleRules
{
	public NarrativeDialogueEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bLegacyPublicIncludePaths = false;

        //Required for registering dialogue assets
        PrivateIncludePathModuleNames.AddRange(
			new string[] {
                "AssetRegistry",
                "AssetTools",
                "PropertyEditor",
                "ContentBrowser"
            }
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
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
                "AssetTools",
                "UMG"
            }
			);



        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                "WorkspaceMenuStructure",
                "AssetRegistry",
                "ContentBrowser"
            }
            );
    }
}
