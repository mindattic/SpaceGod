using UnrealBuildTool;

/// The shader-directory module of the SpaceGodRender plugin. It loads at PostConfigInit, before the shader
/// compiler starts, so the materials' Custom nodes can #include "/Plugin/SpaceGodRender/*.ush".
public class SpaceGodRender : ModuleRules
{
	public SpaceGodRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "RenderCore", "Projects" });
	}
}
