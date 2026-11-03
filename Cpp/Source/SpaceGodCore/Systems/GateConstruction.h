#pragma once
// Port of Assets/UnitySpace/Core/Systems/GateConstruction.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class StarSystem;
	class SystemSpec;
	class JumpGate;
	class Ship;
	class Galaxy;
	class Settlement;
	class World;
	class GateWork;
	struct DVec3;
	enum class Good : int;

	/// <summary>What a gate project is for.</summary>
	enum class GateJob : int
	{
		/// <summary>A new pair of gates: a shorter way between two systems already on the network, or the first way to a frontier star.</summary>
		NewLink,

		/// <summary>Rebuilding a ring damaged or wrecked in a war.</summary>
		Repair,
	};
	SPACEGODCORE_API const char* ToString(GateJob v);

	enum class GateStage : int
	{
		/// <summary>Charting the gate site and the far star before any steel is cut.</summary>
		Survey,

		/// <summary>Constructor ships at the site, building as fast as the materials come.</summary>
		Building,

		Done,

		/// <summary>Given up: its builder fell, or the way it was for was made some other way.</summary>
		Abandoned,
	};
	SPACEGODCORE_API const char* ToString(GateStage v);

	/// <summary>One gate being surveyed, built or rebuilt (see <see cref="GateConstruction"/>).</summary>
	class SPACEGODCORE_API GateProject final
	{
	public:
		GateJob Work = GateJob::NewLink;
		GateStage Stage = GateStage::Survey;

		/// <summary>The polity paying for it and doing the work.</summary>
		cs::Ref<Faction> Builder;

		/// <summary>The system the work is done from: its constructors sit at the gate site at its edge.</summary>
		cs::Ref<StarSystem> From;

		/// <summary>The system at the far end; null while that is a frontier star nobody simulates yet (see <see cref="Frontier"/>).</summary>
		cs::Ref<StarSystem> To;

		/// <summary>The far star's catalogue entry, for the first gate to a frontier star; null otherwise.</summary>
		cs::Ref<SystemSpec> Frontier;

		/// <summary>A repair's ring (in <see cref="From"/>); for a new link, the near gate once it is finished.</summary>
		cs::Ref<JumpGate> Gate;

		/// <summary>A repair's starting <see cref="JumpGate.Integrity"/>.</summary>
		double FromIntegrity = 0;

		cs::String TargetName;
		double Ly = 0;

		double StartJd = 0;
		double BuildStartJd = cs::Double::NaN, DoneJd = cs::Double::NaN;

		/// <summary>How long the survey takes, days (none for a repair).</summary>
		double SurveyDays = 0;

		/// <summary>Days of work the build takes with its full crew of constructors and every tonne to hand (see <see cref="GateConstruction.BuildDays"/>).</summary>
		double WorkDays = 0;

		/// <summary>Days of full-rate work done so far.</summary>
		double WorkDone = 0;

		/// <summary>Tonnes of each of <see cref="GateConstruction.Materials"/> the whole job takes, and has taken so far.</summary>
		cs::List<double> Need = cs::NewArray<double>(3), Used = cs::NewArray<double>(3);

		/// <summary>Credits paid for the materials so far.</summary>
		double Spent = 0;

		/// <summary>The constructor ships at the site (hulls in <see cref="From"/>'s fleet).</summary>
		cs::List<cs::Ref<Ship>> Constructors;

		/// <summary>When the site was last left with no constructor working (lost to raiders or a war), for a replacement.</summary>
		double CrewLostJd = cs::Double::NaN;

		/// <summary>Share of the work done, 0..1.</summary>
		double Progress() const;

		bool Active() const;

		/// <summary>Where the far end is, light-years from the Sun.</summary>
		DVec3 TargetLy() const;

		/// <summary>The gate site in <see cref="From"/>, km from its root: a repair's ring, or the spot facing the far star.</summary>
		DVec3 SiteKm() const;

		/// <summary>Whether this project joins <paramref name="a"/> and the system or star called <paramref name="b"/>, either way round.</summary>
		bool Joins(cs::Ref<StarSystem> a, const cs::String& b) const;

		cs::String ToString() const;
	};

	/// <summary>
	/// How the jump-gate network grows and heals over sim time (U.SPC-T97). The <see cref="GateNetwork"/> a game starts
	/// with is the one grown out from the Sol-Centauri pair before it; from then on the states that hold systems keep
	/// building, a gate at a time:
	///  - Planning, monthly: each state that holds a system and can stand the bill picks its best project within
	///    <see cref="MaxLinkLy"/> of a system it holds, or of an empty one it opened itself: the first gate to a frontier star nobody has reached (see
	///    <see cref="FrontierSystems"/>), or a shortcut between two systems whose way round by other gates is days longer.
	///    A rich state (the Sol Federation) runs two at once; a poor frontier one waits until it can afford any.
	///  - Survey (<see cref="SurveyDays"/>): the site and the far star are charted.
	///  - Building (<see cref="BuildDays"/>, years): constructor ships go out to the site and work through a large bill
	///    of Alloys, Electronics and Machinery (<see cref="Cost"/>), bought day by day out of real market stock in the
	///    system (and the builder's capital system), at the market's price, from its treasury. A day short of material,
	///    money or constructors is a day's work not done, so a war, a blockade or a lean market stretches the years.
	///  - Done: the pair of gates opens. A gate to a frontier star brings that star into the simulation as an empty
	///    system, open to settlement (<see cref="Galaxy.SystemOpened"/>).
	/// Gates are damaged in war: a ring whose far end is held by its owner's enemy is now and then sabotaged
	/// (<see cref="JumpGate.Damage"/>; any other harm, a fleet action at the gate, goes through the same call), and a
	/// ring below <see cref="JumpGate.OperationalIntegrity"/> is out of action until its owner rebuilds it, a repair
	/// costing and taking the share of a new gate that the damage did. Repairs are planned before any new work.
	/// </summary>
	class SPACEGODCORE_API GateConstruction final
	{
		// The galaxy this belongs to (a back-reference: Galaxy makes its GateConstruction with `this`).
		Galaxy* galaxy;
		cs::Random rng;
		cs::List<cs::Ref<GateProject>> projects;
		double nextDailyJd = 0, nextPlanJd = 0;

	public:
		/// <summary>Every project, under way or over.</summary>
		const cs::List<cs::Ref<GateProject>>& Projects() const;

		/// <summary>The goods a gate is built of.</summary>
		static const cs::List<Good>& Materials();

		/// <summary>The farthest a new gate reaches, light-years; <see cref="GateNetwork.MaxLinkLy"/> is the same number, for the lattice it starts from.</summary>
		static constexpr double MaxLinkLy = 7;

		/// <summary>Constructor ships sent to each site; fewer working means slower work.</summary>
		static constexpr int ConstructorsPerProject = 2;

		/// <summary>How often (days) the states look for new work.</summary>
		static constexpr double PlanIntervalDays = 30;

		/// <summary>A state starts a gate only with this share of its bill (at base prices) in its treasury.</summary>
		static constexpr double ReserveShare = 0.5;

		/// <summary>A treasury this deep runs two projects at once.</summary>
		static constexpr double RichTreasury = 2e9;

		/// <summary>A shortcut is only worth building if it saves this many days on the way round.</summary>
		static constexpr double MinSavedDays = 1.5;

		/// <summary>What reaching a new star is worth against days saved on an old way (the frontier comes first).</summary>
		static constexpr double FrontierWorthDays = 6;

		/// <summary>Days before a site that has lost all its constructors gets new ones.</summary>
		static constexpr double ReplaceCrewDays = 60;

		/// <summary>Daily chance a ring whose far end its owner's enemy holds is sabotaged, while the war lasts.</summary>
		static constexpr double SabotageChancePerDay = 1.0 / 400;

		/// <summary>What a gate pair takes, tonnes: a ring tens of kilometres across, with its field coils and the machinery to spin it up.</summary>
		static cs::List<double> Cost(double ly);

		/// <summary>The bill at base prices, credits.</summary>
		static double CostCredits(double ly);

		/// <summary>Days of work a new pair takes, fully supplied: two years and a little more for each light-year it spans.</summary>
		static double BuildDays(double ly);

		/// <summary>Days to chart the site and the far star before building starts.</summary>
		static double SurveyDays(double ly);

		/// <summary>A repair's share of a new gate's bill and time: the damage done (a wreck still has its frame, so at most this much).</summary>
		static constexpr double RepairShare = 0.6;

		GateConstruction(Galaxy* galaxy, double jd, int seed = 9701);

		cs::List<cs::Ref<GateProject>> Active() const;

		/// <summary>
		/// The new gates under way for the galaxy map (see <see cref="GalaxyMap.Build"/>): each one being built between two
		/// systems already simulated. A first gate to a frontier star is not among them: its far end is no system yet.
		/// </summary>
		cs::List<cs::Ref<GateWork>> Works();

		/// <summary>Gate pairs finished, rings rebuilt, and frontier systems opened so far.</summary>
		int Built = 0;
		int Repaired = 0;
		int Opened = 0;

		/// <summary>Raised when a project is finished.</summary>
		cs::Event<cs::Ref<GateProject>> Completed;

		/// <summary>Each day to <paramref name="jd"/>: the work, then (monthly) the planning, then the war's harm.</summary>
		void Update(double jd);

	private:
		void Daily(double jd);

	public:
		// ---------------------------------------------------------------- the work

		/// <summary>A day (or <paramref name="days"/>) of work on <paramref name="p"/>.</summary>
		void Step(cs::Ref<GateProject> p, double jd, double days);

	private:
		/// <summary>Working constructors as a share of a full crew; a site with none left gets new hulls after <see cref="ReplaceCrewDays"/>.</summary>
		double Crew(cs::Ref<GateProject> p, double jd);

		/// <summary>Puts a full crew of constructor ships at the site, holding there until the work is done.</summary>
		void SendConstructors(cs::Ref<GateProject> p, double jd);

		/// <summary>The markets the materials come from: the site's own system first, then the builder's capital system.</summary>
		cs::List<cs::Ref<Settlement>> Sources(cs::Ref<GateProject> p);

		/// <summary>The price the builder pays for a tonne: the dearest market it buys from (a fair estimate of the day's bill).</summary>
		double PriceOf(cs::Ref<GateProject> p, Good g);

		/// <summary>Takes <paramref name="tonnes"/> of material <paramref name="i"/> from the sources, biggest stocks first, and pays for it.</summary>
		void Buy(cs::Ref<GateProject> p, int i, double tonnes);

		void Complete(cs::Ref<GateProject> p, double jd);

		void Abandon(cs::Ref<GateProject> p, double jd, const cs::String& why);

		bool LinkedAlready(cs::Ref<GateProject> p);

		/// <summary>A polity with nothing left: no standing settlement anywhere.</summary>
		bool Fallen(cs::Ref<Faction> f);

		static cs::String SiteName(cs::Ref<GateProject> p);

		static cs::String GateNetworkName(const cs::String& to);

		static void Log(cs::Ref<GateProject> p, double jd, const cs::String& text);

	public:
		// ---------------------------------------------------------------- starting work

		/// <summary>Starts a project directly (the planner's way in, and a test's): a new pair from <paramref name="from"/> to a system or frontier star.</summary>
		cs::Ref<GateProject> Start(cs::Ref<Faction> builder, cs::Ref<StarSystem> from, cs::Ref<StarSystem> to, cs::Ref<SystemSpec> frontier, double jd);

		/// <summary>Starts rebuilding a damaged ring for its owner (or <paramref name="builder"/>).</summary>
		cs::Ref<GateProject> StartRepair(cs::Ref<JumpGate> gate, cs::Ref<Faction> builder, double jd);

		/// <summary>Damage done to a ring (sabotage, a fleet action): logged, and a ring knocked out of action stops traffic until it is rebuilt.</summary>
		void Damage(cs::Ref<JumpGate> gate, double amount, double jd, const cs::String& by = cs::String());

		/// <summary>The project under way at <paramref name="gate"/>, or null.</summary>
		cs::Ref<GateProject> RepairOf(cs::Ref<JumpGate> gate);

	private:
		/// <summary>Repairs first, then each state's next new gate.</summary>
		void Plan(double jd);

		bool Hostile(cs::Ref<Faction> a, cs::Ref<Faction> b);

	public:
		/// <summary>
		/// <paramref name="f"/>'s best new gate: from a system it holds (or an empty one it opened, see <see cref="BasesOf"/>), within <see cref="MaxLinkLy"/>, to a frontier star nobody has
		/// reached (worth <see cref="FrontierWorthDays"/>) or a system whose way round is at least <see cref="MinSavedDays"/> longer
		/// than a direct gate would be; whichever is worth most for its length. False if nothing is.
		/// </summary>
		bool BestProject(cs::Ref<Faction> f, cs::Ref<StarSystem>& from, cs::Ref<StarSystem>& to, cs::Ref<SystemSpec>& frontier, double& ly);

	private:
		/// <summary>Whether a new link from <paramref name="a"/> (to <paramref name="b"/>, or to anywhere when null; a frontier star
		/// is only ever reached once) is already being worked on.</summary>
		bool Targeted(cs::Ref<StarSystem> a, const cs::String& b);

		/// <summary>A gate in <paramref name="s"/> facing <paramref name="towardLy"/> would stand well clear of every gate it has (and every site being built).</summary>
		bool SiteClear(cs::Ref<StarSystem> s, DVec3 towardLy);

	public:
		/// <summary>
		/// Where <paramref name="f"/> can build from: the systems it holds, and the empty ones it opened itself (it holds the
		/// gate in), so its gates go on outward star by star, their materials coming from its capital's markets.
		/// </summary>
		cs::List<cs::Ref<StarSystem>> BasesOf(cs::Ref<Faction> f);

	private:
		cs::Ref<StarSystem> SystemOf(cs::Ref<World> w);

		// ---------------------------------------------------------------- war

		/// <summary>Now and then, a ring whose far end its owner's enemy holds is sabotaged from the far side.</summary>
		void Sabotage(double jd);
	private:
		GateConstruction() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(GateConstruction); // ObjectGraph registration (Persistence/Registrations)
	};
}
