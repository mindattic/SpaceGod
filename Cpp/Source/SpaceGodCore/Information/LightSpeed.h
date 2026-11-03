#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/LightSpeed.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// Light speed, the fastest anything is known inside a system (U.SPC-T100). A scanner sees a ship where it was
	/// when the light left it, and a sighting radioed on to a fleet elsewhere in the system arrives as late again as
	/// the fleet is far from whoever saw it: about 8 minutes an AU, so a picture shared across Sol is minutes to
	/// hours old by the time anyone far away acts on it.
	/// </summary>
	class SPACEGODCORE_API LightSpeed
	{
	public:
		/// <summary>The speed of light, km/s (exact, by the SI definition of the metre).</summary>
		static constexpr double SpeedKmS = 299792.458;

		/// <summary>How long light takes to cross <paramref name="km"/>, seconds.</summary>
		static double DelaySeconds(double km) { return cs::Math::Max(0.0, km) / SpeedKmS; }

		/// <summary>How long light takes to cross <paramref name="km"/>, days.</summary>
		static double DelayDays(double km);

		/// <summary>How long light takes between two points (heliocentric km), days.</summary>
		static double DelayDays(DVec3 a, DVec3 b) { return DelayDays((a - b).Length()); }

		/// <summary>A delay for the HUD: "4.2 s", "8.3 min", "1.4 h", "3.2 days".</summary>
		static cs::String Describe(double days);
	};
}
