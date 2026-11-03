using UnrealBuildTool;

// The C++ track's game module: Unity.SpaceGod's Runtime layer (UnitySpaceApp, camera, input, and later views, UI and
// rendering) on Unreal, driving SpaceGodCore.
public class UnrealSpaceGod : ModuleRules
{
	public UnrealSpaceGod(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);

		// SpaceGodCore's headers are C++20 on Cs/Cs.h: C# exceptions are C++ exceptions and cs::As is a
		// dynamic_pointer_cast, so code that includes them needs both, as the Core module itself does.
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseRTTI = true;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore", "Slate", "SlateCore", "ProceduralMeshComponent", "ImageWrapper",
			"SpaceGodCore", // the C++ port of Unity.SpaceGod's Core (Cpp/Source/SpaceGodCore)
			// Views/Rendering (US-T30): meshes drawn as UDynamicMeshComponents, the plugin found for Resources, ShipModels.json.
			"GeometryCore", "GeometryFramework", "Projects", "Json", "ImageCore",
		});
	}
}
