#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/LocalTraffic.cs (US-T19).
// Ships.h includes this header for CivilCraft, so it must not include Ships.h back: forward declarations only.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;
	class CombatSpec;
	class BodyCatalog;

	/// <summary>The kinds of ordinary local craft (U.SPC-T375): buses and taxis, and people's own small ships.</summary>
	enum class CivilCraft : int
	{
		None,

		/// <summary>An orbital bus: forty seats on a fixed line round a planet's ports.</summary>
		Bus,

		/// <summary>A four-seat taxi taking fares between a planet's ports.</summary>
		Taxi,

		/// <summary>A single-seat personal skiff.</summary>
		Skiff,

		/// <summary>A four-seat personal runabout.</summary>
		Runabout,

		/// <summary>A six-seat personal tourer.</summary>
		Tourer,
	};
	const char* ToString(CivilCraft v);

	/// <summary>
	/// Ordinary civilian traffic (U.SPC-T375), so a system reads as lived-in rather than as a combat arena: every
	/// inhabited port runs orbital buses on a line round its planet's other ports and taxis taking fares among them, and
	/// its people fly their own 1-, 4- and 6-seat craft about their neighbourhood (out to sightsee, over to a moon).
	/// All of it is local: a craft keeps to the worlds and stations of its own planet (one planetary system, so it never
	/// rides a hyperlane), as <see cref="ShipRole.Commuter"/> traffic. It is unarmed and no combatant (a fight never sweeps
	/// it in, and it steers clear of one), carries no trade, and runs on its operators' own fuel rather than the markets'.
	/// </summary>
	class LocalTraffic
	{
	public:
		/// <summary>The most local craft one system raises, the busiest ports first, so a crowded system stays cheap to fly.</summary>
		static constexpr int MaxCraftPerSystem = 60;

		/// <summary>The most stops on one bus line.</summary>
		static constexpr int LineLength = 4;

		/// <summary>The seats aboard a craft of this kind (its crew's included).</summary>
		static int Seats(CivilCraft c);

		/// <summary>Someone's own craft rather than a bus or a taxi.</summary>
		static bool IsPersonal(CivilCraft c);

		/// <summary>Buses a port of <paramref name="population"/> people runs: none for a small outpost, one, or two for a big city.</summary>
		static int BusesFor(double population);

		/// <summary>Taxis working a port of <paramref name="population"/> people.</summary>
		static int TaxisFor(double population);

		/// <summary>Personal craft its people fly about: one at ten thousand people, one more for every hundredfold, up to four.</summary>
		static int PersonalFor(double population);

		/// <summary>The design each kind of craft is built to: small, quick and unarmed, a bridge, shields, an engine and a reactor.</summary>
		static cs::Ref<CombatSpec> DesignOf(CivilCraft c);

	private:
		static const cs::Ref<CombatSpec>& BusDesign();
		static const cs::Ref<CombatSpec>& TaxiDesign();
		static const cs::Ref<CombatSpec>& SkiffDesign();
		static const cs::Ref<CombatSpec>& RunaboutDesign();
		static const cs::Ref<CombatSpec>& TourerDesign();

		/// <summary>An unarmed small-craft design: <paramref name="massKt"/> of hull pushed by <paramref name="thrustMN"/> of
		/// drive, turning as readily for its size as <paramref name="agility"/> says (1 a bus, 3 a nimble skiff).</summary>
		static cs::Ref<CombatSpec> Design(const cs::String& name, double lengthKm, double massKt, double thrustMN, double hull, double shield, double maxSpeed,
			double turnDegS, double crew, double agility);

		static const cs::List<cs::String>& PersonalNames();

		static const cs::List<CivilCraft>& PersonalKinds();

	public:
		/// <summary>The planet whose neighbourhood <paramref name="body"/> is in (itself, for a planet or a body orbiting the
		/// star directly): the moons and stations of one planet share it, and a craft flying within it needs no hyperlane.</summary>
		static int NeighbourhoodOf(const cs::Ref<BodyCatalog>& catalog, int body);

		/// <summary>
		/// Raises the system's local traffic: for each inhabited port (no haven, no hidden rock), the busiest first, its
		/// buses and taxis when its planet has another port to run to, and its people's own craft. Returns the craft raised.
		/// </summary>
		static cs::List<cs::Ref<Ship>> Raise(const cs::Ref<World>& w, double jd);

	private:
		static cs::Ref<Ship> Add(const cs::Ref<World>& w, const cs::String& name, CivilCraft kind, const cs::Ref<Faction>& owner, const cs::List<int>& ports, int at, double jd);
		SG_SERIALIZABLE(LocalTraffic); // ObjectGraph registration (Persistence/Registrations)
	};
}
