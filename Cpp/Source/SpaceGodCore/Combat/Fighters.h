#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Dogfighting.h"
#include "Combat/CarrierWings.h"
#include "Combat/Choreography.h" // Choreography::Flight (by value), US-T164
#include "Combat/Drift.h" // DriftControl (by value), US-T165
#include "Combat/DogfightManeuvers.h" // DogfightManeuvers::State (by value), US-T188

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class CombatSpec;
	class Ship;
	class Subsystem;
	class DeckVisit;

	enum class FighterPhase : int
	{
		Launch,
		Approach,
		AttackRun,
		BreakOff,
		Dogfight,

		/// <summary>Flying home to land on the carrier.</summary>
		Return,
	};
	SPACEGODCORE_API const char* ToString(FighterPhase v);

	/// <summary>A fighter's pilot and squadron: who leads, where it flies in the formation, how it flies.</summary>
	class FighterState final
	{
	public:
		/// <summary>The carrier that launched it: its squadron is this carrier's.</summary>
		cs::Ref<BattleUnit> Carrier;

		/// <summary>Where it lands instead, once its own carrier was lost or gone and it found another friendly carrier or a
		/// station to put in at (U.SPC-T473, see <see cref="StrandedWings"/>); null while it still lands on its own.</summary>
		cs::Ref<BattleUnit> Home;

		/// <summary>Where it lands: its own carrier, or the one or station it found when that was lost.</summary>
		cs::Ref<BattleUnit> LandsOn() const { return Home ? Home : Carrier; }

		/// <summary>Stranded (U.SPC-T473): its carrier was lost or gone and nowhere friendly on the field could take it, so far.</summary>
		bool Stranded = false;

		/// <summary>Seconds until a stranded craft looks again for somewhere to land.</summary>
		double HavenCheckIn = 0;

		/// <summary>What a boarding shuttle's team drilled to in its carrier's habitats (U.SPC-T486): the factor their
		/// fighting quality is multiplied by (<see cref="Interior.DrillFactor"/>).</summary>
		double Drill = 1;

		cs::Ref<BattleUnit> Lead;
		int Squadron = 0;
		int Slot = 0;
		bool Bomber = false;

		/// <summary>Its class (U.SPC-T424): what it is built for and how it flies.</summary>
		FighterRole Role = FighterRole::Interceptor;

		/// <summary>What its squadron's lead has told it to do (U.SPC-T424, see <see cref="Wing"/>).</summary>
		WingOrder Order = WingOrder::FormUp;
		FighterPhase Phase = FighterPhase::Launch;
		double PhaseTime = 0;
		/// <summary>Closest the run has come to its target; a run only counts as past once it has actually closed.</summary>
		double RunClosest = cs::Double::MaxValue;

		/// <summary>The pilot: how good a shot, how quick to react (seconds of lag), how tidy in formation (0..1).</summary>
		double Skill = 0, Reaction = 0, Discipline = 0;

		/// <summary>Where the current run starts and where the break-off heads, battle-local km.</summary>
		DVec3 RunEntry, BreakPoint;

		double Seed = 0;

		/// <summary>Its visit to a flight deck (U.SPC-T518, see <see cref="FlightDecks"/>): flying in to land, down on the deck
		/// being turned round, or launching out of the bow; null while it is out in the fight.</summary>
		cs::Ref<DeckVisit> Deck;

		/// <summary>The named manoeuvre it is flying in a dogfight on an enemy fighter (US-T188, see
		/// <see cref="DogfightManeuvers"/>). Not saved: after a load the pilot picks afresh.</summary>
		DogfightManeuvers::State Tactics;
	};

	/// <summary>The player's stick on a launched craft they have taken (U.SPC-T422, see <see cref="FighterSeat"/>).</summary>
	class FighterControl final
	{
	public:
		/// <summary>Where the player wants the nose (unit, ecliptic axes); zero holds the current heading.</summary>
		DVec3 Aim;

		/// <summary>The share of the craft's top speed it holds along its nose, 0-1.</summary>
		double Throttle = 0;

		/// <summary>The second trigger, held this step: it fires <see cref="SecondaryGroup"/>.</summary>
		bool Secondary = false;

		/// <summary>The guided weapons the second trigger fires (U.SPC-T424, see <see cref="WeaponGroups"/>).</summary>
		WeaponGroup SecondaryGroup = WeaponGroup::Missiles;

		/// <summary>Strafe and roll (each -1..1), the afterburner, and flight assist switched off (U.SPC-T424).</summary>
		double StrafeRight = 0, StrafeUp = 0, Roll = 0;
		bool Boost = false, FlightAssistOff = false;

		/// <summary>The craft's roof and spin, carried from step to step by its flight model (U.SPC-T424).</summary>
		DVec3 Up, AngularVelocity;

		/// <summary>This step's raw burn, -1..1 (US-T87): W the main drive, S the retros. With flight assist off it is the
		/// thrust, as the throttle lever has no meaning when nothing holds a speed; with assist on the lever flies.
		/// A per-step input, like the triggers, so not saved.</summary>
		double Burn = 0;

		/// <summary>The turn-and-burn key held this step (US-T87, <see cref="PilotFlight.FlipAim"/>). Not saved.</summary>
		bool Flip = false;

		/// <summary>Drift held this step (R, US-T165), and the drift it drives: coasting while held, a boost-out once let
		/// go (<see cref="Core::Drift"/>). Not saved.</summary>
		bool DriftHeld = false;
		DriftControl Drift;

		/// <summary>The seat's flight line for the readout (US-T87): with assist off, what stopping costs. Not saved.</summary>
		cs::String Note;
	};

	/// <summary>
	/// Carrier fighters. A carrier launches its squadrons (six craft each, of the classes in
	/// <see cref="FighterCraft.SquadronMix"/>: interceptors, heavy fighters, bombers) as it enters a battle. Each
	/// squadron's lead flies attack runs on an enemy capital ship (approach from the side, or for bombers from where its
	/// weak points lie open, dive at it guns firing or torpedo away, pass close, break off, swing round from a new side)
	/// or, for fighters with enemy fighters about, dogfights. Wingmen fly a loose vee on the lead: each pilot has their
	/// own wander, reaction lag and discipline, so formations breathe and stretch the way human-flown ones do, and spread
	/// out on the run; told to (<see cref="Wing"/>), they break off and fight on their own. A lead that dies is replaced
	/// by its senior wingman. Pilots share their reactor as the fight needs (<see cref="PowerDistribution"/>) and throw
	/// flares at missiles closing on them.
	/// </summary>
	class SPACEGODCORE_API FighterAI
	{
	public:
		static constexpr double FighterLengthKm = 0.016; static constexpr double BomberLengthKm = 0.024;

		static const cs::Ref<CombatSpec>& Interceptor();

		static const cs::Ref<CombatSpec>& BomberSpec();

	private:
		static constexpr int SquadronSize = CarrierWings::SquadronSize;

	public:
		/// <summary>
		/// A carrier entering battle puts every craft docked aboard that is fit to fly into space (U.SPC-T526): each launches
		/// as itself, on its own hull and with its own pilot, in the squadron its berth is in. A crippled craft stays aboard.
		/// </summary>
		static void Launch(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier);

		/// <summary>
		/// One squadron of <paramref name="role"/>'s craft out of <paramref name="carrier"/>'s hangar on the helm's order
		/// (US-T106): the first squadron, in berth order, with a craft of that class aboard fit to fly, all of its craft
		/// aboard that are, out through the flight decks. A craft that landed in this fight and was struck down into the
		/// hangar goes out again as the unit it was; one not yet out launches as itself (<see cref="LaunchCraft"/>). Returns
		/// the craft launched, lead first; none with no hangar left standing or nothing of that class aboard fit to fly.
		/// </summary>
		static cs::List<cs::Ref<BattleUnit>> LaunchOrdered(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, FighterRole role);

		/// <summary>The carrier's helm has recalled its wing (US-T106, <see cref="BattleUnit.WingRecalled"/>): <paramref name="u"/>
		/// flies home to land and stays aboard.</summary>
		static bool Recalled(const cs::Ref<BattleUnit>& u);

		/// <summary>How far round its carrier a craft told to guard it (US-T106) goes after a hostile, km.</summary>
		static constexpr double GuardKm = 8;

	private:
		/// <summary>The target the carrier's helm has called its wing onto for <paramref name="u"/> (US-T106): its locked hull
		/// (never a fighter for a bomber), or the nearest armed hostile within <see cref="GuardKm"/> of the carrier when told
		/// to guard it; null with no call, nothing to go for, or (guarding) nothing near. <paramref name="guard"/> is set
		/// when the call is to guard the carrier.</summary>
		static cs::Ref<BattleUnit> HelmCall(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, bool& guard);

		/// <summary>Docked craft <paramref name="hull"/> out of the bays as slot <paramref name="k"/> of squadron
		/// <paramref name="q"/>, under <paramref name="lead"/> (none: it leads), flying on its own hull with its own pilot
		/// (U.SPC-T526); returns the squadron's lead.</summary>
		static cs::Ref<BattleUnit> LaunchCraft(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<Ship>& hull, int q, int k, const cs::Ref<BattleUnit>& lead, DVec3 right, DVec3 up, FighterPhase phase);

	public:
		static void Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>
		/// A pilot shares its reactor as the fight needs (U.SPC-T424): shields when they are low or something is on its
		/// tail, engines to break off or run home, weapons on the run or in the dogfight, an even share otherwise.
		/// </summary>
		static void SharePower(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	private:
		/// <summary>The senior surviving wingman takes over the squadron.</summary>
		static cs::Ref<BattleUnit> NewLead(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- the lead

		/// <summary>A target still worth fighting: hostile, present, not struck and not taken.</summary>
		static bool Fair(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t);

		/// <summary>
		/// A craft flying its own runs: a squadron's lead, or a wingman told to fight on its own. With
		/// <paramref name="pinned"/> it fights that target (its lead's, or what is on its lead) instead of choosing.
		/// </summary>
		static void Lead(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt, const cs::Ref<BattleUnit>& pinned = nullptr);

		/// <summary>
		/// A recon fighter on its own (U.SPC-T485, see <see cref="ElectronicWarfare"/>): it scouts out ahead to its mark (the
		/// capital its bombers are going for, else the one most worth painting, or <paramref name="pinned"/> when its lead has
		/// told it), and holds off it at <see cref="ElectronicWarfare.StandoffKm"/> on the side it came from, edging round it
		/// and matching its drift, where it jams everything near it and paints it for the bombers. It never closes to fight.
		/// </summary>
		static void Recon(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt, const cs::Ref<BattleUnit>& pinned);

	public:
		/// <summary>
		/// What a pilot goes after (U.SPC-T484): the nearest worthwhile hostile, weighed by its craft's class. Fighters are
		/// preferred by <see cref="FighterCraft.HuntsFightersKm"/> (an interceptor's hunt), and more so when one is closing
		/// on a bomber of its own side (<see cref="FighterCraft.ScreensBombers"/>); a bomber never picks a fighter, and goes
		/// first for a capital whose shields facing it are already down, where its torpedoes do their work
		/// (<see cref="StrikeTorpedoes"/>).
		/// </summary>
		static cs::Ref<BattleUnit> ChooseTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>How much nearer than it is an enemy fighter on one of its own bombers counts for an interceptor, and a
		/// capital with its shields down toward it for a bomber, km.</summary>
		static constexpr double ScreenBonusKm = 8; static constexpr double BareHullBonusKm = 8;

		/// <summary>How near one of <paramref name="u"/>'s side's bombers an enemy fighter must be to count as on it, km.</summary>
		static constexpr double ScreenKm = 4;

	private:
		/// <summary>Whether hostile craft <paramref name="o"/> is within <see cref="ScreenKm"/> of a bomber on <paramref name="u"/>'s side.</summary>
		static bool OnABomber(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);

		/// <summary>A run starts 5-7 km off the target, from a side the squadron picks for itself.</summary>
		static DVec3 PickEntry(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t);

		static void Switch(const cs::Ref<FighterState>& f, FighterPhase p);

		static void Torpedo(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>How near the nose a torpedo's target must be (cosine): about 25 degrees.</summary>
		static constexpr double TorpedoBoresightCos = 0.9;

		/// <summary>
		/// The hardpoint a torpedo goes for (U.SPC-T424): the weak point the player has picked on the hull they have locked;
		/// for a pilot, the most telling weak point lying open from where it is; else wherever the seeker takes it.
		/// </summary>
		static cs::Ref<Subsystem> TorpedoAimPoint(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

	private:
		/// <summary>Every ready torpedo the craft carries, away at <paramref name="target"/>.</summary>
		static void LaunchTorpedoes(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

	public:
		// ---------------------------------------------------------------- flown by the player (U.SPC-T422)

		/// <summary>
		/// One step of a launched craft the player has taken (<see cref="FighterSeat"/>), flown by its own class's flight
		/// model (U.SPC-T17, U.SPC-T424; see <see cref="FighterCraft.Model"/>): the nose swings toward the stick at the
		/// craft's own rate and spin-up, flight assist holds the throttle's share of its top speed along the nose (strafing,
		/// rolling and the afterburner as the model allows, or off, drifting on its momentum), and engine power scales its
		/// thrust and top speed. The guns fire down the crosshair through the battle's usual player path; the second
		/// trigger fires the selected group: missiles at the locked target once the lock is made (the battle's own path),
		/// torpedoes at the locked hull, or the hostile hull nearest the nose. Its own drives knocked out, it drifts.
		/// </summary>
		static void FlyManned(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FighterControl>& stick, double dt);

		/// <summary>The hostile hull (never a fighter, never one that has struck or been taken) a torpedo goes at: the locked
		/// one if it is within torpedo range and the boresight, else the one nearest the nose that is; null if none.</summary>
		static cs::Ref<BattleUnit> TorpedoTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

	private:
		/// <summary>Back to the carrier's hangar and aboard; each landing puts a craft back in its hangar. A stranded craft
		/// lands on the carrier or station it found instead (U.SPC-T473): a station has no hangar of a carrier's to count it in.</summary>
		static void Land(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

	public:
		/// <summary>A craft back aboard <paramref name="home"/>: a fighter docked in its hangar as itself, damage and pilot and all
		/// (U.SPC-T526; a full hangar, or a station's, has no berth for it); a boarding shuttle docked in its shuttle berths as
		/// itself the same way (U.SPC-T486, U.SPC-T590), and any of its team still aboard back to the barracks.</summary>
		static void TakeAboard(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft);

		// ---------------------------------------------------------------- boarding shuttles (U.SPC-T486)

		/// <summary>
		/// Sends a boarding shuttle out of <paramref name="carrier"/>'s hangar toward <paramref name="target"/> with
		/// <paramref name="team"/> marines aboard, who fight as well as they drilled in the carrier's habitats. The shuttle is
		/// <paramref name="hull"/> (U.SPC-T590), else the first fit to fly docked in the carrier's shuttle berths, undocked to
		/// fly as itself on its own hull with its own pilot; a carrier with none docked sends a new one.
		/// </summary>
		static cs::Ref<BattleUnit> LaunchShuttle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<BattleUnit>& target, double team, cs::Ref<Ship> hull = nullptr);

	private:
		/// <summary>
		/// A boarding shuttle's step (U.SPC-T486): latched on, it rides with the hull it is boarding until the fight aboard is
		/// decided (the battle casts it off); otherwise, with a team aboard and a prize it may still board
		/// (<see cref="Battle.ShuttleMayBoard"/>), it flies to it and docks (<see cref="Dock"/>), finding another prize near it
		/// if that one is taken or gone; with none, its team delivered, or its carrier pulling out, it flies home to land.
		/// </summary>
		static void Shuttle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>
		/// Flies a shuttle alongside <paramref name="t"/>: to a point off its flank, slowing as it closes and holding the
		/// hull's own drift, nose turning to the hull's heading at the last. Lying there matched
		/// (<see cref="BoardingShuttles.DockKm"/>, <see cref="BoardingShuttles.MatchKmS"/>) for
		/// <see cref="BoardingShuttles.LatchSeconds"/>, it latches on and its team goes in (<see cref="Battle.ShuttleLatch"/>).
		/// </summary>
		static void Dock(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double dt);

		// ---------------------------------------------------------------- wingmen

		/// <summary>V-formation slots behind the lead (km: right, up, forward), alternating sides.</summary>
		static DVec3 SlotOffset(int slot);

		/// <summary>
		/// What a wingman's order leaves it fighting on its own (U.SPC-T424): its lead's target, whatever is on its lead,
		/// or one of its own; null when it holds the vee.
		/// </summary>
		static bool Independent(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, cs::Ref<BattleUnit>& pinned);

		/// <summary>Whether a wingman is holding the vee on its lead (rather than fighting on its own, as ordered).</summary>
		static bool FlysInFormation(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Gap to its station inside which a wingman holds the lead's heading (km).</summary>
		static constexpr double StationDeadBandKm = 0.03;

		static void Wingman(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

		// ---------------------------------------------------------------- flying

		/// <summary>Flies toward a point: the nose turns at the craft's rate and the craft goes where it points.</summary>
		static void Steer(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 goal, double speed, double dt, cs::Nullable<DVec3> match = std::nullopt, bool homing = false);

		/// <summary>US-T164: how much harder than its cruise a craft burns on a run's boost, and how fast it rolls through a
		/// flip or a rolling break, deg/s.</summary>
		static constexpr double BurnBoost = 1.3; static constexpr double RollDegS = 200;

		/// <summary>US-T164: one step of a strafing run or a dogfight trick (<see cref="Choreography"/>): coasting with the
		/// drive cut and the nose swinging onto the hull, or steering on the run's heading under its throttle and boost;
		/// banked and rolled as it flies.</summary>
		static void FlyRun(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const Choreography::Flight& run, double speed, double dt);
	};
}
