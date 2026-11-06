#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/War.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;
	class Settlement;
	class Battle;
	class BattleUnit;
	class Lane;
	class JumpGate;
	class Pursuer;
	class Crews;
	class Wrecks;
	class EscapePod;
	class ParkingOrbit;
	class PicketWarning;
	class Fleet;
	class Economy;
	class Diplomacy;
	class BodyCatalog;
	class Hyperlanes;
	class BuildPlan;
	class SquadOrders;
	enum class Good : int;
	enum class ShipClass : int;
	enum class SurrenderTerms : int;

	enum class OrderKind : int
	{
		Patrol,

		/// <summary>Lurk beside a hyperlane and pull passing ships out of it (pirates).</summary>
		Ambush,
		Attack,
		Defend,
		Siege,
		Repair,
		Raid,
		Hunt,

		/// <summary>Storm a pirate haven: break its garrison, then board and take it or destroy it.</summary>
		Assault,

		/// <summary>Lie in wait off a jump gate, where traffic bunches up going in and coming out (pirates; U.SPC-T94).</summary>
		GateWatch,

		/// <summary>Hold one of the navy's own jump gates against raiders lying in wait off it: take station on its approach
		/// and fight them there (U.SPC-T374).</summary>
		HoldGate,

		/// <summary>Lie over an enemy port so no ship may dock there, on its polity's orders (U.SPC-T423, see <see cref="StrategicCommand"/>).</summary>
		Blockade,

		/// <summary>Shadow a ship its polity has given it to guard (U.SPC-T423).</summary>
		Escort,

		/// <summary>Visit every world of a system in turn, on its polity's orders (U.SPC-T423).</summary>
		Explore,
	};
	SPACEGODCORE_API const char* ToString(OrderKind v);

	/// <summary>
	/// An armed interdictor a lane rider's scanners have picked up ahead (see <see cref="War.ThreatAhead"/>): who, how many
	/// seconds of the ride until its pulse can reach the rider, how far ahead along the lane it is, and whether the rider
	/// has made out its hull well enough to be braced for the pulse (<see cref="War.Forewarned"/>).
	/// </summary>
	struct LaneThreat
	{
		cs::Ref<Ship> Lurker;
		double SecondsToPulse = 0, DistanceKm = 0;
		bool Braced = false;

		LaneThreat() = default;
		LaneThreat(cs::Ref<Ship> lurker, double secondsToPulse, double distanceKm, bool braced)
			: Lurker(std::move(lurker)), SecondsToPulse(secondsToPulse), DistanceKm(distanceKm), Braced(braced) {}
	};

	/// <summary>What a fleet group (a flagship and its escorts) is doing, and where.</summary>
	class GroupOrder final
	{
	public:
		OrderKind Kind = OrderKind::Patrol;
		int Body = -1;
		double SinceJd = 0;
	};

	/// <summary>
	/// The wars. Fleet groups take orders (patrol home, attack a weak enemy port, defend one under
	/// attack, besiege, refit at a yard), pirates raid busy ports with thin defences, and a battle
	/// starts whenever hostile forces meet at a body: groups, merchants or a settlement's defence
	/// platforms. Outcomes feed back: losses are permanent, yards rebuild from the markets (alloys,
	/// electronics, munitions), a port held without defenders for three days changes hands, and
	/// traders steer clear of where the fighting has been.
	/// </summary>
	class SPACEGODCORE_API War final
	{
	public: // (the save registration names it: Persistence/Registrations/Registrations.h)
		// C#'s value tuple (Lane lane, double at): where a lane rider was last seen, as a fraction of the lane from its A end.
		struct LanePass
		{
			cs::Ref<Lane> lane;
			double at = 0;
		};
	private:

		cs::Ref<World> world;

	public:
		cs::List<cs::Ref<Battle>> Battles = cs::List<cs::Ref<Battle>>();
		cs::List<cs::Ref<Battle>> Ended = cs::List<cs::Ref<Battle>>();

		/// <summary>Lasting wrecks left where ships have died, cleared away once each has run its lifetime.</summary>
		cs::Ref<SpaceGod::Core::Wrecks> Wrecks;

		/// <summary>Escape pods drifting where hulls died, the hostages and prisoners taken from them, and every faction's manpower.</summary>
		cs::Ref<SpaceGod::Core::Crews> Crews;
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<GroupOrder>> Orders = cs::Dictionary<cs::Ref<Ship>, cs::Ref<GroupOrder>>();

		/// <summary>
		/// The order each group held before its current one (U.SPC-T371): the one that brought it where it lies now. A group
		/// is handed its next order as it arrives (to leave on when its stay is up), so while it lies at its objective its
		/// current order already points on; it is this one that still commands it to fight where it is (see <see cref="Commanded"/>).
		/// </summary>
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<GroupOrder>> PriorOrders = cs::Dictionary<cs::Ref<Ship>, cs::Ref<GroupOrder>>();

	private:
		cs::Dictionary<int, double> danger = cs::Dictionary<int, double>();
		cs::Dictionary<cs::Ref<Settlement>, double> siegeDays = cs::Dictionary<cs::Ref<Settlement>, double>();

		/// <summary>Ports with a hostile besieging group parked over them and none of the owner's warships there.</summary>
		cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>> besiegers = cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>>();
		cs::Dictionary<cs::Ref<Faction>, int> losses = cs::Dictionary<cs::Ref<Faction>, int>();
		cs::Dictionary<int, int> battleCount = cs::Dictionary<int, int>();
		cs::Dictionary<cs::Ref<Ship>, double> retreating = cs::Dictionary<cs::Ref<Ship>, double>();
		cs::Dictionary<cs::Ref<Ship>, LanePass> laneSeen = cs::Dictionary<cs::Ref<Ship>, LanePass>();
		cs::Dictionary<cs::Ref<Ship>, double> pulseReady = cs::Dictionary<cs::Ref<Ship>, double>();
		cs::Dictionary<cs::Ref<BattleUnit>, cs::Ref<Faction>> captures = cs::Dictionary<cs::Ref<BattleUnit>, cs::Ref<Faction>>();
		cs::Dictionary<cs::Ref<Settlement>, double> assaultDays = cs::Dictionary<cs::Ref<Settlement>, double>();

	public:
		/// <summary>An interdiction pulse pulls out every ship within this distance along the lane.</summary>
		static constexpr double PulseRadiusKm = 600000;

		/// <summary>
		/// What a raider's pulse emitter gives off as it sits charged beside a lane, in signature units: far brighter than the
		/// dark hull it is fitted to, so a rider's scanners pick up the field ahead (as a blip) long before the hull itself,
		/// at <see cref="SensorSuite.ReachKm"/> of this (U.SPC-T42).
		/// </summary>
		static constexpr double EmitterSignature = 400;

		/// <summary>The chance a hull surging through the lane (<see cref="Fleet.SurgeRide"/>) is past and gone before the pulse's shell closes on it.</summary>
		double SurgeSlipChance = 0.6;

	private:
		cs::List<cs::String> pilotNotices = cs::List<cs::String>();

	public:
		/// <summary>What the war has to tell the player about the lane they ride (a pulse ridden out, slipped, or that caught them), taken once.</summary>
		cs::List<cs::String> TakePilotNotices();

	private:
		cs::Dictionary<int, double> lastBattleEndJd = cs::Dictionary<int, double>();
		cs::Random rng;

		double lastJd = 0, nextDailyJd = 0;
		double warStartJd = 0, ceasefireUntilJd = -1;
		int nextBattleId = 0, builtWarships = 0;

	public:
		/// <summary>Raised when a battle begins (the app may want to slow time or point the camera).</summary>
		cs::Event<cs::Ref<Battle>> BattleStarted;

		/// <summary>Raised when a pirate clan takes loot or a prize here: the clan, what it was worth (credits) and the date
		/// (U.SPC-T94; the clans' victims put a price on them, see <see cref="PirateClans"/>).</summary>
		cs::Event<cs::Ref<Faction>, double, double> Plundered;

		/// <summary>
		/// Raised when a pirate clan destroys a merchant or civilian hull here outright rather than robbing it (U.SPC-T374):
		/// the clan, the heat it draws (credits the robbed states add to the price on its head, see
		/// <see cref="RaidDoctrine.DestroyHeat"/>) and the date. A wreck draws far more than a clean robbery.
		/// </summary>
		cs::Event<cs::Ref<Faction>, double, double> Heat;

		/// <summary>The heat each clan has drawn here by destroying hulls it could have robbed (credits, all time; U.SPC-T374).</summary>
		cs::Dictionary<cs::Ref<Faction>, double> HeatDrawn = cs::Dictionary<cs::Ref<Faction>, double>();

		/// <summary>The chance a raider group, deciding where to go next, lies in wait off its system's busiest gate instead.</summary>
		static constexpr double GateWatchChance = 0.2;

		/// <summary>A gate must have seen at least this much traffic lately (<see cref="JumpGate.Traffic"/>) to be worth watching.</summary>
		static constexpr double MinGateTraffic = 2;

		/// <summary>Raiders lying off a gate fall on ships that come within this of them (km): everyone going in or coming out.</summary>
		static constexpr double GateAmbushRangeKm = 3000;

		/// <summary>A fight off a system's root (out at a jump gate, not over a world) takes in only those this close to it (km).</summary>
		static constexpr double GateFightRangeKm = 20000;

		/// <summary>Raised as a battle is resolved, before its survivors go back to their schedules: every unit's
		/// fate (dead, fled, taken, ransomed) is final. The law judges what was done in it (see <see cref="Law.OnBattleEnded"/>).</summary>
		cs::Event<cs::Ref<Battle>, double> BattleEnded;

		War(cs::Ref<World> world, double jd, int seed);

	private:
		static cs::String Cap(const cs::String& s);

		/// <summary>The body a pod cluster's log line is filed under: where it drifts, or none on a lane.</summary>
		static int PodBody(const cs::Ref<EscapePod>& pod);

	public:
		/// <summary>How often ships near drifting pods get a chance to find them, days.</summary>
		static constexpr double PodSweepDays = 0.02;

	private:
		double nextPodSweepJd = 0;

		cs::Ref<SpaceGod::Core::Fleet> Fleet() const;
		cs::Ref<SpaceGod::Core::Economy> Economy() const;
		cs::Ref<SpaceGod::Core::Diplomacy> Diplomacy() const;
		cs::Ref<SpaceGod::Core::BodyCatalog> Catalog() const;

	public:
		double Danger(int body) const;

		int Losses(const cs::Ref<Faction>& f) const;

		/// <summary>The battle being fought at <paramref name="body"/>, or null. An ambush out on a hyperlane
		/// is filed under the system root but fought out on the lane, so it is at no body.</summary>
		cs::Ref<Battle> BattleAt(int body) const;

		cs::Ref<GroupOrder> OrderOf(const cs::Ref<Ship>& leader) const;

		// ---------------------------------------------------------------- per frame

		void Update(double jd);

		// ---------------------------------------------------------------- lane ambushes

	private:
		/// <summary>
		/// Each lurking raider watches its lane. A ship that passes its station since the last look
		/// (so a fast pass between frames still counts) sets off the pulse: every ship near that
		/// point of the lane is thrown out of it, its drives and shields knocked out for a minute or
		/// two, and the raiders close in.
		/// </summary>
		void Interdictions(double jd);

		/// <summary>
		/// Whether a lurker sets its pulse off on <paramref name="victims"/> passing its station (U.SPC-T374): it sizes them
		/// up first (<see cref="BestMark"/>), and lets a merchant or civilian hull ride on when none of them is worth the guns
		/// riding with them or the heat (a warship passing alone it pulls out as before, if the guns about are few enough).
		/// </summary>
		bool WorthPulsing(const cs::Ref<Ship>& lurker, const cs::List<cs::Ref<Ship>>& victims);

	public:
		/// <summary>How much more strength a navy's lane picket will pull out of a lane than its own group has (the pulse
		/// leaves them stunned and shieldless for the first minute or two).</summary>
		static constexpr double PicketNerve = 1.5;

	private:
		/// <summary>
		/// A group lying in wait beside its lane, pulse at the ready: a raider band, or a navy's lane picket (U.SPC-T479),
		/// that still carries a working field projector (see <see cref="LaneInterdictors"/>; a band whose projector was lost or stripped
		/// lies there with nothing to pulse). Not one in a fight, at the helm, or off running the player down (U.SPC-T273),
		/// which has left its station however its schedule still reads.
		/// </summary>
		bool Lurking(const cs::Ref<Ship>& lurker);

	public:
		/// <summary>
		/// Whether <paramref name="lurker"/>'s pulse is meant for <paramref name="rider"/> (U.SPC-T479): a raider's for any
		/// ship but another raider's, a navy picket's for raiders, its enemies at war, and the player's hull when that navy
		/// has a price on the player's head.
		/// </summary>
		bool Targets(const cs::Ref<Ship>& lurker, const cs::Ref<Ship>& rider);

	private:
		void Interdict(const cs::Ref<Ship>& lurker, double station, cs::List<cs::Ref<Ship>>& victims, double jd);

	public:
		/// <summary>
		/// Whether <paramref name="victim"/>'s group made out <paramref name="lurker"/> (by class at least) as it passed:
		/// the raider lies dark (<see cref="Signatures.Silent"/>) at its stand-off from the lane line, so a merchant's
		/// navigation sensors see only a blip there, if that, while a warship's suite, or a merchant with a scanner suite
		/// fitted, picks out a hull.
		/// </summary>
		bool Forewarned(const cs::Ref<Ship>& victim, const cs::Ref<Ship>& lurker, double jd);

		/// <summary>
		/// What a lane rider's scanners make of the nearest armed interdictor ahead on its lane (U.SPC-T42), or null if
		/// there is none it can see, it has passed them all, or its ride ends short of every pulse. The emitter's field is
		/// picked up at <see cref="SensorSuite.ReachKm"/> of <see cref="EmitterSignature"/> (better scanners see it sooner),
		/// and the countdown is the ride's own time to the edge of the pulse's reach, at its present pace.
		/// </summary>
		cs::Nullable<LaneThreat> ThreatAhead(const cs::Ref<Ship>& rider, double jd);

	private:
		/// <summary>When a rider's progress along its leg reaches <paramref name="progress"/> (at its present pace), by bisection.</summary>
		static double RideReaches(const cs::Ref<Ship>& rider, double progress, double jd);

		cs::Ref<Hyperlanes> lanes0();

		void Hook(const cs::Ref<Battle>& b);

		/// <summary>
		/// Books a captor's terms for a hull that struck its colours (or a player who paid off pirates):
		/// ransomed cargo goes to the captor, a released hull carries on; a ship to be taken waits for the
		/// boarders and is booked by the capture hook like any other prize.
		/// </summary>
		void OnSurrenderDecided(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, SurrenderTerms terms);

		/// <summary>The lane with the most merchant traffic, preferring those that start at the raiders' own system.</summary>
		cs::Ref<Lane> BusiestLane(int system);

		// ---------------------------------------------------------------- prizes

		/// <summary>
		/// What boarders do with a ship they have taken. Pirates keep warships they can crew, strip
		/// merchant holds and mostly leave the hull adrift (or sell it to the breakers at Blackwater,
		/// or scuttle it); navies take serviceable enemy hulls as prizes and scuttle pirates.
		/// </summary>
		void DisposeOfPrize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, double jd);

		/// <summary>
		/// A hull taken by allied factions boarding together (U.SPC-T392) is settled between them before anyone disposes
		/// of it (<see cref="PrizeSettlement.Negotiate"/>): each partner's part of the cargo is paid out to it (raiders' into
		/// their haven, a lawful faction's at the prize price), and the keeper buys its partners out of the hull from its
		/// treasury, or, when none of them can, the hull goes to the breakers and the price is shared. Returns the faction
		/// that keeps the hull, to dispose of it as a lone captor would (what is left of the hold its own share), or null
		/// when it was sold. A boarding by one faction alone returns its captor untouched.
		/// </summary>
		cs::Ref<Faction> SettleJointPrize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, double jd);

		void Loot(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& clan, const cs::Ref<Battle>& b);

	public:
		/// <summary>
		/// A hull broken up in a collision outside any battle (U.SPC-T60): it is lost like a scuttled ship,
		/// leaves a wreck where it struck (<paramref name="offsetKm"/> from <paramref name="body"/>, with whatever
		/// it carried still aboard to salvage) and the loss goes in the log. A ship already gone, or in a battle
		/// (whose own <see cref="Battle.ApplyImpact"/> settles it there), is left alone; true if it was lost here.
		/// </summary>
		bool LoseInCollision(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, const cs::String& what);

		/// <summary>US-T97: <see cref="LoseInCollision"/> for any cause (a self-destruct's blast, <see cref="SelfDestruct"/>), the
		/// log saying <paramref name="logLine"/>.</summary>
		bool LoseOutsideBattle(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, const cs::String& logLine);

	private:
		/// <summary>
		/// Sends a hull to the bottom. Given the battle and its unit there, whoever is still aboard is put off in
		/// the escape pods first, which drift where it was scuttled (see <see cref="Crews"/>).
		/// </summary>
		void Scuttle(const cs::Ref<Ship>& ship, const cs::Ref<Battle>& b = nullptr, const cs::Ref<BattleUnit>& u = nullptr, double jd = 0);

		/// <summary>
		/// The raiders cast the hull off where they took it: it drifts there, fixed in the fight's own frame
		/// (so a salvager can come alongside, match it and latch on), with what they left of its cargo, its
		/// stores, and perhaps some of its crew hiding aboard or some of the raiders still stripping it (see
		/// <see cref="SalvageSession.Strand"/>).
		/// </summary>
		void LeaveAdrift(const cs::Ref<Ship>& ship, const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& raiders, Good good, double leftTonnes, double jd);

		/// <summary>
		/// The people left aboard a hull its captors are keeping or selling become theirs to hold: pirates' hostages
		/// (held at their haven for ransom), an enemy navy's prisoners of war, or -- raiders taken by anyone -- lost
		/// to the Banner in irons.
		/// </summary>
		void TakeCrewCaptive(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& captor, double jd);

		/// <summary>The prize changes hands: a prize crew takes over and it joins the captor (a group of theirs in the fight, if there is room).</summary>
		bool Steal(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& captor, const cs::Ref<Battle>& b, double jd);

		/// <summary>A faction's own settlement bodies (a navy's prize patrols those), or the given body if it has none.</summary>
		cs::List<int> OwnPorts(const cs::Ref<Faction>& f, int fallback);

		/// <summary>A ship leaves its group (taken, or abandoned): it keeps the group's motion as its own, and the group gets a new flagship if it was the old one.</summary>
		void Detach(const cs::Ref<Ship>& ship);

		/// <summary>
		/// A ship leaving its group keeps the group's orbit, but a little way along it (where it was
		/// flying in formation), not on top of its old flagship. Behind it, or ahead of it (<paramref name="ahead"/>, US-T151)
		/// when the old flagship has stopped where it was taken at a helm: the orbit then carries the heir away from it.
		/// </summary>
		static cs::Ref<ParkingOrbit> OwnSpot(const cs::Ref<ParkingOrbit>& orbit, const cs::Ref<Ship>& ship, const cs::Ref<Ship>& lead, bool ahead = false);

		/// <summary>The rest of a ship's motion state that a group's escorts and heirs take over with it (lane shape, ring).</summary>
		static void CopyRest(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to);

		/// <summary>Moves whatever plunder <paramref name="from"/> is carrying onto <paramref name="to"/>'s hold
		/// (U.SPC-T308): used when a band merges into another, or a new flagship takes its place, so a clan's loot
		/// is not left stranded on a hull that no longer runs the group's schedule.</summary>
		static void MovePlunder(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to);

		// ---------------------------------------------------------------- meeting the enemy

	public:
		/// <summary>A ship has arrived at a body: fight if anyone there is hostile, or join a fight already on.</summary>
		void OnArrived(const cs::Ref<Ship>& ship, double jd);

		/// <summary>A player flies a piloted ship (excluded from every other gathering of "ships present" at a
		/// body, see <see cref="PresentGroups"/>) into range of a hostile: starts a fight there if one isn't
		/// already on, or joins the one that is, then places the piloted ship into it at its own actual flown
		/// position -- not a formation slot -- since the Runtime pilot loop, not this battle, owns its pose
		/// from here on (see <see cref="BattleUnit.PlayerControlled"/>). <paramref name="body"/> is the body the
		/// pilot is actually flying beside (the frame <paramref name="offsetFromBodyKm"/> is measured from), not
		/// the ship's scheduled orbit, which for a hull taken over mid-transit is still its far-off destination.
		/// Returns null (no-op) if nothing hostile and willing to fight is actually present.</summary>
		cs::Ref<Battle> TryStartPlayerBattle(const cs::Ref<Ship>& ship, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd);

		/// <summary>
		/// The player opens fire on <paramref name="target"/>, a ship their own faction has no quarrel with (an
		/// enemy's is just <see cref="TryStartPlayerBattle"/>): a fight starts where the player is, the player alone on
		/// one side and the target's group, its faction's other ships here and its platforms over this world on the
		/// other. If a fight is already on here with the target in it, the player joins against it. Attacking a lawful
		/// ship is a crime, judged when the fight ends (see <see cref="Law.OnBattleEnded"/>). The target must hold orbit at
		/// this body (parked or queued at its ring) and the player be in its neighbourhood. Null if not.
		/// </summary>
		cs::Ref<Battle> TryStartPlayerAttack(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& target, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd);

		/// <summary>
		/// A raider band that has closed on its mark falls on it (US-T140, <see cref="SquadOrders"/>): a fight where the mark
		/// is, the band on side 0 (the piloted hull, a captain the SpaceGod holds, stays out of it) and the mark's group on
		/// side 1. Null if the mark is gone or fighting, none of the band can join, or a battle is already on at that body.
		/// </summary>
		cs::Ref<Battle> StrikeAt(const cs::List<cs::Ref<Ship>>& band, const cs::Ref<Ship>& mark, double jd);

		/// <summary>The squads' book (US-T140): bands closing on marks, and those stood down. Null until first used (older saves).</summary>
		cs::Ref<SquadOrders> Squads;

		/// <summary>
		/// A patrol interdicts the player's wanted ship (see <see cref="Law.UpdatePilot"/>): a fight off
		/// <paramref name="body"/> where the player is, the patrol's group on the side enforcing the law
		/// (<see cref="Battle.EnforcingSide"/>). Null if either is already fighting, a battle is already on here,
		/// or the player is not in this body's neighbourhood.
		/// </summary>
		cs::Ref<Battle> TryStartInterdiction(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& patrol, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd);

		/// <summary>
		/// A bounty hunter with a contract on the player's wanted ship has caught up with it (see
		/// <see cref="Outlaws.UpdatePilot"/>): a fight where the player is, the hunter's group enforcing the law as
		/// a patrol would. Null under the same conditions as <see cref="TryStartInterdiction"/>.
		/// </summary>
		cs::Ref<Battle> TryStartHunt(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& hunter, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd);

		/// <summary>
		/// A group running the player's ship down has caught it (U.SPC-T273): the fight is where the player is, anywhere
		/// in open space, with the group's ships where the chase had them about it (no more than
		/// <see cref="PursuitStartKm"/> off). Joins the player's own fight if one is on. Null if the player is not the
		/// group's to fight.
		/// </summary>
		cs::Ref<Battle> TryStartPursuit(const cs::Ref<Ship>& ship, const cs::Ref<Pursuer>& g, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd);

	private:
		/// <summary>The furthest a chasing ship starts from the anchor of the fight it catches the player in, km.</summary>
		static constexpr double PursuitStartKm = 20;

		cs::Ref<Battle> Enforce(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& patrol, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd, const cs::String& name, cs::Func<cs::String, cs::Ref<Ship>, cs::Ref<Battle>> news);

	public:
		/// <summary>
		/// Starts a battle at <paramref name="body"/> if <paramref name="aggressor"/> has a quarrel with anyone there and
		/// is fit to pick it: one of its own warships or raiders there armed and willing, or the other side's platforms
		/// (whose guns open on whoever comes into range). Where only the other side would fight, it is theirs to start
		/// (see <see cref="OnArrived"/> and the daily sweep), with them as the aggressor, not the arrival of a hull that
		/// can only run (U.SPC-T290). <paramref name="byPlayer"/>: for
		/// the player's own ship, which is never one of the hulls counted here (see <see cref="TryStartPlayerBattle"/>).
		/// </summary>
		cs::Ref<Battle> TryStartBattle(int body, const cs::Ref<Faction>& aggressor, double jd, cs::Nullable<DVec3> anchor = cs::Nullable<DVec3>());

	private:
		cs::Ref<Battle> TryStartBattle(int body, const cs::Ref<Faction>& aggressor, double jd, cs::Nullable<DVec3> anchor, bool byPlayer);

		/// <summary>
		/// Whether raiders of <paramref name="clan"/> at <paramref name="body"/> have anything there worth starting a fight
		/// over (U.SPC-T374): any hostile warship or platform to fight (the commanded-combat rules and the raid orders decide
		/// those), or else a merchant or civilian hull among <paramref name="present"/> worth the heat (<see cref="BestMark"/>).
		/// </summary>
		bool WorthAFight(const cs::Ref<Faction>& clan, int body, const cs::List<cs::Ref<Ship>>& present, double jd);

		/// <summary>A defence platform or gate fort lost (or taken) is struck off its settlement's or gate's list.</summary>
		static void StrikeOff(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>How near a gate (km) a ship holding beside it counts as at it, for a gate battle (U.SPC-T92).</summary>
		static constexpr double GateReachKm = 2000;

	private:
		cs::Dictionary<cs::Ref<JumpGate>, int> gateBattles = cs::Dictionary<cs::Ref<JumpGate>, int>();

	public:
		/// <summary>
		/// A gate battle (U.SPC-T92): <paramref name="attacker"/>'s war fleet, holding beside <paramref name="gate"/> (an end
		/// of it in this system), engages the gate's forts and whatever enemy warships hold there with them. Fought where the
		/// gate stands, off the system's root, like any other battle. Null if there is nothing there to fight, the fleet is
		/// in no shape to start it, or a fight is already on at the root.
		/// </summary>
		cs::Ref<Battle> StartGateBattle(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& attacker, double jd);

	private:
		/// <summary>Fit to pick a fight: not withdrawing and not badly hurt.</summary>
		bool Willing(const cs::Ref<Ship>& leader, double jd);

		/// <summary>Pulling back after a fight it ran from or lost (see <see cref="Resolve"/>).</summary>
		bool Withdrawing(const cs::Ref<Ship>& leader, double jd);

		int SideFor(const cs::Ref<Faction>& f, const cs::Ref<Faction>& aggressor, const cs::HashSet<cs::Ref<Faction>>& hostile);

		/// <summary>Ships parked at a body and free to fight (escorts included), not already in a battle; given
		/// <paramref name="near"/>, only those within <see cref="GateFightRangeKm"/> of that point off the body.</summary>
		cs::List<cs::Ref<Ship>> PresentGroups(int body, cs::Nullable<DVec3> near = cs::Nullable<DVec3>(), double jd = 0);

		/// <summary>Where <paramref name="f"/>'s first group parked at <paramref name="body"/> is (km off it), or null.</summary>
		cs::Nullable<DVec3> SpotOf(const cs::Ref<Faction>& f, int body, double jd);

		cs::Ref<Battle> NewBattle(int body, double jd, const cs::List<cs::Ref<Ship>>& present, cs::Nullable<DVec3> at = cs::Nullable<DVec3>());

		// battleCount keys for concealed skirmishes and fights at gates (body indices are never negative).
		static constexpr int ConcealedBelt = -1; static constexpr int ConcealedDark = -2; static constexpr int GateKey = -10;

		/// <summary>The gate whose approach <paramref name="offsetKm"/> (off the system root) is on, or null.</summary>
		cs::Ref<JumpGate> GateNear(DVec3 offsetKm);

		// ---------------------------------------------------------------- gate ambushes

	public:
		/// <summary>
		/// Raiders lying in wait off a jump gate (U.SPC-T94, <see cref="Ship.StakeoutKm"/>) fall on whatever comes within
		/// <see cref="GateAmbushRangeKm"/> of them: ships held at the gate waiting to go through, ships just come out of it,
		/// merchants and navies alike -- unless the warships there are more than the band can take on, when it lets them be.
		/// Run every update, and by the gate traffic as ships go in and come out, so a quick passage between coarse steps is
		/// still caught.
		/// </summary>
		void GateAmbushes(double jd);

		/// <summary>
		/// A raider band's threat assessment (U.SPC-T374, see <see cref="RaidDoctrine"/>): of <paramref name="marks"/> (the
		/// merchant and civilian hulls in reach), the one most worth springing on with <paramref name="guns"/> of hostile
		/// strength about them and the band's own <paramref name="band"/>, or null when none is worth the risk or the heat.
		/// </summary>
		cs::Ref<Ship> BestMark(const cs::List<cs::Ref<Ship>>& marks, double guns, double band);

		/// <summary>
		/// The gate a raider band of <paramref name="clan"/> would lie in wait off: the busiest (most traffic lately, see
		/// <see cref="JumpGate.Traffic"/>), or null when none has seen <see cref="MinGateTraffic"/>.
		/// </summary>
		cs::Ref<JumpGate> BusiestGate(const cs::Ref<Faction>& clan, double jd);

		/// <summary>Sends a raider band to lie in wait off <paramref name="gate"/> until <paramref name="untilJd"/>.</summary>
		void StakeOut(const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double untilJd);

	private:
		/// <summary>Whether <paramref name="body"/> hosts a hidden haven nobody has found yet.</summary>
		bool HidesHaven(int body);

		/// <summary>The body a battle's news names: none while it is fought over an undiscovered haven.</summary>
		int Shown(const cs::Ref<Battle>& b);

		/// <summary>Where a side's line starts: the two sides open about 2 x <see cref="OpenKm"/> apart (US-T180: 14 km, was 26).</summary>
		static DVec3 Spot(const cs::Ref<Battle>& b, int side);

		/// <summary>US-T180: how far off the battle's centre each side's line opens, km.</summary>
		static constexpr double OpenKm = 7;

		/// <summary>The centre of the ring a station's defence platforms are placed on for a side, as before US-T180.</summary>
		static DVec3 StationSpot(const cs::Ref<Battle>& b, int side);

		void Join(const cs::Ref<Battle>& b, const cs::Ref<Ship>& ship, int side, bool late);

		void OnUnitDestroyed(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// A hull is going down in battle: whoever is still aboard runs for the escape pods (fewer make it if the
		/// reactor went), which launch from where it died and drift there broadcasting (see <see cref="Crews"/>).
		/// A battle's ejected fighter pilots drift together, one cluster to a side.
		/// </summary>
		void AbandonShip(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double jd);

		// ---------------------------------------------------------------- after the battle

		void Resolve(const cs::Ref<Battle>& b, double jd);

		/// <summary>
		/// The craft still flying when the fight ends (U.SPC-T473, see <see cref="StrandedWings"/>): each is taken back aboard
		/// the carrier it lands on, if that is still on the field; one whose carrier was lost or gone lands on another
		/// friendly carrier with room, or a station on the field, or puts in at a friendly port at the world the fight was
		/// over; failing all of those it is lost, its pilot ejecting into the pods.
		/// </summary>
		void RecoverWings(const cs::Ref<Battle>& b, double jd);

		/// <summary>A carrier craft that landed on a station keeps its own hull there (U.SPC-T591), held until its carrier calls;
		/// one docked in a hangar needs nothing.</summary>
		static void StrandHull(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& home);

		/// <summary>
		/// Loot taken by <paramref name="clan"/> (U.SPC-T94): into the clan's own haven in this system, if it has one standing;
		/// else into the hold of its band in the fight, to carry home through the gates; else it is lost.
		/// </summary>
		void DeliverLoot(const cs::Ref<Faction>& clan, Good g, double tonnes, const cs::Ref<Battle>& b);

	public:
		/// <summary>A standing haven of <paramref name="clan"/>'s in this system, or null.</summary>
		cs::Ref<Settlement> LiveHavenOf(const cs::Ref<Faction>& clan);

		/// <summary>A raider band at a standing haven of its own clan unloads its plunder into the haven's stores.</summary>
		void UnloadPlunder(const cs::Ref<Ship>& ship, int body);

		/// <summary>When a flagship is lost its strongest surviving escort takes the group.</summary>
		/// <summary>
		/// The player takes the helm of a group's flagship: its escorts carry on under the strongest of
		/// them (as when a flagship is lost) rather than holding station on a hull that no longer runs
		/// the group's schedule -- frozen at the spot piloting began, or left behind for good if it jumps
		/// through the gate. Also used when a flagship crosses a jump gate without some of its escorts
		/// (still fighting, or left hulks): those stay here under the strongest of them (U.SPC-T310).
		/// <paramref name="keepHunter"/> leaves a bounty hunter's place on the boards with the old
		/// flagship (a hunt voyage takes it up again when it comes home) rather than handing it on.
		/// </summary>
		void HandOverCommand(const cs::Ref<Ship>& leader, bool keepHunter = false);

		/// <summary>
		/// A ship gone from this system for good without a successor taking its place (through a jump gate or its own
		/// cruise drive, the same hull living on in another system's fleet): its orders and per-ship bookkeeping here
		/// go with it (U.SPC-T311).
		/// </summary>
		void Forget(const cs::Ref<Ship>& ship);

	private:
		/// <summary>
		/// Drops a ship from the retreat and lane-ambush cooldowns once its cooldown is over or it is gone for good
		/// (destroyed, or through a gate without <see cref="Forget"/> being called for it: a ship lost to the derelict
		/// decay in <see cref="Daily"/>, a scuttling, a prize's disposal, and every other way a hull leaves the fleet
		/// never visits <see cref="Forget"/>), so the books do not grow for ever as a long game runs on (U.SPC-T321's
		/// fix for <see cref="Pursuit"/>'s own lastChased, never applied here).
		/// </summary>
		void PruneTracking(double jd);

		/// <summary>The group <paramref name="dead"/> led goes on under an heir: <paramref name="chosen"/> if given (one of its
		/// escorts), else a command ship among them (U.SPC-T477), else the biggest.</summary>
		void Succession(const cs::Ref<Ship>& dead, bool keepHunter = false, const cs::Ref<Ship>& chosen = nullptr);

		// ---------------------------------------------------------------- orders

	public:
		/// <summary>Fleet hook: where a warship or raider group goes next (its own body to hold), or -1.</summary>
		int ChooseNext(const cs::Ref<Ship>& leader, int here, double jd);

		double Dwell(const cs::Ref<Ship>& ship, double normal);

	private:
		cs::Ref<GroupOrder> Decide(const cs::Ref<Ship>& leader, int here, double jd);

		/// <summary>
		/// The order that carries out the objective <paramref name="leader"/>'s polity has given its group (U.SPC-T423, see
		/// <see cref="StrategicCommand"/>), or null when it has none (or has just finished it) and its own command plans as
		/// before. Take: storm a haven, besiege an enemy port whose defences are broken, else attack it; taken, it holds it.
		/// Hold: defend the place. Blockade: lie over the port. Escort: go where its charge is or is bound. Explore: on to the
		/// nearest world not yet visited, standing down when every one has been. An objective in another system keeps the
		/// group in port here until command sends it there.
		/// </summary>
		cs::Ref<GroupOrder> Directed(const cs::Ref<Ship>& leader, int here, double jd);

		bool OwnsAt(int body, const cs::Ref<Faction>& f);

		double PopulationAt(int body, const cs::Ref<Faction>& f);

		/// <summary>Where a ship is or is going: the fight it is in, its trip's end, or the body it holds at.</summary>
		static int BoundFor(const cs::Ref<Ship>& c);

	public:
		/// <summary>How long a navy group holds a gate it was sent to (days on station after it gets there) before command
		/// looks again (U.SPC-T374).</summary>
		static constexpr double HoldGateDays = 3;

	private:
		/// <summary>
		/// The jump gate command sends navy group <paramref name="leader"/> to hold (U.SPC-T374), or null: one of its own
		/// navy's gates here with raiders known to be lying off it (in its picture, see <see cref="Sensors"/>; every band
		/// staked out there when there are no sensors), which the group is strong enough to clear and can reach within a
		/// month. Already holding one (<paramref name="held"/>, its station on the approach) it stays on it while the watch
		/// lasts. Sets the group's station on the approach (<see cref="Ship.StakeoutKm"/>), where the fight is fought.
		/// </summary>
		cs::Ref<JumpGate> GateToHold(const cs::Ref<Ship>& leader, int here, DVec3 at, double jd, cs::Nullable<DVec3> held);

	public:
		/// <summary>The warnings this system's gate pickets have given (U.SPC-T480, see <see cref="GatePickets"/>), oldest
		/// first (the last <see cref="GatePickets.WarningsKept"/>).</summary>
		cs::List<cs::Ref<PicketWarning>> PicketWarnings = cs::List<cs::Ref<PicketWarning>>();

		/// <summary>
		/// The strength of the hostile groups <paramref name="f"/>'s pickets at <paramref name="gate"/> have warned of in the
		/// last <see cref="GatePickets.WarningDays"/> that are still there (U.SPC-T480): parked off the system root within
		/// <see cref="GateFightRangeKm"/> of the gate. Raiders staked out off it are left to <see cref="RaidersOffGate"/>.
		/// </summary>
		double WarnedOffGate(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f, double jd);

	private:
		/// <summary>
		/// The gate command sends navy group <paramref name="leader"/> to hold on a picket's warning (U.SPC-T480), or null:
		/// one of its navy's own gates here whose pickets have warned of hostiles still at it (<see cref="WarnedOffGate"/>),
		/// the gravest such threat the group, with the gate's forts beside it, is strong enough to meet, within a month's
		/// flight. Takes station on the gate's approach (<see cref="Ship.StakeoutKm"/>), keeping a station it already held there.
		/// </summary>
		cs::Ref<JumpGate> GateWarned(const cs::Ref<Ship>& leader, int here, double jd, cs::Nullable<DVec3> held);

		/// <summary>
		/// A gate picket's orders (U.SPC-T480, see <see cref="GatePickets"/>): to a yard when it is badly hurt or has run from
		/// a fight, else out to its post off its gate and holding it. Never to fight: it is a lookout, not a warship, and
		/// holding a patrol order (not an objective) it opens no fight of its own (see <see cref="Commanded"/>).
		/// </summary>
		cs::Ref<GroupOrder> DecidePicket(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing);

		/// <summary>
		/// A corvette's orders (U.SPC-T476, see <see cref="Corvettes"/>): to a yard when it is hurt or out of missiles; to
		/// defend the port on its beat nearest to it that has enemies at it; to run down the pirates or smugglers its navy has
		/// seen at a port on its beat or anywhere round its home planet, the nearest first; otherwise on round its beat, back
		/// to its station between runs down the lanes. It takes no objective from its polity's command: it is stationed for
		/// good, and never leaves its home system.
		/// </summary>
		cs::Ref<GroupOrder> DecideCorvette(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing);

	public:
		/// <summary>When each navy here went short of its corvettes (U.SPC-T476, see <see cref="Corvettes.Upkeep"/>).</summary>
		cs::Dictionary<cs::Ref<Faction>, double> CorvettesShortSince = cs::Dictionary<cs::Ref<Faction>, double>();

	private:
		/// <summary>The strength of the raider bands navy <paramref name="f"/> (looking from <paramref name="at"/>) knows to be
		/// lying in wait off <paramref name="gate"/>, or 0.</summary>
		double RaidersOffGate(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f, DVec3 at, double jd);

		/// <summary>
		/// A patrol wing's orders (U.SPC-T371, see <see cref="PatrolWings"/>): to a yard when it is hurt or out of missiles;
		/// to defend the port on its beat nearest to it that has enemies at it; to hunt raiders seen working a port on its
		/// beat; otherwise on round its beat. Never to strike at an enemy port: that is the battle groups' business.
		/// </summary>
		cs::Ref<GroupOrder> DecideWing(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing);

		/// <summary>
		/// A navy group's lane picket (U.SPC-T479): with a field projector fitted to a hull of the group (see <see cref="LaneInterdictors"/>)
		/// and nothing more pressing, now and then (<see cref="LaneInterdictors.PicketChance"/>) it lies in wait part-way along
		/// the most troubled hyperlane out of its system, where raiders have struck lately, bound for one of
		/// <paramref name="ports"/> at its far end, to pull the raiders and enemies riding it out into a fight (see
		/// <see cref="Targets"/>). A wing (<paramref name="keepToPorts"/>) takes only a lane that ends at one of them; another
		/// group, failing that, makes for the far end itself. Null when it does not.
		/// </summary>
		cs::Ref<GroupOrder> LanePicket(const cs::Ref<Ship>& leader, int here, const cs::List<int>& ports, bool keepToPorts);

	public:
		/// <summary>
		/// Whether the group <paramref name="ship"/> flies with is commanded to fight at <paramref name="body"/>
		/// (U.SPC-T371): combat is ordered, not picked with whatever hostile happens to be in reach. A group may open a
		/// fight (or turn to run the player down) only where its orders put it: at its own ports, which it always defends;
		/// on its patrol beat; or at the objective of the order it holds (the port it attacks, defends, besieges, assaults
		/// or hunts raiders at, the hunting ground it raids, the gate it watches). A group holding no orders yet (and a
		/// bounty hunter, which follows its contract) keeps the old rule and fights wherever it meets a hostile; a patrol
		/// wing never does, keeping to its beat. Defending is never ordered away: a group set upon is still swept into the
		/// fight, and platforms still open on a hostile come into range.
		/// </summary>
		bool Commanded(const cs::Ref<Ship>& ship, int body);

	private:
		/// <summary>Whether <paramref name="o"/> sends its group to fight at <paramref name="body"/>: any order but a patrol
		/// or a run to the yard, aimed there.</summary>
		static bool Objective(const cs::Ref<GroupOrder>& o, int body);

		cs::Ref<GroupOrder> DecidePirate(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing);

		/// <summary>Whether <paramref name="ship"/> flies in a bounty hunter group (see <see cref="Outlaws"/>).</summary>
		bool Hunter(const cs::Ref<Ship>& ship);

	public:
		/// <summary>The navy's oldest surviving group, which never leaves home.</summary>
		bool IsHomeGuard(const cs::Ref<Ship>& leader);

	private:
		int NextPatrol(const cs::Ref<Ship>& leader, int here);

		int NearestYard(const cs::Ref<Faction>& f, int here, double jd);

		int HavenBody(const cs::Ref<Faction>& clan);

		cs::String HavenName(const cs::Ref<Faction>& clan = nullptr);

		/// <summary>The first haven still standing (not taken or destroyed), or null.</summary>
		cs::Ref<Settlement> FirstLiveHaven();

	public:
		/// <summary>The haven a raider group sails from (the first stop on its route, if a haven is there), or null.</summary>
		cs::Ref<Settlement> HomeHaven(const cs::Ref<Ship>& ship);

		// ---------------------------------------------------------------- hidden havens

		/// <summary>How far a ship's scanners pick out a hidden haven: its reactors, its traffic, its guns.</summary>
		static constexpr double ScanRangeKm = 2e6;

		/// <summary>Chance a navy that wins a fight follows each beaten raider group home.</summary>
		static constexpr double TrailChance = 0.3;

		/// <summary>Days an assaulting group must hold a haven whose garrison is broken before it is boarded or levelled.</summary>
		static constexpr double HavenAssaultDays = 1;

		/// <summary>Days the pirates take to rebuild a haven that was taken or destroyed.</summary>
		static constexpr double HavenRebuildDays = 180;

		/// <summary>
		/// <paramref name="by"/> finds a hidden haven (if it had not already): it is logged, goes on the
		/// map, and that faction's navy may now strike it. Returns whether this was news to it.
		/// </summary>
		bool Discover(const cs::Ref<Settlement>& st, const cs::Ref<Faction>& by, double jd, const cs::String& how);

		/// <summary>A ship of <paramref name="by"/> sweeps its scanners from a heliocentric position: every hidden
		/// haven within <paramref name="rangeKm"/> (<see cref="ScanRangeKm"/>, more with scanner suites fitted) is found. Returns the havens found this time.</summary>
		/// (C#'s `string who = null` is an empty string here.)
		cs::List<cs::Ref<Settlement>> ScanFrom(const cs::Ref<Faction>& by, DVec3 helioKm, double jd, const cs::String& who = cs::String(), double rangeKm = ScanRangeKm);

		/// <summary>A raider beaten by <paramref name="tracker"/> is followed back to its haven, if that haven is hidden.</summary>
		bool TrailHome(const cs::Ref<Ship>& raider, const cs::Ref<Faction>& tracker, double jd);

	private:
		/// <summary>Every navy's warships sweep their scanners once a day.</summary>
		void Scans(double jd);

		/// <summary>A haven <paramref name="f"/>'s navy would strike on its own: a hidden one it has found, still
		/// standing. (Blackwater, the haven everyone knows of over Deimos, is an open secret the navies leave
		/// be; the player can still go in.)</summary>
		bool Assailable(const cs::Ref<Settlement>& s, const cs::Ref<Faction>& f);

		/// <summary>The nearest found haven the group is strong enough to storm (half again its garrison and any raiders there), or null.</summary>
		cs::Ref<Settlement> AssaultTarget(const cs::Ref<Ship>& leader, int here, double jd);

	public:
		/// <summary>Whether the ship's group carries marines to board with.</summary>
		bool HasMarines(const cs::Ref<Ship>& ship);

		/// <summary>
		/// A haven is open to be boarded or destroyed by <paramref name="ship"/>: still standing, hostile,
		/// its garrison platforms all gone, no fight going on there and no raiders parked over it.
		/// </summary>
		bool CanAssault(const cs::Ref<Settlement>& st, const cs::Ref<Ship>& ship);

		/// <summary>
		/// The player's (or anyone's) assault on a haven whose defences are broken: board and take it
		/// (needs marines in the group; the stolen goods are seized) or destroy it (needs guns or
		/// marines). Either way it is out of action, and its raiders lie low, until the pirates rebuild
		/// it. Returns false if it can't be done.
		/// </summary>
		bool AssaultHaven(const cs::Ref<Settlement>& st, const cs::Ref<Ship>& ship, bool capture, double jd);

	private:
		void FallHaven(const cs::Ref<Settlement>& st, const cs::Ref<Faction>& by, bool capture, double jd);

		/// <summary>
		/// Navies storm the havens they have found: once the garrison's platforms are gone and no raiders
		/// hold the rock, an assaulting group boards it (if it has marines) or levels it after
		/// <see cref="HavenAssaultDays"/>. Fallen havens rise again after <see cref="HavenRebuildDays"/>.
		/// </summary>
		void HavenAssaults(double jd);

		void RebuildHaven(const cs::Ref<Settlement>& st, double jd);

	public:
		/// <summary>Days an assaulting group has held a broken haven (see <see cref="HavenAssaultDays"/>).</summary>
		double AssaultDays(const cs::Ref<Settlement>& s) const;

		bool AtWar(const cs::Ref<Faction>& f);

		bool EnemiesAt(int body, const cs::Ref<Faction>& f);

	private:
		/// <summary>Warships of a navy at war with <paramref name="f"/> (not raiders) parked at <paramref name="body"/>.</summary>
		bool EnemyNavyAt(int body, const cs::Ref<Faction>& f);

		bool PiratesAt(int body);

		/// <summary>
		/// Raiders <paramref name="f"/>'s picture holds parked at <paramref name="body"/> (by class at least: a warship
		/// with no transponder over its port), as the word has reached a group at <paramref name="where"/> (U.SPC-T100):
		/// a sighting crosses the system at light speed, so a group far off acts on a look minutes or hours old, sails
		/// for raiders that may have gone, and does not yet know of ones that have just come. What a navy can act on,
		/// rather than what is there.
		/// </summary>
		bool PiratesSeenAt(int body, const cs::Ref<Faction>& f, DVec3 where, double jd);

	public:
		/// <summary>Whether a navy group at <paramref name="where"/> has word of raiders working its own port at
		/// <paramref name="body"/> (see <see cref="PiratesSeenAt"/>).</summary>
		bool KnowsOfRaidersAt(int body, const cs::Ref<Faction>& f, DVec3 where, double jd);

		/// <summary>Strength defending a body against <paramref name="attacker"/>: hostile platforms and parked warships.</summary>
		double DefenceAt(int body, const cs::Ref<Faction>& attacker);

		double GroupStrength(const cs::Ref<Ship>& leader);

	private:
		double GroupDamage(const cs::Ref<Ship>& leader);

		/// <summary>A carrier that has lost half its fighters goes back to base to replace them.</summary>
		bool HangarsLow(const cs::Ref<Ship>& leader);

		/// <summary>A group that has spent most of its marines on boarding goes back for more.</summary>
		bool MarinesLow(const cs::Ref<Ship>& leader);

		bool MissilesLow(const cs::Ref<Ship>& leader);

		// ---------------------------------------------------------------- daily

		void Daily(double jd);

		/// <summary>
		/// Navies concentrate: groups of one faction parked at the same body (and not fighting) merge
		/// into the strongest of them, up to eight hulls, so single new warships become battle groups
		/// instead of patrolling alone. The home guard is never absorbed into or swollen. At the system
		/// root, "the same body" covers every jump gate's approach, so two groups must also be within
		/// <see cref="GateFightRangeKm"/> of each other there: a raider band parked off one gate is left
		/// alone by one lurking at another, AU away (U.SPC-T307).
		/// </summary>
		void MergeGroups(double jd);

		/// <summary>The first formation slot on <paramref name="leader"/> no living escort holds: a ship merged in
		/// never sits on top of one already there (escorts lost or cut loose leave gaps among the slots).</summary>
		int FreeSlot(const cs::Ref<Ship>& leader);

		/// <summary>
		/// A day's running costs out of each navy's treasury: crews, fuel and maintenance for every
		/// warship it has in service and every defence platform over its ports
		/// (<see cref="Economy.UpkeepPerYear"/> of the hull's price a year). Pirates live off plunder.
		/// </summary>
		void PayUpkeep();

	public:
		/// <summary>Credits a port's owner pays to raise one defence platform (besides its steel and electronics).</summary>
		static constexpr double PlatformPrice = 3e7;

	private:
		/// <summary>A day's drill for the marines of every hull out of battle that has a marine habitat (U.SPC-T465).</summary>
		void DrillMarines();

		/// <summary>
		/// Warships at a friendly yard repair and rearm, buying alloys, electronics and munitions from its
		/// market. A navy whose treasury is empty cannot pay the yard: its ships stay as they are.
		/// </summary>
		void Refit(double jd);

	public:
		/// <summary>Chance a day a warship idle at its own yard looks at an upgrade.</summary>
		static constexpr double AiOutfitChance = 0.05;

		/// <summary>Days a captured port is garrisoned and cannot be taken again.</summary>
		static constexpr double CaptureGraceDays = 90;

		/// <summary>Days of uncontested siege a port holds out: four, plus one per 200,000 people, up to twenty.</summary>
		static double SiegeNeeded(const cs::Ref<Settlement>& s);

	private:
		/// <summary>
		/// A port with no platforms left, blockaded by a besieging group with nobody defending it,
		/// changes hands after <see cref="SiegeNeeded"/> days. A fresh capture is garrisoned for
		/// <see cref="CaptureGraceDays"/>.
		/// </summary>
		void Sieges(double jd);

	public:
		/// <summary>Traders bound for a port that is now closed to them (it changed hands, or a war resumed) look elsewhere.</summary>
		void RevokeBookings();

		double SiegeDays(const cs::Ref<Settlement>& s) const;

		/// <summary>The faction whose besieging group holds the space over <paramref name="s"/> with none of the owner's
		/// warships there to contest it (it may still be fighting the platforms), or null. Such a group blockades the port.</summary>
		cs::Ref<Faction> Besieger(const cs::Ref<Settlement>& s) const;

	private:
		void RebuildPlatforms(double jd);

		static const cs::List<cs::String>& RaiderNames();

		int raidersRaised = 0;

	public:
		void RaisePirateGroup(double jd, int body);

	private:
		/// <summary>The most hands a new raider band can need (a cruiser, two destroyers and the destroyer fitted as its lane interdictor).</summary>
		static double BandComplement();

		void PirateRecruiting(double jd);

	public:
		/// <summary>A navy at or below this many warships, and outnumbered three to one, is beaten (see <see cref="Beaten"/>).</summary>
		static constexpr int BeatenNavyShips = 4;

		/// <summary>Warships <paramref name="f"/> still has afloat.</summary>
		int Warships(const cs::Ref<Faction>& f);

		/// <summary>
		/// Whether <paramref name="f"/> has lost its war with <paramref name="enemy"/> for now: after real losses its
		/// navy is down to a handful of hulls outnumbered three to one, or its last yard is under siege by a stronger
		/// navy. A beaten power sues for peace and rebuilds instead of being annihilated.
		/// </summary>
		bool Beaten(const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy);

	private:
		/// <summary>Wars wind down to ceasefires when both sides have bled, and flare up again months later.</summary>
		void DiplomacyCycle(double jd);

		// ---------------------------------------------------------------- shipbuilding

	public:
		/// <summary>What a navy's yard lays down next, or null to leave it to the merchant fleet.</summary>
		cs::Nullable<ShipClass> ChooseWarship(const cs::Ref<Settlement>& yard);

	private:
		static const cs::List<cs::String>& HullNames();

	public:
		cs::Ref<Ship> LaunchWarship(const cs::Ref<Settlement>& yard, ShipClass cls, double jd);

	private:
		/// <summary>Fits <paramref name="ship"/>, newly launched into the group led by <paramref name="leader"/>, with each
		/// facility <paramref name="plan"/> wants in every group (<see cref="BuildPlan.GroupFittings"/>) that no hull of the
		/// group carries yet, as far as it can take them (no yard bill: the hull's price covers it).</summary>
		void FitGroupEquipment(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& leader, const cs::Ref<BuildPlan>& plan);

		/// <summary>Whether a hull of the group led by <paramref name="leader"/> (the leader or a live escort) carries a
		/// facility of kind <paramref name="id"/>.</summary>
		bool GroupCarries(const cs::Ref<Ship>& leader, const cs::String& id);

		/// <summary>Whether the group <paramref name="leader"/> leads carries a command ship (U.SPC-T477): the leader or a
		/// living escort, but not <paramref name="except"/>.</summary>
		bool GroupHasCommandShip(const cs::Ref<Ship>& leader, const cs::Ref<Ship>& except = nullptr);

	public:
		/// <summary>
		/// <paramref name="flag"/>, one of <paramref name="leader"/>'s escorts, takes command of its group (U.SPC-T477): the
		/// fleet's senior officers go aboard the command ship, which takes over the group's schedule, orders and objective
		/// as an heir does (see <see cref="Succession"/>), and the old flagship falls in as one of its escorts, in the
		/// station the command ship held.
		/// </summary>
		void TakeCommand(const cs::Ref<Ship>& leader, const cs::Ref<Ship>& flag);

		// ---------------------------------------------------------------- helpers

	private:
		void BumpDanger(int body, double amount);

		cs::String Describe(const cs::Ref<Battle>& b, int side);

		static cs::String Short(const cs::Ref<Battle>& b, int side);

	public:
		/// <summary>The ordinal a repeat battle is named with: Second, Third, then 4th, ... 21st, 22nd, 23rd, 111th.</summary>
		static cs::String Ordinal(int n);
	private:
		War() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(War); // ObjectGraph registration (Persistence/Registrations)
	};
}
