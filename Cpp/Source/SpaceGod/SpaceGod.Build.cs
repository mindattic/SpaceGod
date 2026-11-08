using UnrealBuildTool;

// The C++ track's game module: Unity.SpaceGod's Runtime layer (UnitySpaceApp, camera, input, and later views, UI and
// rendering) on Unreal, driving SpaceGodCore.
public class SpaceGod : ModuleRules
{
	public SpaceGod(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);

		// SpaceGodCore's headers are C++20 on Cs/Cs.h: C# exceptions are C++ exceptions and cs::As is a
		// dynamic_pointer_cast, so code that includes them needs both, as the Core module itself does.
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseRTTI = true;
		// Each ported file keeps its own file-local helpers (static ToF, anonymous-namespace caches) as the C# files
		// keep their private members: a unity build would merge them into one translation unit and collide.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore", "Slate", "SlateCore", "ProceduralMeshComponent", "ImageWrapper",
			"SpaceGodCore", // the C++ port of Unity.SpaceGod's Core (Cpp/Source/SpaceGodCore)
			// Views/Rendering (US-T30): meshes drawn as UDynamicMeshComponents, the plugin found for Resources, ShipModels.json.
			"GeometryCore", "GeometryFramework", "Projects", "Json", "ImageCore",
		});
		// UI/DisplayBackend (US-T56): the monitor's modes with their refresh rates (SpaceGodRender's SpaceGodDisplayModes.h)
		// and the rate set on the game's swap chain.
		PrivateDependencyModuleNames.AddRange(new[] { "SpaceGodRender", "RHI", "RenderCore", "ApplicationCore" });
		// US-T234: the shared --headless mode (Unreal.Shared's MindAtticShared plugin): Bootstrap and GameLaunch key the
		// game's own opt-ins (scripted input, no main menu) off FMindAtticHeadless::IsActive().
		PrivateDependencyModuleNames.Add("MindAtticHeadless");
		// US-T139: the BYOKey voice (GameVoice) calls the player's own LLM over HTTPS.
		PrivateDependencyModuleNames.Add("HTTP");
	}
}
