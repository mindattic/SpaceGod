#pragma once
// Port of Assets/UnitySpace/Core/Systems/LaneUpkeep.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Lane;
	class Faction;
	class Hyperlanes;
	class Settlement;
	class ParkingOrbit;
	class RangeSetting;
	struct LaneHop;
	enum class ShipClass : int;

	/// <summary>
	/// How a hyperlane wears and what its wear does (U.SPC-T481), every rate a setting in Settings > Simulation. A lane's
	/// condition (<see cref="Hyperlanes.Condition"/>) runs from 1, fully tended, down by <see cref="DecayPerDay"/> a day and
	/// is brought back up by <see cref="TendPerDay"/> a day for each tender working it. Between full repair and
	/// <see cref="FailAt"/> the lane rides ever slower (down to <see cref="SlowestSpeed"/> of its pace) and less reliably (down
	/// to <see cref="WorstReliability"/> of rides going through without a stall of about <see cref="StallDays"/>); at
	/// <see cref="FailAt"/> it fails and carries nothing until tenders bring it back above it.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API LaneCondition
	{
		static cs::String Share(double v);

		static cs::String Days(double v);

	public:
		/// <summary>How much of its condition a lane loses a day with nobody tending it.</summary>
		static const cs::Ref<RangeSetting>& DecayPerDay();

		/// <summary>How much condition each tender working a lane restores a day.</summary>
		static const cs::Ref<RangeSetting>& TendPerDay();

		/// <summary>The condition at or below which a lane fails and carries nothing.</summary>
		static const cs::Ref<RangeSetting>& FailAt();

		/// <summary>The share of its full pace a lane rides at just before it fails.</summary>
		static const cs::Ref<RangeSetting>& SlowestSpeed();

		/// <summary>The chance a ride goes through without a stall on a lane just before it fails.</summary>
		static const cs::Ref<RangeSetting>& WorstReliability();

		/// <summary>How long a stalled ride waits off the lane before it picks up again, days (about: a stall varies by half either way).</summary>
		static const cs::Ref<RangeSetting>& StallDays();

		/// <summary>Whether a lane in condition <paramref name="condition"/> still carries traffic: above <see cref="FailAt"/>.</summary>
		static bool Carries(double condition);

		/// <summary>How worn a lane in condition <paramref name="condition"/> is, 0 in full repair to 1 at <see cref="FailAt"/> and below.</summary>
		static double Wear(double condition);

		/// <summary>The share of its full pace a lane in condition <paramref name="condition"/> rides at.</summary>
		static double SpeedFactor(double condition);

		/// <summary>The chance a ride of a lane in condition <paramref name="condition"/> goes through without a stall.</summary>
		static double Reliability(double condition);
	};

	/// <summary>
	/// The lane tenders of one system and the upkeep of its hyperlanes (U.SPC-T481). Every lane wears
	/// (<see cref="LaneCondition"/>) and is kept up by tenders, unarmed maintenance hulls (<see cref="ShipClass.LaneTender"/>)
	/// that shuttle between its two ramps and lie over the rings at each end. Whoever keeps a lane's ramps
	/// (<see cref="World.LaneKeeper"/>, the owner of the biggest settlement at either end) raises its tenders: at the start
	/// <see cref="TendersPerLane"/> of them, then a new hull from one of its yards whenever one is lost
	/// (<see cref="OrderAt"/>). A lane with none wears out and fails, and routes and trade go round it; so tenders matter in
	/// peace and are a prime mark in war: raiders value them highly (<see cref="TenderValue"/>, see
	/// <see cref="RaidDoctrine.PreyValue"/>) and destroy rather than rob them, every gunner favours them
	/// (<see cref="TenderFocus"/>), and navies at war and raiders go out of their way to fall on them (<see cref="HuntTarget"/>).
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API LaneUpkeep final
	{
	public:
		/// <summary>How many tenders a polity keeps on each lane it keeps.</summary>
		static const cs::Ref<RangeSetting>& TendersPerLane();

		/// <summary>What a lane tender is worth to raiders, credits.</summary>
		static const cs::Ref<RangeSetting>& TenderValue();

		/// <summary>How strongly a hostile gunner favours a lane tender over the other targets in a fight.</summary>
		static const cs::Ref<RangeSetting>& TenderFocus();

		/// <summary>How far, in days of travel, a navy at war or a raider band goes to fall on a hostile lane tender.</summary>
		static const cs::Ref<RangeSetting>& HuntDays();

		/// <summary>The chance a raider band planning its next move goes after a tender in reach rather than its other plans.</summary>
		static const cs::Ref<RangeSetting>& RaiderHuntChance();

		/// <summary>How long a tender lies over the rings at each end of its lane, days (about: it varies by a quarter either way).</summary>
		static const cs::Ref<RangeSetting>& DwellDays();

	private:
		static cs::String Days(double v);

	public:
		/// <summary>The names a polity gives its tenders, numbered on as they run out.</summary>
		static const cs::List<cs::String>& Names();

	private:
		// The world that owns this (a back-reference: World makes its LaneUpkeep with `this`).
		World* world;
		cs::Random rng;

	public:
		/// <summary>The lane each tender works.</summary>
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Lane>> Tending;

	private:
		/// <summary>Who keeps each lane, as of the last look (see <see cref="OwnerOf"/>).</summary>
		cs::Dictionary<cs::Ref<Lane>, cs::Ref<Faction>> owners;

		double lastJd = cs::Double::NaN, ownersJd = cs::Double::NaN;
		bool raised = false;
		int named = 0;

	public:
		/// <summary>Raised when a lane fails (true) or is brought back into service (false).</summary>
		cs::Event<cs::Ref<Lane>, bool, double> LaneChanged;

		LaneUpkeep(World* world, int seed);

	private:
		cs::Ref<Hyperlanes> Lanes() const;

	public:
		/// <summary>Whether <paramref name="ship"/> is a lane tender.</summary>
		static bool IsTender(cs::Ref<Ship> ship);

		/// <summary>The lane <paramref name="tender"/> works, or null.</summary>
		cs::Ref<Lane> LaneOf(cs::Ref<Ship> tender);

		/// <summary>Who keeps <paramref name="lane"/> and raises its tenders: the keeper of its ramps at one end or the other
		/// (<see cref="World.LaneKeeper"/>), never a pirate clan; null when nobody holds either end.</summary>
		cs::Ref<Faction> OwnerOf(cs::Ref<Lane> lane);

	private:
		/// <summary>Whether <paramref name="ship"/> is still a working hull (not lost, a derelict or gone through a gate).</summary>
		static bool Alive(cs::Ref<Ship> ship);

	public:
		/// <summary>How many working tenders are on <paramref name="lane"/>.</summary>
		int TendersOn(cs::Ref<Lane> lane);

		/// <summary>Whether <paramref name="tender"/> is working <paramref name="lane"/> now: riding it (or stalled beside it), or
		/// in orbit at either end, lying over its rings. Not in a fight.</summary>
		bool Working(cs::Ref<Ship> tender, cs::Ref<Lane> lane);

		/// <summary>
		/// Wears every lane down for the time gone by, and lets each tender working one bring it back; logs a lane failing or
		/// coming back into service. The first call raises the system's tenders (<see cref="Raise"/>).
		/// </summary>
		void Update(double jd);

		/// <summary>
		/// Raises <see cref="TendersPerLane"/> tenders for every lane someone keeps, each stationed at one end of its lane.
		/// Returns them.
		/// </summary>
		cs::List<cs::Ref<Ship>> Raise(double jd);

	private:
		cs::Ref<Ship> Commission(cs::Ref<Faction> owner, cs::Ref<Lane> lane, int body, double jd, cs::Ref<ParkingOrbit> orbit);

	public:
		/// <summary>The lane of <paramref name="owner"/>'s most in need of a tender (fewest against <see cref="TendersPerLane"/>,
		/// then the most worn), or null when every one it keeps has its tenders.</summary>
		cs::Ref<Lane> ShortOfTenders(cs::Ref<Faction> owner);

		/// <summary>Yard hook: a lane tender when the yard's owner keeps a lane short of its tenders, else null (the yard
		/// builds what it would have). Lanes come first: everything else rides them.</summary>
		cs::Nullable<ShipClass> OrderAt(cs::Ref<Settlement> yard);

		/// <summary>A yard has finished a tender: it goes to the lane of its owner's most in need. Null if none is short now.</summary>
		cs::Ref<Ship> Launch(cs::Ref<Settlement> yard, double jd);

		/// <summary>
		/// Fleet hook: where a tender goes next. To the other end of its lane; off it (a new hull leaving its yard), to the
		/// nearer end; and with its lane failed, it stays at the end it is at, working the rings until the lane is back. A
		/// tender that has lost its lane (its lane's keeper gone) takes on another of its polity's short of one.
		/// </summary>
		int ChooseNext(cs::Ref<Ship> tender, int here, double jd);

	private:
		int Nearer(cs::Ref<Lane> lane, int here, double jd);

	public:
		/// <summary>How long a tender lies over the rings at a ramp.</summary>
		double Dwell(cs::Ref<Ship> tender);

		/// <summary>
		/// Fleet hook (<see cref="Fleet.LaneJoined"/>): a ride just joined stalls part-way with the chance its lane's wear
		/// gives (<see cref="Hyperlanes.Reliability"/>). A hull at the helm is the pilot's to ride, and never stalls.
		/// </summary>
		void OnLaneJoined(cs::Ref<Ship> ship, LaneHop hop, double jd);

		/// <summary>
		/// Where <paramref name="hunter"/>'s group goes to fall on a hostile lane tender (U.SPC-T481): the place the nearest one
		/// is at, or bound for, within <see cref="HuntDays"/>; -1 when none is in reach. A navy hunts only the tenders of a
		/// power it is at war with; a raider band, anyone's.
		/// </summary>
		int HuntTarget(cs::Ref<Ship> hunter, int here, double jd);

		/// <summary>Whether a raider band planning its next move goes hunting tenders this time (<see cref="RaiderHuntChance"/>).</summary>
		bool RaidersHunt();
	private:
		LaneUpkeep() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(LaneUpkeep); // ObjectGraph registration (Persistence/Registrations)
	};
}
