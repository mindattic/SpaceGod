using UnrealBuildTool;

public class UnrealSpaceGodCSharpEditorTarget : TargetRules
{
	public UnrealSpaceGodCSharpEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSpaceGodCSharp");
	}
}
