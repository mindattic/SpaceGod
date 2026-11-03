#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Companies.cs (US-T19).

#include "Cs/Cs.h"
#include "BodyDefinition.h" // Rgb (by value in Company::Livery)

#include <tuple>

namespace SpaceGod::Core
{
	class Settlement;
	class Faction;
	class Ship;
	class World;
	enum class Good : int;
	enum class ShipClass : int;

	/// <summary>What became of a trading company.</summary>
	enum class CompanyFate : int
	{
		/// <summary>Trading.</summary>
		Trading,

		/// <summary>Bought out by a rival, which took its hulls, its routes and its debts.</summary>
		Merged,

		/// <summary>Ran out of money: its hulls were sold off or broken up and it was wound up.</summary>
		Bankrupt,
	};
	const char* ToString(CompanyFate v);

	/// <summary>
	/// A run a company's ships have worked: loaded at one port, sold at another, and what it earned. The
	/// company learns which runs pay from these and points its fleet at the best of them.
	/// </summary>
	class TradeRoute final
	{
	public:
		cs::Ref<Settlement> From, To;

		/// <summary>Runs completed, and their profit and days in all (smoothed toward the latest, see <see cref="Companies.RouteMemory"/>).</summary>
		int Runs = 0;
		double Profit = 0, Days = 0;
		double LastJd = 0;

		/// <summary>Profit per day under way on this run, credits.</summary>
		double PerDay() const { return Days > 0 ? Profit / Days : 0; }

		/// <summary>Whether the run joins two planetary systems by hyperlane (Earth to Ceres) rather than staying in one.</summary>
		bool CrossSystem = false;

		cs::String ToString() const;
	};

	/// <summary>
	/// An NPC trading corporation (U.SPC-T99): it owns a fleet of freighters, ore haulers and couriers, takes their
	/// trade profit, pays their crews and upkeep and a tax to the flag it sails under, buys new hulls at yards when
	/// the fleet earns enough to pay them back, learns which runs pay and steers its ships to them, and goes under
	/// (or is bought out) when it cannot pay its way. Its ships sail under its flag faction's colours for docking
	/// and the law; the company is who profits.
	/// </summary>
	class Company final
	{
	public:
		cs::String Name;

		/// <summary>A short house mark ("KFL") for lists.</summary>
		cs::String Tag;

		/// <summary>The colours its hulls are painted in.</summary>
		Rgb Livery;

		/// <summary>Where it is headquartered: it prefers runs from and to here and buys its hulls here when it can.</summary>
		cs::Ref<Settlement> HomePort;

		/// <summary>The faction it is registered with and sails under (its hulls' <see cref="Ship.Faction"/>).</summary>
		cs::Ref<Faction> Flag;

		/// <summary>Cash in hand, credits (negative: in debt to its bankers).</summary>
		double Cash = 0;

		/// <summary>The hulls it owns now (anywhere, abroad too).</summary>
		cs::List<cs::Ref<Ship>> Ships = cs::List<cs::Ref<Ship>>();

		/// <summary>How each faction regards it, -1 (barred) to 1 (favoured). It trades at a faction's ports only above
		/// <see cref="Companies.ShunnedStanding"/> and buys hulls at its yards only from <see cref="Companies.YardStanding"/>.</summary>
		cs::Dictionary<cs::Ref<Faction>, double> Standing = cs::Dictionary<cs::Ref<Faction>, double>();

		/// <summary>The runs it knows, and the ones it is steering its fleet to.</summary>
		cs::List<cs::Ref<TradeRoute>> Routes = cs::List<cs::Ref<TradeRoute>>();
		cs::List<cs::Ref<TradeRoute>> Chosen = cs::List<cs::Ref<TradeRoute>>();

		double FoundedJd = 0;
		CompanyFate Fate = CompanyFate::Trading;
		double FateJd = 0;

		/// <summary>The company that bought it out, if it merged.</summary>
		cs::Ref<Company> MergedInto;

		/// <summary>Net income a day (trade profit less running costs and tax), smoothed over a couple of months.</summary>
		double IncomePerDay = 0;

		/// <summary>Trade profit a hull earns a day, by hull class, smoothed (NaN until a hull of that class has worked).</summary>
		cs::List<double> ClassPerDay = { cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN, cs::Double::NaN };

		/// <summary>Lifetime trade profit, running costs, tax and hull purchases, credits.</summary>
		double Earned = 0, RunningCosts = 0, TaxPaid = 0, HullsSpent = 0;
		int HullsBought = 0, HullsLost = 0, HullsSold = 0, HullsScrapped = 0;

		/// <summary>A hull on order: its class, the yard building it and when it was ordered. The price is held from
		/// <see cref="Cash"/> until the yard lays it down.</summary>
		cs::Nullable<ShipClass> OnOrder;
		cs::Ref<Settlement> OrderYard;
		double OrderJd = 0, OrderPrice = 0;

		/// <summary>When it last went into debt, or NaN while solvent.</summary>
		double InDebtSinceJd = cs::Double::NaN;

		bool Trading() const { return Fate == CompanyFate::Trading; }

		int Hulls() const { return Ships.Count(); }

		/// <summary>Its standing with <paramref name="f"/> (0 for a faction it has no dealings with).</summary>
		double StandingWith(const cs::Ref<Faction>& f) const;

		void Adjust(const cs::Ref<Faction>& f, double by); // internal

		/// <summary>What it is worth: cash and its hulls at second-hand prices.</summary>
		double Value() const;

		/// <summary>The known run from <paramref name="from"/> to <paramref name="to"/>, or null.</summary>
		cs::Ref<TradeRoute> RouteOf(const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to) const;

		/// <summary>
		/// Trade profit a year, after running costs and tax, one more hull of <paramref name="cls"/> could be expected to
		/// earn: from what its own hulls of that class make, else what the trade makes (<paramref name="marketPerDay"/>,
		/// what every company's do), else a guess from its freighters scaled by hold size. 0 with nothing to go on.
		/// </summary>
		double ExpectedYearly(ShipClass cls, double marketPerDay = cs::Double::NaN) const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// The trading companies of one system (U.SPC-T99) and the market in hulls between them: every ten days each
	/// company books its ships' trade, pays its running costs and tax, adjusts its standing with the factions it deals
	/// with, re-picks the runs it steers to, orders a hull if its fleet is earning enough to pay one back, and if it is
	/// in trouble sells hulls, is bought out by a richer rival flying friendly colours, or goes bankrupt. New companies
	/// are founded at busy ports when the trade pays. So the merchant fleet's size and where it trades come out of
	/// what trade earns, rather than a fixed number of spawns.
	/// </summary>
	class Companies final
	{
		cs::Ref<World> world;
		cs::Random rng;

	public:
		cs::List<cs::Ref<Company>> All = cs::List<cs::Ref<Company>>();

		/// <summary>The most companies trading at once, and how often a new one may be founded (days).</summary>
		int MaxCompanies = 8;
		double FoundingDays = 60;

		static constexpr double ReviewDays = 10;

		/// <summary>How fast a company's sense of its income follows the latest (days).</summary>
		static constexpr double SmoothingDays = 60;

		/// <summary>How much the latest run counts in a route's record against the ones before.</summary>
		static constexpr double RouteMemory = 0.35;

		/// <summary>Share of positive profit paid to the flag as tax.</summary>
		static constexpr double CorporateTax = 0.1;

		/// <summary>A hull is bought only if one more is expected to pay its price back within this many years ...</summary>
		static constexpr double PaybackYears = 5;

		/// <summary>... and the company keeps this share of the price in hand after paying it.</summary>
		static constexpr double ReserveShare = 0.25;

		/// <summary>What a second-hand hull fetches (to a rival), and what a breaker pays for one, as shares of its price.</summary>
		static constexpr double ResaleShare = 0.55, ScrapShare = 0.15;

		/// <summary>Debt beyond which a company sells hulls, and how long it may stay in debt before it is wound up (days).</summary>
		static constexpr double DebtLimit = 4e6;
		static constexpr double BankruptDays = 180;

		/// <summary>Below this standing a company will not trade at a faction's ports; from this one it may buy at its yards.</summary>
		static constexpr double ShunnedStanding = -0.5, YardStanding = -0.1;

		/// <summary>How much more a run the company has chosen scores in a captain's planning, and its home port.</summary>
		static constexpr double ChosenBonus = 0.3, HomeBonus = 0.1;

		/// <summary>How many runs a company steers its fleet to.</summary>
		static constexpr int ChosenRoutes = 6;

		/// <summary>An order a yard has not laid down in this many days is cancelled and placed again.</summary>
		static constexpr double OrderPatienceDays = 120;

		/// <summary>Capital the investors put into a new company, credits.</summary>
		// (C# initialiser `2.2 * Economy.HullPrice(ShipClass.Freighter)`: set in the constructor's initialiser list,
		// in this same declaration order, so this header need not include Economy.h.)
		double FoundingCapital;

	private:
		double nextReviewJd = cs::Double::NaN, lastReviewJd = cs::Double::NaN, nextFoundingJd = cs::Double::NaN;
		int foundedCount = 0, hullsNamed = 0;

	public:
		Companies(cs::Ref<World> world, int seed);

		// (C# yield iterator, never stopped early: built eagerly, see docs/MIGRATION.md.)
		cs::List<cs::Ref<Company>> Live() const;

		int LiveCount() const;

		/// <summary>Whether this system's merchant shipping is company-run (else the yards' owners build it, as before).</summary>
		bool Enabled() const { return All.Count() > 0; }

		// ---------------------------------------------------------------- founding

		/// <summary>Founds a company headquartered at <paramref name="home"/>, registered with <paramref name="flag"/>, with <paramref name="cash"/> in hand.</summary>
		cs::Ref<Company> Found(const cs::String& name, const cs::String& tag, Rgb livery, const cs::Ref<Settlement>& home, const cs::Ref<Faction>& flag, double cash, double jd);

		/// <summary>Puts <paramref name="ship"/> in <paramref name="c"/>'s fleet, under its flag; its trade from here on is the company's.</summary>
		void Enlist(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship);

		/// <summary>
		/// A hull leaving <paramref name="c"/>'s fleet between reviews (sold, merged, taken into the player's fleet): what it
		/// has taken since the last review is booked to the company now, as <see cref="Book"/> would, before it goes.
		/// </summary>
		void SettleOut(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship);

		/// <summary>A company hull came into this system (through a gate, as a copy): it is still its company's.</summary>
		void OnShipAdded(const cs::Ref<Ship>& ship);

		// ---------------------------------------------------------------- what captains are told

		/// <summary>
		/// How a company's captain weighs a run from <paramref name="from"/> (null: whatever is in the hold already) to
		/// <paramref name="to"/>: 0 for a port of a faction that shuns the company, more for one of the company's chosen
		/// runs or its home port, less for a run it has lost money on. 1 for a ship no company owns.
		/// </summary>
		static double RouteFactor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to);

		/// <summary>A company ship loaded a cargo at <paramref name="from"/>: what the run earns is reckoned from here.</summary>
		void OnLoaded(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from);

		/// <summary>A company ship sold its cargo at <paramref name="to"/>: the run goes in the company's book of routes,
		/// and the port's owner thinks a little better of the company.</summary>
		void OnSold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& to, Good good, double tonnes);

		/// <summary>Puts a completed run in <paramref name="c"/>'s book of routes.</summary>
		void Record(const cs::Ref<Company>& c, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, double profit, double days, double jd);

		// ---------------------------------------------------------------- the review

		void Update(double jd);

		/// <summary>One review of every trading company, now (<see cref="Update"/> holds one every <see cref="ReviewDays"/>).</summary>
		void Review(double jd);

	private:
		/// <summary>
		/// Takes in what <paramref name="c"/>'s ships earned since the last review (the change in their running
		/// profit, which their flag's treasury booked for them and now hands over), pays their crews and upkeep and
		/// the flag's tax, and drops hulls it no longer has.
		/// </summary>
		void Book(const cs::Ref<Company>& c, double days, double jd);

		/// <summary>Standing drifts: down toward hostility with whoever is at war with the company's flag, back toward
		/// neutral with everyone else, and the flag always thinks well of its own.</summary>
		void Standings(const cs::Ref<Company>& c);

		/// <summary>Picks the runs <paramref name="c"/> steers its fleet to: the best paying a day it knows, worked
		/// recently, at ports that still deal with it.</summary>
		void ChooseRoutes(const cs::Ref<Company>& c, double jd);

		/// <summary>A company in debt sells a hull to pay it down, is bought out, or after long enough is wound up. True if it is in trouble.</summary>
		bool Trouble(const cs::Ref<Company>& c, double jd);

		// ---------------------------------------------------------------- growth

		/// <summary>
		/// If <paramref name="c"/>'s fleet earns enough that one more hull pays its price back in
		/// <see cref="PaybackYears"/> and it has the price and a reserve in hand, it orders one at the nearest yard
		/// that will build for it (its home port's first). An order the yard has sat on too long is placed again.
		/// </summary>
		void Expand(const cs::Ref<Company>& c, double jd);

	public:
		/// <summary>What a hull of <paramref name="cls"/> earns a day across the companies that run them (hull-weighted), or NaN if none do.</summary>
		double MarketPerDay(ShipClass cls) const;

	private:
		int Ordered() const;

		bool YardWillBuild(const cs::Ref<Company>& c, const cs::Ref<Settlement>& yard) const;

	public:
		/// <summary>Where <paramref name="c"/> orders its hulls: its home port if it has a yard that will build for it, else the
		/// yard in the same planetary system, else the one whose owner thinks best of it (fewest build points owed first).</summary>
		cs::Ref<Settlement> YardFor(const cs::Ref<Company>& c) const;

	private:
		void CancelOrder(const cs::Ref<Company>& c);

	public:
		/// <summary>The hull a yard should lay down for a company next (the oldest order placed with it), or null.</summary>
		cs::Nullable<ShipClass> OrderAt(const cs::Ref<Settlement>& yard) const;

	private:
		cs::Ref<Company> FirstOrderAt(const cs::Ref<Settlement>& yard, cs::Nullable<ShipClass> cls) const;

	public:
		/// <summary>Whether a company has already paid for the <paramref name="cls"/> hull <paramref name="yard"/> is about to lay down.</summary>
		bool PaidFor(const cs::Ref<Settlement>& yard, ShipClass cls, double price) const;

		/// <summary>The yard has finished a hull of <paramref name="cls"/>: the company whose order it was pays the yard's
		/// owner and takes delivery. Returns that company, or null if nobody ordered it.</summary>
		cs::Ref<Company> TakeDelivery(const cs::Ref<Settlement>& yard, ShipClass cls, double jd);

		/// <summary>A name for a new company hull ("Kepler Endeavour"), from the company's own name and a list of ship names.</summary>
		cs::String HullName(const cs::Ref<Company>& c, const cs::List<cs::String>& names);

		// ---------------------------------------------------------------- trouble

	private:
		/// <summary>The hull a company in trouble lets go of first: the one earning least, of those in port and free to go.</summary>
		cs::Ref<Ship> Weakest(const cs::Ref<Company>& c) const;

		bool Saleable(const cs::Ref<Ship>& s) const;

		/// <summary>Sells <paramref name="c"/>'s weakest hull: to the richest rival under friendly colours that can use it, else to the breakers.</summary>
		void SellHull(const cs::Ref<Company>& c, double jd);

		void Scrap(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship, double jd);

		bool Friendly(const cs::Ref<Company>& a, const cs::Ref<Company>& b) const;

	public:
		/// <summary>
		/// A company in debt is bought out by the richest healthy rival under friendly colours that can pay what it is
		/// worth (its hulls at second-hand prices, less its debt) and keep a reserve: the rival takes its hulls, its
		/// routes and its debt, and the shareholders are paid off. True if it merged.
		/// </summary>
		bool TryMerge(const cs::Ref<Company>& target, double jd);

		/// <summary><paramref name="buyer"/> takes over <paramref name="target"/>: its hulls (repainted, under the buyer's flag), its
		/// routes, its cash or debt, for <paramref name="price"/> paid to its shareholders.</summary>
		void Merge(const cs::Ref<Company>& buyer, const cs::Ref<Company>& target, double price, double jd);

		/// <summary>Winds <paramref name="c"/> up: rivals buy what hulls they can use, the breakers take the rest.</summary>
		void Bankrupt(const cs::Ref<Company>& c, double jd);

		// ---------------------------------------------------------------- new companies

		/// <summary>
		/// Founds a company when the trade pays and there is room: at the busiest port that has no company of its own,
		/// with capital from investors under the port owner's flag (its treasury), if the companies already trading
		/// earn well enough per hull that a new fleet would pay.
		/// </summary>
		cs::Ref<Company> TryFound(double jd);

	private:
		static const cs::List<cs::String>& Houses();

		std::tuple<cs::String, cs::String> NewName(const cs::Ref<Settlement>& port) const;

		static cs::String TagOf(const cs::String& name);

	public:
		/// <summary>A livery for the <paramref name="n"/>th company: hues spaced by the golden angle so neighbours differ.</summary>
		static Rgb LiveryFor(int n);

	private:
		static Rgb Hsv(double h, double s, double v);

		static double Channel(double n, double h, double s, double v);

	public:
		static cs::String Credits(double c);
	};
}
