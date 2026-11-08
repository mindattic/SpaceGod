#include "GameSettingsHost.h"
#include "ActiveProfile.h"
#include "EngineSettings.h"
#include "PlayerOptions.h" // US-T252
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Input/DriftKey.h"
#include "Input/Gamepad.h" // US-T250
#include "Rendering/Pipeline/RenderBackend.h"
#include "Views/BattleFx.h"
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
		// US-T70: URP's post-processing settings first. In Unity they live in an assembly of their own
		// (Unity.SpaceGod.Rendering.Urp) that declares them before the game's Runtime does, so the original's Render pipeline
		// page reads URP's rows, then render.msaa, then the pipeline and HDRP's (SpaceGod.Unity.CSharp's audit; the Unreal C#
		// copy's Bootstrap.MatchSettingsOrder, 57c111a). Nothing touches these settings before this discovery (GameSettingsHost::Bind).
		SettingsRegistry::AddDeclarationSource([]
		{
			using Rendering::UrpPostProcessSettings;
			UrpPostProcessSettings::Enabled(); UrpPostProcessSettings::Metering(); UrpPostProcessSettings::Compensation();
			UrpPostProcessSettings::Iso(); UrpPostProcessSettings::Shutter(); UrpPostProcessSettings::Aperture();
			UrpPostProcessSettings::Bloom(); UrpPostProcessSettings::BloomThreshold(); UrpPostProcessSettings::SunBrightness();
			UrpPostProcessSettings::Tonemapping(); UrpPostProcessSettings::Contrast(); UrpPostProcessSettings::Saturation();
			UrpPostProcessSettings::Temperature();
		});
		// The Runtime assembly's [SettingsSource] classes, each accessor touched in C# field order.
		SettingsRegistry::AddDeclarationSource([] { EngineSettings::FrameRateCap(); EngineSettings::Msaa(); EngineSettings::MasterVolume(); });
		SettingsRegistry::AddDeclarationSource([] { PlayerOptions::Declare(); }); // US-T252: Settings > Gameplay and Accessibility
		SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::AutoReorientSetting(); });
		SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::TutorialHintsSetting(); }); // US-T254: the first-time hints
		SettingsRegistry::AddDeclarationSource([] { Cameras::SpaceCamera::LookSensitivitySetting(); Cameras::SpaceCamera::InvertLook(); });
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::DriftKey::ModeSetting(); }); // US-T165: drift on R, hold or toggle
		SettingsRegistry::AddDeclarationSource([] { ::SpaceGod::Input::Gamepad::Declare(); }); // US-T250: the pad's dead zone, aim and buttons
		SettingsRegistry::AddDeclarationSource([]
		{
			using Rendering::RenderPipelineSettings;
			RenderPipelineSettings::Pipeline(); RenderPipelineSettings::PhysicalSky(); RenderPipelineSettings::Volumetrics();
			RenderPipelineSettings::Exposure(); RenderPipelineSettings::ExposureCompensation(); RenderPipelineSettings::SunBrightness();
			RenderPipelineSettings::RayTracing();
		});
		SettingsRegistry::AddDeclarationSource([] { Views::BattleFx::EffectsBudget(); });
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
