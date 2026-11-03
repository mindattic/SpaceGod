#pragma once
// Port of Unity.SpaceGod Core/Systems/InterstellarWars.cs (US-T15, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Conflict;
	class Faction;
	class StarSystem;
	class Settlement;
	class JumpGate;
	class FleetSupply;
	class Treaty;
	class Galaxy;
	class World;
	class Ship;
	class Voyage;
	class Polities;
	class Diplomacy;
	class InterstellarTraffic;
	class MilitaryLogistics;
	enum class ForceKind : int;
	enum class CampaignMission : int;

	/// <summary>Where a war fleet's campaign stands (see <see cref="Campaign"/>).</summary>
	enum class CampaignStage : int
	{
		/// <summary>On its way over the gates to the system it means to take, forcing and taking enemy gates as it meets them.</summary>
		Marching,

		/// <summary>In the goal system: breaking the defences over each enemy settlement and landing marines to take it.</summary>
		Invading,

		/// <summary>Every enemy settlement in its goal taken: it stays to hold the system until the peace.</summary>
		Holding,

		/// <summary>Called home (beaten, spent, or the war is over), the way it can get through.</summary>
		Returning,

		/// <summary>Home again, or lost.</summary>
		Over,
	};
	SPACEGODCORE_API const char* ToString(CampaignStage v);

	/// <summary>
	/// One war fleet's campaign (U.SPC-T92): a navy group sent across the gates to take an enemy system. It plans its way
	/// over the gate graph (<see cref="GateNetwork.Route"/>, forcing gates the enemy holds), fights each such gate's forts
	/// and holds the gate until it is taken, then, in the goal system, breaks the platforms and warships over each enemy
	/// settlement and lands its marines to take it. The flagship carries the campaign on its <see cref="Voyage"/>, so it
	/// survives the gates' carrying a hull from one system to the next and a flagship lost to its heir.
	/// </summary>
	class SPACEGODCORE_API Campaign final
	{
	public:
		Campaign();

		cs::Ref<Conflict> War;
		cs::Ref<::SpaceGod::Core::Faction> Faction;

		/// <summary>Where it set out from (and comes back to), and the system it is taking.</summary>
		cs::Ref<StarSystem> Home, Goal;

		CampaignStage Stage = static_cast<CampaignStage>(0);
		double SetOutJd = 0, EndedJd = cs::Double::NaN;

		/// <summary>Its flagship's name, as it last stood (the flagship can change if it is lost).</summary>
		cs::String Flagship;

		/// <summary>The settlement it is reducing now, or null.</summary>
		cs::Ref<Settlement> Objective;

		/// <summary>The broken enemy gate it holds beside, and for how many days, until it is taken (see <see cref="InterstellarWars.GateHoldDays"/>).</summary>
		cs::Ref<JumpGate> Besieging;
		double BesiegedDays = 0;

		/// <summary>Gate battles it has fought, gates and settlements it has taken.</summary>
		int GateBattles = 0, GatesTaken = 0, SettlementsTaken = 0;

		/// <summary>Destroyed or taken to the last hull.</summary>
		bool Lost = false;

		/// <summary>What it has left of its propellant, munitions and spares, and its supply line (U.SPC-T93, see <see cref="MilitaryLogistics"/>).</summary>
		cs::Ref<FleetSupply> Supply; // = new FleetSupply() (made in the constructor, Systems/InterstellarWars.cpp)

		/// <summary>The kind of force sent (U.SPC-T474, see <see cref="FleetDoctrine"/>): a gate fleet marching through the gates,
		/// or a jump strike group crossing under its own drives.</summary>
		ForceKind Force = static_cast<ForceKind>(0);

		/// <summary>What it was sent to do (U.SPC-T474): invade, open a second front, relieve or raid.</summary>
		CampaignMission Mission = static_cast<CampaignMission>(0);

		/// <summary>When a strike group reached the system it was sent to (NaN until then).</summary>
		double ArrivedJd = cs::Double::NaN;

		/// <summary>Ports a raid has broken the defences of, and garrisons a relief has stiffened.</summary>
		int PortsRaided = 0, Relieved = 0;

		/// <summary>The ports a raid has fallen on, or a relief has seen to, so far.</summary>
		cs::List<cs::Ref<Settlement>> Raided = cs::List<cs::Ref<Settlement>>();

		bool Active() const { return Stage != CampaignStage::Over; }

		cs::String ToString() const;
	};

	/// <summary>Marines put down on an enemy settlement, fighting its garrison (U.SPC-T92).</summary>
	class Landing final
	{
	public:
		cs::Ref<Settlement> Target;
		cs::Ref<Faction> By;
		cs::Ref<::SpaceGod::Core::Campaign> Campaign;
		double Troops = 0, SinceJd = 0;
	};

	/// <summary>
	/// A war between two powers over systems (U.SPC-T92): the aggressor's goals (the enemy systems it means to take), the
	/// campaigns either side has sent, and, once it is over, the treaty that ended it.
	/// </summary>
	class SPACEGODCORE_API Conflict final
	{
	public:
		cs::Ref<Faction> Aggressor, Defender;
		double StartJd = 0, EndedJd = cs::Double::NaN;

		/// <summary>The systems the aggressor means to take from the defender.</summary>
		cs::List<cs::Ref<StarSystem>> Goals = cs::List<cs::Ref<StarSystem>>();

		cs::List<cs::Ref<Campaign>> Campaigns = cs::List<cs::Ref<Campaign>>();

		/// <summary>When either side last had a campaign in the field (for a stalemate's end).</summary>
		double LastActiveJd = 0;

		/// <summary>The peace that ended it; null while it is fought.</summary>
		cs::Ref<::SpaceGod::Core::Treaty> Treaty;

		bool Over() const { return Treaty != nullptr; }

		bool Involves(const cs::Ref<Faction>& f) const { return f != nullptr && (f == Aggressor || f == Defender); }

		cs::Ref<Faction> Enemy(const cs::Ref<Faction>& f) const { return f == Aggressor ? Defender : f == Defender ? Aggressor : nullptr; }

		cs::String ToString() const;
	};

	/// <summary>
	/// The wars between the systems' powers (U.SPC-T92), run by the galaxy whether or not the player is in any of the
	/// systems involved (a system nobody is watching fights at coarse detail, see <see cref="Galaxy.Focus"/>).
	///
	/// A state goes to war with a neighbour (<see cref="Declare"/>, or of its own accord when its navy far outweighs the
	/// other's, see <see cref="AutoDeclare"/>) with war goals: the neighbour's systems it means to take. Each day each side
	/// that has a goal and a navy group fit to go sends it on a <see cref="Campaign"/> over the gate graph; gates the enemy
	/// holds are forced: the fleet fights the gate's forts (<see cref="War.StartGateBattle"/>), holds the broken gate
	/// <see cref="GateHoldDays"/> and takes it. In the goal system it breaks each enemy settlement's platforms and the
	/// warships defending it (the system's own <see cref="War"/> fights those battles), then lands its marines against
	/// the settlement's <see cref="Settlement.Garrison"/>; when the garrison is beaten the settlement is occupied: it
	/// changes hands, the marines are its garrison, and its people are restive (<see cref="Settlement.Unrest"/>) and may
	/// rise against a thin garrison while the war lasts. A defender sends campaigns to take back what it has lost.
	///
	/// A war ends in a <see cref="Treaty"/> signed through <see cref="Diplomacy.Sign"/>: when the aggressor has taken its
	/// goals, the defender's navy is gone, a year has passed, or neither side has had a fleet in the field for a season.
	/// Every system where one side now holds the most populous settlement, taken from the other, is ceded to it: the
	/// loser's other settlements and its gates there go with it. Whatever else was occupied goes back, gates taken
	/// elsewhere go back to their owners, the fleets go home, and the two keep the peace for <see cref="TruceDays"/>.
	/// </summary>
	class SPACEGODCORE_API InterstellarWars final
	{
	public:
		/// <summary>C#'s (Conflict War, Faction Before).</summary>
		struct TakenGate
		{
			cs::Ref<Conflict> War;
			cs::Ref<Faction> Before;
		};

	private:
		cs::Ref<::SpaceGod::Core::Galaxy> galaxy;
		cs::Random rng;
		cs::List<cs::Ref<Conflict>> wars = cs::List<cs::Ref<Conflict>>();
		cs::List<cs::Ref<Conflict>> ended = cs::List<cs::Ref<Conflict>>();
		cs::List<cs::Ref<Campaign>> campaigns = cs::List<cs::Ref<Campaign>>();
		cs::List<cs::Ref<Landing>> landings = cs::List<cs::Ref<Landing>>();

		/// <summary>
		/// Gates taken in a war still being fought, with who held them before and which war took them (a treaty that
		/// does not cede their system gives them back). Kept per war, not just per gate (U.SPC-T323): two overlapping
		/// wars can each force the same gate in turn (the second declared by hand, past the one-war-per-faction rule
		/// a plain <see cref="Declare"/> enforces), and the first of them to make peace must leave the gate alone once
		/// it is the other war's to answer for, rather than handing it back to a now-stale "before".
		/// </summary>
		cs::Dictionary<cs::Ref<JumpGate>, TakenGate> takenGates = cs::Dictionary<cs::Ref<JumpGate>, TakenGate>();

		double nextDailyJd = 0, nextDeclareJd = 0;

	public:
		/// <summary>Whether states go to war of their own accord (see <see cref="Declare"/>). A scripted war turns it off.</summary>
		bool AutoDeclare = true;

		/// <summary>Wars being fought, and those ended (with their treaties).</summary>
		const cs::List<cs::Ref<Conflict>>& Active() const { return wars; }
		const cs::List<cs::Ref<Conflict>>& Ended() const { return ended; }

		/// <summary>Every campaign sent so far, in the field or over.</summary>
		const cs::List<cs::Ref<Campaign>>& Campaigns() const { return campaigns; }

		/// <summary>Marines on the ground, fighting garrisons.</summary>
		const cs::List<cs::Ref<Landing>>& Landings() const { return landings; }

		/// <summary>The war fleets' supply: their stores, naval bases and supply convoys (U.SPC-T93).</summary>
		cs::Ref<MilitaryLogistics> Logistics;

		/// <summary>Troops a settlement's own garrison musters, per person living there.</summary>
		static constexpr double GarrisonPerPerson = 0.001;

		/// <summary>The smallest garrison anywhere settled.</summary>
		static constexpr double MinGarrison = 15;

		/// <summary>Days a war fleet holds a gate whose forts it has broken before it is theirs.</summary>
		static constexpr double GateHoldDays = 2;

		/// <summary>A day's ground fighting: the share of its own strength each side's troops kill of the other's.</summary>
		static constexpr double GroundRate = 0.3;

		/// <summary>How much a garrison fighting from its own works is worth, man for man, over marines coming down on it.</summary>
		static constexpr double DugIn = 1.5;

		/// <summary>Unrest in a settlement just occupied.</summary>
		static constexpr double OccupationUnrest = 0.8;

		/// <summary>Marines go down past defending warships only while the fleet overhead has this many times their strength (inverted: their share of it).</summary>
		static constexpr double LandingCover = 0.5;

		/// <summary>A war fleet sets out only with this much more strength than it expects to meet (gate forts, platforms, warships).</summary>
		static constexpr double LaunchRatio = 1.2;

		/// <summary>A war is ended by a treaty after this long whatever stands (days).</summary>
		static constexpr double MaxWarDays = 365;

		/// <summary>Neither side with a fleet in the field for this long: a stalemate, and a peace (days).</summary>
		static constexpr double StalemateDays = 90;

		/// <summary>How long a treaty keeps the peace between its two sides (days).</summary>
		static constexpr double TruceDays = 730;

		/// <summary>A state goes to war of its own accord only with a navy this many times the neighbour's.</summary>
		static constexpr double DeclareRatio = 3;

		/// <summary>Monthly chance that a state able to (see <see cref="DeclareRatio"/>) goes to war, when no war is being fought.</summary>
		double DeclareChancePerMonth = 0.12;

		InterstellarWars(cs::Ref<::SpaceGod::Core::Galaxy> galaxy, double jd, int seed = 9203);

	private:
		cs::Ref<::SpaceGod::Core::Polities> Polities() const;
		cs::Ref<::SpaceGod::Core::Diplomacy> Diplomacy() const;
		cs::Ref<InterstellarTraffic> Traffic() const;

	public:
		/// <summary>The war being fought between <paramref name="a"/> and <paramref name="b"/>, or null.</summary>
		cs::Ref<Conflict> Between(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>The war <paramref name="f"/> is fighting, or null.</summary>
		cs::Ref<Conflict> Of(const cs::Ref<Faction>& f) const;

		// ---------------------------------------------------------------- going to war

		/// <summary>
		/// <paramref name="aggressor"/> goes to war with <paramref name="defender"/> (they are at war in the one diplomacy table
		/// every system shares) to take <paramref name="goals"/>: systems where the defender holds settlements. None given,
		/// it goes for every system the defender holds. Null if either is no state, they are bound by a treaty's truce, or
		/// either is already fighting such a war.
		/// </summary>
		cs::Ref<Conflict> Declare(const cs::Ref<Faction>& aggressor, const cs::Ref<Faction>& defender, double jd, cs::List<cs::Ref<StarSystem>> goals = {});

	private:
		static cs::String Names(const cs::List<cs::Ref<StarSystem>>& systems);

		/// <summary>The worlds where either side holds anything, or a campaign of the war is.</summary>
		cs::List<cs::Ref<World>> Involved(const cs::Ref<Conflict>& w) const;

		/// <summary>Whether <paramref name="f"/> still holds a standing settlement (not a haven) in <paramref name="s"/>.</summary>
		static bool EnemyHolds(const cs::Ref<Faction>& f, const cs::Ref<StarSystem>& s);

	public:
		// ---------------------------------------------------------------- the day

		void Update(double jd);

	private:
		void Daily(double jd);

		// ---------------------------------------------------------------- campaigns

		/// <summary>The systems <paramref name="f"/> is fighting for in <paramref name="w"/>: the aggressor its goals still
		/// held against it, the defender those where it has lost settlements to the aggressor.</summary>
		cs::List<cs::Ref<StarSystem>> GoalsOf(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f) const;

		/// <summary>
		/// A day of <paramref name="f"/>'s strategic command in <paramref name="w"/> (U.SPC-T474, see <see cref="FleetDoctrine"/>):
		/// with no gate fleet in the field, its best one marches on its next goal; with no strike group out, its best jump
		/// strike group is given the mission its objectives call for. Neither goes if it would leave a system of its own
		/// short of the reserve it keeps against the enemy's strike groups.
		/// </summary>
		void Plan(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd);

		/// <summary>Sends <paramref name="f"/>'s best gate fleet fit to go on a campaign for its next goal, when the group is strong
		/// enough for what it expects to meet (with the <paramref name="doctrine"/> off, its best group of any kind).</summary>
		void PlanGateFleet(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd, bool doctrine, double threat);

		/// <summary>
		/// Gives <paramref name="f"/>'s best idle jump strike group the mission its objectives call for (U.SPC-T474), in this
		/// order: relieve one of its own systems the enemy is attacking; open a second front against a goal the gate fleet is not
		/// marching on (or, with no gate fleet out, jump on the nearest goal itself); else raid the nearest enemy system with a
		/// port it outguns. Each only within <see cref="FleetDoctrine.StrikeRangeLy"/> under its own drive and against what it
		/// outweighs there, the gates' forts left behind.
		/// </summary>
		void PlanStrike(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd, double threat);

		/// <summary>Sends <paramref name="leader"/>'s jump strike group from <paramref name="from"/> on <paramref name="mission"/>
		/// against <paramref name="goal"/> in <paramref name="w"/>, under its own drives. Null if it cannot go.</summary>
		cs::Ref<Campaign> LaunchStrike(const cs::Ref<Conflict>& w, const cs::Ref<Ship>& leader, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& goal, CampaignMission mission, double jd);

		/// <summary>The strength of <paramref name="enemy"/>'s warships loose in <paramref name="world"/>, where <paramref name="f"/>
		/// holds a port (at least a token when only its marines are down on one). 0 when nobody is attacking it there.</summary>
		double Attacking(const cs::Ref<World>& world, const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy) const;

		/// <summary>Whether <paramref name="s"/> is a warship of <paramref name="enemy"/>'s that fights: not a picket, not a hulk.</summary>
		static bool EnemyWarship(const cs::Ref<Ship>& s, const cs::Ref<Faction>& enemy);

		/// <summary>What <paramref name="enemy"/> holds <paramref name="goal"/> with: its platforms and warships there (a strike
		/// group jumping in meets no gate forts).</summary>
		static double DefenceOf(const cs::Ref<Faction>& enemy, const cs::Ref<StarSystem>& goal);

		/// <summary>The port of <paramref name="enemy"/>'s in <paramref name="world"/> a raid of <paramref name="strength"/> falls on
		/// next: the most worth breaking (people, a yard) for what defends it, of those it outguns whose defences still stand
		/// (or that still have people to frighten off), none of <paramref name="done"/>. Null when there is none.</summary>
		static cs::Ref<Settlement> RaidTarget(const cs::Ref<World>& world, const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy, double strength, const cs::List<cs::Ref<Settlement>>* done);

	public:
		/// <summary>
		/// Sends <paramref name="leader"/>'s group (idle in <paramref name="from"/>) on a campaign to take <paramref name="goal"/>,
		/// on its polity's orders (U.SPC-T423, see <see cref="StrategicCommand"/>): the war it is fighting against whoever holds
		/// ports there. Null if it is at war with nobody there or there is no way to fight through.
		/// </summary>
		cs::Ref<Campaign> Launch(const cs::Ref<Ship>& leader, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& goal, double jd);

	private:
		/// <summary>A group <paramref name="f"/> can send: a sound, armed flagship of its navy idle in port, not its home guard,
		/// with marines to land.</summary>
		static bool Fit(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f);

		/// <summary>A group <paramref name="f"/> can send on a raid or a relief (U.SPC-T474): as <see cref="Fit"/>, marines or none.</summary>
		static bool FitStrike(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f);

		static double GroupMarines(const cs::Ref<World>& world, const cs::Ref<Ship>& leader);

		/// <summary>
		/// The hardest fight a campaign of <paramref name="f"/> expects on the way to and in <paramref name="goal"/>, by
		/// strength: the forts of any one enemy gate on <paramref name="route"/>, or the platforms and warships the enemy
		/// has in the goal system. They are met one after another, so it is the biggest of them that the fleet must outweigh.
		/// </summary>
		double Expected(const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy, const cs::Ref<StarSystem>& goal, const cs::List<cs::Ref<JumpGate>>& route) const;

		/// <summary>C#'s (StarSystem system, Ship ship).</summary>
		struct Located
		{
			cs::Ref<StarSystem> system;
			cs::Ref<Ship> ship;
		};

		/// <summary>Where a campaign's flagship is: its system and hull, or (null, null) inside a gate or gone.</summary>
		Located Locate(const cs::Ref<Campaign>& c) const;

		bool InGate(const cs::Ref<Campaign>& c) const;

		/// <summary>Whether a campaign's group is out between the stars under its own drives (a jump strike group, U.SPC-T474).</summary>
		bool InCruise(const cs::Ref<Campaign>& c) const;

		/// <summary>A day of a campaign: at an enemy gate, fight it or hold it; in the goal system, reduce and take each enemy settlement.</summary>
		void Step(const cs::Ref<Campaign>& c, double jd);

		/// <summary>
		/// A day of a jump strike group's relief or raid in the system it was sent to (U.SPC-T474). A relief makes for the
		/// port of its own the enemy is at (else wherever the enemy's warships loose in the system are) and fights them off,
		/// and at a port of its own with the orbit clear puts its marines down to stiffen the garrison; with nobody left
		/// attacking, it goes home. A raid falls on the enemy's ports it outguns one after another, breaking their defences,
		/// and jumps out for home when it has none left. Either jumps home when its time is up
		/// (<see cref="FleetDoctrine.StrikeDays"/>), before the enemy can gather.
		/// </summary>
		void StrikeStep(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>C#'s (int Body, Settlement Port).</summary>
		struct StrikeMark
		{
			int Body = 0;
			cs::Ref<Settlement> Port;
		};

		/// <summary>
		/// Where a strike group on a relief or a raid makes for next in <paramref name="system"/>: the body, and the port there
		/// (null when it is going after the enemy's warships away from any port of its own); -1 when it has nothing left to do.
		/// </summary>
		StrikeMark StrikeObjective(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd) const;

		static double GroupDamage(const cs::Ref<World>& world, const cs::Ref<Ship>& leader);

		/// <summary>A war fleet beside an enemy gate: fights its forts (and whatever warships hold it), then holds it until it is taken.</summary>
		void Siege(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>Enemy warships holding beside <paramref name="gate"/>.</summary>
		static bool DefendersAt(const cs::Ref<World>& world, const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f);

		/// <summary>A war fleet's group: its leader and the ships keeping station on it.</summary>
		static cs::List<cs::Ref<Ship>> Group(const cs::Ref<World>& world, const cs::Ref<Ship>& leader);

		void TakeGate(const cs::Ref<Campaign>& c, const cs::Ref<JumpGate>& gate, double jd) { SeizeGate(c, gate, jd, false); }

		/// <summary>Takes <paramref name="gate"/> for the campaign; <paramref name="boarded"/> when its boarding shuttles put the
		/// marines aboard (U.SPC-T486); <paramref name="breaker"/>, the gate breaker whose boarding and engineering teams stormed
		/// it, its engineers making good the damage the ring has taken (U.SPC-T478).</summary>
		void SeizeGate(const cs::Ref<Campaign>& c, const cs::Ref<JumpGate>& gate, double jd, bool boarded, const cs::Ref<Ship>& breaker = nullptr);

	public:
		/// <summary>
		/// A war fleet has come out beside <paramref name="gate"/> in <paramref name="system"/> (U.SPC-T92): if the gate is held
		/// against it, it holds there to fight the forts and take the gate before going on (true); else it goes on (false).
		/// </summary>
		bool OnEmerged(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>Whether a war fleet on <paramref name="v"/> must take <paramref name="gate"/> (in the system it is in) before
		/// going through: it is the enemy's, and armed, or the fleet is advancing and secures its way as it goes.</summary>
		bool MustTake(const cs::Ref<JumpGate>& gate, const cs::Ref<Voyage>& v) const;

		/// <summary>
		/// Where a war fleet on campaign goes next in <paramref name="system"/> (the voyage hook, see
		/// <see cref="World.VoyageOrders"/>): it stays beside a gate it is taking, and in its goal system makes for the
		/// settlement it is reducing. -1 leaves it to its own navy's orders.
		/// </summary>
		int Orders(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, int here, double jd);

	private:
		/// <summary>The enemy settlement a campaign reduces next in its goal system: the most populous it has not yet taken.</summary>
		static cs::Ref<Settlement> Objective(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system);

		/// <summary>Its goal taken: on to the next, or it stays to hold the system until the peace.</summary>
		void Won(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Calls a campaign home.</summary>
		void Recall(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd, const cs::String& why);

		void Finish(const cs::Ref<Campaign>& c, double jd);

	public:
		// ---------------------------------------------------------------- marines and garrisons

		/// <summary>A settlement's garrison, mustered from its people the first time a war asks.</summary>
		static double GarrisonOf(const cs::Ref<Settlement>& s);

		/// <summary>What its own people keep under arms: <see cref="GarrisonPerPerson"/> of them, at least <see cref="MinGarrison"/>.</summary>
		static double FullGarrison(const cs::Ref<Settlement>& s);

	private:
		/// <summary>The orbit over <paramref name="target"/> is clear: every marine the fleet has goes down.</summary>
		void Land(const cs::Ref<Campaign>& c, const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Settlement>& target, double jd);

		/// <summary>A day's fighting on the ground at every settlement marines have landed on.</summary>
		void GroundWar(double jd);

		/// <summary>The garrison beaten: the settlement changes hands, the marines its garrison, its people restive.</summary>
		void Occupy(const cs::Ref<Landing>& l, const cs::Ref<World>& world, double jd);

		/// <summary>
		/// Garrisons and unrest, everywhere a war has asked about them: a garrison of its own people rebuilds to strength;
		/// unrest wears down, faster under a strong garrison; and while the war is fought, a restive settlement whose
		/// occupier's garrison has grown thin may rise and let its old owner back in.
		/// </summary>
		void Garrisons(double jd);

		bool UnderAssault(const cs::Ref<Settlement>& s) const;

		/// <summary>Gate forts rebuilt by their owners, a fort every few weeks while the treasury can pay, when nobody is fighting there.</summary>
		void RebuildForts(double jd);

		// ---------------------------------------------------------------- peace

		/// <summary>Whether a war is over: its goals taken, the defender's navy gone, too long fought, or stalled.</summary>
		void Judge(const cs::Ref<Conflict>& w, double jd);

		/// <summary>Settlements <paramref name="by"/> holds that it took from <paramref name="from"/>.</summary>
		int Occupied(const cs::Ref<Faction>& by, const cs::Ref<Faction>& from) const;

		/// <summary>
		/// Ends a war in a treaty: each system where one side now holds the most populous settlement, having taken it (or
		/// another there) from the other, is ceded to it, with every settlement and gate the other still had there; all else
		/// occupied, and every gate taken elsewhere, goes back; the fleets go home.
		/// </summary>
		void Settle(const cs::Ref<Conflict>& w, double jd, const cs::String& why);

		// ---------------------------------------------------------------- ambition

		/// <summary>
		/// Now and then a state whose navy far outweighs a neighbour's (one a gate away from a system where it holds
		/// settlements) goes to war with it for that system, when no other war is being fought.
		/// </summary>
		void Ambitions(double jd);

		static bool Warlike(const cs::Ref<Faction>& f);

		double NavyStrength(const cs::Ref<Faction>& f) const;

		cs::Ref<StarSystem> SystemOf(const cs::Ref<World>& w) const;
	private:
		InterstellarWars() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(InterstellarWars); // ObjectGraph registration (Persistence/Registrations)
	};
}
