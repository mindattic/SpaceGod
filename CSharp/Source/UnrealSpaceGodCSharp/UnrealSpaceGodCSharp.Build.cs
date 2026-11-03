using UnrealBuildTool;

// The C# track's only C++: the primary game module every UE game target needs. All game code is C#
// (Script/ManagedUnrealSpaceGodCSharp), hosted by the UnrealSharp plugin.
public class UnrealSpaceGodCSharp : ModuleRules
{
	public UnrealSpaceGodCSharp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		// sg.SetNearClipPlane (US-T45): the engine's near-plane setter without r.SetNearClipPlane's 1 cm floor.
		PrivateDependencyModuleNames.Add("RenderCore");
		// sg.QueryDisplayModes / sg.SetFullscreenRefresh (US-T56): the monitor's modes with their refresh rates and the
		// rate set on the game's swap chain (SpaceGodRender's SpaceGodDisplayModes.h); sg.SetLineWidthPx (US-T55).
		PrivateDependencyModuleNames.AddRange(new[] { "RHI", "Slate", "SlateCore", "ApplicationCore", "SpaceGodRender" });
	}
}
