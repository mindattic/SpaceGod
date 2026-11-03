#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Settlement.cs (US-T19).

#include "Cs/Cs.h"
#include "Economy/Goods.h"
#include "Economy/Colonisation.h" // ColonyStage (by value)

#include <tuple>

namespace SpaceGod::Core
{
	class Faction;
	class Facility;
	class Ship;
	class ShipCombat;
	class ColonySite;
	enum class ShipClass : int;

	enum class SettlementKind : int
	{
		Colony,
		Station,
		MiningOutpost,
		Haven,
	};
	const char* ToString(SettlementKind v);

	/// <summary>
	/// A populated place with a market: a colony on a world, a station in orbit, a mining outpost or a
	/// pirate haven. It produces through its facilities, consumes through its people and industry,
	/// and prices every good from how its stock compares with what it uses.
	/// </summary>
	class Settlement final
	{
	public:
		int Id = 0;
		cs::String Name;
		int Body = 0;
		SettlementKind Kind = static_cast<SettlementKind>(0);
		cs::Ref<Faction> Owner;
		double Population = 0;
		double InitialPopulation = 0;
		cs::List<cs::Ref<Facility>> Facilities = cs::List<cs::Ref<Facility>>();

		cs::List<double> Stock = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Carrier craft that put down here with no hangar to take them (U.SPC-T591): each still its own hull, waiting for its carrier.</summary>
		cs::List<cs::Ref<Ship>> Strays = cs::List<cs::Ref<Ship>>();
		cs::List<double> Price = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Tonnes already bought for delivery here by ships still on their way.</summary>
		cs::List<double> Inbound = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Full-capacity daily use (people plus facility inputs) and production, tonnes.</summary>
		cs::List<double> Demand = cs::NewArray<double>(GoodInfo::Count);
		cs::List<double> Supply = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Daily use by things other than people and industry (navies refitting and rearming here), tonnes.</summary>
		cs::List<double> ExtraDemand = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>
		/// Ships' propellant bought here, tonnes a day, smoothed over about <see cref="Economy.PropellantDemandDays"/>:
		/// reaction mass (water, hydrocarbons) and the helium-3 that burns it, part of what the market expects to sell.
		/// </summary>
		cs::List<double> PropellantDemand = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Propellant sold since the last market tick, tonnes (folded into <see cref="PropellantDemand"/>).</summary>
		cs::List<double> PropellantSold = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Share of the last tick's needs that was met, per good (1 where nothing was needed).</summary>
		cs::List<double> Satisfaction = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Build points banked by a shipyard, spent when a hull is laid down.</summary>
		double BuildPoints = 0;

		/// <summary>The hull the yard is working toward (what it last chose to build), or null.</summary>
		cs::Nullable<ShipClass> OnSlip;

		/// <summary>Days since the owner last changed; production is disrupted for a while after a capture.</summary>
		double DaysSinceCapture = 9999;

		/// <summary>
		/// Troops holding it for its owner (U.SPC-T92): its own militia, or the marines who took it. NaN until a war first
		/// asks, when it is mustered at <see cref="InterstellarWars.GarrisonPerPerson"/> of the people. An invader's
		/// landed marines must beat it down before the place changes hands.
		/// </summary>
		double Garrison = cs::Double::NaN;

		/// <summary>How restive its people are under their owner, 0 (content) to 1 (in open revolt): an occupation sets it
		/// high, a garrison wears it down, and it idles part of the industry while it lasts (U.SPC-T92).</summary>
		double Unrest = 0;

		/// <summary>The power it was taken from in a war still being fought, or null: a peace treaty either cedes it to the
		/// occupier or gives it back, and its people may rise to let the old owner back in (U.SPC-T92).</summary>
		cs::Ref<Faction> OccupiedFrom;

		double TradeVolumeCredits = 0;

		/// <summary>Share of industry working (a strike idles all but life support); 1 normally. See <see cref="MarketShocks"/>.</summary>
		double IndustryFactor = 1;

		/// <summary>Under war rationing: the share of essentials (food, water, medicine) people are allowed; 1 normally.</summary>
		double Ration = 1;

		/// <summary>What a day's work buys, as an index of the cost of living it last caught up with (0 until first set).
		/// Wages follow prices slowly; a wide gap brings strikes (see <see cref="MarketShocks"/>).</summary>
		double WageIndex = 0;

		/// <summary>Per-person use of <paramref name="g"/> under rationing: essentials are cut, the rest untouched.</summary>
		double RationOf(Good g) const;

		/// <summary>The price of a day's living here (what <see cref="PersonNeeds"/> cost) against its price at base prices (1 = normal).</summary>
		double CostOfLiving() const;

		/// <summary>Orbital defence platforms (gun stations) that fight any hostile force at this body.</summary>
		cs::List<cs::Ref<ShipCombat>> Platforms = cs::List<cs::Ref<ShipCombat>>();
		int MaxPlatforms = 0;

		cs::Ref<Facility> Shipyard() const;

		bool HasShipyard() const;

		/// <summary>
		/// A hidden haven is off every chart: nobody knows it is there until a ship's scanners pass
		/// within <see cref="War.ScanRangeKm"/> of it or a navy follows raiders home (see
		/// <see cref="War.Discover"/>). An ordinary settlement is known to all.
		/// </summary>
		bool Hidden = false;

		/// <summary>
		/// A lawless station (U.SPC-T94): a company camp too far out and too small to keep a constabulary, whose
		/// middlemen run a black market (see <see cref="BlackMarket"/>) and whose guns look the other way when a clan's
		/// smuggler docks. Havens are black markets too, of their own clan's loot.
		/// </summary>
		bool Lawless = false;

		/// <summary>Stolen goods held in a lawless station's black market, tonnes (a haven keeps its loot in <see cref="Stock"/>).</summary>
		cs::List<double> BlackStock = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>The factions that have found this hidden haven.</summary>
		cs::HashSet<cs::Ref<Faction>> KnownTo = cs::HashSet<cs::Ref<Faction>>();

		/// <summary>On the map: not hidden, or found by somebody (a find is news across the system).</summary>
		bool Discovered() const;

		/// <summary>Whether <paramref name="f"/> knows where this settlement is.</summary>
		bool KnownBy(const cs::Ref<Faction>& f) const;

		/// <summary>A haven that has been taken or destroyed: out of action (no raiders sail from it, its own
		/// raiders lie low) until the pirates rebuild it at <see cref="RebuildJd"/>.</summary>
		bool Fallen = false;
		double RebuildJd = 0;

		/// <summary>Days of use a market likes to hold; prices are base at this cover.</summary>
		static constexpr double CoverDays = 20;

		/// <summary>Warehouses hold this many days of flow; production stops when they are full.</summary>
		static constexpr double StorageDays = 90;

		/// <summary>
		/// Daily per-person needs, tonnes, after recycling, for this kind of settlement (see
		/// <see cref="People.NeedsOf"/>: food, water lost from a closed loop, nitrogen top-up for air
		/// that leaks away, medical supplies, and the upkeep of the habitat itself).
		/// </summary>
		const cs::List<std::tuple<Good, double>>& PersonNeeds() const;

		/// <summary>People of working age in work or looking for it (<see cref="People.LabourForceShare"/>).</summary>
		double Workforce() const;

		/// <summary>Workers the facilities need at their current levels.</summary>
		double Jobs() const;

		/// <summary>Share of the facilities' jobs filled over the last tick (1 where there are none).</summary>
		double Staffing = 1;

		/// <summary>Credits a day paid to the people working the facilities, over the last tick.</summary>
		double WageBill = 0;

		/// <summary>Credits a day the owner collected in tax on those wages, over the last tick.</summary>
		double WageTax = 0;

		/// <summary>
		/// What a member of the labour force here can expect to earn a day: the facility wage times the
		/// chance of a facility job, the rest at the service wage (<see cref="People.ExpectedEarnings"/>).
		/// Set at each review.
		/// </summary>
		double Earnings = 0;

		/// <summary>
		/// How well people here have been living, 0..1: food and water met, and medicine, smoothed over
		/// about a month (<see cref="People.WellbeingDays"/>). 1 for a settlement that has wanted for nothing.
		/// </summary>
		double Wellbeing = 1;

		/// <summary>Medical needs met, smoothed the same way as <see cref="Wellbeing"/>.</summary>
		double HealthWellbeing = 1;

		/// <summary>How strongly this place pulls migrants (living standard times earnings), set at each review.</summary>
		double Attraction = 1;

		/// <summary>People who have left on contract passage (<see cref="Contracts"/>) and not yet been counted against the
		/// migration that would have taken them anyway: the next migration takes that many fewer from here.</summary>
		double Emigrated = 0;

		/// <summary>Net people a day arriving (positive) or leaving (negative) by migration, over the last review.</summary>
		double NetMigration = 0;

		/// <summary>
		/// People here with no room (U.SPC-T98): the growth a settlement at its population cap could not take,
		/// and migrants who would have left for a place that was full. What a faction founds colonies with
		/// (<see cref="Colonisation"/>); held to <see cref="People.OverflowShare"/> of the population (the rest give up).
		/// </summary>
		double Overflow = 0;

		/// <summary>Founded in play (an outpost, a colony, a ruin) or one of the system's own (U.SPC-T98).</summary>
		ColonyStage Stage = static_cast<ColonyStage>(0);

		/// <summary>For a founded settlement: when it was founded, and when it was abandoned (NaN if it never was).</summary>
		double FoundedJd = cs::Double::NaN, RuinedJd = cs::Double::NaN;

		/// <summary>For a founded settlement: the site it stands on (what it can dig, how far it can grow).</summary>
		cs::Ref<ColonySite> Site;

		double Flow(Good g) const;

		/// <summary>The stock a market wants on hand: <see cref="CoverDays"/> of its larger flow.</summary>
		double Target(Good g) const;

		double Capacity(Good g) const;

		/// <summary>Price at a hypothetical stock level, with the same curve the market uses.</summary>
		double PriceAt(Good g, double stock) const;

		void RefreshPrices();

		/// <summary>Tonnes a trader can buy without dipping into the local reserve.</summary>
		double Available(Good g) const;

		/// <summary>Tonnes this market would still take before it is over-supplied, counting deliveries on the way.</summary>
		double Absorb(Good g) const;

		/// <summary>Credits for selling <paramref name="tonnes"/>, walking the price down as stock rises.</summary>
		double SaleValue(Good g, double tonnes) const;

		/// <summary>Credits for buying <paramref name="tonnes"/>, walking the price up as stock falls.</summary>
		double PurchaseCost(Good g, double tonnes) const;

		/// <summary>
		/// A copy of this market's board as it stands at this moment (its stock, prices, flows and deliveries booked),
		/// for news carried elsewhere (U.SPC-T100): it prices a sale with the same curve as the live market, but stays
		/// as it was when it was taken, however the real market moves on. No facilities, platforms or people's state.
		/// </summary>
		cs::Ref<Settlement> Snapshot() const;

		cs::String ToString() const { return Name; }
	};
}
