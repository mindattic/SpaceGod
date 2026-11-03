using UnrealBuildTool;

// The C# track's only C++: the primary game module every UE game target needs. All game code is C#
// (Script/ManagedUnrealSpaceGodCSharp), hosted by the UnrealSharp plugin.
public class UnrealSpaceGodCSharp : ModuleRules
{
	public UnrealSpaceGodCSharp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
	}
}
