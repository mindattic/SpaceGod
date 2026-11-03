using UnrealBuildTool;

public class UnrealSpaceGodTarget : TargetRules
{
	public UnrealSpaceGodTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSpaceGod");
	}
}
