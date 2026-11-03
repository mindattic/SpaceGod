// SpaceGodRender: maps the plugin's Shaders/ folder to the virtual shader path /Plugin/SpaceGodRender, which the
// materials in this plugin's Content/ #include from their Custom expression nodes (see ../../../README.md).

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FSpaceGodRenderModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpaceGodRender"));
		check(Plugin.IsValid());
		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/SpaceGodRender"), ShaderDir);
	}
};

IMPLEMENT_MODULE(FSpaceGodRenderModule, SpaceGodRender)
