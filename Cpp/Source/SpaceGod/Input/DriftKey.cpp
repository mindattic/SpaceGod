// US-T165: the drift key, hold or toggle (Input/DriftKey.h).

#include "Input/DriftKey.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	const cs::Ref<ChoiceSetting>& DriftKey::ModeSetting()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("controls.driftMode", SettingGroups::Controls,
			"Drift key (R)", cs::List<cs::String>{ "Hold", "Toggle" }, static_cast<int>(Mode::Hold),
			"Hold: a fighter drifts while R is held and boosts out when you let go. Toggle: R starts the drift and R again ends it.",
			"drift hold toggle fighter rcs r key")->Applying([](int) { latched = false; });
		return value;
	}

	DriftKey::Mode DriftKey::Current() { return ModeSetting()->Index == static_cast<int>(Mode::Toggle) ? Mode::Toggle : Mode::Hold; }

	bool DriftKey::Read(bool down, bool pressed)
	{
		if (Current() == Mode::Hold)
		{
			latched = false;
			return down;
		}
		if (pressed) latched = !latched;
		return latched;
	}
}
