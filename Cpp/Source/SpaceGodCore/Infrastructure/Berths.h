#pragma once
// US-T435: stations big enough for dozens of ships at once, and the berths that hold them.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class OrbitalStructure;
	enum class StructureKind : int;

	/// <summary>
	/// US-T435: every number that sizes a station and lays out its berths, in one place (docs/ScaleProportions.md,
	/// "Stations and their berths").
	///
	/// A station a ship calls at (a port's habitat and transfer station, its shipyard and works, a depot) is several km long so
	/// that dozens of hulls fit alongside it at once. Ships berth nose-in along four piers that run the station's length, one
	/// on each flank (a spinning habitat's or gateway's run along a despun docking spine through its hub, out past its wheel
	/// on both sides), a berth every <see cref="PitchKm"/>: wide enough for a freighter's (0.6 km) beam with room between, so
	/// the capacity grows with the station's length. The real spacecraft (the ISS, the telescopes, the probes) and the signal
	/// relays take no ships.
	/// </summary>
	struct BerthTuning
	{
		// ---- the stations' real lengths (km), drawn at the ship scale like every hull (ScaleModel::ShipLength)

		/// <summary>A port's spinning habitat: its wheel's diameter, its docking spine as long again through the hub.</summary>
		static constexpr double HabitatKm = 9.0;
		/// <summary>A transfer station (gateway): the busiest stop, a little bigger than a habitat.</summary>
		static constexpr double GatewayKm = 10.0;
		/// <summary>An orbital shipyard: its frame round the slip, the fitting-out quays along both sides.</summary>
		static constexpr double ShipyardKm = 8.0;
		/// <summary>A factory station (foundry, fab, works, arsenal, labs): its loading quays.</summary>
		static constexpr double FactoryKm = 4.5;
		/// <summary>A propellant depot: its tank farm with fuelling berths down both sides.</summary>
		static constexpr double DepotKm = 6.0;
		/// <summary>A signal relay: an antenna farm, no berths.</summary>
		static constexpr double RelayKm = 0.6;

		// ---- the solid core inside the piers

		// What a passing hull steers round and what a hull flown into it hits (Fleet::StructuresOf, SectionColliders::
		// StationsNear) is the station's core, the pressurised hull and works the piers are built out from, not the open
		// docking space round it, where traffic control clears ships in and out: the sizes the stations were before they grew
		// their piers, so the traffic flies past them as it always has (Bench 5/5 identical; giving the obstacles the full
		// length instead moves the economy-year scenario's digest).
		static constexpr double HabitatCoreKm = 2.2;
		static constexpr double GatewayCoreKm = 1.8;
		static constexpr double ShipyardCoreKm = 3.5;
		static constexpr double FactoryCoreKm = 1.6;
		static constexpr double DepotCoreKm = 1.4;

		// ---- the berths

		/// <summary>One berth every this far along a pier (km).</summary>
		static constexpr double PitchKm = 0.75;
		/// <summary>Piers per station: one along each of its four flanks (around a habitat's spine, four ways).</summary>
		static constexpr int Piers = 4;
		/// <summary>The share of a station's long axis its piers line with berths.</summary>
		static constexpr double PierShare = 0.85;
		/// <summary>A spinning station's wheel, as a share of its length, that its spine's berths start clear of
		/// (half its thickness each side of the hub, plus room).</summary>
		static constexpr double WheelShare = 0.15;
		/// <summary>The docking arm from the plating out to a berthed hull's nose (km).</summary>
		static constexpr double ArmKm = 0.12;
		/// <summary>Ships waiting for a berth hold this far apart off the station (km), the first this far beyond its plating.</summary>
		static constexpr double HoldSpacingKm = 1.2;
	};

	/// <summary>US-T435: what a station's size gives it in berths, and the length each kind is built to.</summary>
	class SPACEGODCORE_API Berths final
	{
	public:
		/// <summary>The length (km) a station of <paramref name="kind"/> is built to (0 for the real spacecraft, which keep theirs).</summary>
		static double LengthKmFor(StructureKind kind);

		/// <summary>The solid core's length (km) of a station of <paramref name="kind"/> (see BerthTuning's cores); 0 for a kind
		/// that is all core.</summary>
		static double CoreKmFor(StructureKind kind);

		/// <summary>The length (km) a hull meets as solid: its core, never more than its whole length (a real spacecraft's, a
		/// relay's and a station saved before US-T435 are their whole length).</summary>
		static double SolidKm(const OrbitalStructure& s);

		/// <summary>A spinning station (habitat, gateway), whose berths run along a despun spine through its hub.</summary>
		static bool Spins(StructureKind kind);

		/// <summary>Whether ships berth at a station of this kind at all (not the real spacecraft or the relays).</summary>
		static bool TakesShips(StructureKind kind);

		/// <summary>Berths along each pier of a station <paramref name="lengthKm"/> long (for a spinning one, both sides of its
		/// wheel together, an even number).</summary>
		static int PerPier(StructureKind kind, double lengthKm);

		/// <summary>How many hulls can berth at once: <see cref="BerthTuning::Piers"/> times <see cref="PerPier"/>.</summary>
		static int Capacity(StructureKind kind, double lengthKm);
		static int Capacity(const OrbitalStructure& s);
	};
}
