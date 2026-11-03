using UnrealBuildTool;

public class UnrealSpaceGodEditorTarget : TargetRules
{
	public UnrealSpaceGodEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSpaceGod");
	}
}
