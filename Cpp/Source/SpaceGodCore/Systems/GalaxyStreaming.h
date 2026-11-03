#pragma once
// Port of Assets/UnitySpace/Core/Systems/GalaxyStreaming.cs (US-T17). The Galaxy members this file adds (its partial
// class part) are declared in Systems/Galaxy.h and defined in GalaxyStreaming.cpp.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;

	/// <summary>
	/// The settings menu's control of how much of the galaxy is charted and simulated about the pilot (U.SPC-T413, see
	/// <see cref="Galaxy.Explore"/>).
	/// </summary>
	// [SettingsSource]: the settings registry finds these by touching the accessors (no reflection in C++).
	class GalaxyStreaming
	{
	public:
		/// <summary>How far about the pilot stars are charted: shown in the interstellar view and open to a cruise.</summary>
		static const cs::Ref<RangeSetting>& ChartRadius();

		/// <summary>How near the pilot a charted star is built into a running system.</summary>
		static const cs::Ref<RangeSetting>& SystemRadius();

		/// <summary>The most charted stars kept at once, nearest first.</summary>
		static const cs::Ref<RangeSetting>& MaxCharted();
	};
}
