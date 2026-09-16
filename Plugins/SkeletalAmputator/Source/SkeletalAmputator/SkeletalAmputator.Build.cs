// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

using UnrealBuildTool;

public class SkeletalAmputator : ModuleRules
{
	public SkeletalAmputator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		
		PrivateIncludePaths.AddRange(
			new string[]
			{
				// ... add other private include paths required here ...
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ProceduralMeshComponent",
				"GeometryCore",
				"GeometryFramework"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Niagara",
				"NiagaraCore",
				"RenderCore",
				"RHI",
				"DynamicMesh",
				"GeometryAlgorithms",
				"MeshConversion",
				"MeshDescription",
				"MeshConversionEngineTypes",
				"SkeletalMeshDescription",
				"AssetRegistry"
			}
		);
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
                "AssetTools"
            });
        }
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}