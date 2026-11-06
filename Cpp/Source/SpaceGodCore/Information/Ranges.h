#pragma once
// US-T156: every range the player reads, on the HUD or over comms, comes from here, so one hull is never three
// distances at once. A range is the true distance in the simulation's km at both scales: the gameplay scale changes how
// the world is drawn, not how far apart things are, and a fight (where most ranges are read) is drawn alike at both.
// Two ways of saying it: the HUD's readout ("319.0 km") and the radio's ("319 klicks"), the same number.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	struct SPACEGODCORE_API Ranges
	{
		/// <summary>The range between two points in one frame (heliocentric, or a pilot's frame), km.</summary>
		static double Km(DVec3 from, DVec3 to) { return (to - from).Length(); }

		/// <summary>The HUD's readout: "850 m" under 10 km, else "319.0 km".</summary>
		static cs::String Readout(double km);

		/// <summary>The radio's: "850 metres", "319 klicks", "4.2 thousand klicks", "1.3 million klicks", "0.42 AU".</summary>
		static cs::String Spoken(double km);
	};
}
