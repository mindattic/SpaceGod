#pragma once
// Port of Unity.SpaceGod Core/Systems/InterstellarTraffic.cs (US-T15, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"

namespace SpaceGod::Core
{
	class StarSystem;
	class JumpGate;
	class GateLink;
	class GateNetwork;
	class Faction;
	class Campaign;
	class SupplyConvoy;
	class Ship;
	class World;
	class Galaxy;
	class SystemReport;
	class Newswire;
	class PirateClans;
	class RangeSetting;
	class Settlement;
	class Cruise;
	struct NearbyStar;
	enum class ShipRole : int;
	enum class Good : int;

	/// <summary>Why a ship is crossing the gates.</summary>
	enum class VoyagePurpose : int
	{
		/// <summary>A freighter carrying goods to another system's markets (and a backhaul home).</summary>
		Trade,

		/// <summary>A courier carrying mail and data cores between the settled systems, for a fee.</summary>
		Courier,

		/// <summary>A yacht or survey ship off to look at an unsettled system.</summary>
		Survey,

		/// <summary>A raider group off to prey on another system's traffic.</summary>
		Raid,

		/// <summary>A navy group sent after raiders to the system they came from.</summary>
		Expedition,

		/// <summary>A war fleet on campaign against another power's systems (U.SPC-T92, see <see cref="InterstellarWars"/>).</summary>
		Campaign,

		/// <summary>A pirate clan's smuggler running loot to a black market in another system (U.SPC-T94).</summary>
		Smuggle,

		/// <summary>A raider band moving to its clan's haven in another system, or off to found one (U.SPC-T94).</summary>
		Relocate,

		/// <summary>A bounty hunter group gone after a pirate clan with a price on it in another system (U.SPC-T94).</summary>
		Hunt,

		/// <summary>A raider band or a navy picket gone out under its own drive to lie in wait on an FTL route with a gravity
		/// projector (U.SPC-T421, see <see cref="DeepSpace.LieInWait"/>).</summary>
		Ambush,

		/// <summary>A navy group sent by its polity's command to carry out an objective in another system (U.SPC-T423, see
		/// <see cref="StrategicCommand"/>): there until the objective is done or called off.</summary>
		Directed,

		/// <summary>A hull its captain sent along a plotted course (U.SPC-T516, see <see cref="TravelCourse"/>): the crew flies it
		/// gate by gate (by the hyperlanes across each system where they run, see <see cref="Fleet"/>), through every system on the
		/// way, and its voyage ends on arrival.</summary>
		Plotted,

		/// <summary>A navy's supply convoy (U.SPC-T525, see <see cref="MilitaryLogistics"/>): tankers and ammunition colliers
		/// sailing from a naval base after a war fleet on campaign, through the gates to wherever it is now; they hand over what
		/// they carry when they come up with it, and come home.</summary>
		Supply,

		/// <summary>A colony ship's hull taking its colonists to a site in another system (U.SPC-T524, see <see cref="Colonisation"/>):
		/// there until it has landed them (<see cref="Voyage.TargetBody"/>, the site), then home; home with them still aboard
		/// if the site was taken.</summary>
		Colonise,
	};
	const char* ToString(VoyagePurpose v);

	/// <summary>
	/// One ship's (or group's) trip across the gate network: where it belongs, where it is bound,
	/// the gates to take, and what it does when it gets there. Kept on the flagship
	/// (<see cref="Ship.Voyage"/>), the same object all the way (U.SPC-T453).
	/// </summary>
	class Voyage final
	{
	public:
		VoyagePurpose Purpose = static_cast<VoyagePurpose>(0);

		/// <summary>The system the ship belongs to, and comes back to.</summary>
		cs::Ref<StarSystem> Home;

		/// <summary>Where this leg ends: the far system outbound, <see cref="Home"/> on the way back.</summary>
		cs::Ref<StarSystem> Destination;

		/// <summary>The gates still to take, in order (see <see cref="GateNetwork.Route"/>), and the next one's index.</summary>
		cs::List<cs::Ref<JumpGate>> Route = cs::List<cs::Ref<JumpGate>>();
		int Leg = 0;

		/// <summary>Held at a gate: waiting to go through (it is about to, or the way is shut).</summary>
		bool AtGate = false;
		double AtGateJd = 0;

		/// <summary>When a ship held at a shut gate started waiting; NaN when not waiting.</summary>
		double WaitingSinceJd = cs::Double::NaN;

		/// <summary>Abroad, when it turns for home.</summary>
		double StayUntilJd = 0;

		/// <summary>When it left home.</summary>
		double SetOutJd = 0;

		/// <summary>The colours it set out under: a ship taken by another faction on the way is off the voyage.</summary>
		cs::Ref<::SpaceGod::Core::Faction> Faction;

		/// <summary>An expedition's quarry: the raiders it was sent after.</summary>
		cs::Ref<::SpaceGod::Core::Faction> Quarry;

		/// <summary>A war fleet's campaign (U.SPC-T92), or null.</summary>
		cs::Ref<::SpaceGod::Core::Campaign> Campaign;

		/// <summary>The supply convoy a navy's supply ship sails in (U.SPC-T525), while it is still bound for its war fleet;
		/// null once it has handed over its load, been turned back, or for any other voyage.</summary>
		cs::Ref<SupplyConvoy> Convoy;

		/// <summary>The enemy gate a war fleet holds beside, having come out of it, until it has taken it (U.SPC-T92); null when none.</summary>
		cs::Ref<JumpGate> HoldGate;

		/// <summary>A smuggler's market at the far end (a black market's body there), or a band's haven or haven site; -1 if none.</summary>
		int TargetBody = -1;

		/// <summary>The name a band off to found a haven will give it (U.SPC-T94).</summary>
		cs::String SiteName;

		/// <summary>The hull that set out (a bounty hunter's), whose place at home the ship takes up again when it is back.</summary>
		cs::Ref<Ship> Origin;

		/// <summary>A hunt voyage that ends with no hull left to come home (U.SPC-T588): the hunter's row stops being kept
		/// for an heir that will never arrive, so it can be pruned. Does nothing for any other voyage.</summary>
		void LostWithAllHands();

		/// <summary>Light-years along the outbound route (what tolls and fees scale with).</summary>
		double Ly = 0;

		/// <summary>Gate tolls paid so far, credits.</summary>
		double Tolls = 0;

		/// <summary>What it was doing at home, to take up again when it gets back (body indices of the home catalogue).</summary>
		ShipRole HomeRole = static_cast<ShipRole>(0);
		cs::List<int> HomeRoute;
		cs::List<int> HomePorts;

		/// <summary>A trade run's plan, on the news it was planned on (U.SPC-T100): the market it was bound for, what that
		/// market's board said the cargo would fetch there, and how old that board was when the ship set out.</summary>
		cs::String PlannedMarket;
		double ExpectedSale = 0, PlannedOnNewsJd = cs::Double::NaN;

		/// <summary>
		/// This leg is flown under the ship's own cruise drive through open space (U.SPC-T412, see <see cref="DeepSpace"/>),
		/// not by the gates: its <see cref="Ship.GateKm"/> is the point at the edge of its star's well where it engages.
		/// </summary>
		bool ByCruise = false;

		/// <summary>A plain star (one nobody simulates) this leg is bound for under the ship's own drive, by name; null for a system.</summary>
		cs::String StarName;

		/// <summary>A point in open space this leg is bound for under the ship's own drive (an ambush's station, U.SPC-T421),
		/// and what it is called; null for a star or a system.</summary>
		cs::Nullable<GalacticPosition> Station;
		cs::String StationName;

		/// <summary>The way out from its destination's star (its system's km frame) a jump strike group drops out at, on the
		/// flank away from the gates held against it (U.SPC-T474, <see cref="FleetDoctrine.DropDirection"/>); null to drop
		/// out on the side facing the way it came.</summary>
		cs::Nullable<DVec3> DropDir;

		cs::Ref<JumpGate> NextGate() const { return Leg < Route.Count() ? Route[Leg] : nullptr; }
		bool Homeward() const { return Destination == Home; }
	};

	/// <summary>
	/// One group's trip through a gate, kept for the galaxy map's trade flows (U.SPC-T103): which link, which way, when,
	/// what for, under whose colours, how many hulls and how many tonnes of cargo.
	/// </summary>
	class GateCrossing final
	{
	public:
		cs::Ref<GateLink> Link;

		/// <summary>The system it left by this link.</summary>
		cs::Ref<StarSystem> From;

		double Jd = 0;
		VoyagePurpose Purpose = static_cast<VoyagePurpose>(0);
		cs::Ref<::SpaceGod::Core::Faction> Faction;
		int Ships = 0;
		double Tonnes = 0;
	};

	/// <summary>A ship (and its escorts) inside a gate, between leaving one system and coming out in the next.</summary>
	class Passage final
	{
	public:
		/// <summary>Flagship first, then its escorts: the same hulls that will come out at the far end (U.SPC-T453).</summary>
		cs::List<cs::Ref<Ship>> Ships = cs::List<cs::Ref<Ship>>();
		cs::Ref<JumpGate> From, To;
		double EnterJd = 0, ExitJd = 0;

		/// <summary>Which of <see cref="Ships"/> were hired fighters in the system this passage left, so the
		/// destination's own <see cref="MerchantEscorts"/> can pick their tracking back up on arrival (U.SPC-T301).</summary>
		cs::HashSet<cs::Ref<Ship>> Hired = cs::HashSet<cs::Ref<Ship>>();

		/// <summary>The news it carries: what the system it left knew when it went in (U.SPC-T100, <see cref="Newswire.Bundle"/>).</summary>
		cs::List<cs::Ref<SystemReport>> News;

		cs::Ref<Ship> Leader() const { return Ships[0]; }

		/// <summary>How far through (0 at the near gate, 1 at the far one).</summary>
		double Progress(double jd) const { return ExitJd > EnterJd ? cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - EnterJd) / (ExitJd - EnterJd))) : 1; }
	};

	/// <summary>
	/// The traffic between the star systems. Every system runs its own world and nothing else is
	/// shared, so a ship crosses by flying to one of its system's gates (a burn to the gate's fixed
	/// point, see <see cref="Ship.GateKm"/>), going in (the hull itself leaves its fleet's list, a gone
	/// marker left in its place, as the piloted crossing does; see <see cref="GateTransit"/>, <see cref="Fleet.LetGo"/>),
	/// spending <see cref="GateNetwork.TransitDays"/> in the gate (a <see cref="Passage"/>), and coming out the
	/// same hull in the next system's fleet, at the far gate. A route of several gates crosses each system between them gate to gate.
	///
	/// Each day it sends out what the settled systems would: freighters carrying a good to another
	/// system's market when the price gap there beats the cost of the trip, gate tolls included
	/// (<see cref="MarkupPerLy"/> of the cargo's cost a light-year, paid to the gates' owners);
	/// couriers with the mail; now and then a raider group bound for another system's lanes and a
	/// yacht off to look at an unsettled star; and, after raiders come through, a navy group sent to
	/// hunt them in the system they came from. Abroad, each does its work for a few weeks (the
	/// freighter trades in the local markets, the raiders raid) and then comes home the quickest open
	/// way. A gate's owner closes it to all but its own ships for <see cref="BlockadeDays"/> after
	/// raiders come through it; traffic then goes round by other gates where it can, or waits.
	/// </summary>
	// [SettingsSource]
	class InterstellarTraffic final
	{
		/// <summary>C#'s (StarSystem system, Ship ship).</summary>
		struct AtGateEntry
		{
			cs::Ref<StarSystem> system;
			cs::Ref<Ship> ship;
		};

		/// <summary>C#'s (StarSystem from, StarSystem to, Faction quarry, double jd).</summary>
		struct ExpeditionWanted
		{
			cs::Ref<StarSystem> from;
			cs::Ref<StarSystem> to;
			cs::Ref<::SpaceGod::Core::Faction> quarry;
			double jd = 0;
		};

		cs::Ref<::SpaceGod::Core::Galaxy> galaxy;
		cs::Random rng;
		cs::List<cs::Ref<Passage>> inGates = cs::List<cs::Ref<Passage>>();
		cs::List<AtGateEntry> atGates = cs::List<AtGateEntry>();
		cs::List<ExpeditionWanted> expeditionsWanted = cs::List<ExpeditionWanted>();
		double nextDailyJd = 0;

	public:
		/// <summary>Ships inside a gate right now.</summary>
		const cs::List<cs::Ref<Passage>>& InGates() const { return inGates; }

		/// <summary>Every ship inside a gate or out between the stars under its own drive right now (in no system's fleet
		/// until it comes out at the far end).</summary>
		cs::List<cs::Ref<Ship>> InTransit() const;

	private:
		/// <summary>The voyages of the groups inside the gates and out between the stars (their flagships').</summary>
		cs::List<cs::Ref<Voyage>> VoyagesInTransit() const;

		/// <summary>Decisions about the open space between the stars (U.SPC-T412), on their own numbers so the gate traffic's
		/// stay as they were whenever no ship goes that way.</summary>
		cs::Random cruiseRng;

		/// <summary>The bands sent to lie in wait on the routes (U.SPC-T421), on their own numbers too.</summary>
		cs::Random ambushRng;

	public:
		/// <summary>Share of survey trips a survey hull with a drive of its own makes to a star off the gate network.</summary>
		static constexpr double StarSurveyShare = 0.3;

		/// <summary>The farthest (ly) a survey service sends a hull under its own drive to look at a plain star.</summary>
		static constexpr double MaxStarSurveyLy = 16;

		/// <summary>What each system knows of the others, and how old it is (U.SPC-T100).</summary>
		cs::Ref<Newswire> News;

		/// <summary>Voyages that have reached their far end so far, by purpose (homecomings not counted).</summary>
		cs::Dictionary<VoyagePurpose, int> Arrivals = cs::Dictionary<VoyagePurpose, int>();

		/// <summary>The crossings of the last <see cref="CrossingLogDays"/> days, oldest first (U.SPC-T103): what the gates' owners
		/// log of the traffic through them, and the galaxy map draws as trade flows.</summary>
		const cs::List<cs::Ref<GateCrossing>>& CrossingLog() const { return crossingLog; }

	private:
		cs::List<cs::Ref<GateCrossing>> crossingLog = cs::List<cs::Ref<GateCrossing>>();

	public:
		/// <summary>How long the crossing log is kept, days.</summary>
		static constexpr double CrossingLogDays = 180;

		/// <summary>Logs a crossing (kept in date order): every group that goes into a gate is logged as it goes in.</summary>
		void LogCrossing(const cs::Ref<GateCrossing>& c);

		/// <summary>Gate crossings so far (a group counts once), and the tolls paid on cargo, credits.</summary>
		int Crossings = 0;
		double TollsPaid = 0;

		/// <summary>Gate tolls and freight, as a share of a cargo's cost per light-year crossed.</summary>
		static constexpr double MarkupPerLy = 0.03;

		/// <summary>A freighter goes only for a margin of at least this share of what the cargo cost landed.</summary>
		static constexpr double MinMargin = 0.12;

		/// <summary>The most freighters out on one system's runs to another at once (4 unless the player tunes it, U.SPC-T431).</summary>
		static int MaxTradersPerRoute();

		/// <summary>The settings menu's Economy tuning of <see cref="MaxTradersPerRoute"/>.</summary>
		static const cs::Ref<RangeSetting>& TradersPerRoute();

		/// <summary>
		/// The most of a system's own freighters that may be out on interstellar runs at once, as a share of them: the
		/// rest stay to carry its own trade, so a thin frontier system (U.SPC-T90) is not emptied by a dozen routes.
		/// </summary>
		static constexpr double MaxAbroadShare = 0.25;

		/// <summary>What a mail run pays a courier a light-year, credits (the receiving system's government pays).</summary>
		static constexpr double MailFeePerLy = 60000;

		/// <summary>How long a gate stays closed after raiders come through it (days).</summary>
		static constexpr double BlockadeDays = 30;

		/// <summary>A ship held at a shut gate gives up after this long (days) and goes about its business where it is.</summary>
		static constexpr double MaxWaitDays = 8;

		/// <summary>Daily chance a settled system's raiders set out for another system, when none are out.</summary>
		static constexpr double RaidChancePerDay = 0.02;

		/// <summary>Daily chance a yacht or survey ship sets out for an unsettled system.</summary>
		static constexpr double SurveyChancePerDay = 0.03;

		/// <summary>A gate's traffic (<see cref="JumpGate.Traffic"/>) counts the ships through it over about this many days.</summary>
		static constexpr double GateTrafficDays = 30;

		/// <summary>The pirate clans' business across the gates: havens, smuggling, splits and the hunt for them (U.SPC-T94).</summary>
		cs::Ref<PirateClans> Clans;

		InterstellarTraffic(cs::Ref<::SpaceGod::Core::Galaxy> galaxy, double jd, int seed = 1601);

		/// <summary>Takes <paramref name="s"/>'s gate traffic on: ships there reaching a gate, and the orders of those abroad (a system opened by a new gate, U.SPC-T97, as well as the first ones).</summary>
		void Register(const cs::Ref<StarSystem>& s); // internal in C#

		cs::Ref<GateNetwork> Network() const; // internal in C#

		/// <summary>Gate arrivals, emergences and blocked ships to <paramref name="jd"/>; each day, new voyages.</summary>
		void Update(double jd);

	private:
		// ---------------------------------------------------------------- at the gate

		void OnReachedGate(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Takes a held ship through its next gate if it opens for it; else round by another, or waits.</summary>
		void TryEnter(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double when, double now);

		/// <summary>
		/// Takes the group itself (never copies, U.SPC-T453) out of the fleet here and into the gate, bound for the
		/// far end: each hull the same object all the way, <see cref="Fleet.LetGo"/> leaving a gone marker in its old
		/// slot so no other id here moves.
		/// </summary>
		void Enter(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>
		/// Takes <paramref name="leader"/> itself out of <paramref name="world"/>'s fleet (U.SPC-T453), whether bound
		/// for a gate or across open space under its own drive: its orders here are for a hull no longer here
		/// (U.SPC-T311), and those left behind (U.SPC-T310) are no longer its escorts, so they carry on under the
		/// strongest of them rather than holding station for ever on a hull that has gone -- a hunter's place on the
		/// boards goes with it. The hand-over reads the leader's own state (route, orbit, journey) to pass to any
		/// such heir, so <see cref="Fleet.LetGo"/> (which clears some of it) comes after, and the voyage is masked
		/// off first so a stay-behind heir starts fresh rather than inheriting a journey it never asked for; the
		/// caller gets the same object back, ready to cross, with its own voyage restored, everything else about it
		/// (orbit included) still exactly as it was here until <see cref="Ship.ResetForArrival"/> at the far end.
		/// </summary>
		static void DepartLeader(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Voyage>& v);

		/// <summary>Out of the far gate: into that system's fleet, then on to the next gate or to the voyage's business there.</summary>
		void Emerge(const cs::Ref<Passage>& p);

	public:
		// ---------------------------------------------------------------- under its own drive (U.SPC-T412)

		/// <summary>Whether <paramref name="ship"/> carries a cruise drive, and so can cross between the stars without a gate: by the
		/// owner's roster (U.SPC-T472, <see cref="FtlRoster"/>) a carrier, a battleship fitted with one or an explorer, never a
		/// cruiser or destroyer, whose voyages here go by the gates.</summary>
		static bool CanCruise(const cs::Ref<Ship>& ship);

		/// <summary>
		/// Sends <paramref name="ship"/> (idle in <paramref name="from"/>, with a cruise drive) to <paramref name="to"/> through
		/// open space under its own drive, gates or none: out to the edge of the well, across, and in from the far edge.
		/// Null if it cannot go.
		/// </summary>
		cs::Ref<Voyage> SendByCruise(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, VoyagePurpose purpose, double jd);

		/// <summary>
		/// Sends <paramref name="ship"/> (idle in <paramref name="from"/>, with a cruise drive) to <paramref name="star"/>, a star
		/// nobody simulates, under its own drive: it drops out at the edge of the star's well, stays a while and comes home.
		/// Null if it cannot go.
		/// </summary>
		cs::Ref<Voyage> SendToStar(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const NearbyStar& star, VoyagePurpose purpose, double jd);

		/// <summary>
		/// Sends <paramref name="ship"/> (idle in <paramref name="from"/>, with a cruise drive, and its escorts) under its own
		/// drive to <paramref name="station"/> in open space, to lie in wait there with a gravity projector (U.SPC-T421) and then
		/// come home. Null if it cannot go.
		/// </summary>
		cs::Ref<Voyage> SendToAmbush(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const GalacticPosition& station, const cs::String& where, double jd);

	private:
		cs::Ref<Voyage> SetOutByCruise(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::String& star, const cs::Ref<Ship>& ship, VoyagePurpose purpose, double jd,
			cs::Nullable<GalacticPosition> station = std::nullopt, const cs::String& stationName = cs::String());

		/// <summary>Turns a voyage under way in <paramref name="system"/> onto its own drive for <paramref name="to"/>: out to the well's edge from here.</summary>
		void CruiseFrom(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& to, double jd);

		cs::Nullable<NearbyStar> FindStar(const cs::String& name) const;

		/// <summary>
		/// At the edge of the well: the group's hulls with a drive go out into open space together, the same objects
		/// (U.SPC-T453), never copies, with the fighters and small craft that fit docked aboard them (U.SPC-T419, see
		/// <see cref="FtlCarriage"/>); escorts that cannot cross on their own and find no berth stay behind under the
		/// strongest of them.
		/// </summary>
		void EngageCruise(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, double jd);

		/// <summary>Dropped out at the edge of a system's well and in its fleet: home, or to its business there.</summary>
		void OnCruiseArrived(const cs::Ref<Cruise>& c, double jd);

		/// <summary>Dropped out at a plain star it was sent to look at: it holds there a while, then sets out for home.</summary>
		void OnReachedStar(const cs::Ref<Cruise>& c, double jd);

	public:
		/// <summary>What the first survey of a plain star pays, credits, plus as much again for every ten light-years out.</summary>
		static constexpr double StarSurveyCredits = 300000;

		/// <summary>How long a band lies in wait on a route at least, days (up to twice as long).</summary>
		static constexpr double AmbushDays = 15;

		/// <summary>Daily chance a settled system's pirates, or its navy at war, send a band out to lie in wait on an FTL route.</summary>
		static constexpr double AmbushChancePerDay = 0.01;

	private:
		/// <summary>
		/// Now and then a band goes out under its own drive to lie in wait on an FTL route with a gravity projector
		/// (U.SPC-T421): a system's pirates on the line between it and another settled system, its navy, when at war, on the
		/// approach from the enemy's. Only hulls with a drive of their own go (U.SPC-T472: a carrier or a battleship with one
		/// fitted, so a raider band of cruisers and destroyers keeps to the gates and lies in wait off them and on their lanes
		/// instead); none goes while one from here is still out.
		/// </summary>
		void DispatchAmbush(const cs::Ref<StarSystem>& from, const cs::List<cs::Ref<StarSystem>>& settled, double jd);

		/// <summary>Back where it belongs: takes up its old role, route and ports.</summary>
		void Home(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		static cs::String Describe(const cs::Ref<Voyage>& v);

		/// <summary>At the voyage's far end: to work, by what it came for.</summary>
		void Arrive(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<JumpGate>& cameIn, double jd);

	public:
		/// <summary>
		/// Shuts <paramref name="gate"/> to everyone but its owner for <paramref name="days"/> (or
		/// longer, if already shut longer).
		/// </summary>
		void Blockade(const cs::Ref<JumpGate>& gate, double jd, double days, const cs::String& why = cs::String());

		/// <summary>Opens a blockaded gate again.</summary>
		void Lift(const cs::Ref<JumpGate>& gate, double jd);

	private:
		/// <summary>The way is shut and stays shut: it gives up and goes about its business where it is.</summary>
		void GiveUp(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>
		/// Calls a voyage off where the ship is (it was taken over by the pilot or by captors, or never
		/// got away); <paramref name="restore"/> gives it back its home role if it is at home.
		/// </summary>
		void CancelVoyage(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd, bool restore = true);

		// ---------------------------------------------------------------- orders abroad

		/// <summary>World hook: where a raider group or an expedition abroad goes next (-1 for anything else).</summary>
		int Orders(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, int here, double jd);

		// ---------------------------------------------------------------- sending ships out

		void Daily(double jd);

	public:
		/// <summary>A ship that can be sent: its own master (not an escort, not the player's), idle in port, not fighting.</summary>
		static bool Idle(const cs::Ref<World>& world, const cs::Ref<Ship>& s);

		/// <summary>The body a ship in port is at (its ring's world when landed).</summary>
		static int PortOf(const cs::Ref<Ship>& s); // internal in C#

	private:
		/// <summary>The voyages under way, while <see cref="Daily"/> sends ships out (null otherwise).</summary>
		cs::Ref<cs::List<cs::Ref<Voyage>>> outCache;

	public:
		/// <summary>How many of <paramref name="home"/>'s ships with <paramref name="purpose"/> are out (on the way, abroad or coming back).</summary>
		int Out(const cs::Ref<StarSystem>& home, VoyagePurpose purpose, const cs::Ref<StarSystem>& bound = nullptr) const; // internal in C#

		/// <summary>
		/// Sends <paramref name="ship"/> (a flagship or a ship on its own, in <paramref name="from"/>)
		/// to <paramref name="to"/> on <paramref name="purpose"/>, by the quickest way open to it.
		/// Returns the voyage, or null if there is no way through or the ship is not free to go.
		/// </summary>
		cs::Ref<Voyage> Send(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, VoyagePurpose purpose, double jd);

		/// <summary>
		/// Sends a war fleet (<paramref name="ship"/>, a flagship idle in <paramref name="from"/>) on <paramref name="campaign"/>
		/// to <paramref name="to"/> (U.SPC-T92), by the quickest way it can fight through: gates held by the enemy count,
		/// to be forced. Null if there is no way at all or the ship is not free to go.
		/// </summary>
		cs::Ref<Voyage> SendCampaign(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Campaign>& campaign, double jd);

		/// <summary>
		/// Sends <paramref name="ship"/> (the player's hull, handed to its crew, or one the god view follows, standing in
		/// <paramref name="from"/>) to <paramref name="to"/> along the quickest open course (U.SPC-T516): gate by gate, each system
		/// on the way crossed to its next gate, never a jump to the far system. Null if there is no way or it is not free to go.
		/// </summary>
		cs::Ref<Voyage> SendPlotted(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd);

		/// <summary>
		/// Sends a navy's supply convoy (U.SPC-T525, <paramref name="ship"/> the hull leading it, in port at its naval base in
		/// <paramref name="from"/>, the rest of its hulls in formation on it) after a war fleet in <paramref name="to"/> by
		/// <paramref name="route"/> (gates that will open for it; empty when the fleet is in the base's own system, when it
		/// just flies to it). The hulls carry <paramref name="convoy"/> on the voyage until they hand over its load.
		/// </summary>
		cs::Ref<Voyage> SendSupply(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::List<cs::Ref<JumpGate>>& route, const cs::Ref<SupplyConvoy>& convoy, double jd);

		/// <summary>
		/// Stops a hull on a voyage where it is (U.SPC-T525: supply ships that have found their war fleet in the system they
		/// are in): whatever gate it was bound for, it now stays in <paramref name="system"/>, which its voyage now ends in.
		/// </summary>
		void Hold(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>
		/// Sends a jump strike group (<paramref name="ship"/>, its carrier or FTL capital flagship idle in <paramref name="from"/>)
		/// on <paramref name="campaign"/> to <paramref name="to"/> under its own drives (U.SPC-T474, see <see cref="FleetDoctrine"/>):
		/// out to the edge of the well, across open space with its escorts clamped to the carrier, and in on the flank of
		/// <paramref name="to"/> away from the gates held against it. Null if it cannot go.
		/// </summary>
		cs::Ref<Voyage> SendStrike(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Campaign>& campaign, double jd);

		/// <summary>
		/// Sends a navy group (<paramref name="ship"/>, a flagship idle in <paramref name="from"/>) to <paramref name="to"/> on its
		/// polity's orders (U.SPC-T423, see <see cref="StrategicCommand"/>), by the quickest way open to it, or under its own
		/// drive where no gate goes. It stays there while its objective there stands. Null if it cannot go.
		/// </summary>
		cs::Ref<Voyage> SendDirected(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd);

		/// <summary>
		/// Points a war fleet on campaign (<paramref name="ship"/>, its flagship in <paramref name="system"/>) at another
		/// system (U.SPC-T92): on to its next goal, or (<paramref name="to"/> its home) back where it set out from, the way it
		/// can fight through. At home already, it takes up its old duties at once. False if there is no way (it stays).
		/// <paramref name="force"/> false keeps it to gates that will open for it (a navy's supply ships, U.SPC-T525, which
		/// cannot fight their way through).
		/// </summary>
		bool Redirect(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& to, double jd, bool force = true);

		/// <summary>
		/// A war fleet holding beside a gate it has taken goes on (U.SPC-T92): to its next gate, or, at the end of its way, to
		/// its work in this system.
		/// </summary>
		void Release(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Starts <paramref name="ship"/> on a voyage from <paramref name="from"/> to <paramref name="to"/> by <paramref name="route"/>.</summary>
		cs::Ref<Voyage> SetOut(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, VoyagePurpose purpose, const cs::List<cs::Ref<JumpGate>>& route, double jd); // internal in C#

	private:
		/// <summary>Sends an idle freighter with the most profitable cargo for <paramref name="to"/>'s markets, if one pays.</summary>
		void DispatchTrade(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd);

		static cs::String Tonnes(double t);

	public:
		/// <summary>
		/// The best good to carry from the ship's port to <paramref name="to"/>'s markets: bought here,
		/// marked up by the tolls for the route's light-years (<see cref="MarkupPerLy"/>) and the days
		/// on the way, against what the dearest market there would pay. True if one clears <see cref="MinMargin"/>.
		/// </summary>
		static bool BestCargo(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route, double jd,
			cs::Ref<Settlement>& source, Good& good, double& tonnes, double& profit);

		/// <summary>
		/// As <see cref="BestCargo(StarSystem, StarSystem, Ship, int, List{JumpGate}, double, out Settlement, out Good, out double, out double)"/>,
		/// but against <paramref name="markets"/>: the far system's boards as the news here has them (U.SPC-T100), not as
		/// they are. <paramref name="market"/> is the board the run is planned on and <paramref name="sale"/> what it says
		/// the cargo will fetch there, after port tax.
		/// </summary>
		static bool BestCargo(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route, double jd, const cs::List<cs::Ref<Settlement>>& markets,
			cs::Ref<Settlement>& source, Good& good, double& tonnes, double& profit, cs::Ref<Settlement>& market, double& sale);

	private:
		/// <summary>A freighter's running costs a day (as at home).</summary>
		static constexpr double RunningCostPerDay = 1500;

	public:
		/// <summary>Rough door-to-door days: out to the gate, through the gates, and in from the far gate to a port.</summary>
		static double VoyageDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route);

	private:
		/// <summary>A system's own working freighters, at home or away.</summary>
		int Freighters(const cs::Ref<StarSystem>& home) const;

		/// <summary>A courier with the mail, every so often, when none is out on the run.</summary>
		void DispatchCourier(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd);

		/// <summary>Now and then a raider group slips out for another settled system's lanes.</summary>
		void DispatchRaid(const cs::Ref<StarSystem>& from, const cs::List<cs::Ref<StarSystem>>& settled, double jd);

		/// <summary>Now and then a yacht or survey ship sets off to look at an unsettled star, often several gates out.</summary>
		void DispatchSurvey(const cs::Ref<StarSystem>& from, double jd);

	public:
		/// <summary>A free pilot's hull that goes surveying: a fast courier through the gates, or a deep-space explorer
		/// (U.SPC-T419), which can also go off the gate network under its own FTL drive.</summary>
		static bool IsSurveyHull(const cs::Ref<Ship>& ship);

	private:
		/// <summary>A survey hull with a cruise drive sent to a plain star within <see cref="MaxStarSurveyLy"/>; false if none could go.</summary>
		bool DispatchStarSurvey(const cs::Ref<StarSystem>& from, double jd);

	public:
		/// <summary>A navy group sent after raiders to the system they came from; false if none could go yet.</summary>
		bool DispatchExpedition(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Faction>& quarry, double jd); // internal in C#

	private:
		/// <summary>Ships abroad whose stay is over turn for home, a freighter with a backhaul if one pays.</summary>
		void SendHomeWhenDone(const cs::Ref<StarSystem>& system, double jd);
	};
}
