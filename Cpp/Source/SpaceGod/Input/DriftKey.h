#pragma once

// US-T165: the drift key's two ways of working, as the player sets them under Controls. Hold: the craft drifts while R is
// held, and boosts out when it is let go. Toggle: R starts the drift and R again ends it (the same boost-out). Either way
// what comes out is InputState::Drift, R as if held; the physics downstream (Core::DriftControl) sees no difference, and
// a co-op guest's frames carry its own player's drift already resolved by its own setting.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
}

namespace SpaceGod::Input
{
	class DriftKey final
	{
	public:
		/// <summary>The modes, in the setting's order.</summary>
		enum class Mode { Hold = 0, Toggle = 1 };

		/// <summary>Controls: Hold (default) or Toggle. C#-style [SettingsSource] accessor, touched by GameSettingsHost.</summary>
		static const cs::Ref<Core::ChoiceSetting>& ModeSetting();

		/// <summary>The mode set now.</summary>
		static Mode Current();

		/// <summary>This frame's drift from the R key (<paramref name="down"/> held, <paramref name="pressed"/> pressed this
		/// frame): held in hold mode; in toggle mode a latch each press flips.</summary>
		static bool Read(bool down, bool pressed);

		/// <summary>Lets go of a toggled drift (out of the controls, in command, where R toggles auto-reorient).</summary>
		static void Drop() { latched = false; }

	private:
		static inline bool latched = false;
	};
}
