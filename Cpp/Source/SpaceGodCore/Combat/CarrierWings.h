#pragma once
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class ShipCombat;
	class CombatSpec;
	class Faction;
	class Settlement;
	enum class FighterRole : int;

	/// <summary>The pilot of one of a carrier's craft (U.SPC-T526): how good a shot, how quick to react (seconds of lag), how
	/// tidy in formation (0..1), and the wander that makes their flying their own. Rolled the first time the craft launches and
	/// kept with it from then on, fight after fight, until the craft is lost.</summary>
	class SPACEGODCORE_API CraftPilot final
	{
	public:
		double Skill = 0, Reaction = 0, Discipline = 0, Seed = 0;

		/// <summary>Times this pilot has launched in this craft.</summary>
		int Sorties = 0;
	};

	/// <summary>
	/// What makes a <see cref="Ship"/> one of a carrier's craft (U.SPC-T526, see <see cref="CarrierWings"/>): its class, the
	/// berth it holds in its carrier's hangar (and so the squadron it flies in), and its pilot. Null on any other hull.
	/// </summary>
	class SPACEGODCORE_API WingCraft final
	{
	public:
		FighterRole Role{};

		/// <summary>Its berth in the hangar it is docked in: an index into that carrier's layout (<see cref="CarrierWings.Berth"/>).</summary>
		int Berth = 0;

		/// <summary>Its pilot, or null until it first launches.</summary>
		cs::Ref<CraftPilot> Pilot;

		/// <summary>The carrier this craft last launched from (U.SPC-T591): where a stray set down on a station or at a port
		/// goes back to when that carrier calls there.</summary>
		cs::Ref<Ship> Carrier;
	};

	/// <summary>
	/// A carrier's wing as real craft (U.SPC-T526): every fighter, interceptor, bomber and recon craft a carrier carries is a
	/// <see cref="Ship"/> of its own class, docked in the carrier's hangar (<see cref="ShipCombat.Hangar"/>), which goes
	/// wherever the carrier goes, through a gate, a cruise or its own jump. When a fight starts each craft still fit to fly
	/// launches as itself (<see cref="FighterAI.Launch"/>), flying on its own hull (<see cref="Ship.Combat"/>: its own damage
	/// and rounds) with its own pilot; one that lands, or is taken back aboard when the fight ends, docks again as itself,
	/// rearmed and its shields recharged by the deck crews but its damage carried until a yard mends it; one shot down is
	/// gone. A yard builds new craft only for the berths left empty (<see cref="ShipCombat.RestoreFighters"/>).
	///
	/// A hangar's berths are laid out as its squadrons were always launched: the recon flight first
	/// (<see cref="CombatSpec.ReconCraft"/>), then squadrons of <see cref="SquadronSize"/> in
	/// <see cref="FighterCraft.SquadronMix"/>'s order, so a part-filled hangar holds the recon flight and the first squadrons.
	///
	/// A carrier's boarding shuttles (U.SPC-T590, <see cref="BoardingShuttles"/>) are real craft the same way, in berths of
	/// their own beside the squadrons (<see cref="ShipCombat.ShuttleBay"/>, <see cref="CombatSpec.Shuttles"/> of them): each
	/// an assault shuttle hull with its own damage and pilot, sent out as itself with a marine team, docked again as itself
	/// when it lands, carried hurt until a yard mends it, lost as itself when shot down; a yard builds new ones only for the
	/// shuttle berths left empty (<see cref="ShipCombat.RestoreShuttles"/>).
	/// </summary>
	class SPACEGODCORE_API CarrierWings
	{
	public:
		static constexpr int SquadronSize = 6;

		/// <summary>The role berth <paramref name="berth"/> of a hangar laid out for <paramref name="spec"/> is built for, the
		/// squadron it flies in (-1 for the recon flight) and its place in that squadron.</summary>
		static FighterRole Berth(const cs::Ref<CombatSpec>& spec, int berth, int& squadron, int& slot);

		/// <summary>A new craft of <paramref name="role"/>'s class for berth <paramref name="berth"/>: whole, loaded, its pilot not yet rolled.</summary>
		static cs::Ref<Ship> NewCraft(FighterRole role, int berth, const cs::Ref<Faction>& faction = nullptr);

		/// <summary>Whether <paramref name="s"/> is one of a carrier's craft.</summary>
		static bool IsCraft(const cs::Ref<Ship>& s);

		/// <summary>The craft docked in <paramref name="c"/>'s hangar, in berth order.</summary>
		static const cs::List<cs::Ref<Ship>>& Aboard(const cs::Ref<ShipCombat>& c);

		/// <summary>How many craft of <paramref name="role"/> are docked aboard.</summary>
		static int Aboard(const cs::Ref<ShipCombat>& c, FighterRole role);

		/// <summary>Builds new craft for the first <paramref name="count"/> empty berths of <paramref name="c"/>'s hangar (as many
		/// as it has empty), each of the class its berth is laid out for.</summary>
		static void Fill(const cs::Ref<ShipCombat>& c, int count);

		/// <summary>Builds new boarding shuttles for the first <paramref name="count"/> empty shuttle berths of
		/// <paramref name="c"/> (U.SPC-T590), as many as it has empty.</summary>
		static void FillShuttles(const cs::Ref<ShipCombat>& c, int count);

		/// <summary>Takes shuttles out of the shuttle berths from the last until <paramref name="count"/> are left (U.SPC-T590).</summary>
		static void TrimShuttles(const cs::Ref<ShipCombat>& c, int count);

		/// <summary>Whether <paramref name="craft"/> is a boarding shuttle's hull (U.SPC-T590).</summary>
		static bool IsShuttle(const cs::Ref<Ship>& craft);

		/// <summary>The first shuttle docked in <paramref name="c"/>'s shuttle berths fit to fly (U.SPC-T590), or null.</summary>
		static cs::Ref<Ship> ReadyShuttle(const cs::Ref<ShipCombat>& c);

		/// <summary>Whether <paramref name="craft"/> is docked in <paramref name="c"/>, in its hangar or its shuttle berths.</summary>
		static bool DockedIn(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft);

		/// <summary>Takes craft out of the hangar from its last berths until <paramref name="count"/> are left: they are lost
		/// with the bay they were in.</summary>
		static void Trim(const cs::Ref<ShipCombat>& c, int count);

		/// <summary>
		/// Docks <paramref name="craft"/> in <paramref name="c"/>'s hangar, if it has room: in its own berth when that is free,
		/// else in the first free berth laid out for its class, else in any free one (a stray from a lost carrier, U.SPC-T473).
		/// The deck crews rearm it, top up its flares and recharge its shields; its damage stays until a yard mends it.
		/// Returns false, leaving it out, when the hangar is full. A boarding shuttle docks in the shuttle berths instead
		/// (U.SPC-T590): its own when free, else any free one.
		/// </summary>
		static bool Dock(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft);

	private:
		static bool DockShuttle(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft);

	public:
		/// <summary>Leaves <paramref name="craft"/>, a stray with no berth to dock in, held as its own hull at <paramref name="at"/>
		/// until its carrier (or another of its faction's, when that is lost) calls there (U.SPC-T591).</summary>
		static void Strand(const cs::Ref<Settlement>& at, const cs::Ref<Ship>& craft);

		/// <summary>Takes back aboard <paramref name="carrier"/> the strays held at <paramref name="at"/> that are its own, or
		/// (when their carrier is lost) of its faction's, as far as its hangar has room; returns how many rejoined.</summary>
		static int Rejoin(const cs::Ref<Settlement>& at, const cs::Ref<Ship>& carrier);

		/// <summary>Takes <paramref name="craft"/> out of the hangar it is docked in, to fly.</summary>
		static bool Undock(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft);

	private:
		static void Insert(const cs::Ref<ShipCombat>& c, const cs::Ref<Ship>& craft);

		static void Insert(cs::List<cs::Ref<Ship>>& berths, const cs::Ref<Ship>& craft);

	public:
		/// <summary>Whether a docked craft is fit to launch: alive and not crippled (its drives or most of its hull gone).</summary>
		static bool Flyable(const cs::Ref<Ship>& craft);

		/// <summary>The deck crews' turnaround: magazines and flare dispensers full, live shields charged, the afterburner charged.</summary>
		static void Rearm(const cs::Ref<ShipCombat>& c);

		/// <summary>Repairs every damaged craft docked in <paramref name="c"/>'s hangar and shuttle berths whole, as a yard
		/// does; returns how many it mended.</summary>
		static int MendAboard(const cs::Ref<ShipCombat>& c);

		/// <summary>Docked craft in <paramref name="c"/>'s hangar and shuttle berths carrying damage.</summary>
		static int Damaged(const cs::Ref<ShipCombat>& c);

	private:
		static int DamagedIn(const cs::List<cs::Ref<Ship>>& berths);

		/// <summary>Every craft docked in <paramref name="c"/>: the hangar's, then the shuttle berths'.</summary>
		static cs::List<cs::Ref<Ship>> Docked(const cs::Ref<ShipCombat>& c);

	public:
		/// <summary>The hangar for the HUD: "18 of 26 aboard", and how many of them are hurt.</summary>
		static cs::String Describe(const cs::Ref<ShipCombat>& c);

		/// <summary>The craft's pilot, rolled from <paramref name="rng"/> the first time it is asked for.</summary>
		static cs::Ref<CraftPilot> PilotOf(const cs::Ref<Ship>& craft, cs::Random& rng);
	};
}
