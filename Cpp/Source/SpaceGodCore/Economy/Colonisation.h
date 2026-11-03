#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Colonisation.cs (US-T19).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Faction;
	class Settlement;
	class Economy;
	class Ship;
	class Facility;
	class Colonisation;
	enum class Good : int;
	enum class HabitableZone : int;

	/// <summary>Where a settlement stands in its life (U.SPC-T98).</summary>
	enum class ColonyStage : int
	{
		/// <summary>One of the system's settlements from the start of the game.</summary>
		Established,

		/// <summary>Founded in play by a colony ship: a small foothold living off its starter goods, its farms and its ice.</summary>
		Outpost,

		/// <summary>A founded outpost that has grown into a colony (<see cref="Colonisation.ColonyPopulation"/>, <see cref="Colonisation.ColonyJobs"/>).</summary>
		Colony,

		/// <summary>A founded settlement that failed: its last people have gone, its facilities stand empty.</summary>
		Ruin,
	};
	SPACEGODCORE_API const char* ToString(ColonyStage v);

	/// <summary>
	/// A world a faction could settle: where it sits against its star's habitable zone, what can be dug there
	/// and how many people it could hold. Surveyed from the catalogue (<see cref="Survey"/>); the resources and
	/// capacity are game-layer judgements from the body's kind, size and sunlight, set out there.
	/// </summary>
	class SPACEGODCORE_API ColonySite final
	{
	public:
		int Body = 0;
		cs::String Name;
		HabitableZone Zone = static_cast<HabitableZone>(0);
		cs::List<Good> Resources = cs::List<Good>();

		/// <summary>The most people the site's habitat can be built out for (the base population times <see cref="People.MaxGrowth"/>).</summary>
		double Capacity = 0;

		/// <summary>Starlight at the site, Earth = 1.</summary>
		double Flux = 0;

		/// <summary>An empty orbit marked out for a habitat (<see cref="BodyDefinition.HabitatSite"/>) rather than a world to land on.</summary>
		bool Habitat = false;

		bool Has(Good g) const;

		/// <summary>
		/// People can live here: in the habitable zone, or with ice to melt for water (nobody founds a colony
		/// that would drink only what is shipped in).
		/// </summary>
		bool Viable() const;

		/// <summary>
		/// How much a faction wants the site (0 if it cannot be lived on): the habitable zone counts most,
		/// then what there is to dig and how far the habitat can grow.
		/// </summary>
		double Score() const;

		/// <summary>
		/// Surveys body <paramref name="index"/> of a catalogue as a site, or null if nobody could found a
		/// settlement on it (a star, a station, a gas giant, or a body with no star to orbit). The zone is
		/// Kopparapu's (<see cref="HabitableZones"/>) for the orbit of the body (or of the planet it circles)
		/// about its star. What can be dug, assumed from the sunlight and size: worlds in the zone have water
		/// (and, if big enough to hold an atmosphere, nitrogen and titanium); hot ones bare metal; cold ones ices
		/// (water, and on the bigger ones nitrogen and hydrocarbons); small bodies of the warm inner system
		/// metals and the precious ones. A site with water but no nitrogen lives on nitrogen shipped in.
		/// </summary>
		static cs::Ref<ColonySite> Survey(const cs::Ref<BodyCatalog>& catalog, int index);

		cs::String ToString() const { return Name; }
	};

	/// <summary>A colony ship under way: the colonists it carries, its starter goods, and where it is bound.</summary>
	class SPACEGODCORE_API ColonyShip final
	{
	public:
		ColonyShip();

		cs::String Name;
		cs::Ref<Faction> Owner;

		/// <summary>The port it sailed from, where the colonists gathered.</summary>
		cs::Ref<Settlement> From;

		/// <summary>The site it goes to found a settlement on; or, for a ship taking more people out to a young one, its body.</summary>
		cs::Ref<ColonySite> Site;

		/// <summary>The young settlement it takes more people to, or null for a founding voyage.</summary>
		cs::Ref<Settlement> Reinforcing;

		double Colonists = 0;

		/// <summary>Starter goods in the hold, tonnes, by good.</summary>
		cs::List<double> Cargo; // new double[GoodInfo.Count], sized in the constructor

		double DepartJd = 0, ArriveJd = 0;

		/// <summary>
		/// For a voyage through the gates to another system (U.SPC-T293): that system's colonisation and markets, and
		/// its name; null for a voyage within the system it sailed from.
		/// </summary>
		cs::Ref<Colonisation> Into;
		cs::Ref<Economy> IntoEconomy;
		cs::String Where; // C# null is empty here

		bool Abroad() const { return Into != nullptr; }

		/// <summary>
		/// The colony ship's own hull (U.SPC-T524): a freighter fitted out for the voyage (<see cref="ShipRole.Colonist"/>), a real
		/// ship in its system's fleet that flies from the port to the site, through the gates for a site abroad, and can be
		/// fought, boarded or lost on the way. The colonists land only when it gets there. Null for a voyage reckoned by its
		/// timetable alone (a <see cref="Colonisation"/> with no fleet to raise hulls in, see <see cref="Colonisation.Embark"/>).
		/// </summary>
		cs::Ref<Ship> Hull;

		/// <summary>The colonisation that sent it (whose <see cref="Colonisation.Voyages"/> it is in).</summary>
		cs::Ref<Colonisation> Of;

		/// <summary>Its hull is carrying the colonists back to <see cref="From"/>: the site was taken, or the settlement lost, while they were on their way.</summary>
		bool Returning = false;

		/// <summary>The body the hull is bound for now: the site (or the young settlement), or home to <see cref="From"/> when <see cref="Returning"/>.</summary>
		int Body() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>What a colony ship's hull reaching a body came to (U.SPC-T524, <see cref="Colonisation.HullArrived"/>).</summary>
	enum class ColonyLanding : int
	{
		/// <summary>Not where its voyage is bound: nothing happens.</summary>
		None,

		/// <summary>Its colonists founded a settlement on the site: the hull is broken up for the new habitat.</summary>
		Founded,

		/// <summary>Its colonists joined the young settlement it was sent to: the hull goes back to trading.</summary>
		Reinforced,

		/// <summary>The site was taken (or the settlement lost) while they were on their way: the hull carries them home.</summary>
		TurnedBack,

		/// <summary>Home again with the colonists it could not land: they rejoin their port, and the hull goes back to trading.</summary>
		Returned,
	};
	SPACEGODCORE_API const char* ToString(ColonyLanding v);

	/// <summary>A free site in another system that a colony ship could reach through the gates, and how long it would take (U.SPC-T293).</summary>
	class ForeignSite final
	{
	public:
		cs::Ref<ColonySite> Site;

		/// <summary>The system's colonisation and markets, and its name.</summary>
		cs::Ref<Colonisation> Into;
		cs::Ref<::SpaceGod::Core::Economy> Economy;
		cs::String Where;

		/// <summary>Days from the port to the site: out to the gate, through the gates, and in to the site.</summary>
		double Days = 0;
	};

	/// <summary>
	/// Colonisation (U.SPC-T98): factions found new settlements as their people run out of room. Nothing
	/// here runs on a timer: the pressure is the population model's own. A settlement at its population cap
	/// (<see cref="People.MaxGrowth"/> times its founding people) can neither grow nor take in migrants, and
	/// both the growth it cannot take and the migrants turned away from full places are counted as its
	/// <see cref="Settlement.Overflow"/>: people with nowhere to go (a crowded place, past <see cref="People.CrowdedShare"/>
	/// of its cap, counts a rising share of its growth the same way). Once a faction has a ship's worth of them
	/// (<see cref="PartySize"/>) and can pay for the voyage, it sends a colony ship from its most crowded port
	/// with those colonists and starter goods: to a young settlement of its own that has room, or else to
	/// the best free site in reach (the habitable zone first, then what there is to dig), which may be in another
	/// system newly opened to settlement, through the gates (<see cref="Abroad"/>, U.SPC-T293). The colony ship is a real
	/// hull in the fleet (U.SPC-T524, <see cref="Embark"/>): it flies the voyage itself, can be fought, boarded and lost on
	/// the way, and its colonists land only when it actually gets there (<see cref="HullArrived"/>). On landing the
	/// colonists found an <see cref="ColonyStage.Outpost"/> with farms and ice mines to feed itself and a
	/// mine on the site's best ore.
	///
	/// A founded settlement then lives by the same markets and population model as any other. One that is
	/// fed and full is built out (<see cref="ExpandFactor"/>, paid by its owner) and its life support with
	/// it; past <see cref="ColonyPopulation"/> people and <see cref="ColonyJobs"/> facility jobs it is a
	/// <see cref="ColonyStage.Colony"/>. One that starves or sickens shrinks to its hard core (the
	/// population floor), at which its last people give up and leave for the owner's biggest settlement:
	/// it is a <see cref="ColonyStage.Ruin"/>, and after <see cref="RuinYears"/> its site is free again.
	/// </summary>
	// Passes itself as a Ref (ship.Of = this, `v.Into ?? this`), hence enable_shared_from_this.
	class SPACEGODCORE_API Colonisation final : public std::enable_shared_from_this<Colonisation>
	{
	public:
		/// <summary>Every site in the system, occupied or not (whether one is free is checked when it is wanted).</summary>
		cs::List<cs::Ref<ColonySite>> Sites = cs::List<cs::Ref<ColonySite>>();

		/// <summary>Colony ships under way that sailed from here (to this system or, through the gates, another).</summary>
		cs::List<cs::Ref<ColonyShip>> Voyages = cs::List<cs::Ref<ColonyShip>>();

		/// <summary>Colony ships under way from other systems to a site here (U.SPC-T293); they land through their own system's review.</summary>
		cs::List<cs::Ref<ColonyShip>> Incoming = cs::List<cs::Ref<ColonyShip>>();

		/// <summary>
		/// The free sites in other systems open to settlement that a faction's colonists at a port could reach through
		/// the gates (U.SPC-T293); null for a system on its own.
		/// </summary>
		// IEnumerable<ForeignSite> built eagerly (the caller walks it all).
		cs::Func<cs::List<cs::Ref<ForeignSite>>, cs::Ref<Faction>, cs::Ref<Settlement>, double> Abroad;

		/// <summary>
		/// What a site in a system nobody has settled yet is worth against one at home: a whole system to hold, its gate
		/// and its markets the founder's (assumed).
		/// </summary>
		static constexpr double NewSystemWorth = 2;

		/// <summary>Days a colony ship takes from a port to a body at a date; null for <see cref="DefaultTravelDays"/>.</summary>
		cs::Func<double, cs::Ref<Settlement>, int, double> TravelDays;

		/// <summary>A colony ship has sailed.</summary>
		cs::Action<cs::Ref<ColonyShip>> Launched;

		/// <summary>A colony ship has landed at a young settlement with more people (or turned back, the site taken: settlement null).</summary>
		cs::Action<cs::Ref<ColonyShip>, cs::Ref<Settlement>> Landed;

		/// <summary>
		/// Raises the hull for a colony ship that has just sailed (U.SPC-T524): a real ship in this system's fleet at its port,
		/// fitted out for the voyage (<see cref="Ship.Colony"/>), or null for none (the voyage is then reckoned by its timetable).
		/// </summary>
		cs::Func<cs::Ref<Ship>, cs::Ref<ColonyShip>, double> Embark;

		/// <summary>Sends a colony ship's hull bound for another system off through the gates (U.SPC-T524); false if it cannot go.</summary>
		cs::Func<bool, cs::Ref<ColonyShip>, double> SailAbroad;

		/// <summary>A colony ship's hull is done with its voyage (or never got away): it goes back to ordinary work where it is.</summary>
		cs::Action<cs::Ref<Ship>> Refit;

		/// <summary>
		/// A colony ship has been lost on the way with its colonists (U.SPC-T524): its hull destroyed, taken by the faction given,
		/// or (neither) never heard from again.
		/// </summary>
		cs::Action<cs::Ref<ColonyShip>, cs::Ref<Faction>> Lost;

		/// <summary>A colony ship that found its site taken has brought its colonists home (U.SPC-T524).</summary>
		cs::Action<cs::Ref<ColonyShip>> Returned;

		/// <summary>Days past its reckoned arrival after which a colony ship's hull, wherever it is, is given up for lost.</summary>
		static constexpr double OverdueDays = 730;

		/// <summary>A new outpost has been founded.</summary>
		cs::Action<cs::Ref<Settlement>> Founded;

		/// <summary>A founded settlement's habitat has been built out for more people.</summary>
		cs::Action<cs::Ref<Settlement>> Expanded;

		/// <summary>An outpost has grown into a colony.</summary>
		cs::Action<cs::Ref<Settlement>> Promoted;

		/// <summary>A founded settlement has failed and been abandoned.</summary>
		cs::Action<cs::Ref<Settlement>> Ruined;

		/// <summary>Colonists a colony ship carries (a party the size of a small town; assumed).</summary>
		static constexpr double PartySize = 5000;

		/// <summary>What a founding voyage costs its faction, credits: the ship's charter and a prefabricated habitat (assumed).</summary>
		static constexpr double FoundingCost = 1.5e8;

		/// <summary>What a voyage taking more people to a young settlement costs, credits (the charter alone; assumed).</summary>
		static constexpr double ReinforceCost = 4e7;

		/// <summary>Days of food, water and air the colonists' starter goods hold, and of medicine and the habitat's spares.</summary>
		static constexpr double StarterDays = 180, MedicineDays = 365;

		/// <summary>Days a voyage takes when nobody has said how far it is.</summary>
		static constexpr double DefaultTravelDays = 60;

		/// <summary>An outpost becomes a colony with this many people and this many facility jobs.</summary>
		static constexpr double ColonyPopulation = 25000, ColonyJobs = 1000;

		/// <summary>A founded settlement this full (of its cap), and this well, is built out by <see cref="ExpandFactor"/>.</summary>
		static constexpr double ExpandAt = 0.75, ExpandWellbeing = 0.9, ExpandFactor = 1.5;

		/// <summary>Share of its industrial labour a founded settlement's first mine employs, and each mine opened as it is built out (assumed).</summary>
		static constexpr double FirstMineShare = 0.25, NextMineShare = 0.15;

		/// <summary>Building out a habitat, credits per person of new base population (assumed).</summary>
		static constexpr double ExpansionCostPerPerson = 4000;

		/// <summary>A young settlement takes more colonists while it is this well and has room for half a party.</summary>
		static constexpr double ReinforceWellbeing = 0.85;

		/// <summary>A founded settlement at its floor that has been living this badly is abandoned.</summary>
		static constexpr double FailWellbeing = 0.8;

		/// <summary>Years before a ruin's site may be settled again.</summary>
		static constexpr double RuinYears = 5;

		/// <summary>Share of their own people's needs the founders' farms and ice mines are built to cover.</summary>
		static constexpr double SelfSufficiency = 1.0;

	private:
		static const cs::List<cs::String>& ShipNames();

		int launched = 0;

	public:
		/// <summary>Surveys every body of a catalogue (<see cref="ColonySite.Survey"/>).</summary>
		void SurveyAll(const cs::Ref<BodyCatalog>& catalog);

		/// <summary>Called at each market review: ships land, young settlements grow or fail, crowded factions send ships.</summary>
		void Review(const cs::Ref<Economy>& economy, double jd);

		// ---------------------------------------------------------------- pressure

		/// <summary>People with no room across <paramref name="f"/>'s settlements (<see cref="Settlement.Overflow"/>).</summary>
		static double Pressure(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& f);

	private:
		static bool Lives(const cs::Ref<Settlement>& s);

	public:
		static bool IsFounded(const cs::Ref<Settlement>& s);

		/// <summary>The live settlement on <paramref name="body"/>, if any (a fresh ruin counts: its site is not yet free).</summary>
		static bool Occupied(const cs::Ref<Economy>& economy, int body, double jd);

		/// <summary>A colony ship is on its way to <paramref name="body"/> here (one bound for another system does not count).</summary>
		bool Bound(int body) const;

		/// <summary>Whether any colony ship from another system is on its way here.</summary>
		bool Awaited() const { return Incoming.Count() > 0; }

	private:
		bool Sailing(const cs::Ref<Faction>& f) const;

		double Days(const cs::Ref<Settlement>& from, int body, double jd) const;

		// ---------------------------------------------------------------- launching

		void Launch(const cs::Ref<Economy>& economy, double jd);

	public:
		/// <summary>
		/// Sends a colony ship for <paramref name="f"/> if it has a party's worth of people with no room, nothing
		/// already under way, a place to send them and the money to do it. Returns the ship, or null.
		/// </summary>
		cs::Ref<ColonyShip> TryLaunch(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& f, double jd);

		/// <summary>The colonisation whose system a colony ship's hull must be in to reach <see cref="ColonyShip.Body"/>.</summary>
		static cs::Ref<Colonisation> Bearing(const cs::Ref<ColonyShip>& v);

		/// <summary>
		/// Where a colony ship's hull in the system of <paramref name="here"/> goes next (U.SPC-T524): the body its voyage is bound
		/// for, -1 to hold where it is (it is not in that system), or null if it is on no voyage under way.
		/// </summary>
		static cs::Nullable<int> HullDestination(const cs::Ref<Ship>& hull, const cs::Ref<Colonisation>& here);

		/// <summary>
		/// A colony ship's hull has reached <paramref name="hull"/>'s body in the system of <paramref name="here"/> (its markets
		/// <paramref name="economy"/>) (U.SPC-T524): where its voyage is bound, the colonists land, found their settlement or join
		/// the one they were sent to (<see cref="Arrive"/>); with the site taken, they stay aboard and the hull turns for home;
		/// home again, they rejoin their port. Says what came of it, for the hull's world to see to the hull.
		/// </summary>
		ColonyLanding HullArrived(const cs::Ref<Ship>& hull, const cs::Ref<Colonisation>& here, const cs::Ref<Economy>& economy, double jd);

		/// <summary>Turns a colony ship for home with its colonists still aboard (U.SPC-T524): its hull carries them back to <see cref="ColonyShip.From"/>.</summary>
		void TurnBack(const cs::Ref<ColonyShip>& v);

	private:
		/// <summary>
		/// Checks on a colony ship's hull at a market review (U.SPC-T524): destroyed, or taken by another faction, it is lost with
		/// its colonists and their goods; long overdue, it is given up. True if the voyage is over.
		/// </summary>
		bool HullLost(const cs::Ref<ColonyShip>& v, double jd);

		cs::Ref<ColonySite> SiteAt(int body) const;

		// ---------------------------------------------------------------- landing

		void Land(const cs::Ref<Economy>& economy, double jd);

	public:
		/// <summary>Lands a colony ship: its people join the young settlement it was sent to, or found one on its site.</summary>
		cs::Ref<Settlement> Arrive(const cs::Ref<Economy>& economy, const cs::Ref<ColonyShip>& v, double jd);

	private:
		static void Unload(const cs::Ref<ColonyShip>& v, const cs::Ref<Settlement>& s);

	public:
		/// <summary>Founds an outpost of <paramref name="people"/> for <paramref name="owner"/> on <paramref name="site"/>.</summary>
		static cs::Ref<Settlement> Found(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& owner, const cs::Ref<ColonySite>& site, double people, double jd);

	private:
		static void AddMine(const cs::Ref<Settlement>& s, Good ore, double perDay);

	public:
		/// <summary>
		/// Builds a founded settlement's life support up to its people's needs (<see cref="SelfSufficiency"/>):
		/// farms for its food, and mines for its water and air where the site has the ice. A site without them
		/// lives on what it is shipped.
		/// </summary>
		static void Provision(const cs::Ref<Settlement>& s);

	private:
		static void Grow(const cs::Ref<Facility>& f, double addLevel);

		/// <summary>Adds <paramref name="perDay"/> tonnes a day to a mine (whose recipe is per tonne of its first build).</summary>
		static void MineMore(const cs::Ref<Settlement>& s, cs::Ref<Facility>& mine, Good ore, double perDay);

		// ---------------------------------------------------------------- growth and failure

		void Tend(const cs::Ref<Economy>& economy, double jd);

	public:
		/// <summary>A founded settlement fails when hunger or sickness has worn it down to its hard core (the population floor).</summary>
		static bool Failing(const cs::Ref<Settlement>& s);

		/// <summary>Builds out a fed, full founded settlement's habitat and life support, if its owner can pay and the site has room.</summary>
		bool TryExpand(const cs::Ref<Settlement>& s);

	private:
		static bool HasMine(const cs::Ref<Settlement>& s, Good g);

	public:
		/// <summary>An outpost with <see cref="ColonyPopulation"/> people and <see cref="ColonyJobs"/> facility jobs becomes a colony.</summary>
		bool TryPromote(const cs::Ref<Settlement>& s);

		/// <summary>
		/// Abandons a failed settlement: its last people leave for the owner's biggest live settlement (or any that
		/// will have them), and what is left is a ruin with nobody in it, no market and no work. A frontier outpost
		/// that is the only settlement in its system falls back to its owner's seat of government, wherever that
		/// stands (<see cref="Faction.Capital"/>), rather than simply losing its last settlers (U.SPC-T319).
		/// </summary>
		void Abandon(const cs::Ref<Economy>& economy, const cs::Ref<Settlement>& s, double jd);
		SG_SERIALIZABLE(Colonisation); // ObjectGraph registration (Persistence/Registrations)
	};
}
