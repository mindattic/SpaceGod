using UnrealBuildTool;

public class UnrealSpaceGod : ModuleRules
{
	public UnrealSpaceGod(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore", "Slate", "SlateCore", "ProceduralMeshComponent", "ImageWrapper",
		});
	}
}
