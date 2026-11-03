using UnrealBuildTool;

/// The shader-directory module of the SpaceGodRender plugin. It loads at PostConfigInit, before the shader
/// compiler starts, so the materials' Custom nodes can #include "/Plugin/SpaceGodRender/*.ush".
public class SpaceGodRender : ModuleRules
{
	public SpaceGodRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "RenderCore", "Projects" });

		// Unity.SpaceGod's Resources/Data files, read as plain files (not assets) by both tracks' Resources facades
		// through the plugin's content directory: staged loose beside the plugin in a package (US-T46).
		foreach (string data in new[] { "asteroids.bytes", "ShipModels.json" })
			RuntimeDependencies.Add("$(PluginDir)/Content/Resources/Data/" + data, StagedFileType.NonUFS);
	}
}
