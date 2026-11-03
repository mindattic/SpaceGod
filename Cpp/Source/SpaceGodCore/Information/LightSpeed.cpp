// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/LightSpeed.cs (see docs/MIGRATION.md).

#include "Information/LightSpeed.h"

#include "Astro.h"

namespace SpaceGod::Core
{
	double LightSpeed::DelayDays(double km) { return DelaySeconds(km) / Astro::SecondsPerDay; }

	cs::String LightSpeed::Describe(double days)
	{
		double s = days * Astro::SecondsPerDay;
		if (s < 90) return cs::Cat(cs::Fmt(s, "0.0"), " s");
		if (s < 5400) return cs::Cat(cs::Fmt(s / 60, "0.0"), " min");
		if (days < 2) return cs::Cat(cs::Fmt(s / 3600, "0.0"), " h");
		return cs::Cat(cs::Fmt(days, "0.0"), " days");
	}
}
