#pragma once
// Port of Assets/UnitySpace/Core/Systems/StarDiscovery.cs (US-T17). The Galaxy members this file adds (its partial
// class part) are declared in Systems/Galaxy.h and defined in StarDiscovery.cpp.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"

namespace SpaceGod::Core
{
	class Galaxy;
	class RangeSetting;

	/// <summary>Whether a star of the real neighbourhood is known yet (U.SPC-T456, see <see cref="Galaxy.Undiscovered"/>).</summary>
	enum class DiscoveryState : int
	{
		/// <summary>Known: on the charts, the galaxy map and the FTL targets, as every star always was.</summary>
		Discovered,

		/// <summary>
		/// Not known to anyone yet: in the galaxy, its well pulling a cruise out like any star's, but on no chart, not on the
		/// galaxy map, not a destination to plot, survey or aim an FTL drive at, and never built into a running system,
		/// until something comes within <see cref="StarDiscovery.Range"/> of it.
		/// </summary>
		Undiscovered,
	};
	SPACEGODCORE_API const char* ToString(DiscoveryState v);

	/// <summary>
	/// How the real neighbourhood starts and is found (U.SPC-T456): every system of docs/nearest_stars_2027.csv and every void
	/// station between them is in the galaxy from the start (<see cref="Galaxy.SetUpNeighbourhood"/>), those within
	/// <see cref="KnownToLy"/> of Sol charted and known, the rest <see cref="DiscoveryState.Undiscovered"/>. One is discovered
	/// when a lookout (<see cref="Lookouts"/>: the pilot, a group crossing open space) comes within <see cref="Range"/> of it.
	/// <see cref="VoidStations"/> keeps every leg at <see cref="VoidStations.MaxLegLy"/> or less, so with the range at least that,
	/// going to each star as it is found reaches every one in the end. New ways of finding a star plug in as a lookout.
	/// </summary>
	// [SettingsSource]: the settings registry finds Range by touching StarDiscovery::Range() (no reflection in C++).
	class SPACEGODCORE_API StarDiscovery
	{
	public:
		/// <summary>How far from Sol the neighbourhood is known at the start: every real system and void station within it.</summary>
		static constexpr double KnownToLy = 21;

		/// <summary>How near a lookout must come to a star to discover it, light-years.</summary>
		static const cs::Ref<RangeSetting>& Range();

		/// <summary>One place a lookout is looking from: the C# (GalacticPosition at, string by) tuple.</summary>
		struct Sighting
		{
			GalacticPosition at;
			cs::String by;
		};

		/// <summary>
		/// A source of the places stars are being looked for from: (the galaxy, when) to each place and who is looking, for the
		/// news. A new way of finding stars (a long-range telescope, a probe) plugs in by adding one.
		/// </summary>
		// C# delegate IEnumerable<(GalacticPosition at, string by)> Lookout(Galaxy galaxy, double jd), built eagerly.
		using Lookout = cs::Func<cs::List<Sighting>, cs::Ref<Galaxy>, double>;

		/// <summary>Where stars are looked for from: the pilot, and every group crossing open space.</summary>
		static cs::List<Lookout>& Lookouts();

		/// <summary>The pilot (<see cref="Galaxy.Viewpoint"/>), wherever they are.</summary>
		static cs::List<Sighting> Pilot(const cs::Ref<Galaxy>& galaxy, double jd);

		/// <summary>Every group between the stars, where it is now.</summary>
		static cs::List<Sighting> Cruises(const cs::Ref<Galaxy>& galaxy, double jd);
	};

	/// <summary>A star found (U.SPC-T456): which, where it was found from, by whom, and when.</summary>
	class StarDiscoveredEvent final
	{
	public:
		cs::String Name;
		DVec3 PositionLy;
		cs::String By;
		double Jd = 0;
	};
}
