using UnrealBuildTool;

public class SpaceGodTarget : TargetRules
{
	public SpaceGodTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SpaceGod");
	}
}
