#pragma once
// Port of Unity.SpaceGod Core/World.cs, World.Mining.cs, World.Colonies.cs and World.MiningConvoys.cs (US-T8):
// one partial class, one header, one .cpp per C# file.
//
// Many of World's members share their type's name (Fleet Fleet, Economy Economy, War War, ...). Inside the class
// the member hides the type, so every such type is spelled ::SpaceGod::Core::T here.

#include <tuple>
#include <utility>

#include "Cs/Cs.h"
#include "Astro.h"          // Astro::AuKm (TugRaidReachKm)
#include "DVec3.h"
#include "BodyDefinition.h" // Rgb (by value: SolFederationColor)
#include "Ships.h"          // ShipClass, ShipRole, Faction, Ship
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Ephemeris;
	class BodyCatalog;
	class Fleet;
	class Economy;
	class Polities;
	class Diplomacy;
	class War;
	class Infrastructure;
	class EventDirector;
	class MarketShocks;
	class Law;
	class Sensors;
	class Outlaws;
	class Pursuit;
	class Companies;
	class Company;
	class Contracts;
	class MerchantEscorts;
	class Holdings;
	class TradeDesk;
	class Colonisation;
	class ColonyShip;
	class LaneUpkeep;
	class JumpGate;
	class Settlement;
	class Wreck;
	class EscapePod;
	class Opportunity;
	class SalvageSession;
	class Battle;
	class Hyperlanes;
	class BeltMining;
	class MiningOperation;
	class TugHaul;
	class MaterialConvoy;
	class SocietySpec;
	class Facility;
	class BioMass;
	class GasHarvest;
	struct TugDanger;
	struct HeavyLanePath;
	enum class PodState : int;
	enum class IndustryKind : int;

	enum class WorldEventKind : int
	{
		Info,
		Launch,
		Battle,
		Capture,
		Diplomacy,
		Loss,

		/// <summary>An adventure offered to the player, or how one ended (see <see cref="EventDirector"/>).</summary>
		Opportunity,

		/// <summary>An economic event: an accident, a blight, a strike, rationing, a blockade (see <see cref="MarketShocks"/>).</summary>
		Economy,

		/// <summary>A price spike at a port, or across the system.</summary>
		Market,

		/// <summary>A crime recorded, a bounty posted, paid or collected (see <see cref="Law"/>).</summary>
		Law,
	};
	SPACEGODCORE_API const char* ToString(WorldEventKind v);

	struct WorldEvent
	{
		double Jd = 0;
		cs::String Text;
		WorldEventKind Kind = WorldEventKind::Info;
		int Body = 0;

		WorldEvent() = default;
		WorldEvent(double jd, cs::String text, WorldEventKind kind, int body) : Jd(jd), Text(std::move(text)), Kind(kind), Body(body) {}
	};

	/// <summary>
	/// Everything that happens in the system, advanced together in date order: the fleet's
	/// schedules, the markets, and the wars. The orbital mechanics are real; the settlements,
	/// factions and their economy are the game layer, placed on worlds that plausibly hold what they
	/// produce (ice moons sell water, Titan hydrocarbons, Io copper and zinc from its sulphide flows).
	/// </summary>
	// World hands itself to its subsystems (new Sensors(this), Polities.Register(this), ...), hence enable_shared_from_this;
	// its C# constructor body runs as Construct() right after the Ref is made (shared_from_this is not usable inside a constructor).
	class SPACEGODCORE_API World final : public std::enable_shared_from_this<World>
	{
	public:
		// ================================================================ World.cs

		cs::Ref<::SpaceGod::Core::Ephemeris> Ephemeris;
		cs::Ref<BodyCatalog> Catalog() const;
		cs::Ref<::SpaceGod::Core::Fleet> Fleet;
		cs::Ref<::SpaceGod::Core::Economy> Economy;
		/// <summary>The interstellar polities this world's factions belong to (U.SPC-T91): shared by every system of a galaxy.</summary>
		cs::Ref<::SpaceGod::Core::Polities> Polities;

		/// <summary>Who is at war with whom: the one table every system shares (see <see cref="Polities.Diplomacy"/>).</summary>
		cs::Ref<::SpaceGod::Core::Diplomacy> Diplomacy() const;
		cs::Ref<::SpaceGod::Core::War> War;

		/// <summary>Stations, yards, factories and spacecraft in orbit, and the docking rings over populous worlds.</summary>
		cs::Ref<::SpaceGod::Core::Infrastructure> Infrastructure;

		cs::Ref<Faction> Sol, Belt, Reach, Independents, Pirates;

		/// <summary>The two powers whose wars and ceasefires come and go (Sol: the Federation and the Reach).</summary>
		cs::Ref<Faction> RivalA, RivalB;

		/// <summary>Names the pirates give their raiders; null for the default list.</summary>
		// (null is the empty list here, see docs/MIGRATION.md)
		cs::List<cs::String> RaiderNames;

		/// <summary>The merchant fleet yards keep up before navies get their slips, and the most they build to.</summary>
		int MerchantTarget = 100, MerchantCap = 170;

		/// <summary>How many warships a navy keeps in peace and in war.</summary>
		int NavyCap = 20, NavyCapAtWar = 34;

		/// <summary>The most raider groups the pirates field, and what smuggling and rackets bring them a day.</summary>
		int PirateGroupCap = 4;
		double PirateIncomePerDay = 1.5e6;

		/// <summary>Things worth telling an observer about (a battle, a capture, a new hull), newest last.</summary>
		cs::List<WorldEvent> Events;

		/// <summary>The ship under direct player control, if any; its schedule is suspended while flown
		/// (see <see cref="Fleet.IsHeld"/>) and a pilot-flight controller owns its position instead.</summary>
		cs::Ref<Ship> PilotedShip;

		/// <summary>The dynamic event system: adventures near the player drawn from what the sim is doing
		/// (see <see cref="EventDirector"/>), advanced by <see cref="UpdateAdventures"/>.</summary>
		cs::Ref<EventDirector> Adventures;

		/// <summary>Economic events driven by the sim (accidents, worked-out seams, blights, strikes, war rationing,
		/// blockades and embargoes) and the price spikes they and the war cause, reported in <see cref="Events"/>.</summary>
		cs::Ref<MarketShocks> Shocks;
		/// <summary>Law, crime and reputation: jurisdictions, the player's and every captain's record, bounties and
		/// the patrols that act on them (see <see cref="Core.Law"/>).</summary>
		cs::Ref<::SpaceGod::Core::Law> Law;

		/// <summary>Sensors, signatures and stealth: what each faction's ships and stations, and the player's own ship,
		/// can see of every other ship (see <see cref="Core.Sensors"/>).</summary>
		cs::Ref<::SpaceGod::Core::Sensors> Sensors;

		/// <summary>Named outlaw captains, the ports' bounty boards and the bounty hunters who work them (see <see cref="Core.Outlaws"/>).</summary>
		cs::Ref<::SpaceGod::Core::Outlaws> Outlaws;

		/// <summary>Hostile groups running the piloted ship down (U.SPC-T273).</summary>
		cs::Ref<::SpaceGod::Core::Pursuit> Pursuit;

		/// <summary>The trading companies that own and run the merchant fleet (U.SPC-T99; see <see cref="Core.Companies"/>).</summary>
		cs::Ref<::SpaceGod::Core::Companies> Companies;

		/// <summary>The stations' contracts boards: freight, passengers and colonists, escorts, patrols, surveys and
		/// bounties posted from what the system needs, and who has taken them (see <see cref="Core.Contracts"/>).</summary>
		cs::Ref<::SpaceGod::Core::Contracts> Contracts;

		/// <summary>The merchant fleet's fighter escorts (U.SPC-T275).</summary>
		cs::Ref<MerchantEscorts> Escorts;

		/// <summary>The player's own fleet and property: owned ships under hired captains' standing orders, warehouses,
		/// station shares and mining claims, and the house account their earnings go to (see <see cref="Core.Holdings"/>).</summary>
		cs::Ref<::SpaceGod::Core::Holdings> Holdings;

		/// <summary>The player's trading career (U.SPC-T426): price intelligence, trade news, charters, insurance, the
		/// back door and the trader's rank (see <see cref="Core.TradeDesk"/>).</summary>
		cs::Ref<TradeDesk> Trade;

		/// <summary>Factions founding new settlements as their people run out of room (U.SPC-T98).</summary>
		cs::Ref<::SpaceGod::Core::Colonisation> Colonisation;

		/// <summary>The hyperlanes' upkeep and the lane tenders that keep them running (U.SPC-T481, see <see cref="Core.LaneUpkeep"/>).</summary>
		cs::Ref<::SpaceGod::Core::LaneUpkeep> LaneUpkeep;

		/// <summary>The Bio Mass adrift in this system (US-T125): made on first use, so older saves load without it.</summary>
		cs::Ref<::SpaceGod::Core::BioMass> BioMass;

		/// <summary>This system's Bio Mass, made now (with its own random stream, seeded from the system's name) if it has none.</summary>
		cs::Ref<::SpaceGod::Core::BioMass> EnsureBioMass(double jd);

		/// <summary>This system's gas giant works (US-T146): made on first use, so older saves load without it.</summary>
		cs::Ref<::SpaceGod::Core::GasHarvest> Harvest;

		/// <summary>This system's gas giant works, planned now (with its own random stream, seeded from the system's name) if it has none.</summary>
		cs::Ref<::SpaceGod::Core::GasHarvest> EnsureHarvest(double jd);

		/// <summary>The most bounty hunter groups the free traders keep in service.</summary>
		int HunterCap = 3;

		/// <summary>Raised when a ship joins the fleet after the start (a yard finished it, or it came through a gate).</summary>
		cs::Event<cs::Ref<Ship>> ShipAdded;

		/// <summary>Raised when a ship bound for a jump gate reaches it (see <see cref="Ship.GateKm"/> and <see cref="InterstellarTraffic"/>).</summary>
		cs::Event<cs::Ref<Ship>, double> ReachedGate;

		/// <summary>
		/// Orders for a ship here on an interstellar voyage (raiders from another system, a navy's
		/// expedition): the body to go to next, or -1 to fall back on the usual choice.
		/// </summary>
		cs::Func<int, cs::Ref<Ship>, int, double> VoyageOrders;

		/// <summary>This system's jump gates (see <see cref="StarSystem.Gates"/>); empty for a world on its own.</summary>
		cs::List<cs::Ref<JumpGate>> Gates;

		/// <summary>
		/// Hides in this system's belts and discs where no haven stands yet (body indices): where a pirate clan from
		/// elsewhere can found one (U.SPC-T94, see <see cref="FoundHaven"/>).
		/// </summary>
		cs::List<int> HavenSites;

	private:
		int builtCount = 0;

		World() = default;
		// The C# constructor World(Ephemeris, double, int, Polities): Make builds the Ref, then runs Construct, the body.
		static cs::Ref<World> Make(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double startJd, int seed, cs::Ref<::SpaceGod::Core::Polities> polities);
		void Construct(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double startJd, int seed, cs::Ref<::SpaceGod::Core::Polities> polities);

		double nextRecountJd = 0;

	public:
		/// <summary>The date the world has been run to (NaN before its first <see cref="Update"/>).</summary>
		double Jd = cs::Double::NaN;

		/// <summary>
		/// Run at a coarse level of detail (U.SPC-T104): a system the player is not in, stepped every few hours
		/// rather than every frame (see <see cref="Galaxy.Focus"/>). Trade, industry and the markets are
		/// integrated over the step as they always are; ships keep their real schedules but fly without
		/// sidestepping one another and plan their windows on a coarser grid (<see cref="Fleet.Coarse"/>); and
		/// battles are fought out in steps of <see cref="Battle.CoarseStep"/> seconds rather than one. Nothing is
		/// summarised away, so a world promoted back to full detail carries on from exactly where it stands.
		/// </summary>
		bool Coarse() const;
		void SetCoarse(bool value);

		void Update(double jd);

		/// <summary>
		/// Advances the dynamic event system to <paramref name="jd"/> around the player, who is at
		/// <paramref name="playerHelioKm"/> (the piloted ship, or the camera when only watching); only a
		/// piloted ship (<see cref="PilotedShip"/>) can arrive on the scene and collect.
		/// </summary>
		void UpdateAdventures(double jd, DVec3 playerHelioKm);

		/// <summary>Where a wreck is now, heliocentric km: fixed in its body's frame, or beside the lane it died by.</summary>
		DVec3 WreckHelio(cs::Ref<Wreck> w, double jd);

		/// <summary>Where an escape pod cluster is now, heliocentric km: drifting in its body's frame, or beside the lane its hull died by.</summary>
		DVec3 PodHelio(cs::Ref<EscapePod> pod, double jd);

		/// <summary>
		/// The player's ship (<paramref name="ship"/>) comes alongside drifting escape pods and takes them aboard:
		/// a friend brings them home, a pirate holds them for ransom, an enemy takes them prisoner (see
		/// <see cref="Crews.Collect"/>). The player is paid through the adventure, not the owners' rescue fee.
		/// </summary>
		PodState PickUpPods(cs::Ref<EscapePod> pod, cs::Ref<Ship> ship, double jd);

		/// <summary>
		/// The player's ship lets go of a hulk (or has just claimed it): the clamps come off, what came of it
		/// goes in the event log, and the derelict or wreck adventure on it, if one is open, is seen through
		/// (<see cref="EventDirector.HulkWorked"/>). Returns that adventure, or null.
		/// </summary>
		cs::Ref<Opportunity> FinishSalvage(cs::Ref<SalvageSession> session, double jd);

		void NotifyAdded(cs::Ref<Ship> ship);

		/// <summary>Raised for every entry in <see cref="Events"/> as it is logged (the log itself keeps only the latest 400).</summary>
		cs::Event<WorldEvent> Logged;

		void Log(double jd, cs::String text, WorldEventKind kind = WorldEventKind::Info, int body = -1);

		bool IsTrader(cs::Ref<Ship> s) const;

		/// <summary>The chance any one civilian transit springs a leak and leaves the ship to run dry (see <see cref="Fleet.LeakChance"/>).</summary>
		static constexpr double PropellantLeakChance = 0.003;

		/// <summary>
		/// Ships whose transits burn real propellant from their tanks (see <see cref="Propulsion"/>): every civilian
		/// hull in its own system. One on an interstellar voyage is not charged on the way: its run in to a jump gate
		/// hard by the star is a direct crossing of the whole system, far past any tank, flown on the gate network's
		/// own approach (it pays the gates' tolls instead) until fuel for it is modelled too.
		/// </summary>
		bool BurnsPropellant(cs::Ref<Ship> s) const;

	private:
		/// <summary>
		/// A ship joins a hyperlane: civilian traffic pays the ride's toll (<see cref="Economy.LaneToll"/>, by its
		/// all-up mass) to whoever keeps the system's ramps, the owner of its biggest settlement. Navies and
		/// raiders ride free; so does the player's ship (the helm is local flight, never a lane), and a lane tender at its work.
		/// </summary>
		void PayToll(cs::Ref<Ship> ship, LaneHop hop, double jd);

	public:
		/// <summary>Who keeps a planetary system's hyperlane ramps (and takes their tolls): the owner of its most populous settlement, or nobody.</summary>
		cs::Ref<Faction> LaneKeeper(int system);

		// ---------------------------------------------------------------- polities

		/// <summary>
		/// Founds the polity called <paramref name="name"/> here, or, if another system already has, gives it a presence
		/// here (U.SPC-T91): it is the same faction, so this system's holdings add their revenues to its one treasury
		/// (<paramref name="treasury"/> is added either way) and its ships join its one navy. Its marines, colours and
		/// kind are those it was founded with.
		/// </summary>
		cs::Ref<Faction> Polity(cs::String name, cs::String prefix, Rgb color, PolityKind kind, double treasury, double marines = 1);

		/// <summary>People a clan's newly founded haven starts with.</summary>
		static constexpr double FoundedHavenPopulation = 5e3;

		/// <summary>
		/// A pirate clan founds a haven here (U.SPC-T94): a hidden hold at <paramref name="body"/> (one of the
		/// <see cref="HavenSites"/>), feeding itself, with one gun platform, and the clan gains a presence in this system.
		/// Where no clan of its own holds a haven here, this one's raiders are now the system's: new bands sail from the
		/// haven (one at a time) and a bounty hunter can be fitted out to go after them.
		/// </summary>
		cs::Ref<Settlement> FoundHaven(cs::String name, int body, cs::Ref<Faction> clan, double jd);

	private:
		/// <summary>The raiders of a system with no clan of its own: one unknown, the same in every such system.</summary>
		cs::Ref<Faction> UnknownRaiders();

		/// <summary>Gives each polity present here and still without a seat its capital: its most populous settlement.</summary>
		void SeatCapitals();

		/// <summary>
		/// Every hull is crewed; from here on new ones draw on a real reserve of trained spacers, mustered from every
		/// settlement a polity holds in any system (a polity joined here is mustered again, with this system's people).
		/// </summary>
		void MusterReserves();

	public:
		/// <summary>Who holds this system (see <see cref="Polities.HolderOf"/>).</summary>
		cs::Ref<Faction> Holder();

		// ---------------------------------------------------------------- the default system

		static cs::Ref<World> CreateDefault(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double jd, int seed = 1977, cs::Ref<::SpaceGod::Core::Polities> polities = nullptr);

		/// <summary>The polity that holds the core: Sol, and the frontier at Barnard's Star.</summary>
		static inline const cs::String SolFederation = "Sol Federation";

		static inline const Rgb SolFederationColor = Rgb(0.35f, 0.65f, 1.0f);

	private:
		/// <summary>Connects the markets, the wars and the fleet's schedule (the same in every system).</summary>
		void Wire(double startJd, int seed, cs::Ref<Hyperlanes> lanes);

		/// <summary>
		/// Colonisation (U.SPC-T98): every world of the system surveyed as a site, colony ships timed by the
		/// distance at the date they sail (a freighter's flip-and-burn, as the contract boards reckon it), and
		/// the news of each voyage, founding, build-out, promotion and failure in the log.
		/// </summary>
		void WireColonisation();

	public:
		// ---------------------------------------------------------------- Alpha Centauri

		/// <summary>
		/// Alpha Centauri, a century or so after settlement: the same machinery as Sol, but young and
		/// thin. The Centauri Accord governs the Proxima colonies with a small navy; the Toliman
		/// Combine, a corporate concern, runs the stations around A and B with no warships of its own;
		/// Kentaurus Free Traders carry most of the freight; and the Long Dark raid the long lanes
		/// from a derelict station out by Proxima. All fiction, on real (and candidate) worlds.
		/// </summary>
		static cs::Ref<World> CreateAlphaCentauri(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double jd, int seed = 4243, cs::Ref<::SpaceGod::Core::Polities> polities = nullptr);

		/// <summary>
		/// A system nobody has settled yet: real stars and worlds, the same machinery, but no
		/// settlements, lanes or ships. Two factions exist only so the game layer has someone to
		/// hand a ship to (a survey service and the raiders it will one day meet); both are fiction.
		/// </summary>
		static cs::Ref<World> CreateUnsettled(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double jd, int seed = 1, cs::Ref<::SpaceGod::Core::Polities> polities = nullptr);

	private:
		void BuildCentauriSettlements();

		void BuildSettlements();

	public:
		/// <summary>The hidden pirate havens of Sol's main belt: name, asteroid, people.</summary>
		static const cs::List<std::tuple<cs::String, cs::String, double>>& BeltHavens();

		/// <summary>The hidden pirate havens of Jupiter's Trojan swarms: name, Trojan, people.</summary>
		static const cs::List<std::tuple<cs::String, cs::String, double>>& TrojanHavens();

	private:
		/// <summary>
		/// Every settlement grows most of its own food and melts most of its own water (a haven all of
		/// it, since nobody else will deliver there); imports and supply contracts cover the rest.
		/// </summary>
		static void Subsist(cs::Ref<Settlement> s, cs::Action<cs::Ref<Settlement>, Good, double> mine, cs::Action<cs::Ref<Settlement>, IndustryKind, double> make);

		void BuildMilitary(double jd);

		static const cs::List<cs::String>& FreighterNames();

		static const cs::List<cs::String>& MinerNames();
		static const cs::List<cs::String>& CourierNames();

		void BuildMerchants(double jd, cs::List<cs::String> freighters, cs::List<cs::String> miners, cs::List<cs::String> couriers, cs::List<cs::Ref<Faction>> owners, cs::Func<cs::Ref<Faction>, int> minerOwner);

		// (C# shares the static arrays by reference; nothing writes to them, so a copy is the same.)
		cs::List<cs::String> merchantNames = FreighterNames(), minerNames = MinerNames();

	public:
		/// <summary>The worlds with a port a merchant may trade at (every settlement but a haven).</summary>
		cs::List<int> PortBodies();

		/// <summary>Puts a new hull in space: out of its yard's orbit if the yard has one, else in a parking orbit.</summary>
		void Launch(cs::Ref<Ship> ship, cs::Ref<Settlement> yard, double jd);

		/// <summary>
		/// A battle has broken out at a body: merchants still on their way there change course for the
		/// nearest other port they can reach without a hyperlane (often turning back where they came from).
		/// A fight off the system's root (at one jump gate) turns aside only those bound for that spot, not
		/// everyone bound for any of the system's gates (U.SPC-T313).
		/// </summary>
		void DivertAround(cs::Ref<Battle> b, double jd);

		/// <summary>Where a free ship may go: any visible world but a star -- and but a hidden haven's rock,
		/// which nobody knows to go to. Shared by the free roamers, sandbox spawns and gate arrivals.</summary>
		cs::List<int> FreeShipPorts();

	private:
		/// <summary>
		/// Independent pilots who go where they please: yachts and survey ships (fast courier hulls), prospectors (ore
		/// haulers) and deep-space explorers (U.SPC-T419, survey hulls with an FTL drive of their own, which now and then
		/// go off the gate network to look at a star), roaming every world in the system, not just its ports.
		/// </summary>
		void AddFreeShips(double jd, cs::List<cs::String> yachts, cs::List<cs::String> prospectors, cs::List<cs::String> explorers = cs::List<cs::String>());

		cs::Ref<Ship> AddMerchant(cs::String name, ShipClass cls, cs::Ref<Faction> owner, ShipRole role, cs::List<int> ports, double jd, int at = -1, cs::Ref<Settlement> yard = nullptr);

		/// <summary>
		/// Founds the system's first trading companies (name, house mark, headquarters, flag, whether it is in the ore
		/// trade) and hands each the merchant hulls already sailing under its flag: ore haulers to the miners, the rest
		/// to the carriers, each to whichever has fewest. A hull whose flag has no company stays its faction's own.
		/// </summary>
		void FoundCompanies(double jd, cs::List<std::tuple<cs::String, cs::String, cs::String, cs::Ref<Faction>, bool>> table);

	public:
		// ---------------------------------------------------------------- shipbuilding

		int TraderCount();

	private:
		/// <summary>
		/// A navy at war that has been worn below its peacetime strength takes its own yards' slips ahead of the
		/// companies' orders, to rebuild; otherwise null (the companies' hulls first, then the navy's).
		/// </summary>
		cs::Nullable<ShipClass> Requisition(cs::Ref<Settlement> yard);

		/// <summary>Yards build merchant hulls while the merchant fleet is under strength.</summary>
		cs::Nullable<ShipClass> ChooseBuild(cs::Ref<Settlement> yard);

		void OnShipBuilt(cs::Ref<Settlement> yard, ShipClass cls, double jd);

		static cs::String Roman(int n);

		// ================================================================ World.Mining.cs

	public:
		/// <summary>
		/// The asteroid-belt mining pipeline that feeds this system's megastructure builds (U.SPC-T330, see
		/// <see cref="BeltMining"/>): capture, grinding, separation, convoys to warehouses, and the builds they supply.
		/// </summary>
		cs::Ref<BeltMining> Mining;

		/// <summary>Raiders about at which the pipeline's convoys and warehouses run their full risk.</summary>
		static constexpr int MiningPressureRaiders = 6;

	private:
		/// <summary>Connects the pipeline to this world: real distances, the raiders actually about, its guns, its news.</summary>
		void WireMining(int seed);

		/// <summary>
		/// How many raiders are about, 0..1: the raider hulls at sea (escorts aside) and the havens they work from,
		/// against <see cref="MiningPressureRaiders"/>.
		/// </summary>
		double MiningPiratePressure();

		/// <summary>
		/// The heavy lanes an ore tug can push a load along from <paramref name="from"/> to <paramref name="to"/> (U.SPC-T483):
		/// the quickest route between their planetary systems over heavy lanes alone, its length, and the pushes from the
		/// source to the first ramp and from the last ramp to the destination. Null where no heavy lanes join them.
		/// </summary>
		cs::Nullable<HeavyLanePath> MiningHeavyLane(int from, int to, double jd);

		/// <summary>
		/// Sol's belt works: the Belt Consortium's refinery at Ceres ships what it separates to a depot on Callisto,
		/// out at Jupiter, which feeds the tether going up at Ganymede and the first ring of a swarm around the Sun.
		/// Its two ore tugs (U.SPC-T483), real hulls in the fleet (U.SPC-T498), push blocks of ice cut in the belt out to
		/// the depot along the heavy Ceres-Jupiter lane, water and volatiles for the builds (the tether's carbon ribbon among them).
		/// </summary>
		void SeedSolMining(double jd);

		// ---------------------------------------------------------------- the ore tugs' hulls (U.SPC-T498)

		/// <summary>Names the belt works give their ore tugs, in turn.</summary>
		static const cs::List<cs::String>& TugNames();

		int tugsNamed = 0;

	public:
		/// <summary>How near (km) raiders must be to an ore tug, where it actually is, to threaten it.</summary>
		static constexpr double TugRaidReachKm = 0.2 * Astro::AuKm;

		/// <summary>Raider hulls about a tug at which it runs its full risk of being seized.</summary>
		static constexpr int TugPressureRaiders = 3;

		/// <summary>
		/// Gives <paramref name="op"/> a real hull for each of its ore tugs (U.SPC-T498): every tug is a ship in this world's
		/// fleet, lying at the field it works (<see cref="MiningOperation.TugHome"/>) until the works sends it out. Returns them.
		/// </summary>
		cs::List<cs::Ref<Ship>> RaiseTugs(cs::Ref<MiningOperation> op, double jd);

		// ---------------------------------------------------------------- the capture tugs' hulls (U.SPC-T522)

	private:
		/// <summary>Names the belt works give their capture tugs, in turn.</summary>
		static const cs::List<cs::String>& CaptureTugNames();

		int captureTugsNamed = 0;

	public:
		/// <summary>
		/// Gives <paramref name="op"/> a real hull for each of its capture tugs (U.SPC-T522): every one a ship
		/// (<see cref="ShipClass.CaptureTug"/>) in this world's fleet, lying at its refinery until the works sends it out into
		/// the field round it for a rock. Flown, threatened, seized and helmed as an ore tug's hull is. Returns them.
		/// </summary>
		cs::List<cs::Ref<Ship>> RaiseCaptureTugs(cs::Ref<MiningOperation> op, double jd);

	private:
		/// <summary>Whether a tug's hull lying at its field may be sent out: not at the player's helm, not fighting, not held.</summary>
		bool TugFree(cs::Ref<Ship> tug);

		/// <summary>
		/// The works has sent a tug's hull out on a haul: with its load latched on it sets out for where the load is for now,
		/// by the heavy lanes where they run (see <see cref="Hyperlanes.RouteFor"/>); with its load out in the field of the
		/// body it is for, it flies out empty to it first.
		/// </summary>
		void TugSent(cs::Ref<TugHaul> haul, double jd);

		/// <summary>The tug's hull has arrived somewhere: its works latches its load on, or takes the load in.</summary>
		void TugArrived(cs::Ref<Ship> tug, double jd);

		/// <summary>
		/// What threatens a tug's hull where it actually is (U.SPC-T498): raider hulls within <see cref="TugRaidReachKm"/> of it,
		/// against <see cref="TugPressureRaiders"/>, the nearest of them the ones who would take it, and the world it is nearest.
		/// </summary>
		TugDanger TugThreat(cs::Ref<TugHaul> haul, double jd);

		/// <summary>The world (planet, moon or minor body, not a star) nearest a point on a date.</summary>
		int NearestBody(DVec3 helioKm, double jd);

		/// <summary>
		/// Raiders have seized a tug's hull where it was (U.SPC-T498): it flies their colours from there, and makes for one of
		/// their havens (or, with none, goes where it pleases); its load is already theirs.
		/// </summary>
		void HandOverTug(cs::Ref<TugHaul> haul, double jd);

		// ================================================================ World.Colonies.cs

		// ---------------------------------------------------------------- the colony ships' hulls (U.SPC-T524)

		/// <summary>
		/// Gives this system's colonisation real hulls (U.SPC-T524): each colony ship that sails is a freighter fitted out at its
		/// port (<see cref="ShipRole.Colonist"/>, <see cref="Ship.Colony"/>), in this fleet, flying the voyage itself; it can be
		/// fought, boarded or lost on the way, and its colonists land only when it gets there (see <see cref="ColonyHullArrived"/>).
		/// </summary>
		void WireColonyHulls(cs::Ref<::SpaceGod::Core::Colonisation> c);

		/// <summary>
		/// Fits out a freighter at a colony ship's port for its voyage (U.SPC-T524): a new hull in this fleet under the
		/// colonists' flag and the voyage's name, lying at the port with the colonists and their starter goods aboard.
		/// </summary>
		cs::Ref<Ship> EmbarkColonists(cs::Ref<ColonyShip> v, double jd);

		/// <summary>
		/// A colony ship's hull has reached a body here (U.SPC-T524): its colonisation lands them, if this is where they are
		/// bound. A founding voyage's hull is broken up for the new settlement's habitat; one that has landed settlers at a young
		/// settlement, or brought home colonists it could not land, goes back to trading; one whose site was taken turns for
		/// home with them still aboard (through the gates, from abroad). True if the hull is gone.
		/// </summary>
		bool ColonyHullArrived(cs::Ref<Ship> hull, double jd);

		/// <summary>
		/// Where a colony ship's hull at <paramref name="here"/> goes next (U.SPC-T524): on to the site (or the settlement) it is
		/// bound for, or home to its port with colonists it could not land; it holds where it is in a system it is only passing
		/// through. One bound abroad that is back at home with no voyage (the gates shut to it and it gave up waiting) carries its
		/// colonists back to port. A hull with no voyage left goes back to trading: -1, for the ordinary trader's choice.
		/// </summary>
		int ColonyHullNext(cs::Ref<Ship> hull, int here, double jd);

		/// <summary>A founding voyage's hull is broken up where it landed: its frame and fittings are the new settlement's first habitat.</summary>
		void BreakUpColonyHull(cs::Ref<Ship> hull, double jd);

	public:
		/// <summary>
		/// A colony ship's hull done with its voyage, or never sent on one, goes back to work as an ordinary freighter of its
		/// flag (U.SPC-T524). Out on a voyage through the gates it first comes home, and takes up trading there.
		/// </summary>
		void RefitColonyHull(cs::Ref<Ship> hull);

		// ================================================================ World.MiningConvoys.cs

		// ---------------------------------------------------------------- the raw-material convoys' freighters (U.SPC-T523)

	private:
		/// <summary>Names the belt works give their convoy freighters, in turn.</summary>
		static const cs::List<cs::String>& WorksFreighterNames();

		int worksFreightersNamed = 0;

		/// <summary>The escort classes a works' freighter flies with, in turn: a gunship leading, then a fighter.</summary>
		static const cs::List<ShipClass>& WorksEscortClasses();

	public:
		/// <summary>
		/// Gives <paramref name="op"/> a real hull for each of its convoy freighters (U.SPC-T523): every one a
		/// <see cref="ShipClass.Freighter"/> in this world's fleet, owned by the works' faction, lying at its refinery until
		/// the works loads it, each with <see cref="MiningOperation.EscortsPerFreighter"/> of its owner's warships flying
		/// with it in formation as its escort. Returns the freighters.
		/// </summary>
		cs::List<cs::Ref<Ship>> RaiseFreighters(cs::Ref<MiningOperation> op, double jd);

	private:
		int worksEscortsRaised = 0;

	public:
		/// <summary>
		/// The works make good the escorts its freighters have lost (U.SPC-T592): a freighter lying idle and empty at its
		/// refinery that flies with fewer warships than <see cref="MiningOperation.EscortsPerFreighter"/> (some lost in a
		/// fight, left adrift or taken) is given fresh ones, in the classes the works first sent, before it loads again.
		/// Returns how many were built.
		/// </summary>
		int ReplaceWorksEscorts(double jd);

	private:
		/// <summary>Whether a works' freighter lying at its refinery may sail: not at the player's helm, not fighting, not held.</summary>
		bool FreighterFree(cs::Ref<Ship> freighter);

		/// <summary>The works has loaded a freighter with a convoy: it sets out at once for the convoy's warehouse, its escorts with it.</summary>
		void FreighterSailed(cs::Ref<MaterialConvoy> convoy, double jd);

		/// <summary>A works' freighter has arrived somewhere: at its convoy's warehouse its hold is emptied there.</summary>
		void FreighterArrived(cs::Ref<Ship> freighter, double jd);

		/// <summary>A works' freighter has brought its convoy in: the news, and home empty to its refinery.</summary>
		void FreighterDelivered(cs::Ref<MaterialConvoy> convoy, double jd);

		// ================================================================ Systems/SocietySpec.cs (defined in Systems/SocietySpec.cpp)

	public:
		/// <summary>
		/// A nearby system settled by <paramref name="society"/> (U.SPC-T90): the same machinery as Sol and Alpha
		/// Centauri, sized to how far out it is. Every settlement grows most of its own food and water; the mines
		/// and factories are then sized so that the system as a whole makes a little more of every good than it
		/// uses (<see cref="BalanceSupply"/>), leaving the traders a living carrying the rest about.
		/// </summary>
		static cs::Ref<World> CreateSettled(cs::Ref<::SpaceGod::Core::Ephemeris> ephemeris, double jd, int seed, cs::Ref<SocietySpec> society,
			cs::Ref<::SpaceGod::Core::Polities> polities = nullptr);

		/// <summary>
		/// Sizes a system's mines and factories so it makes <see cref="SupplyMargin"/> times what it uses of every good
		/// that is used: each good short is made up by raising every facility that makes it in proportion, again and
		/// again, since a bigger smelter wants more ore and a bigger mine more machinery; each counted at the share of
		/// its capacity it can expect to run at (<see cref="ExpectedRunning"/>). Havens fend for themselves
		/// and are left out. A good used but made nowhere is a mistake in the society's plan, and throws.
		/// </summary>
		static void BalanceSupply(const cs::List<cs::Ref<Settlement>>& settlements, const cs::String& system);

	private:
		/// <summary>
		/// The share of capacity a facility can count on: all of it if every consumable it burns (a mine's machinery
		/// and helium-3, a lab's rare earths) is made where it stands, else <see cref="Recipe.MinorFloor"/>, since in a
		/// thin system those come in dribs and drabs. Life support runs on regardless.
		/// </summary>
		static double ExpectedRunning(const cs::Ref<Settlement>& s, const cs::Ref<Facility>& f);

	public:
		/// <summary>How much more of every good a settled system makes than it uses: the traders' living, and the slack for the shipyards' and the ships' own wants.</summary>
		static constexpr double SupplyMargin = 1.15;
		SG_SERIALIZABLE(World); // ObjectGraph registration (Persistence/Registrations)
		/// A loaded world wires its delegates again (Persistence/Registrations/Wiring.cpp, which repeats Construct and Wire's wiring).
		void RewireAfterLoad();
		/// An adventure seen through: its standing goes on the record of the pilot who did it (US-T148, Law.Credit).
		void CreditAdventure(const cs::Ref<Opportunity>& op);
	};
}
