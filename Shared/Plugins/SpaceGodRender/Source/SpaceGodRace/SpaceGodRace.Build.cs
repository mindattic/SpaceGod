using UnrealBuildTool;

/// The race's frame sampler (US-T46, docs/RACE.md): one implementation, loaded by both tracks' games, so the C++ and
/// the C# game are timed by the same code. Inert unless a race is running (console commands sg.Race.*).
public class SpaceGodRace : ModuleRules
{
	public SpaceGodRace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RenderCore", "RHI" });
	}
}
