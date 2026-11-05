#include "EngineSettings.h"
#include "SessionAudio.h"
#include "UnityCompat/UnityTypes.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	const cs::List<int>& EngineSettings::FrameRates()
	{
		static const cs::List<int> value{ 30, 60, 90, 120, 144, 165, 240, -1 };
		return value;
	}

	const cs::Ref<ChoiceSetting>& EngineSettings::FrameRateCap()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.frameRateCap", SettingGroups::Graphics,
			"Frame rate cap", cs::List<cs::String>{ "30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "240 fps", "Unlimited" }, 4,
			"The most frames drawn a second when VSync is off (with VSync on, the screen's refresh rate sets it).",
			"fps performance framerate")->Applying([](int i) { Application::SetTargetFrameRate(FrameRates()[i]); });
		return value;
	}

	const cs::Ref<ToggleSetting>& EngineSettings::Msaa()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("render.msaa", SettingGroups::RenderPipeline,
			"Anti-aliasing (MSAA 4x)", true, "Smooths the edges of hulls, rings and orbit lines, at some cost on a slow GPU.",
			"antialiasing jaggies edges multisample quality")->Applying([](bool on)
			{
				for (Camera* cam : Camera::allCameras()) Rendering::RenderBackend::Active()->SetMsaa(cam, on);
			});
		return value;
	}

	const cs::Ref<RangeSetting>& EngineSettings::MasterVolume()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("audio.masterVolume", SettingGroups::Audio,
			"Master volume", 100, 0, 100, 5, "How loud everything the game plays is.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }, "sound mute loudness")
			->Applying([](double v) { AudioListener::volume = SessionAudio::ListenerVolume(v); }); // 0 under the CLI mute (U.SPC-T536)
		return value;
	}
}
