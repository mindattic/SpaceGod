#include "GameSettingsHost.h"
#include "ActiveProfile.h"
#include "EngineSettings.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "UnityCompat/UnityTypes.h"
#include "Settings/SettingsRegistry.h"
#include "Settings/GameSetting.h"
#include "Persistence/Profiles.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	const cs::Ref<SettingsRegistry>& GameSettingsHost::Registry() { return SettingsRegistry::Default(); }

	void GameSettingsHost::RegisterRuntimeSources()
	{
		// The Runtime assembly's [SettingsSource] classes in this ticket's scope. Rendering's RenderPipelineSettings and
		// UrpPostProcessSettings and Views' BattleFx (EffectsBudget) register theirs the same way from their tickets.
		SettingsRegistry::AddDeclarationSource([] { EngineSettings::FrameRateCap(); EngineSettings::Msaa(); EngineSettings::MasterVolume(); });
		SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::AutoReorientSetting(); });
		SettingsRegistry::AddDeclarationSource([] { Cameras::SpaceCamera::LookSensitivitySetting(); Cameras::SpaceCamera::InvertLook(); });
	}

	void GameSettingsHost::Bind()
	{
		if (!discovered)
		{
			discovered = true;
			RegisterRuntimeSources();
			SettingsRegistry::DiscoverLoadedDeclarations();
			Registry()->StoreFailed += [](cs::Ref<GameSetting> s, const cs::Exception& e)
			{
				Debug::LogError(cs::Cat("[Unity.SpaceGod] Saving the setting '", s->Key, "' failed: ", e.what()));
			};
		}
		Registry()->Bind(cs::New<ProfileSettingStore>(ActiveProfile::Current()));
	}
}
