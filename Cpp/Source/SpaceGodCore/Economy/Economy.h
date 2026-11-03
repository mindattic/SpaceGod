#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Economy.cs (US-T18).

#include "Cs/Cs.h"
#include "Economy/Propulsion.h" // TripEstimate (by value in EstimateTrip)

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Settlement;
	class Diplomacy;
	class Ship;
	class Faction;
	class Colonisation;
	class Recipe;
	class Facility;
	enum class Good : int;
	enum class ShipClass : int;
	enum class IndustryKind : int;
	enum class SettlementKind : int;

	/// <summary>
	/// The system's markets. Every settlement produces through its facilities (each limited by its
	/// scarcest input and by warehouse space), feeds its people, and prices every good from its stock
	/// against what it uses. Merchant ships do the rest: at each port a trader looks for the cargo and
	/// destination that earn the most per day of travel, counting deliveries already on the way, so
	/// goods flow from surplus to shortage on their own. Ships' profits go to their faction, ports tax
	/// the trade they host, and shipyards turn alloys, electronics and machinery into new hulls.
	/// </summary>
	class SPACEGODCORE_API Economy final : public std::enable_shared_from_this<Economy>
	{
	public:
		cs::List<cs::Ref<Settlement>> Settlements = cs::List<cs::Ref<Settlement>>();
		cs::Ref<::SpaceGod::Core::Diplomacy> Diplomacy;

		/// <summary>Economic time; advanced in ticks by <see cref="AdvanceTo"/>.</summary>
		double Jd = 0;

		/// <summary>Estimated days for a ship to fly between two bodies (ship, from, to, departure date).</summary>
		cs::Func<double, cs::Ref<Ship>, int, int, double> EstimateTravelDays;

		/// <summary>
		/// What a trip takes (days, the delta-v of its own burns, the lanes it rides) for a ship between two
		/// bodies at a date. When set, traders price every run by its real propellant, lane tolls and days
		/// (and drop runs their tanks cannot cover); when null, by days alone.
		/// </summary>
		cs::Func<TripEstimate, cs::Ref<Ship>, int, int, double> EstimateTrip;

		/// <summary>Extra cost a trader puts on a body (pirates, battles), as a fraction of a trip's value.</summary>
		cs::Func<double, int> Danger;

		/// <summary>Called when a yard has banked enough to lay down a hull. Return the class to build, or null to wait.</summary>
		cs::Func<cs::Nullable<ShipClass>, cs::Ref<Settlement>> ChooseBuild;

		/// <summary>
		/// Whether a settlement's owner favours an industry (its production focus, U.SPC-T423: it grows on thinner margins and
		/// is kept up through a glut) and the extra growth a review its research gives it. Unset, every industry follows its market.
		/// </summary>
		cs::Func<std::tuple<bool, double>, cs::Ref<Settlement>, IndustryKind> Priority;

		/// <summary>Factions founding new settlements as their people run out of room (U.SPC-T98), run at each review; null for none.</summary>
		cs::Ref<::SpaceGod::Core::Colonisation> Colonisation;

		/// <summary>Raised when a yard completes a hull; the world puts it into service.</summary>
		cs::Action<cs::Ref<Settlement>, ShipClass, double> ShipBuilt;

		/// <summary>Raised when a trader sells a load at a port (ship, port, good, tonnes): who supplies whom, for the news and for embargoes.</summary>
		/// (A plain Action field that World and MarketShocks chain with `+=`: a cs::Delegate.)
		cs::Delegate<cs::Ref<Ship>, cs::Ref<Settlement>, Good, double> Sold;

		/// <summary>Whether a yard's owner has the hands to crew a hull of this class (see <see cref="Crews"/>); null for always.</summary>
		cs::Func<bool, cs::Ref<Settlement>, ShipClass> CanCrew;

		/// <summary>Whether a buyer (a trading company, see <see cref="Companies"/>) has already paid for the hull a yard is
		/// about to lay down, so its owner's treasury is not charged; null for nobody.</summary>
		cs::Func<bool, cs::Ref<Settlement>, ShipClass, double> PaidFor;

		/// <summary>Raised when a ship loads a cargo for sale (the ship and the settlement it bought at).</summary>
		/// (A plain Action field that World chains with `+=`: a cs::Delegate.)
		cs::Delegate<cs::Ref<Ship>, cs::Ref<Settlement>> Loaded;

		/// <summary>
		/// How a ship's owners weigh a run from one settlement (null: the cargo already aboard) to another: multiplies a
		/// paying run's score in <see cref="PlanTrade"/>, and 0 rules the run out (see <see cref="Companies.RouteFactor"/>).
		/// Null for 1.
		/// </summary>
		cs::Func<double, cs::Ref<Ship>, cs::Ref<Settlement>, cs::Ref<Settlement>> RouteFactor;

	private:
		double RouteFactorOf(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to) const;

	public:
		/// <summary>Port tax on every sale and purchase, paid to the port's owner.</summary>
		static constexpr double PortTax = 0.04;

		/// <summary>
		/// The smallest load worth booking, tonnes, for a market that wants less than a usual load (an outpost's
		/// medicine, a fab's platinum); bigger markets are sent at least 2% of the hold, up to 20 t.
		/// </summary>
		static constexpr double MinLoadTonnes = 2;

		static constexpr double TickDays = 0.25;

		/// <summary>Ships' propellant purchases count toward a market's demand averaged over about this many days.</summary>
		static constexpr double PropellantDemandDays = 10;

		/// <summary>A hyperlane's toll for one ride: a flat fee plus so much per tonne of ship (hull, tanks and hold).</summary>
		static constexpr double LaneTollBase = 5000, LaneTollPerTonne = 0.5;

	private:
		cs::Random rng;
		double nextReviewJd = 0, lastReviewJd = 0;

	public:
		Economy(cs::Ref<::SpaceGod::Core::Diplomacy> diplomacy, double startJd, int seed = 2077);

		cs::Ref<Settlement> Add(const cs::String& name, int body, SettlementKind kind, cs::Ref<Faction> owner, double population);

		/// <summary>Recomputes every settlement's full-capacity demand and supply and stocks it at its target cover.</summary>
		void Initialise();

		/// <summary>
		/// Opens a settlement added in play (a colony founded, U.SPC-T98) for business: its flows, prices and crews
		/// as <see cref="Initialise"/> sets them, but with only the stock it was given.
		/// </summary>
		void Open(const cs::Ref<Settlement>& s);

		cs::List<cs::Ref<Settlement>> At(int body) const;

		// ---------------------------------------------------------------- production and consumption

		/// <summary>Runs the markets forward to <paramref name="jd"/> in ticks of about <see cref="TickDays"/>.</summary>
		void AdvanceTo(double jd);

	private:
		void Tick(double dt);
		static void RecomputeFlows(const cs::Ref<Settlement>& s);

		/// <summary>Folds what ships bought as propellant since the last tick into the market's smoothed propellant demand.</summary>
		static void FoldPropellantSales(const cs::Ref<Settlement>& s, double dt);

		void Produce(const cs::Ref<Settlement>& s, double dt);
		static void Consume(const cs::Ref<Settlement>& s, double dt);

		/// <summary>A haven's fences smuggle loot onward to the black market: stock drains, the owner is paid.</summary>
		static void Fence(const cs::Ref<Settlement>& s, double dt);

		/// <summary>Every 10 days: facilities that sell dear expand, those glutted shrink; yards lay down hulls.</summary>
		void Review();

		/// <summary>Value of a recipe's outputs over its inputs at local prices (mines count only their running costs).</summary>
		static double Margin(const cs::Ref<Settlement>& s, const cs::Ref<Recipe>& r);

	public:
		/// <summary>Build points (tonnes of alloys through the yard) to lay down a hull.</summary>
		static double HullCost(ShipClass c);

		/// <summary>
		/// Share of a warship's (or platform's) price its navy pays a year to keep it in service: crews,
		/// fuel, spares. Operating and support is about 70% of a US weapon system's life-cycle cost (GAO),
		/// so over 30 years each year costs 0.7 / 0.3 / 30 of the purchase.
		/// </summary>
		static constexpr double UpkeepPerYear = 0.078;

		/// <summary>Credits the owner pays the yard (crew, weapons, fit-out) on top of the materials.</summary>
		static double HullPrice(ShipClass c);

		// ---------------------------------------------------------------- trade

		static double CapacityOf(ShipClass c);

		/// <summary>The goods a hull is fitted to carry: bulk holds for haulers, secure holds for couriers.</summary>
		static bool Carries(ShipClass c, Good g);

		/// <summary>Crew, upkeep and insurance a day under way (propellant and tolls are counted separately).</summary>
		static double RunningCostPerDay(ShipClass c);

		/// <summary>The toll for one hyperlane ride by a ship of <paramref name="massTonnes"/>, credits.</summary>
		static double LaneToll(double massTonnes);

		/// <summary>The toll a ship of <paramref name="cls"/> pays for one ride: a bulk freighter rides at its bulk share
		/// (<see cref="BulkFreighters.LaneTollShare"/>, U.SPC-T482), any other ship the full toll for its mass.</summary>
		static double LaneToll(ShipClass cls, double massTonnes);

	private:
		/// <summary>What a run costs and whether the tanks cover it (see <see cref="PriceRun"/>).</summary>
		struct Run
		{
			double Days = 0, Propellant = 0, Cost = 0;
			bool Reachable = false;
		};

		/// <summary>
		/// Prices a run from <paramref name="from"/> to <paramref name="to"/> by what it really takes: the
		/// days under way at the ship's running cost, the propellant its burns throw out with
		/// <paramref name="cargoTonnes"/> aboard and <paramref name="tanks"/> in the tanks (rocket equation)
		/// at <paramref name="fuelPrice"/> a tonne, and a toll for every lane it rides. Unreachable if the
		/// tanks do not hold that propellant and a reserve. Without <see cref="EstimateTrip"/>, days alone.
		/// </summary>
		Run PriceRun(const cs::Ref<Ship>& ship, int from, int to, double jd, double cargoTonnes, double tanks, double fuelPrice) const;

	public:
		/// <summary>Whether <paramref name="ship"/>'s tanks, as they stand, cover a run from <paramref name="from"/> to <paramref name="to"/> (always true when trips are not priced).</summary>
		bool CanReach(const cs::Ref<Ship>& ship, int from, int to, double jd) const;

		// A fallen haven makes and trades nothing until it is rebuilt.
		bool CanTrade(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const;

		/// <summary>Whether the law bars <paramref name="ship"/> from a port its faction could otherwise use (see <see cref="Law.CanDock"/>).</summary>
		bool LawRefuses(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const;

		/// <summary>The law's say on docking: false bars a ship from a port (its commander's standing there, see <see cref="Law.CanDock"/>); null for no law.</summary>
		cs::Func<bool, cs::Ref<Ship>, cs::Ref<Settlement>> DockAllowed;

		/// <summary>What standing does to a ship's prices at a port: the player's hold purchases cost this much more
		/// and its sales fetch this much less (see <see cref="Law.PriceFactor"/>); null for 1.</summary>
		cs::Func<double, cs::Ref<Ship>, cs::Ref<Settlement>> PriceFactor;

	private:
		double PriceFactorOf(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const;

	public:
		/// <summary>Sells whatever is in the hold at the best market at the body the ship has reached.</summary>
		void OnArrive(const cs::Ref<Ship>& ship, int body, double jd);

	private:
		void Sell(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double value);

	public:
		/// <summary>Cancels a ship's booking of its cargo at its destination (it arrived, died or changed plans).</summary>
		void Release(const cs::Ref<Ship>& ship);

		/// <summary>Rebuilds every market's book of deliveries on the way from what ships are actually carrying.</summary>
		void RecountInbound(const cs::List<cs::Ref<Ship>>& ships);

	private:
		struct Option
		{
			cs::Ref<Settlement> From, To;
			::SpaceGod::Core::Good Good = static_cast<::SpaceGod::Core::Good>(0);
			double Tonnes = 0, Score = 0, Cost = 0, Bonus = 0;
			bool Empty = false;
		};

	public:
		/// <summary>
		/// Picks a trader's next port from <paramref name="here"/> and loads its cargo: the trade with the
		/// best profit per day, among the top few (so the fleet does not stampede), or an empty run to
		/// where the best trades start. Returns -1 if nothing is worth doing.
		/// </summary>
		int PlanTrade(const cs::Ref<Ship>& ship, int here, double jd);

		/// <summary>
		/// A captain on a fixed trade route (U.SPC-T102): loads, at <paramref name="here"/>, the cargo that earns the
		/// most per day carried to the route's next stop, <paramref name="toBody"/>, if any run there pays at all.
		/// Paid for and booked as <see cref="PlanTrade"/> does. Returns whether it loaded anything.
		/// </summary>
		bool LoadFor(const cs::Ref<Ship>& ship, int here, int toBody, double jd);

		/// <summary>
		/// Puts <paramref name="tonnes"/> of <paramref name="good"/> the ship came by without buying (ore cut from a
		/// mining claim) in its hold, at no cost, and books it for the best market it can reach; returns that body, or -1.
		/// </summary>
		int LoadOwn(const cs::Ref<Ship>& ship, Good good, double tonnes, int here, double jd);

		/// <summary>Sells <paramref name="tonnes"/> of an own-mined <paramref name="good"/> straight to a market on <paramref name="here"/>
		/// that wants it (the claim body's own port); false, hold untouched, when none does.</summary>
		bool SellOwn(const cs::Ref<Ship>& ship, Good good, double tonnes, int here, double jd);

	private:
		/// <summary>
		/// The loads a ship could take from <paramref name="from"/> and where to: each priced by its run
		/// (<see cref="PriceRun"/>) plus <paramref name="extraDays"/> and <paramref name="extraCost"/> already
		/// spent getting there, leaving out any its <paramref name="tanks"/> cannot carry that far.
		/// </summary>
		void AddLoadedOptions(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, int fromBody, double jd, double extraDays, double extraCost, double tanks, double fuelPrice, cs::List<Option>& into);

		/// <summary>
		/// Owners post supply contracts for essentials their settlements are short of: a flat fee per
		/// run plus a premium per tonne, scaled by how short they are, paid on delivery.
		/// </summary>
		static double ContractBonus(const cs::Ref<Settlement>& to, Good g, double tonnes);

		/// <summary>What a load will fetch once the deliveries already heading there have landed.</summary>
		static double SaleEstimate(const cs::Ref<Settlement>& to, Good g, double tonnes);

		void Load(const cs::Ref<Ship>& ship, const Option& o);

	public:
		/// <summary>
		/// Loads a contract's lot (see <see cref="Contracts"/>): <paramref name="tonnes"/> of <paramref name="good"/>
		/// bought at <paramref name="from"/> as <see cref="Load"/> buys, booked for delivery at <paramref name="to"/>
		/// (on its inbound book) with no supply bonus: the contract pays for the run.
		/// </summary>
		void LoadFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, Good good, double tonnes, const cs::Ref<Settlement>& to);

		/// <summary>
		/// Loads <paramref name="tonnes"/> of <paramref name="good"/> bought at <paramref name="from"/>
		/// for a market outside this economy (another system's, across the gates): paid for as
		/// <see cref="Load"/> pays, but booked at no settlement here. <paramref name="bound"/> names
		/// where it is going, for the HUD. Returns what it cost, tax included.
		/// </summary>
		double LoadUnbooked(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, Good good, double tonnes, const cs::String& bound);

		static cs::String CargoText(const cs::Ref<Ship>& ship);

	private:
		/// <summary>
		/// Nothing worth doing from here: a ship running low makes for the nearest port it can reach that sells
		/// propellant, so it is not stranded somewhere with none; otherwise -1 (it waits for work).
		/// </summary>
		int LowOnPropellant(const cs::Ref<Ship>& ship, int here, double jd);

	public:
		/// <summary>
		/// The quickest port from <paramref name="here"/> that sells <paramref name="ship"/> propellant and that
		/// its tanks can still reach, or -1 (also -1 if it can fill up where it is, or trips are not priced).
		/// </summary>
		int NearestPropellant(const cs::Ref<Ship>& ship, int here, double jd) const;

	private:
		cs::Ref<Settlement> NearestPropellantPort(const cs::Ref<Ship>& ship, int here, double jd) const;

		double TravelDays(const cs::Ref<Ship>& ship, int from, int to, double jd) const;

		double DangerAt(int body) const;

		static double Best(const cs::List<Option>& options);

		/// <summary>A weighted pick among the best few options, so traders spread across good routes.</summary>
		Option Choose(cs::List<Option>& options);

	public:
		// ---------------------------------------------------------------- propellant

		/// <summary>Whether <paramref name="ship"/> can buy any propellant at <paramref name="s"/> (it may dock, and there is reaction mass to spare).</summary>
		bool HasPropellant(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const;

		/// <summary>Whether any market at <paramref name="body"/> sells <paramref name="ship"/> propellant.</summary>
		bool HasPropellantAt(const cs::Ref<Ship>& ship, int body) const;

		/// <summary>
		/// A tonne of propellant at <paramref name="s"/>, credits: the cheaper reaction mass it has to spare
		/// (water or hydrocarbons) and the helium-3 that burns it, with the port tax.
		/// </summary>
		static double PropellantPrice(const cs::Ref<Settlement>& s);

		/// <summary>The best price <paramref name="ship"/> can fill its tanks at <paramref name="body"/>, or the systemwide base price if nowhere there sells it.</summary>
		double PropellantPriceAt(const cs::Ref<Ship>& ship, int body) const;

		/// <summary>
		/// Tops up a ship's tanks at <paramref name="body"/> (an AI ship in port): from the cheapest market
		/// there it may trade at, reaction mass from what it can spare beyond its own reserve, paid from the
		/// ship's faction. Returns the tonnes taken on. A tank at or above the top-up threshold is left alone unless
		/// <paramref name="force"/> (a planned trip it cannot cover as it stands, U.SPC-T545).
		/// </summary>
		double Refuel(const cs::Ref<Ship>& ship, int body, double jd, bool force = false);

		// ---------------------------------------------------------------- player trade

		/// <summary>Result of one manual buy/sell click, for the HUD to pick a toast from. <see cref="Ok"/>
		/// also covers a partial fill (ran out of stock, credits or hold room partway through a multi-crate
		/// request), since part of the trade still went through.</summary>
		enum class TradeOutcome : int
		{
			Ok,
			CannotDock,
			NoStock,
			NoRoom,
			CantAfford,
			NothingToSell,

			/// <summary>The port's faction could deal with the ship's, but the law bars the ship from docking (see <see cref="Law.CanDock"/>).</summary>
			Refused,

			/// <summary>All there is of that good aboard is stolen, and a lawful market will not take it.</summary>
			Contraband,
		};
		friend const char* ToString(TradeOutcome v);

		/// <summary>
		/// Fills the player's piloted <paramref name="ship"/> at <paramref name="s"/>, as far as the market can
		/// spare and the faction's credits stretch. Reports the tonnes and what they cost.
		/// </summary>
		TradeOutcome RefuelAt(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double& tonnes, double& cost);

	private:
		/// <summary>
		/// Buys up to <paramref name="tonnes"/> of propellant into a ship's tanks at <paramref name="s"/>:
		/// reaction mass cheapest first from what the market can spare, and the helium-3 that burns it from
		/// what it has (a port short of helium-3 still fills the tanks). Stock, prices, the port's tax and its
		/// propellant demand all follow; the ship's faction pays (only what it can, if <paramref name="mustAfford"/>).
		/// </summary>
		double BuyPropellant(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double tonnes, bool mustAfford, double& cost);

		/// <summary>Takes <paramref name="tonnes"/> of a good out of a market as propellant; returns what it cost with tax.</summary>
		static double Purchase(const cs::Ref<Settlement>& s, Good g, double tonnes);

	public:
		/// <summary>
		/// Buys up to <paramref name="crates"/> standard crates of <paramref name="good"/> (see
		/// <see cref="CargoFootprint"/>) at <paramref name="s"/> for the player's piloted <paramref name="ship"/>:
		/// walks the price up and drains stock exactly as <see cref="Load"/> does for an AI trader's purchase,
		/// paying from the ship's own faction treasury, and places each crate into the ship's personal
		/// <see cref="CargoHold"/> -- never the single-slot cargo fields above, which belong to the AI trade
		/// loop and a player-flown ship does not use. Stops at the first crate that cannot be bought; reports
		/// how many actually went through in <paramref name="bought"/>.
		/// </summary>
		TradeOutcome BuyIntoHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& bought);

		/// <summary>
		/// As <see cref="BuyIntoHold(Ship, Settlement, Good, int, out int)"/>, and also reports why the run of crates
		/// ended in <paramref name="stoppedBy"/>: <see cref="TradeOutcome.Ok"/> when all asked for were bought, else the
		/// limit hit (<see cref="TradeOutcome.NoStock"/>, <see cref="TradeOutcome.CantAfford"/>, <see cref="TradeOutcome.NoRoom"/>),
		/// even when some crates went through first.
		/// </summary>
		TradeOutcome BuyIntoHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& bought, TradeOutcome& stoppedBy);

		/// <summary>
		/// Sells up to <paramref name="crates"/> crates of <paramref name="good"/> out of the player's
		/// piloted <paramref name="ship"/>'s own <see cref="CargoHold"/> at <paramref name="s"/>: walks the
		/// price down and credits treasuries exactly as <see cref="Sell"/> does for an AI trader's cargo.
		/// Stops at the first crate of that good no longer in the hold; reports how many actually sold in
		/// <paramref name="sold"/>.
		/// </summary>
		TradeOutcome SellFromHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& sold);

		// ---------------------------------------------------------------- queries

		/// <summary>Systemwide average price relative to base for a good (1 = normal).</summary>
		double PriceIndex(Good g) const;

		/// <summary>Captures a settlement: the owner changes and production is disrupted for a while.</summary>
		void Capture(const cs::Ref<Settlement>& s, const cs::Ref<Faction>& newOwner);
	private:
		Economy() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Economy); // ObjectGraph registration (Persistence/Registrations)
	};

	SPACEGODCORE_API const char* ToString(Economy::TradeOutcome v);
}
