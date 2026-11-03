using UnrealBuildTool;

public class UnrealSpaceGodCSharpTarget : TargetRules
{
	public UnrealSpaceGodCSharpTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSpaceGodCSharp");
	}
}
