#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class ToggleSetting;
	class RangeSetting;
}

namespace SpaceGod
{
	/// The settings that belong to the engine itself rather than to one of the game's systems (U.SPC-T431): the
	/// frame rate cap, multisample anti-aliasing and the master volume. Each goes into effect the moment it changes
	/// and whenever a profile is loaded. (The resolution, window mode and VSync have their own Display page.)
	///
	/// C#: [SettingsSource] -- registered with the settings registry by GameSettingsHost.
	class EngineSettings
	{
	public:
		static const cs::Ref<Core::ChoiceSetting>& FrameRateCap();
		static const cs::Ref<Core::ToggleSetting>& Msaa();
		static const cs::Ref<Core::RangeSetting>& MasterVolume();

	private:
		static const cs::List<int>& FrameRates();
	};
}
