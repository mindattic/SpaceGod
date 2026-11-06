// US-T156: the one source of every range the player reads (Ranges.h).

#include "Information/Ranges.h"

namespace SpaceGod::Core
{
	cs::String Ranges::Readout(double km)
	{
		return km < 10 ? cs::Cat(cs::Fmt(km * 1000, "N0"), " m") : cs::Cat(cs::Fmt(km, "0.0"), " km");
	}

	cs::String Ranges::Spoken(double km)
	{
		const double au = 1.495978707e8;
		if (km < 1) return cs::Cat(cs::Fmt(km * 1000, "N0"), " metres");
		if (km < 1000) return cs::Cat(cs::Fmt(km, km < 10 ? "0.#" : "0"), km < 1.5 ? " klick" : " klicks");
		if (km < 1e6) return cs::Cat(cs::Fmt(km / 1000, km < 1e4 ? "0.#" : "0"), " thousand klicks");
		if (km < 0.05 * au) return cs::Cat(cs::Fmt(km / 1e6, "0.#"), " million klicks");
		return cs::Cat(cs::Fmt(km / au, km < au ? "0.##" : "0.#"), " AU");
	}
}
