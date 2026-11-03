#pragma once
// Port of Unity.SpaceGod Core/Systems/StrategicCommand.cs (US-T15, see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;
	class Campaign;
	class Polities;
	class Galaxy;
	class StarSystem;
	class Settlement;
	class GroupOrder;
	class BodyCatalog;
	class RangeSetting;
	enum class IndustryKind : int;
	enum class ShipClass : int;

	/// <summary>What a polity's command sends a fleet group to do (U.SPC-T423, see <see cref="StrategicCommand"/>).</summary>
	enum class ObjectiveKind : int
	{
		/// <summary>Take an enemy port (or every enemy port in a system): break its defences and besiege it until it falls, or storm a
		/// raider haven. Once it is taken the group holds it.</summary>
		Take,

		/// <summary>Hold a port or place: stay there and fight anything hostile that comes.</summary>
		Hold,

		/// <summary>Blockade an enemy port: lie over it so no ship may dock, and fight what comes to break it.</summary>
		Blockade,

		/// <summary>Shadow one ship (a freighter, a convoy, the player's own hull) wherever it goes, across the gates too.</summary>
		Escort,

		/// <summary>Visit every world of a system in turn, then stand down.</summary>
		Explore,
	};
	const char* ToString(ObjectiveKind v);

	/// <summary>How many of a polity's groups one command reaches: one squadron, every group in a system, or the whole navy.</summary>
	enum class CommandScope : int
	{
		Group,
		System,
		Polity,
	};
	const char* ToString(CommandScope v);

	/// <summary>An objective a polity's command has given one of its fleet groups (U.SPC-T423).</summary>
	class FleetObjective final
	{
	public:
		ObjectiveKind Kind = static_cast<ObjectiveKind>(0);

		/// <summary>The system it is to be carried out in (its world).</summary>
		cs::Ref<World> Where;

		/// <summary>The port or place, or -1 for the whole system (Take: every enemy port there; Explore: every world).</summary>
		int Body = -1;

		/// <summary>The ship an Escort shadows.</summary>
		cs::Ref<Ship> Charge;

		double SinceJd = 0;

		/// <summary>Worlds an Explore has been to.</summary>
		cs::List<int> Visited = cs::List<int>();

		/// <summary>The campaign a Take in another system at war was handed to (see <see cref="InterstellarWars.Launch"/>), or null.</summary>
		cs::Ref<::SpaceGod::Core::Campaign> Campaign;
	};

	/// <summary>
	/// What a polity's industry is pointed at (U.SPC-T423): the industries it favours grow on thinner margins and are kept
	/// up through a glut, so a war economy turns out alloys, electronics and munitions and a civil one food and medicine.
	/// A new focus is one <see cref="Register"/> call.
	/// </summary>
	class ProductionFocus final
	{
	public:
		static const cs::List<cs::Ref<ProductionFocus>>& All();

		cs::String Name, Description;

		static cs::Ref<ProductionFocus> Register(cs::String name, cs::String description, cs::List<IndustryKind> favoured = {});

		bool Favours(IndustryKind k) const;

		static cs::Ref<ProductionFocus> Find(const cs::String& name);

		static const cs::Ref<ProductionFocus>& Balanced();

		static const cs::Ref<ProductionFocus>& War();

		static const cs::Ref<ProductionFocus>& Civil();

		static const cs::Ref<ProductionFocus>& Extraction();

		cs::String ToString() const { return Name; }

	private:
		cs::List<IndustryKind> favoured;

		ProductionFocus(cs::String name, cs::String description, cs::List<IndustryKind> favoured);

		// C# initialises the type's statics in textual order the first time any is touched: `all`, then each Register below.
		static cs::List<cs::Ref<ProductionFocus>>& all();
		struct Statics;
		static const Statics& statics();
		static cs::Ref<ProductionFocus> Add(cs::String name, cs::String description, cs::List<IndustryKind> favoured);
	};

	/// <summary>
	/// What a polity's yards lay down for its navy (U.SPC-T423): the order of hulls they cycle through and how big a navy
	/// they keep (a share of the system's cap). A plan with no hulls leaves the slips to the merchant fleet. A new plan is
	/// one <see cref="Register"/> call.
	/// </summary>
	class BuildPlan final
	{
	public:
		static const cs::List<cs::Ref<BuildPlan>>& All();

		cs::String Name, Description;

		/// <summary>The hulls laid down in turn; empty for none.</summary>
		cs::List<ShipClass> Cycle;

		/// <summary>The navy kept, as a share of the system's cap (<see cref="World.NavyCap"/>).</summary>
		double NavyShare = 0;

		/// <summary>
		/// Heavy equipment the plan wants in every group its yards build up (U.SPC-T507): facility kind ids, each fitted to a
		/// newly launched hull able to take it whose group does not carry one yet (see <see cref="War.LaunchWarship"/>). Lane
		/// security fits a field projector (<see cref="FacilityKinds.FieldProjector"/>), making a lane interdictor of a
		/// destroyer or cruiser rather than laying down a class of its own. Empty for most plans.
		/// </summary>
		cs::List<cs::String> GroupFittings = cs::List<cs::String>();

		static cs::Ref<BuildPlan> Register(cs::String name, cs::String description, double navyShare, cs::List<ShipClass> cycle = {});

		static cs::Ref<BuildPlan> Find(const cs::String& name);

		/// <summary>This plan, with <paramref name="facilities"/> fitted to one hull of every group it builds up.</summary>
		cs::Ref<BuildPlan> WithGroupFittings(cs::List<cs::String> facilities);

		/// <summary>What every navy builds unless told otherwise: a command cruiser among them to lead a group (U.SPC-T477).</summary>
		static const cs::Ref<BuildPlan>& Balanced();

		static const cs::Ref<BuildPlan>& BattleLine();

		/// <summary>Carriers, their screens and the frigates that ride clamped to their hulls (U.SPC-T475).</summary>
		static const cs::Ref<BuildPlan>& Carriers();

		/// <summary>Strike flotillas (U.SPC-T475): frigates that alpha-strike the enemy's capitals, with destroyers to screen them.</summary>
		static const cs::Ref<BuildPlan>& Strike();

		static const cs::Ref<BuildPlan>& Screens();

		/// <summary>Lane security (U.SPC-T479): destroyers and cruisers, one hull of every group fitted with a field projector
		/// as its lane interdictor (U.SPC-T507), the rest to fight what it pulls out: groups that hold pickets on the
		/// hyperlanes raiders work.</summary>
		static const cs::Ref<BuildPlan>& LaneSecurity();

		static const cs::Ref<BuildPlan>& Mobilise();

		static const cs::Ref<BuildPlan>& Merchant();

		cs::String ToString() const { return Name; }

	private:
		BuildPlan(cs::String name, cs::String description, cs::List<ShipClass> cycle, double navyShare);

		static cs::List<cs::Ref<BuildPlan>>& all();
		struct Statics;
		static const Statics& statics();
		static cs::Ref<BuildPlan> Add(cs::String name, cs::String description, double navyShare, cs::List<ShipClass> cycle);
	};

	/// <summary>
	/// A line of research a polity can put its people to (U.SPC-T423): each level costs more than the last, and what it
	/// does is its own (<see cref="OnLevel"/>, or read by the code it improves through <see cref="StrategicCommand.Level"/>).
	/// A new track is one <see cref="Register"/> call.
	/// </summary>
	class ResearchTrack final
	{
	public:
		static const cs::List<cs::Ref<ResearchTrack>>& All();

		cs::String Name, Description;

		/// <summary>Points the first level costs; level n costs n times this.</summary>
		double BaseCost = 0;

		int MaxLevel = 0;

		/// <summary>What reaching a level does to the polity at once (null: its effect is read where it applies).</summary>
		cs::Action<cs::Ref<Faction>, int> OnLevel;

		static cs::Ref<ResearchTrack> Register(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel = nullptr);

		static cs::Ref<ResearchTrack> Find(const cs::String& name);

		/// <summary>Points the next level after <paramref name="level"/> costs.</summary>
		double CostOf(int level) const { return BaseCost * (level + 1); }

		/// <summary>Marine quality per level.</summary>
		static constexpr double MarinesPerLevel = 0.1;

		/// <summary>Navy cap per level of shipyards (a share of it).</summary>
		static constexpr double YardsPerLevel = 0.1;

		/// <summary>Extra facility growth a review per level of industry, for the favoured industries.</summary>
		static constexpr double IndustryPerLevel = 0.005;

		static const cs::Ref<ResearchTrack>& Marines();

		static const cs::Ref<ResearchTrack>& Shipyards();

		static const cs::Ref<ResearchTrack>& Industry();

		cs::String ToString() const { return Name; }

	private:
		ResearchTrack(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel);

		static cs::List<cs::Ref<ResearchTrack>>& all();
		struct Statics;
		static const Statics& statics();
		static cs::Ref<ResearchTrack> Add(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel);
	};

	/// <summary>One polity's standing directives (U.SPC-T423): its production, build and research priorities and its research so far.</summary>
	class PolityDirectives final
	{
	public:
		cs::Ref<::SpaceGod::Core::Faction> Faction;
		cs::Ref<ProductionFocus> Production = ProductionFocus::Balanced();
		cs::Ref<BuildPlan> Build = BuildPlan::Balanced();

		/// <summary>What its people are researching, or null for nothing.</summary>
		cs::Ref<ResearchTrack> Research;

		/// <summary>Points put into the next level of <see cref="Research"/>.</summary>
		double ResearchPoints = 0;

		cs::Dictionary<cs::Ref<ResearchTrack>, int> Levels = cs::Dictionary<cs::Ref<ResearchTrack>, int>();

		explicit PolityDirectives(cs::Ref<::SpaceGod::Core::Faction> faction) { Faction = faction; }

		int Level(const cs::Ref<ResearchTrack>& t) const;
	};

	/// <summary>One fleet group in a polity's command report: where it is, what it is doing and what it was told to do.</summary>
	struct GroupLine
	{
		cs::Ref<Ship> Leader;
		cs::Ref<World> Where;
		cs::String System, Order, Objective;
		int Hulls = 0;
		double Strength = 0;

		GroupLine() = default;
		GroupLine(cs::Ref<Ship> leader, cs::Ref<World> where, cs::String system, cs::String order, cs::String objective, int hulls, double strength)
		{
			Leader = leader;
			Where = where;
			System = system;
			Order = order;
			Objective = objective;
			Hulls = hulls;
			Strength = strength;
		}
	};

	/// <summary>A polity at a glance (U.SPC-T423): its holdings, people and money, its navy and its wars, as the strategy layer shows it.</summary>
	class PolityReport final
	{
	public:
		cs::Ref<::SpaceGod::Core::Faction> Faction;
		int Systems = 0, Ports = 0, Warships = 0, Groups = 0, Commanded = 0, Campaigns = 0;

		/// <summary>Its groups by kind of force (U.SPC-T474, see <see cref="FleetDoctrine"/>): gate fleets and jump strike groups.</summary>
		int GateFleets = 0, StrikeGroups = 0;
		double Population = 0, Treasury = 0, Strength = 0, Unrest = 0, MarineQuality = 0, Morale = 0, CrewLost = 0;
		cs::List<cs::Ref<::SpaceGod::Core::Faction>> AtWarWith = cs::List<cs::Ref<::SpaceGod::Core::Faction>>();
		cs::List<GroupLine> Fleet = cs::List<GroupLine>();
	};

	/// <summary>
	/// The strategy layer (U.SPC-T423): a polity commanded from the galaxy map. Its production focus steers which of its
	/// industries grow (<see cref="ProductionFocus"/>), its build plan what its yards lay down and how big a navy they keep
	/// (<see cref="BuildPlan"/>), and its research (<see cref="ResearchTrack"/>) accrues from its people day by day. Its fleet
	/// groups can be given objectives (take, hold, blockade, escort, explore) one squadron at a time, every group in a
	/// system, or the whole navy at once; the faction command layer (<see cref="War"/>, U.SPC-T374) carries them out ahead of
	/// its own plans, refitting first when a group is hurt, and sends a group whose objective lies in another system there
	/// through the gates (a Take in a system it is at war with goes as a campaign, <see cref="InterstellarWars"/>). Every
	/// polity runs as before until it is given a directive: these are the player's levers, not a second AI.
	/// </summary>
	// [SettingsSource]
	class StrategicCommand final
	{
	public:
		/// <summary>Economy hook result: C#'s (bool Favoured, double Bonus).</summary>
		struct PriorityResult
		{
			bool Favoured = false;
			double Bonus = 0;
		};

		/// <summary>One of <see cref="Groups"/>: C#'s (Ship Leader, World Where).</summary>
		struct GroupAt
		{
			cs::Ref<Ship> Leader;
			cs::Ref<World> Where;
		};

	private:
		cs::Ref<::SpaceGod::Core::Polities> polities;
		cs::Dictionary<cs::Ref<Faction>, cs::Ref<PolityDirectives>> directives = cs::Dictionary<cs::Ref<Faction>, cs::Ref<PolityDirectives>>();
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<FleetObjective>> objectives = cs::Dictionary<cs::Ref<Ship>, cs::Ref<FleetObjective>>();
		double nextDailyJd = cs::Double::NaN;

	public:
		/// <summary>The galaxy whose gates and wars carry a group to another system (null for a system on its own).</summary>
		cs::Ref<::SpaceGod::Core::Galaxy> Galaxy;

		/// <summary>The polity the player commands from the galaxy map, or null.</summary>
		cs::Ref<Faction> Player;

		/// <summary>The settings menu's research pace (Civilizations): 1 is the standard pace, 0 stops research.</summary>
		static const cs::Ref<RangeSetting>& ResearchPace();

		/// <summary>Research points a day a polity of a million people turns out at the standard pace (it grows as the square root of its people).</summary>
		static constexpr double PointsPerMillion = 10;

		explicit StrategicCommand(cs::Ref<::SpaceGod::Core::Polities> polities) { this->polities = polities; }

		// ---------------------------------------------------------------- directives

		/// <summary><paramref name="f"/>'s directives (made the first time they are asked for).</summary>
		cs::Ref<PolityDirectives> Of(const cs::Ref<Faction>& f);

		/// <summary><paramref name="f"/>'s directives, or null if it has never been given any.</summary>
		cs::Ref<PolityDirectives> Find(const cs::Ref<Faction>& f) const;

		void SetProduction(const cs::Ref<Faction>& f, const cs::Ref<ProductionFocus>& focus);

		void SetBuild(const cs::Ref<Faction>& f, const cs::Ref<BuildPlan>& plan);

		/// <summary>Puts <paramref name="f"/>'s people to <paramref name="track"/> (null: nothing). Points toward another track's next level are lost.</summary>
		void SetResearch(const cs::Ref<Faction>& f, const cs::Ref<ResearchTrack>& track);

		/// <summary>The level <paramref name="f"/> has reached in <paramref name="track"/>.</summary>
		int Level(const cs::Ref<Faction>& f, const cs::Ref<ResearchTrack>& track) const;

		/// <summary>What <paramref name="f"/>'s yards build (the balanced fleet unless told otherwise).</summary>
		cs::Ref<BuildPlan> BuildPlanOf(const cs::Ref<Faction>& f) const;

		/// <summary>The navy <paramref name="f"/> keeps, as a share of a system's cap: its build plan's share, raised by its shipyards research.</summary>
		double NavyShare(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// Economy hook: whether <paramref name="owner"/>'s production focus favours <paramref name="kind"/> (it grows on thinner
		/// margins and is kept up through a glut) and the extra growth its industry research gives a favoured industry.
		/// </summary>
		PriorityResult Priority(const cs::Ref<Faction>& owner, IndustryKind kind) const;

		// ---------------------------------------------------------------- objectives

		/// <summary>The objective <paramref name="leader"/>'s group holds, or null.</summary>
		cs::Ref<FleetObjective> ObjectiveOf(const cs::Ref<Ship>& leader) const;

		/// <summary>Whether <paramref name="leader"/>'s group has an objective (in <paramref name="where"/>, if given).</summary>
		bool Holds(const cs::Ref<Ship>& leader, const cs::Ref<World>& where = nullptr) const;

		int ObjectiveCount() const { return objectives.Count(); }

		/// <summary>The groups <paramref name="f"/> commands at <paramref name="scope"/>: <paramref name="group"/>'s alone, every
		/// one of its groups in <paramref name="system"/>, or every one it has anywhere (not those inside a gate).</summary>
		cs::List<GroupAt> Groups(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group = nullptr, const cs::Ref<World>& system = nullptr) const;

		/// <summary>The world <paramref name="ship"/> is in, or null (inside a gate, or gone).</summary>
		cs::Ref<World> WorldOf(const cs::Ref<Ship>& ship) const;

		/// <summary>The star system <paramref name="w"/> is, when there is a galaxy.</summary>
		cs::Ref<StarSystem> SystemOf(const cs::Ref<World>& w) const;

		/// <summary>
		/// Gives <paramref name="kind"/> in <paramref name="target"/> to <paramref name="f"/>'s groups at <paramref name="scope"/>
		/// (see <see cref="Groups"/>). <paramref name="body"/> picks the port or place (-1: command picks: every enemy port for a
		/// Take, the busiest enemy port for a Blockade, its own biggest port for a Hold, every world for an Explore);
		/// <paramref name="charge"/> is the ship an Escort shadows (null: its own biggest merchant hull there). Returns null and
		/// how many groups took it, or why it cannot be given.
		/// </summary>
		cs::String Order(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group, const cs::Ref<World>& system, ObjectiveKind kind, const cs::Ref<World>& target, double jd, int& given,
			int body = -1, cs::Ref<Ship> charge = nullptr);

		/// <summary>Clears every objective of <paramref name="f"/>'s groups at <paramref name="scope"/>: they go back to their own command's plans.</summary>
		int Clear(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group = nullptr, const cs::Ref<World>& system = nullptr);

		/// <summary>Whether <paramref name="f"/> may take <paramref name="s"/>: a standing port of a state it is at war with, or a raider haven.</summary>
		bool Enemy(const cs::Ref<Faction>& f, const cs::Ref<Settlement>& s) const;

		/// <summary>Whether <paramref name="f"/> may blockade <paramref name="s"/>: a standing port (not a haven) of someone hostile to it.</summary>
		bool Blockadable(const cs::Ref<Faction>& f, const cs::Ref<Settlement>& s) const;

		/// <summary>Ends <paramref name="leader"/>'s objective, logging why: the group goes back to its own command's plans.</summary>
		void Complete(const cs::Ref<Ship>& leader, const cs::Ref<World>& where, double jd, const cs::String& why);

		/// <summary>A Take achieved: the group holds what it took.</summary>
		void Taken(const cs::Ref<Ship>& leader, const cs::Ref<World>& where, int body, double jd);

		/// <summary>A flagship lost (or handed over): its heir carries the group's objective on.</summary>
		void OnSuccession(const cs::Ref<Ship>& dead, const cs::Ref<Ship>& heir);

		// ---------------------------------------------------------------- the day

		/// <summary>Once a day: research accrues, objectives whose group is gone lapse, and groups bound for another system are sent.</summary>
		void Update(double jd);

		// ---------------------------------------------------------------- the picture

		/// <summary>What <paramref name="kind"/> in <paramref name="w"/> means, in words.</summary>
		cs::String Describe(ObjectiveKind kind, const cs::Ref<World>& w, int body, const cs::Ref<Ship>& charge) const;

		cs::String Describe(const cs::Ref<FleetObjective>& o) const;

		/// <summary>What a group's current order is, in words.</summary>
		static cs::String DescribeOrder(const cs::Ref<GroupOrder>& o, const cs::Ref<BodyCatalog>& catalog);

		/// <summary><paramref name="f"/> at a glance: what it holds, its people, money and navy, its wars, and every group's orders.</summary>
		cs::Ref<PolityReport> Report(const cs::Ref<Faction>& f, double jd) const;

	private:
		/// <summary>A group <paramref name="f"/>'s command can give an objective: its own navy group's flagship, flying (not the helm's,
		/// not a supply convoy, not a corvette stationed for good at its port, U.SPC-T476).</summary>
		static bool Commandable(const cs::Ref<World>& w, const cs::Ref<Ship>& s, const cs::Ref<Faction>& f);

		cs::String NameOf(const cs::Ref<World>& w) const;

		cs::String Resolve(const cs::Ref<Faction>& f, ObjectiveKind kind, const cs::Ref<World>& w, int& body, cs::Ref<Ship>& charge);

		void Daily(double jd);

		bool AnyEnemy(const cs::Ref<Faction>& f, const cs::Ref<World>& w, int body) const;

		void Research(const cs::Ref<PolityDirectives>& d, double jd);

		/// <summary>Sends a group whose objective lies in another system there: as a campaign if it is to take a system it is at
		/// war over, else through the open gates (or under its own drive).</summary>
		void Dispatch(const cs::Ref<Ship>& leader, const cs::Ref<World>& from, const cs::Ref<FleetObjective>& o, double jd);
	};
}
