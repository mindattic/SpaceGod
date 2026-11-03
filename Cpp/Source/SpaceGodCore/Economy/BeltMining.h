#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/BeltMining.cs (US-T18).

#include "Cs/Cs.h"
#include "Astro.h"         // Astro::AuKm (TugLocalHaulKm)
#include "DVec3.h"         // Position's return (by value)
#include "Economy/Goods.h" // GoodInfo::Count (the per-good arrays' length)

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Ship;
	class MiningOperation;
	class MegastructureProject;
	class BeltMining;
	enum class Good : int;

	/// <summary>
	/// A kind of asteroid a capture tug can bring in, and what grinding and separating one yields (U.SPC-T330).
	/// The shares are the belt's rough mix by spectral class; the yields are assumed bulk fractions of each element
	/// by mass (carbonaceous rock is a tenth water and a few percent organics, a stony one a fifth free metal, a
	/// metallic one nearly all nickel-iron with platinum-group metals in the tens of parts per million). Everything
	/// not listed is slag. New kinds plug in through <see cref="Register"/>; nothing that runs the pipeline needs
	/// to change.
	/// </summary>
	class AsteroidClass final
	{
	public:
		cs::String Name;

		/// <summary>How often a tug finds one, relative to the other kinds.</summary>
		double Share = 0;

		/// <summary>Bulk density, tonnes per cubic metre (rubble piles included).</summary>
		double Density = 0;

		/// <summary>Mass fraction of each element the separators can recover.</summary>
		cs::List<std::tuple<Good, double>> Yield;

		AsteroidClass(const cs::String& name, double share, double density, cs::List<std::tuple<Good, double>> yield);

		static const cs::Ref<AsteroidClass>& Carbonaceous();

		static const cs::Ref<AsteroidClass>& Stony();

		static const cs::Ref<AsteroidClass>& Metallic();

		/// <summary>
		/// A block of comet ice (U.SPC-T483), cut from an icy body or a dead comet and pushed in by an ore tug: mostly water,
		/// with frozen ammonia and organics, light and loose. Not one of the belt rocks <see cref="Pick"/> draws: only the
		/// tugs bring ice, an operation's <see cref="MiningOperation.TugIceShare"/> of their loads.
		/// </summary>
		static const cs::Ref<AsteroidClass>& CometIce();

	private:
		static cs::List<cs::Ref<AsteroidClass>>& registered();

	public:
		static const cs::List<cs::Ref<AsteroidClass>>& All();

		/// <summary>Adds a kind of rock to what the tugs can find.</summary>
		static void Register(const cs::Ref<AsteroidClass>& kind);

		/// <summary>One kind, drawn by <see cref="Share"/>.</summary>
		static cs::Ref<AsteroidClass> Pick(cs::Random& rng);
	private:
		AsteroidClass() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(AsteroidClass); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>An asteroid a tug has brought in to a refinery, waiting for (or going through) the grinders.</summary>
	class CapturedRock final
	{
	public:
		cs::String Designation;
		cs::Ref<AsteroidClass> Class;
		double DiameterM = 0;
		double Tonnes = 0;

		/// <summary>Tonnes not yet ground.</summary>
		double Remaining = 0;

		double CapturedJd = 0;
	};

	/// <summary>
	/// A depot that holds separated elements for its owner's builds: a cache on a remote moon, out of the way and lightly
	/// guarded. Raided now and then (see <see cref="BeltMining.WarehouseRaidPerDay"/>), but far less often than the
	/// convoys that fill it are robbed on the way.
	/// </summary>
	class MaterialWarehouse final
	{
	public:
		cs::String Name;
		cs::Ref<Faction> Owner;
		int Body = 0;
		double CapacityTonnes = 50e6;
		cs::List<double> Stock = cs::NewArray<double>(GoodInfo::Count);

		int Raids = 0;
		double TonnesLostToRaids = 0;

		double StoredTonnes() const;

		cs::String ToString() const { return Name; }
	};

	enum class ConvoyState : int
	{
		EnRoute,
		Delivered,

		/// <summary>Taken by raiders on the way: the cargo is theirs.</summary>
		Stolen,

		/// <summary>Lost on the way with nobody the richer: an ore tug blown apart with its load in a fight (U.SPC-T498).</summary>
		Lost,
	};
	const char* ToString(ConvoyState v);

	/// <summary>A raw-material convoy: one load of separated elements from a refinery to a warehouse.</summary>
	class MaterialConvoy final
	{
	public:
		cs::String Name;
		cs::Ref<MiningOperation> From;
		cs::Ref<MaterialWarehouse> To;
		cs::List<double> Cargo = cs::NewArray<double>(GoodInfo::Count);
		double DepartJd = 0, ArriveJd = 0;
		ConvoyState State = ConvoyState::EnRoute;

		/// <summary>The raiders who took it, for a stolen convoy.</summary>
		cs::Ref<Faction> TakenBy;

		/// <summary>
		/// The freighter's own hull that carries this load, a real ship in its world's fleet owned by the works' faction
		/// (U.SPC-T523), or null for a pipeline run on its own, where a convoy is its cargo and timetable alone. With a hull,
		/// the load is in its hold (<see cref="Ship.CargoTonnes"/>), the convoy ends when the hull gets in, not at
		/// <see cref="ArriveJd"/> (only the estimate it sailed on), and only raiders who reach the hull and board it in a
		/// fight take it; its escorts (<see cref="MiningOperation.EscortShare"/>) are real hulls flying with it.
		/// </summary>
		cs::Ref<Ship> Freighter;

		cs::String ToString() const { return Name; }

		double Tonnes() const;
	};

	/// <summary>The heavy lanes an ore tug's haul can take between two bodies (U.SPC-T483): km along the lanes, and km pushed to and from their ramps.</summary>
	struct HeavyLanePath
	{
		double LaneKm = 0, LegKm = 0;
		cs::String Name;

		HeavyLanePath() = default;
		HeavyLanePath(double laneKm, double legKm, const cs::String& name) : LaneKm(laneKm), LegKm(legKm), Name(name) {}
	};

	/// <summary>
	/// One ore tug's haul (U.SPC-T483): latched onto a captured asteroid or a block of comet ice, pushing it from the
	/// field to a refinery (where it joins the grinders' backlog) or, for ice, straight to a depot (melted there). Slow
	/// in proportion to its load (see <see cref="BeltMining.TugAccel"/>), and a target for raiders the whole way.
	/// </summary>
	class TugHaul final
	{
	public:
		cs::String Name;
		cs::Ref<MiningOperation> Operation;
		cs::Ref<CapturedRock> Load;
		int FromBody = 0, ToBody = 0;

		/// <summary>The depot an ice block goes to; null for a load brought in to the refinery.</summary>
		cs::Ref<MaterialWarehouse> ToDepot;

		/// <summary>The heavy lanes it rides, or null when it pushes the whole way through open space.</summary>
		/// (C#'s null string is the empty string here.)
		cs::String HeavyLane;

		bool ViaHeavyLane() const { return !HeavyLane.empty(); }

		/// <summary>The tug's acceleration with its load, m/s².</summary>
		double AccelMs2 = 0;

		/// <summary>When it set out, when it arrives with its load, and when the tug is back for another (the empty run home).</summary>
		double DepartJd = 0, ArriveJd = 0, FreeJd = 0;

		ConvoyState State = ConvoyState::EnRoute;

		/// <summary>The raiders who took the tug and its load, if they did.</summary>
		cs::Ref<Faction> TakenBy;

		/// <summary>
		/// The tug's own hull, a real ship in its world's fleet that flies the haul (U.SPC-T498), or null for a pipeline run on
		/// its own, where a haul is its timetable alone. With a hull, the haul ends when the hull gets its load in, not at
		/// <see cref="ArriveJd"/> (only the estimate it set out on), and is lost when the hull is.
		/// </summary>
		cs::Ref<Ship> Tug;

		/// <summary>
		/// Whether the hull has its load latched on. One whose load lies in the field round the very body it is for flies out
		/// to it empty first and latches on there; any other latches on where it lies, at the field it sets out from.
		/// </summary>
		bool Latched = false;

		/// <summary>
		/// A capture tug's catch (U.SPC-T522, <see cref="MiningOperation.CaptureHulls"/>), not an ore tug's haul: a belt rock of
		/// <see cref="MiningOperation.MinRockM"/> to <see cref="MiningOperation.MaxRockM"/> its hull flies out into the field
		/// round the refinery for, matches, latches onto and pushes in for the grinders (see <see cref="BeltMining.Captures"/>).
		/// </summary>
		bool Capture = false;

		/// <summary>What flies it, for the news: a capture tug or an ore tug.</summary>
		cs::String TugKind() const { return Capture ? "capture tug" : "ore tug"; }

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// What threatens an ore tug's hull where it actually is (U.SPC-T498): the raiders nearest it, how thick they are about
	/// it (0 none in reach, 1 as bad as it gets), and the body it is nearest, where any seizure is reported.
	/// </summary>
	struct TugDanger
	{
		cs::Ref<Faction> Raiders;
		double Pressure = 0;
		int Body = 0;

		TugDanger() = default;
		TugDanger(cs::Ref<Faction> raiders, double pressure, int body) : Raiders(std::move(raiders)), Pressure(pressure), Body(body) {}
	};

	/// <summary>
	/// One faction's belt works: capture tugs bringing asteroids in to a refinery, grinders reducing them to dust,
	/// separators splitting the dust into elements, and convoys hauling the elements to a warehouse.
	/// </summary>
	class MiningOperation final
	{
	public:
		cs::String Name;
		cs::Ref<Faction> Owner;

		/// <summary>Where the refinery sits (a belt body such as Ceres).</summary>
		int RefineryBody = 0;

		/// <summary>Where its convoys take what it separates.</summary>
		cs::Ref<MaterialWarehouse> Destination;

		/// <summary>
		/// Capture tugs at work, and the days each takes to find, match orbits with and bring in one rock (for a works run
		/// on its own; with hulls, see <see cref="CaptureHulls"/>, each flies its own catches and takes what it takes).
		/// </summary>
		int CaptureTugs = 2;
		double CaptureDays = 24;

		/// <summary>
		/// Its capture tugs' own hulls when the works runs in a world (U.SPC-T522, see <see cref="World.RaiseCaptureTugs"/>):
		/// real ships (<see cref="ShipClass.CaptureTug"/>) in its fleet, one for each of <see cref="CaptureTugs"/>, which lie at
		/// the refinery between catches and fly every one themselves, out into the field round it and back in with a rock
		/// latched on (<see cref="Ship.Tow"/>, <see cref="Ship.Homing"/>). Null for a works run on its own, whose capture
		/// tugs are a count and a timer.
		/// </summary>
		/// (A Ref to a list: null is load-bearing here, it picks the works' mode.)
		cs::Ref<cs::List<cs::Ref<Ship>>> CaptureHulls;

		/// <summary>Capture tugs' hulls the works has lost (taken, left adrift, destroyed).</summary>
		int CaptureTugsLost = 0;

		/// <summary>Smallest and largest rock a tug can move, metres across.</summary>
		double MinRockM = 40, MaxRockM = 140;

		/// <summary>The most rocks parked waiting for the grinders; the tugs stand down beyond it.</summary>
		int MaxBacklog = 4;

		/// <summary>Tonnes a day the grinders reduce to dust, and the separators split into elements.</summary>
		double GrindTonnesPerDay = 20000, SeparateTonnesPerDay = 20000;

		/// <summary>Share of each element in the dust the separators recover; the rest goes out with the slag.</summary>
		double SeparationEfficiency = 0.9;

		/// <summary>A convoy sails with this many tonnes, or with whatever is waiting once it has waited <see cref="MaxHoldDays"/>.</summary>
		double ConvoyTonnes = 40000, MaxHoldDays = 30;

		/// <summary>
		/// Share of an escort's full protection its convoys sail with (0 for none, 1 for a full escort). In a world it is
		/// real hulls (U.SPC-T523): each of its freighters flies with <see cref="EscortsPerFreighter"/> warships of its owner.
		/// </summary>
		double EscortShare = 0;

		/// <summary>The most escorts one of a works' freighters flies with, at a full <see cref="EscortShare"/>.</summary>
		static constexpr int MaxEscortsPerFreighter = 2;

		/// <summary>Escort hulls each of its freighters flies with (U.SPC-T523): its <see cref="EscortShare"/> of a full escort.</summary>
		int EscortsPerFreighter() const;

		/// <summary>
		/// Freighters (<see cref="ShipClass.Freighter"/>) the works runs its convoys in when it runs in a world (U.SPC-T523,
		/// see <see cref="World.RaiseFreighters"/>): each a real hull in its fleet, owned by <see cref="Owner"/>, that lies at
		/// the refinery between runs and carries one hold of separated elements to <see cref="Destination"/> at a time.
		/// </summary>
		int ConvoyFreighters = 0;

		/// <summary>Its freighters' own hulls in a world (U.SPC-T523), or null for a works run on its own, whose convoys are cargo and a timetable.</summary>
		/// (A Ref to a list: null is load-bearing here.)
		cs::Ref<cs::List<cs::Ref<Ship>>> Freighters;

		int FreightersLost = 0;

		/// <summary>
		/// Ore tugs (<see cref="ShipClass.OreTug"/>) at work (U.SPC-T483): each latches onto a far bigger load than a capture
		/// tug's, a rock or a block of ice of <see cref="TugMinLoadM"/> to <see cref="TugMaxLoadM"/> across, and pushes it in,
		/// slowly. Raiders who take one keep the tug too.
		/// </summary>
		int OreTugs = 0;

		/// <summary>
		/// Its ore tugs' own hulls when the works runs in a world (U.SPC-T498, see <see cref="World.RaiseTugs"/>): real ships in
		/// its fleet, one for each of <see cref="OreTugs"/>, which lie at <see cref="TugHome"/> between hauls and fly every
		/// haul themselves. Null for a works run on its own, whose tugs are a count and a timetable.
		/// </summary>
		/// (A Ref to a list: null is load-bearing here.)
		cs::Ref<cs::List<cs::Ref<Ship>>> Tugs;

		/// <summary>Where the tugs find their loads: the field around this body (-1: around the refinery).</summary>
		int TugSourceBody = -1;

		/// <summary>The body whose field the ore tugs work, and where their hulls lie between hauls.</summary>
		int TugHome() const { return TugSourceBody >= 0 ? TugSourceBody : RefineryBody; }

		/// <summary>Share of the tugs' loads that are blocks of comet ice rather than belt rock.</summary>
		double TugIceShare = 0;

		/// <summary>Where ice blocks go, melted there into water and volatiles; null: to the refinery with the rock.</summary>
		cs::Ref<MaterialWarehouse> IceDepot;

		/// <summary>Smallest and largest load an ore tug latches onto, metres across (capped by what its route can carry).</summary>
		double TugMinLoadM = 120, TugMaxLoadM = 300;

		int TugHaulsSent = 0, TugHaulsDelivered = 0, TugsLost = 0;
		double TugTonnesDelivered = 0;

		cs::List<cs::Ref<CapturedRock>> Backlog = cs::List<cs::Ref<CapturedRock>>();

		/// <summary>Dust waiting for the separators, and the tonnes of each element it holds.</summary>
		double Dust = 0;
		cs::List<double> DustContent = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Separated elements waiting for a convoy.</summary>
		cs::List<double> Separated = cs::NewArray<double>(GoodInfo::Count);

		int RocksCaptured = 0, ConvoysSent = 0;
		double TonnesCaptured = 0, TonnesGround = 0, TonnesSeparated = 0, Slag = 0;

		/// (C#'s null array is an empty list here; the rebuild test treats both alike.)
		cs::List<double> TugReadyJd;
		double LastConvoyJd = cs::Double::NaN;

		double WaitingTonnes() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// A kind of megastructure, and its bill of separated elements (U.SPC-T330). New kinds plug in through
	/// <see cref="Register"/>; what one does once finished is its <see cref="Completed"/> handler.
	/// </summary>
	class MegastructureSpec final
	{
	public:
		cs::String Name;

		/// <summary>Tonnes of each good the whole build takes.</summary>
		cs::List<double> Bill;

		/// <summary>The fewest days the build can take, however much is to hand: it draws at most a <see cref="MinDays"/>th of its bill a day.</summary>
		double MinDays = 0;

		/// <summary>
		/// Run when a build of this kind is finished; it may start another (a Dyson swarm adds one ring of collectors at a
		/// time). Not saved: specs are the build's static tables, written by name.
		/// </summary>
		cs::Action<cs::Ref<BeltMining>, cs::Ref<MegastructureProject>, double> Completed;

		MegastructureSpec(const cs::String& name, double minDays, cs::List<std::tuple<Good, double>> bill);

		double BillTonnes() const;

		/// <summary>A yard's slips, docks and gantries: on completion the settlement at its site gains a level of shipbuilding.</summary>
		static const cs::Ref<MegastructureSpec>& Shipyard();

		/// <summary>A tether from a world's surface to beyond its stationary orbit: steel anchor and counterweight, a carbon-fibre ribbon.</summary>
		static const cs::Ref<MegastructureSpec>& SpaceElevator();

		/// <summary>
		/// One ring of a Dyson swarm's collectors. Finishing one lays down the next, so a builder who keeps the
		/// material coming slowly closes its star in.
		/// </summary>
		static const cs::Ref<MegastructureSpec>& DysonRing();

	private:
		static cs::List<cs::Ref<MegastructureSpec>>& registered();

	public:
		static const cs::List<cs::Ref<MegastructureSpec>>& All();

		static void Register(const cs::Ref<MegastructureSpec>& spec);

		static cs::Ref<MegastructureSpec> Find(const cs::String& name);
	private:
		MegastructureSpec() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(MegastructureSpec); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>One megastructure going up, built from what its builder's warehouses hold.</summary>
	class MegastructureProject final
	{
	public:
		cs::Ref<MegastructureSpec> Spec;
		cs::Ref<Faction> Builder;
		int SiteBody = 0;

		/// <summary>Its name without a stage (a Dyson swarm's rings share one).</summary>
		cs::String BaseName;
		int Stage = 1;
		cs::String Name;

		cs::List<double> Used = cs::NewArray<double>(GoodInfo::Count);
		double StartJd = 0, DoneJd = cs::Double::NaN;

		bool Done() const { return !cs::Double::IsNaN(DoneJd); }

		/// <summary>Share built: the share of its scarcest material delivered, 0..1.</summary>
		double Progress() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// The asteroid-belt mining pipeline that feeds every megastructure build (U.SPC-T330):
	///  1. Capture: each <see cref="MiningOperation"/>'s tugs bring rocks of a few tens to a hundred-odd metres in from
	///     the belt to its refinery, one every <see cref="MiningOperation.CaptureDays"/> or so each, of a kind drawn from
	///     the registered <see cref="AsteroidClass"/>es. Its ore tugs (<see cref="ShipClass.OreTug"/>, U.SPC-T483) latch
	///     onto far bigger loads, rock or blocks of <see cref="AsteroidClass.CometIce"/>, and push them in slowly (load
	///     mass governs their acceleration, <see cref="TugAccel"/>): rock to the refinery, ice to the refinery or straight
	///     to a depot. Through open space a tug takes at most <see cref="OpenSpaceMaxTonnes"/>; where a heavy lane
	///     (<see cref="Lane.Heavy"/>) joins the field and the destination it carries bigger loads, and far faster.
	///  2. Grind: the grinders reduce the waiting rocks to dust, which carries the elements its rocks held.
	///  3. Separate: the separators split the dust into elements (goods) and slag.
	///  4. Ship: a convoy carries a load of elements to the operation's <see cref="MaterialWarehouse"/>, often on a
	///     remote moon, taking days in proportion to the distance at the time it sails.
	///  5. Build: each <see cref="MegastructureProject"/> draws what its bill needs from its builder's warehouses; when
	///     every tonne is in, it is finished and its kind's <see cref="MegastructureSpec.Completed"/> handler runs.
	/// Raiders prey on the chain where it is weakest: a convoy or a tug and its load (<see cref="TugTheftPerDay"/>) on
	/// the way is robbed far more often
	/// (<see cref="ConvoyTheftPerDay"/>) than a warehouse is raided (<see cref="WarehouseRaidPerDay"/>), both in
	/// proportion to how many raiders are about (<see cref="PiratePressure"/>), less for an escorted convoy or a
	/// warehouse its owner guards.
	///
	/// The pipeline runs in whole days off its own random stream, and reaches the world it sits in only through the
	/// hooks below, so it runs the same on its own (the tests) as wired into a <see cref="World"/>. In a world every ore tug
	/// is a real hull (U.SPC-T498, <see cref="MiningOperation.Tugs"/>), and so is every capture tug (U.SPC-T522,
	/// <see cref="MiningOperation.CaptureHulls"/>, flying its catches in <see cref="Captures"/>): the pipeline sends it, its world's fleet flies it with
	/// its load latched on (<see cref="Ship.Tow"/>), the haul ends when the hull gets in (<see cref="TugArrived"/>), and the
	/// raiders who can take it are those about it where it actually is (<see cref="TugThreat"/>) or the fight that boards it.
	/// </summary>
	/// (BeltMining has a method named Ship, so inside it the type is always written ::SpaceGod::Core::Ship.)
	class BeltMining final : public std::enable_shared_from_this<BeltMining>
	{
		cs::Random rng;
		double nextDayJd = cs::Double::NaN;
		int rockSerial = 0, convoySerial = 0, haulSerial = 0;

	public:
		cs::List<cs::Ref<MiningOperation>> Operations = cs::List<cs::Ref<MiningOperation>>();
		cs::List<cs::Ref<MaterialWarehouse>> Warehouses = cs::List<cs::Ref<MaterialWarehouse>>();
		cs::List<cs::Ref<MegastructureProject>> Projects = cs::List<cs::Ref<MegastructureProject>>();

		/// <summary>Every convoy, en route or done (trimmed to the latest few hundred finished ones).</summary>
		cs::List<cs::Ref<MaterialConvoy>> Convoys = cs::List<cs::Ref<MaterialConvoy>>();

		/// <summary>Chance a day, at full <see cref="PiratePressure"/>, that raiders take a convoy on the way.</summary>
		static constexpr double ConvoyTheftPerDay = 0.005;

		/// <summary>Chance a day, at full <see cref="PiratePressure"/>, that raiders hit an unguarded warehouse: well under a convoy's.</summary>
		static constexpr double WarehouseRaidPerDay = 0.0008;

		/// <summary>Share of what a warehouse holds that a raid carries off.</summary>
		static constexpr double RaidShare = 0.35;

		/// <summary>A full escort cuts a convoy's risk by this share; a guarded warehouse's risk is cut by <see cref="GuardedRaidCut"/>.</summary>
		static constexpr double EscortCut = 0.75, GuardedRaidCut = 0.6;

		/// <summary>How fast a convoy covers the distance from refinery to warehouse, AU a day, and the shortest trip, days.</summary>
		static constexpr double ConvoyAuPerDay = 0.1, MinTransitDays = 2;

	private:
		/// <summary>Tonnes in a sphere of <paramref name="diameterM"/> metres at <paramref name="density"/> tonnes a cubic metre.</summary>
		static double RockTonnes(double diameterM, double density);

	public:
		/// <summary>A body's heliocentric position (km) on a date: for a convoy's trip time. Null: every trip is <see cref="MinTransitDays"/>.</summary>
		cs::Func<DVec3, int, double> Position;

		/// <summary>How many raiders are about, 0 (none) to 1 (as bad as it gets). Null: none.</summary>
		cs::Func<double> PiratePressure;

		/// <summary>The raiders who would take a convoy or raid a warehouse. Null: nobody in particular.</summary>
		cs::Func<cs::Ref<Faction>> Raiders;

		/// <summary>Whether a warehouse's owner guards it (gun platforms or a garrison at its body).</summary>
		cs::Func<bool, cs::Ref<MaterialWarehouse>> Guarded;

		/// <summary>Where the pipeline's news goes: (date, text, body).</summary>
		cs::Action<double, cs::String, int> Log;

		/// <summary>A shipyard was finished: its world gives the settlement at the site a yard (see <see cref="MegastructureSpec.Shipyard"/>).</summary>
		cs::Action<cs::Ref<MegastructureProject>, double> YardBuilt;

		cs::Event<cs::Ref<MaterialConvoy>> ConvoyStolen;
		cs::Event<cs::Ref<MaterialWarehouse>, double> WarehouseRaided;
		cs::Event<cs::Ref<MegastructureProject>> ProjectCompleted;

		/// <summary>Tonnes of each good raiders have taken, convoys, tugs' loads and warehouses together.</summary>
		cs::List<double> Plundered = cs::NewArray<double>(GoodInfo::Count);

		int ConvoysStolen = 0, ConvoysDelivered = 0, WarehouseRaids = 0;

		// ---------------------------------------------------------------- ore tugs (U.SPC-T483)

		/// <summary>Every ore tug's haul, under way or done (trimmed to the latest few hundred finished ones).</summary>
		cs::List<cs::Ref<TugHaul>> Hauls = cs::List<cs::Ref<TugHaul>>();

		/// <summary>Chance a day, at full <see cref="PiratePressure"/>, that raiders take a tug and its load on the way.</summary>
		static constexpr double TugTheftPerDay = 0.002;

		/// <summary>How far out in the field a tug finds its load, km, before it is pushed to the source body's neighbourhood.</summary>
		static constexpr double TugLocalHaulKm = 0.05 * Astro::AuKm;

		/// <summary>The biggest load a tug will push through open space, tonnes; a heavy lane carries up to <see cref="Hyperlanes.HeavyLaneMaxTonnes"/>.</summary>
		static constexpr double OpenSpaceMaxTonnes = 20e6;

		/// <summary>The heavy lanes between two bodies on a date, or null for none. Null: no heavy lanes anywhere.</summary>
		cs::Func<cs::Nullable<HeavyLanePath>, int, int, double> HeavyLane;

		cs::Event<cs::Ref<TugHaul>> TugTaken;

		int TugsTaken = 0, TugHaulsDelivered = 0;

		// ---------------------------------------------------------------- the tugs' own hulls (U.SPC-T498)

		/// <summary>Whether a tug's hull lying at its field is free to be sent out (not at the player's helm, not fighting). Null: any is.</summary>
		cs::Func<bool, cs::Ref<::SpaceGod::Core::Ship>> TugFree;

		/// <summary>A tug's hull has been sent out on a haul: its world flies it (to the field first, unless its load is already latched on).</summary>
		cs::Action<cs::Ref<TugHaul>, double> HaulSent;

		/// <summary>What threatens a tug's hull where it actually is on a date (see <see cref="TugDanger"/>). Null: nothing does.</summary>
		cs::Func<TugDanger, cs::Ref<TugHaul>, double> TugThreat;

		/// <summary>Raiders have seized a tug's hull where it was: its world hands the hull over to them (the load is already theirs).</summary>
		cs::Action<cs::Ref<TugHaul>, double> TugSeized;

		/// <summary>The ore tugs' hauls lost with their hulls, blown apart on the way with nobody the richer.</summary>
		int TugsDestroyed = 0;

		// ---------------------------------------------------------------- the capture tugs' own hulls (U.SPC-T522)

		/// <summary>
		/// Every capture tug's catch flown by a real hull (<see cref="MiningOperation.CaptureHulls"/>), under way or done
		/// (trimmed to the latest few hundred finished ones). Kept apart from the ore tugs' <see cref="Hauls"/>; both fly the
		/// same way, by the same hull machinery.
		/// </summary>
		/// (A Ref to a list: the C# checks it for null and re-creates it, so null stays possible.)
		cs::Ref<cs::List<cs::Ref<TugHaul>>> Captures = cs::New<cs::List<cs::Ref<TugHaul>>>();

		/// <summary>Capture tugs' catches lost with their hulls: taken by raiders, or blown apart.</summary>
		int CaptureTugsTaken = 0, CaptureTugsDestroyed = 0;

		/// <summary>The haul (or capture tug's catch) <paramref name="tug"/>'s hull is flying, or null.</summary>
		cs::Ref<TugHaul> HaulOf(const cs::Ref<::SpaceGod::Core::Ship>& tug) const;

		/// <summary>The works <paramref name="tug"/>'s hull (an ore tug's or a capture tug's) belongs to, or null.</summary>
		cs::Ref<MiningOperation> OperationOf(const cs::Ref<::SpaceGod::Core::Ship>& tug) const;

		/// <summary>
		/// Where a tug's hull goes next (its world's fleet asks): on a haul with its load latched on, to where the load is
		/// for; on a haul still to latch on, out into the field round where it lies (<see cref="Ship.Wander"/>, so the
		/// body it is at); with no haul, home to its field (a capture tug's is its refinery). -1 for a hull that is no tug of any works.
		/// </summary>
		int TugNext(const cs::Ref<::SpaceGod::Core::Ship>& tug, int here);

		/// <summary>
		/// A tug's hull has arrived somewhere (its world's fleet tells): out in the field for a load it latches on, and pushes
		/// it in (<see cref="Ship.Homing"/>); with its load at the body it is for, it lets go there and the haul is done.
		/// True if either happened.
		/// </summary>
		bool TugArrived(const cs::Ref<::SpaceGod::Core::Ship>& tug, double jd);

	private:
		/// <summary>A hull that is gone from its works: lost, left adrift, or flying someone else's colours.</summary>
		static bool Gone(const cs::Ref<MiningOperation>& op, const cs::Ref<::SpaceGod::Core::Ship>& tug);

	public:
		/// <summary>An ore tug's own mass, tonnes: its stock design, dry, with full tanks.</summary>
		static double TugMassTonnes();

		/// <summary>
		/// An ore tug's acceleration pushing <paramref name="loadTonnes"/>, m/s²: its drive's thrust (its class's unladen
		/// acceleration times its own mass) over the tug and its load together.
		/// </summary>
		static double TugAccel(double loadTonnes);

		/// <summary>Days to push <paramref name="loadTonnes"/> over <paramref name="km"/>: accelerate half way, brake the rest.</summary>
		static double PushDays(double km, double loadTonnes);

		/// <summary>A capture tug's own mass, tonnes (U.SPC-T522): its stock design, dry, with full tanks.</summary>
		static double CaptureTugMassTonnes();

		/// <summary>A capture tug's acceleration pushing <paramref name="loadTonnes"/>, m/s², as <see cref="TugAccel"/> is an ore tug's.</summary>
		static double CaptureTugAccel(double loadTonnes);

		/// <summary>
		/// Days a capture tug's catch of <paramref name="loadTonnes"/> takes (U.SPC-T522), the estimate it sets out on: out
		/// into the field empty, then the rock pushed back in, each <see cref="TugLocalHaulKm"/> flipped and burned.
		/// </summary>
		static double CaptureDaysFor(double loadTonnes);

	private:
		/// <summary>Metres across of a sphere of <paramref name="tonnes"/> at <paramref name="density"/>.</summary>
		static double RockDiameter(double tonnes, double density);

	public:
		explicit BeltMining(int seed);

		// ---------------------------------------------------------------- setting up

		cs::Ref<MaterialWarehouse> AddWarehouse(const cs::String& name, const cs::Ref<Faction>& owner, int body, double capacityTonnes = 50e6);

		cs::Ref<MiningOperation> AddOperation(const cs::String& name, const cs::Ref<Faction>& owner, int refineryBody, const cs::Ref<MaterialWarehouse>& destination);

		/// <summary>Lays down a megastructure of <paramref name="spec"/> for <paramref name="builder"/> at <paramref name="siteBody"/>.</summary>
		cs::Ref<MegastructureProject> Begin(const cs::Ref<MegastructureSpec>& spec, const cs::Ref<Faction>& builder, int siteBody, const cs::String& name, double jd, int stage = 1);

		// ---------------------------------------------------------------- running

		/// <summary>Runs the pipeline to <paramref name="jd"/>, a whole day at a time.</summary>
		void Update(double jd);

	private:
		void Day(double jd);

		void Capture(const cs::Ref<MiningOperation>& op, double jd);

		/// <summary>
		/// Sends every one of <paramref name="op"/>'s capture tug hulls (U.SPC-T522) lying idle at its refinery out for a rock
		/// of its own, once those its world has lost (left adrift, taken, destroyed) when on no catch have left the works. Each
		/// flies out empty into the field round the refinery, matches its rock there and latches on (<see cref="TugArrived"/>),
		/// and pushes it in (<see cref="Ship.Homing"/>), where it joins the grinders' backlog. The tugs stand down while the
		/// grinders' yard is full, counting what every tug of the works is bringing in.
		/// </summary>
		void SendCaptureHulls(const cs::Ref<MiningOperation>& op, double jd);

		static void Grind(const cs::Ref<MiningOperation>& op);

		static void Separate(const cs::Ref<MiningOperation>& op);

		void Ship(const cs::Ref<MiningOperation>& op, double jd);

		/// <summary>
		/// Loads one convoy of up to <paramref name="most"/> tonnes of <paramref name="op"/>'s separated elements for its
		/// warehouse, once a full load is waiting or the oldest has waited <see cref="MiningOperation.MaxHoldDays"/>; null
		/// when none sails.
		/// </summary>
		cs::Ref<MaterialConvoy> NewConvoy(const cs::Ref<MiningOperation>& op, double jd, double most);

		/// <summary>
		/// Sails every one of <paramref name="op"/>'s freighters (U.SPC-T523) lying idle at its refinery with a hold of
		/// separated elements, once those its world has lost (left adrift, taken, destroyed) when on no run have left the works.
		/// </summary>
		void SailFreighters(const cs::Ref<MiningOperation>& op, double jd);

	public:
		/// <summary>Days a convoy takes from <paramref name="from"/> to <paramref name="to"/> sailing on <paramref name="jd"/>.</summary>
		double TransitDays(int from, int to, double jd) const;

	private:
		void MoveConvoys(double jd, double pressure);

	public:
		// ---------------------------------------------------------------- the convoys' own freighters (U.SPC-T523)

		/// <summary>Whether a freighter lying at its refinery is free to sail (not at the player's helm, not fighting). Null: any is.</summary>
		cs::Func<bool, cs::Ref<::SpaceGod::Core::Ship>> FreighterFree;

		/// <summary>A freighter has sailed with a convoy's load in its hold: its world flies it to the convoy's warehouse.</summary>
		cs::Action<cs::Ref<MaterialConvoy>, double> ConvoySailed;

		/// <summary>A freighter has brought its convoy's load in to the warehouse: its world sends it home to the refinery.</summary>
		cs::Action<cs::Ref<MaterialConvoy>, double> ConvoyDelivered;

		/// <summary>Convoys lost with their freighters, blown apart on the way with nobody the richer.</summary>
		int ConvoysLost = 0;

		cs::Event<cs::Ref<MaterialConvoy>> ConvoyLost;

		/// <summary>The convoy <paramref name="freighter"/>'s hull is carrying, or null.</summary>
		cs::Ref<MaterialConvoy> ConvoyOf(const cs::Ref<::SpaceGod::Core::Ship>& freighter) const;

		/// <summary>The works <paramref name="freighter"/>'s hull belongs to, or null.</summary>
		cs::Ref<MiningOperation> WorksOf(const cs::Ref<::SpaceGod::Core::Ship>& freighter) const;

		/// <summary>
		/// Where a works' freighter goes next (its world's fleet asks): with a convoy's load in its hold, to the warehouse it
		/// is for; with none, home to its refinery. -1 for a hull that is no freighter of any works.
		/// </summary>
		int FreighterNext(const cs::Ref<::SpaceGod::Core::Ship>& freighter, int here) const;

		/// <summary>
		/// A works' freighter has arrived somewhere (its world's fleet tells): at its convoy's warehouse, its hold is emptied
		/// into the warehouse's stock and the convoy is delivered. True if it was.
		/// </summary>
		bool FreighterArrived(const cs::Ref<::SpaceGod::Core::Ship>& freighter, double jd);

	private:
		/// <summary>
		/// A day of a convoy carried by a real freighter (U.SPC-T523): there is no dice roll on the raiders about. It is taken
		/// only by raiders who reach the hull and board it in a fight (the war's boarding rules: left adrift, taken as a prize,
		/// or its hold looted before it goes to the breakers), and lost with it if it is blown apart with its load still aboard.
		/// Its arrival is the hull's own (<see cref="FreighterArrived"/>), not the timetable's.
		/// </summary>
		void MoveFreighter(const cs::Ref<MaterialConvoy>& c, double jd);

		// ---------------------------------------------------------------- ore tugs (U.SPC-T483)

		/// <summary>Km a tug pushes through open space from the field around <paramref name="from"/> to <paramref name="to"/>.</summary>
		double OpenKm(int from, int to, double jd) const;

	public:
		/// <summary>
		/// Days an ore tug takes to push <paramref name="tonnes"/> from the field around <paramref name="from"/> to
		/// <paramref name="to"/> setting out on <paramref name="jd"/>: by heavy lane where one joins them and it is the
		/// quicker (or the only) way, else through open space. <paramref name="heavy"/> is the heavy lanes taken, if any;
		/// infinite when the load is too big for every way open to it.
		/// </summary>
		double HaulDays(int from, int to, double tonnes, double jd, cs::Nullable<HeavyLanePath>& heavy) const;

		/// <summary>The biggest load a tug can take from <paramref name="from"/> to <paramref name="to"/>: a heavy lane's, where one joins them.</summary>
		double MaxLoadTonnes(int from, int to, double jd) const;

	private:
		/// <summary>Sends every idle ore tug of <paramref name="op"/> out to latch onto a load and push it in.</summary>
		void SendTugs(const cs::Ref<MiningOperation>& op, double jd);

		/// <summary>
		/// Sends every one of <paramref name="op"/>'s tug hulls (U.SPC-T498) lying idle at its field out on a haul of its own,
		/// once those its world has lost (left adrift, taken, destroyed) when on no haul have left the works.
		/// </summary>
		void SendHulls(const cs::Ref<MiningOperation>& op, double jd);

		/// <summary>Plans one haul for one of <paramref name="op"/>'s tugs setting out on <paramref name="jd"/>, or null when it stands down.</summary>
		cs::Ref<TugHaul> NewHaul(const cs::Ref<MiningOperation>& op, double jd, int& inbound);

		void MoveHauls(double jd, double pressure);

		/// <summary>
		/// A day of every capture tug's catch (U.SPC-T522): each flown by its hull, so lost with it or seized where it is, as
		/// an ore tug's haul is (<see cref="MoveHull"/>); its arrival is the hull's own (<see cref="TugArrived"/>).
		/// </summary>
		void MoveCaptures(double jd);

		static bool Finished(const cs::Ref<TugHaul>& h, double jd);

		/// <summary>
		/// A day of a haul flown by a real hull (U.SPC-T498): lost with the hull if a fight took it (adrift, captured or blown
		/// apart), else at the risk the raiders about it where it actually is put it in (<see cref="TugThreat"/>). Its
		/// arrival is the hull's own (<see cref="TugArrived"/>), not the timetable's; a hull fighting is the fight's to settle.
		/// </summary>
		void MoveHull(const cs::Ref<TugHaul>& h, double jd);

		/// <summary>A haul's hull leaves its works: the load comes off its claws, and the works counts it lost.</summary>
		void LoseHull(const cs::Ref<TugHaul>& h);

		/// <summary>
		/// Raiders take a tug and what it holds, at <paramref name="body"/>: the load is theirs, the hull lost to its works.
		/// A real hull is handed over to them where it is (<see cref="TugSeized"/>) unless a fight already did.
		/// </summary>
		void Seize(const cs::Ref<TugHaul>& h, const cs::Ref<Faction>& by, int body, double jd, bool handOver = true);

		/// <summary>A haul's load is in: ice melted into its depot's stock, rock parked at the refinery for the grinders.</summary>
		void Deliver(const cs::Ref<TugHaul>& h, double jd);

		void RaidWarehouses(double jd, double pressure);

		void Build(double jd);

		static double Clamp01(double v);

		static cs::String By(const cs::Ref<Faction>& f);
	private:
		BeltMining() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(BeltMining); // ObjectGraph registration (Persistence/Registrations)
	};
}
