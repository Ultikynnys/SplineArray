// Copyright (c) 2026. MIT License.

using UnrealBuildTool;

public class SplineArray : ModuleRules
{
	public SplineArray(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GeometryCore",
			"MeshConversion",
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore"
		});
	}
}
