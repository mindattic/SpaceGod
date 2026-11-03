#pragma once
// Port of Assets/UnitySpace/Core/Systems/MilitarySupply.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"

#include <utility>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Settlement;
	class SupplyConvoy;
	class Campaign;
	class Faction;
	class StarSystem;
	class Ship;
	class Galaxy;
	class World;
	class Diplomacy;
	class JumpGate;
	enum class Good : int;

	/// <summary>
	/// What a war fleet on campaign has left of the three things it burns through (U.SPC-T93), each a share of a full
	/// load (1 as it sets out, 0 empty): propellant for its drives, munitions for its magazines, and the spares it mends
	/// itself with. They drain by the day (marching burns propellant far faster than lying in a system, fighting empties
	/// the magazines, and field repairs eat the spares), and its <see cref="MilitaryLogistics"/> fills them again from a
	/// naval base by supply convoys. Low on any of them the fleet loses <see cref="Readiness"/>, and too low it withdraws.
	/// </summary>
	class SPACEGODCORE_API FleetSupply final
	{
	public:
		double Propellant = 1, Munitions = 1, Spares = 1;

		/// <summary>No way open over the gates to any naval base of its own (see <see cref="MilitaryLogistics.LineTo"/>), and since when.</summary>
		bool OutOfSupply = false;
		double OutOfSupplySinceJd = cs::Double::NaN;

		/// <summary>The naval base it draws on now, or null.</summary>
		cs::Ref<Settlement> Base;

		/// <summary>The supply ships on their way to it, or null.</summary>
		cs::Ref<SupplyConvoy> Convoy;

		/// <summary>Convoys that reached it, and those that could not get through.</summary>
		int ConvoysArrived = 0, ConvoysLost = 0;

		/// <summary>Below this share of a full load, a store starts to tell on the fleet's readiness.</summary>
		static constexpr double LowSupply = 0.3;

		/// <summary>A fleet's readiness with a store empty.</summary>
		static constexpr double ReadinessFloor = 0.25;

		/// <summary>A fleet below this readiness, with no convoy on its way, gives up its campaign and goes home.</summary>
		static constexpr double WithdrawReadiness = 0.6;

		/// <summary>A fleet sends for supply ships when its lowest store is below this.</summary>
		static constexpr double ResupplyBelow = 0.75;

		/// <summary>A full load of propellant lasts this many days of marching (under way in a system, or in the gates)...</summary>
		static constexpr double MarchDays = 30;

		/// <summary>...and this many days lying in a system, station-keeping.</summary>
		static constexpr double IdleDays = 150;

		/// <summary>Full magazines last this many days of fighting.</summary>
		static constexpr double BattleDays = 5;

		/// <summary>Its spares wear away over this many days whatever it does (breakdowns, wear).</summary>
		static constexpr double WearDays = 120;

		/// <summary>A day's field repair mends this share of a hull's damage, at full readiness...</summary>
		static constexpr double FieldRepairPerDay = 0.01;

		/// <summary>...and takes this share of the fleet's spares per share mended, per hull.</summary>
		static constexpr double SparesPerRepair = 1.5;

		/// <summary>Readiness for what is left of one store.</summary>
		static double ReadinessOf(double stock);

		/// <summary>The fleet's readiness: as low as its lowest store makes it (see <see cref="ShipCombat.Readiness"/>).</summary>
		double Readiness() const;

		/// <summary>Its lowest store.</summary>
		double Lowest() const;

		/// <summary>Too run down to go on, and nothing on its way to it: it must withdraw.</summary>
		bool MustWithdraw() const;

		/// <summary><paramref name="days"/> of campaigning: under way or not, fighting or not.</summary>
		void Drain(double days, bool moving, bool fighting);

		/// <summary>
		/// A day's field repair of one of its <paramref name="hulls"/> carrying <paramref name="damage"/>: the share of it
		/// mended (slower as readiness falls), paid for out of the spares, as far as they go.
		/// </summary>
		double FieldRepair(double damage, int hulls, double days = 1);

		/// <summary>Takes in what a convoy brings.</summary>
		void Deliver(cs::Ref<SupplyConvoy> c);
	};

	/// <summary>
	/// Supply ships a naval base sends after a war fleet (U.SPC-T93): tankers with its propellant, ammunition colliers
	/// with its munitions and spares. Every one is a real hull (U.SPC-T525, <see cref="Hulls"/>): a freighter of its navy's
	/// that sails from the base in formation with the rest, through the gates that will open for it to wherever the fleet
	/// is now, and hands over what it carries only when it comes up with it. A gate cut or shut against them behind the
	/// fleet and they cannot get through; whatever meets them on the way can destroy or take them, and what they carried
	/// goes with them.
	/// </summary>
	class SPACEGODCORE_API SupplyConvoy final
	{
	public:
		cs::Ref<Core::Campaign> Campaign;

		/// <summary>The navy whose ships they are.</summary>
		cs::Ref<Core::Faction> Faction;

		/// <summary>The naval base it set out from, and its system.</summary>
		cs::Ref<Settlement> Base;
		cs::Ref<StarSystem> From;

		/// <summary>When it set out, and when it was reckoned to come up with the fleet as it then lay (an estimate: it
		/// delivers when its hulls actually get there).</summary>
		double SetOutJd = 0, DueJd = 0;

		/// <summary>How many of <see cref="Hulls"/> are tankers (the first ones) and ammunition colliers (the rest).</summary>
		int Tankers = 0, Colliers = 0;

		/// <summary>
		/// The hulls themselves (U.SPC-T525): ships in their system's fleet, the first leading the rest, the first
		/// <see cref="Tankers"/> of them tankers and the others colliers. Each delivers its share only if it gets there.
		/// </summary>
		cs::List<cs::Ref<Ship>> Hulls;

		/// <summary>What it carries, as shares of the fleet's full load.</summary>
		double Propellant = 0, Munitions = 0, Spares = 0;

		/// <summary>What it carries, in tonnes, and what the navy paid for it.</summary>
		double Tonnes = 0, Cost = 0;

		cs::String ToString() const;
	};

	/// <summary>
	/// Military logistics (U.SPC-T93): the supply of the war fleets out on campaign (see <see cref="InterstellarWars"/>).
	///
	/// Every fleet burns through its <see cref="FleetSupply"/>: propellant on the march, munitions in battle, spares
	/// mending itself in the field. A navy's naval bases are its shipyard ports; each day a fleet looks for the nearest one
	/// it can reach over gates that will open for it (its own, a neutral's, or enemy gates it has taken), not blockaded or
	/// fought over, and when its stores run down the base sends supply ships after it, bought from the base's own market
	/// (water and hydrocarbons for reaction mass, munitions, alloys and electronics for spares) and paid for from the
	/// navy's treasury: an empty market or an empty treasury sends less, or nothing. So the war draws on the economy, and
	/// the economy's shortages tell on the war.
	///
	/// A fleet that can reach no base is out of supply. As its stores run low its readiness falls (its guns reload slower,
	/// its crews strike sooner, it mends slower: see <see cref="ShipCombat.Readiness"/>), and below
	/// <see cref="FleetSupply.WithdrawReadiness"/> it gives up the campaign and withdraws. Cutting a gate behind it, or
	/// blockading the base it draws on, starves the fleet.
	/// </summary>
	class SPACEGODCORE_API MilitaryLogistics final
	{
		// The galaxy this belongs to (a back-reference, not owned).
		Galaxy* galaxy;
		cs::List<cs::Ref<SupplyConvoy>> convoys;

	public:
		/// <summary>A gate route (GateNetwork.Route): null when there is no way through.</summary>
		using GateRoute = cs::Ref<cs::List<cs::Ref<JumpGate>>>;

		/// <summary>Supply convoys on their way.</summary>
		const cs::List<cs::Ref<SupplyConvoy>>& Convoys() const;

		/// <summary>Munitions (t) a hull's magazines hold, full.</summary>
		static constexpr double MunitionsPerHull = 150;

		/// <summary>Alloys and electronics (t) a hull's stores of spares hold, full.</summary>
		static constexpr double AlloysPerHull = 120; static constexpr double ElectronicsPerHull = 20;

		/// <summary>What one tanker and one ammunition collier carry (t).</summary>
		static constexpr double TankerTonnes = 4000; static constexpr double CollierTonnes = 1500;

		/// <summary>Days a base takes to load its supply ships, and to cross a system to a fleet there.</summary>
		static constexpr double LoadingDays = 0.5; static constexpr double CrossingDays = 0.5;

		explicit MilitaryLogistics(Galaxy* galaxy);

	private:
		cs::Ref<Core::Diplomacy> Diplomacy() const;

	public:
		/// <summary>
		/// Whether <paramref name="s"/> serves its owner as a naval base now: a standing shipyard port, not under blockade
		/// and nobody besieging or fighting over it.
		/// </summary>
		bool BaseOpen(cs::Ref<Settlement> s, cs::Ref<World> world);

		/// <summary>(Settlement Base, List&lt;JumpGate&gt; Route)</summary>
		struct LineToResult { cs::Ref<Settlement> Base; GateRoute Route; };

		/// <summary>
		/// The naval base of <paramref name="f"/> that can supply a fleet in <paramref name="here"/> soonest, and the way
		/// its supply ships take there (empty if it is in the same system); (null, null) when none can: the fleet is out of supply.
		/// </summary>
		LineToResult LineTo(cs::Ref<Faction> f, cs::Ref<StarSystem> here, double jd);

		/// <summary>A campaign's day in the gates: under way, burning propellant.</summary>
		void InGate(cs::Ref<Campaign> c, double jd);

		/// <summary>
		/// A day of a campaign's supply, its flagship <paramref name="leader"/> in <paramref name="system"/>: its stores drain
		/// and pay for field repairs, a convoy due comes up with it (or cannot get through), it sends for another when it
		/// runs low and a base can reach it, and its hulls take on its readiness.
		/// </summary>
		void Day(cs::Ref<Campaign> c, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, double jd);

		/// <summary>How near (km) a convoy's leading hull must be to the war fleet's flagship to come up with it, when the two
		/// are not lying at the same world.</summary>
		static constexpr double RendezvousKm = 0.01 * Astro::AuKm;

	private:
		/// <summary>
		/// A day of a convoy's way to its war fleet (U.SPC-T525), the fleet's flagship <paramref name="leader"/> in
		/// <paramref name="system"/>: lost if every hull of it is gone (destroyed, a hulk, or taken), or if it cannot get
		/// through to where the fleet is now; sent on after the fleet if it has moved to another system; and, its hulls come
		/// up with the fleet, it hands over what it carries.
		/// </summary>
		void Track(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd);

	public:
		/// <summary>
		/// Where a convoy's supply ships make for in <paramref name="system"/> (U.SPC-T525, the traffic's orders for a hull
		/// on a <see cref="VoyagePurpose.Supply"/> voyage): the world its war fleet's flagship lies at or is bound for, or,
		/// with the fleet not here, nowhere (it holds where it is until its navy sends it on).
		/// </summary>
		int ConvoyOrders(cs::Ref<StarSystem> system, cs::Ref<Ship> ship, int here, double jd);

	private:
		/// <summary>Whether a convoy's leading hull, in port, has come up with the war fleet's flagship: lying at the same
		/// world, or within <see cref="RendezvousKm"/> of it.</summary>
		static bool Met(cs::Ref<World> world, cs::Ref<Ship> flag, cs::Ref<Ship> leader, double jd);

		/// <summary>A campaign's flagship in <paramref name="world"/>, or null when it is not there.</summary>
		static cs::Ref<Ship> FlagshipIn(cs::Ref<World> world, cs::Ref<Campaign> c);

		/// <summary>The hull leading a convoy now (its first, or an heir if that was lost) and its system; (null, null) when
		/// none is in any system (inside a gate, or none left on the voyage).</summary>
		std::pair<cs::Ref<StarSystem>, cs::Ref<Ship>> LeaderOf(cs::Ref<SupplyConvoy> convoy);

		bool InGate(cs::Ref<SupplyConvoy> convoy);

		/// <summary>How many of a convoy's hulls from <paramref name="from"/> (count <paramref name="count"/>) are still its navy's.</summary>
		static int Alive(cs::Ref<SupplyConvoy> convoy, int from, int count);

		/// <summary>A convoy that will never come up with its fleet: written off, and whatever is left of it turns for home.</summary>
		void Lost(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, double jd, const cs::String& why);

		/// <summary>
		/// A convoy's work is over (delivered, lost or turned back): its hulls are no longer bound for the fleet, and those
		/// still in a system turn for their base's system (home at once if they are there); those inside a gate turn back
		/// when they come out.
		/// </summary>
		void Release(cs::Ref<SupplyConvoy> convoy, double jd);

		/// <summary>The convoy comes up with the fleet: its hulls that got there hand over what they carry.</summary>
		void Arrive(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd);

		/// <summary>
		/// A naval base sends supply ships after a fleet: what it needs to fill its stores, as far as the base's market has
		/// it and the navy can pay, on the way <paramref name="route"/>.
		/// </summary>
		void Dispatch(cs::Ref<Campaign> c, cs::Ref<Settlement> station, GateRoute route, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd);

		// ---------------------------------------------------------------- the supply ships' hulls (U.SPC-T525)

		/// <summary>Names a navy gives its tankers and its ammunition colliers, in turn.</summary>
		static const cs::List<cs::String>& TankerNames();
		static const cs::List<cs::String>& CollierNames();

		int tankersNamed = 0, colliersNamed = 0;

		/// <summary>
		/// Gives <paramref name="convoy"/> its hulls (U.SPC-T525): the navy's supply ships lying idle in the base's system
		/// first, then new ones fitted out at the base for the rest. The first <see cref="SupplyConvoy.Tankers"/> are its
		/// tankers; the first leads, and the rest take station on it in formation.
		/// </summary>
		void Muster(cs::Ref<SupplyConvoy> convoy, cs::Ref<Settlement> station, cs::Ref<StarSystem> from, double jd);

		static cs::String Named(const cs::List<cs::String>& names, int& named);

		/// <summary>A supply ship its own master, lying at its naval base: there between runs, and making for it if elsewhere.</summary>
		static void Stand(cs::Ref<Ship> s, cs::Ref<Settlement> station, cs::Ref<World> world, double jd);

		/// <summary>Buys up to <paramref name="tonnes"/> of <paramref name="g"/> off <paramref name="s"/>'s market: what it cost.</summary>
		static double Take(cs::Ref<Settlement> s, Good g, double tonnes);

	public:
		/// <summary>A campaign is over: whatever is still on its way to it turns back.</summary>
		void Ended(cs::Ref<Campaign> c, double jd = cs::Double::NaN);

	private:
		cs::Ref<StarSystem> SystemOf(cs::Ref<Settlement> st);

		/// <summary>A fleet's flagship and the hulls in its group.</summary>
		static cs::List<cs::Ref<Ship>> Group(cs::Ref<World> world, cs::Ref<Ship> leader);
	private:
		MilitaryLogistics() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(MilitaryLogistics); // ObjectGraph registration (Persistence/Registrations)
	};
}
