#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Holdings.cs (US-T18).

#include "Cs/Cs.h"
#include "Economy/Goods.h" // GoodInfo::Count (Property::Stock's length)
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Faction;
	class Ship;
	class Settlement;
	class BodyCatalog;
	class Charter;
	class Economy;
	enum class ShipClass : int;
	enum class ShipRole : int;

	/// <summary>What a hired captain does with an owned ship while the player is not flying it.</summary>
	enum class StandingOrderKind : int
	{
		/// <summary>Lie in port where it is.</summary>
		Hold,

		/// <summary>Work a fixed round of ports, at each stop buying what pays best at the next.</summary>
		TradeRoute,

		/// <summary>Cut ore on one of the player's mining claims and sell it wherever it fetches most.</summary>
		Mining,

		/// <summary>Shadow another ship port to port and fight beside it.</summary>
		Escort,

		/// <summary>Make the round of a list of waypoints, fighting whatever hostile it finds there.</summary>
		Patrol,

		/// <summary>Trade wherever pays best, as a merchant captain does: buy what earns most a day and carry it there
		/// (U.SPC-T426; a hull out on charter works this way for its charterer).</summary>
		FreeTrade,
	};
	SPACEGODCORE_API const char* ToString(StandingOrderKind v);

	class Property;

	/// <summary>A captain's standing orders: what to do, and where (the stops of a route or patrol, a claim, a charge).</summary>
	class SPACEGODCORE_API StandingOrder final
	{
	public:
		StandingOrderKind Kind = StandingOrderKind::Hold;

		/// <summary>A trade route's ports or a patrol's waypoints (body indices), in order.</summary>
		/// (C#'s null array is an empty list here.)
		cs::List<int> Stops;

		/// <summary>The stop the ship is bound for (or will make for next).</summary>
		int StopIndex = 0;

		/// <summary>The mining claim worked (a <see cref="PropertyKind.MiningClaim"/>).</summary>
		cs::Ref<Property> Claim;

		/// <summary>The ship escorted.</summary>
		cs::Ref<Ship> Charge;

		static cs::Ref<StandingOrder> Hold();
		static cs::Ref<StandingOrder> TradeRoute(cs::List<int> ports);
		static cs::Ref<StandingOrder> Patrol(cs::List<int> waypoints);
		static cs::Ref<StandingOrder> Mining(cs::Ref<Property> claim);
		static cs::Ref<StandingOrder> Escort(cs::Ref<Ship> charge);
		static cs::Ref<StandingOrder> FreeTrade();

		/// <summary>The orders in a line: "trade route Earth - Mars - Ceres", "escorting MV Plenty".</summary>
		cs::String Describe(const cs::Ref<BodyCatalog>& catalog) const;

		cs::String ToString() const;
	};

	/// <summary>A captain the player has hired: a name, how good they are, and what they cost a day.</summary>
	class Captain final
	{
	public:
		cs::String Name;

		/// <summary>0.5 (green) to 1.5 (a veteran): a better captain turns a ship round faster in port, and costs more.</summary>
		double Skill = 0;

		/// <summary>The captain's own pay a day, credits (the crew's is on top: <see cref="Economy.RunningCostPerDay"/>).</summary>
		double WagePerDay = 0;

		double HiredJd = 0;

		/// <summary>Days of pay owed and not paid: at <see cref="Holdings.QuitDays"/> the captain walks off.</summary>
		int DaysUnpaid = 0;

		cs::String ToString() const { return Name; }
	};

	/// <summary>One of the player's ships: the hull, who commands it, their orders and the ship's books.</summary>
	class SPACEGODCORE_API OwnedShip final
	{
	public:
		cs::Ref<::SpaceGod::Core::Ship> Ship;
		cs::Ref<::SpaceGod::Core::Captain> Captain;
		cs::Ref<StandingOrder> Orders = StandingOrder::Hold();
		double SinceJd = 0, PricePaid = 0;

		/// <summary>Its books, credits: trade profit and ore sold; propellant, tolls and tankers; wages; yard bills.</summary>
		double Earned = 0, Spent = 0, Wages = 0, Repairs = 0;

		/// <summary>Ore cut from claims, tonnes, over its life.</summary>
		double TonnesMined = 0;

		/// <summary>Gone from the player's fleet: destroyed, taken, cast adrift or out through a gate.</summary>
		bool Lost = false;
		double LostJd = 0;
		cs::String LostHow;

		/// <summary>The ship's running profit the books last caught up with.</summary>
		double SeenCredits = 0;

		/// <summary>Mining time owed before it leaves the claim, days (set when it loads, spent as its dwell).</summary>
		double MiningDays = 0;

		/// <summary>The time charter it is out on (U.SPC-T426, see <see cref="Charters"/>), or null: while it runs, the ship
		/// trades for its charterer, who keeps its takings and pays the house a day rate, and its orders are the charter's.</summary>
		cs::Ref<::SpaceGod::Core::Charter> Charter;

		bool OnCharter() const;

		double Net() const { return Earned - Spent - Wages - Repairs; }
		cs::String Name() const;
		cs::String ToString() const { return Name(); }
	};

	enum class PropertyKind : int
	{
		/// <summary>Storage at a port: goods bought into it wait there to be sold when the price is right.</summary>
		Warehouse,

		/// <summary>A share of a station or colony: a dividend of the port taxes its trade pays.</summary>
		StationShare,

		/// <summary>The right to cut one ore on one world: the player's ore haulers mine it under standing orders.</summary>
		MiningClaim,
	};
	SPACEGODCORE_API const char* ToString(PropertyKind v);

	/// <summary>Something the player owns that is not a ship.</summary>
	class SPACEGODCORE_API Property final
	{
	public:
		PropertyKind Kind = PropertyKind::Warehouse;

		/// <summary>The port a warehouse stands at, or the settlement a share is in (null for a claim).</summary>
		cs::Ref<::SpaceGod::Core::Settlement> Settlement;

		/// <summary>Where it is.</summary>
		int Body = 0;

		/// <summary>A station share: the fraction owned (0..1).</summary>
		double Share = 0;

		/// <summary>A claim: what it yields, and how fast a hauler on it cuts it, tonnes a day.</summary>
		Good Ore = static_cast<Good>(0);
		double YieldPerDay = 0;

		/// <summary>A warehouse: what it holds and how much, tonnes.</summary>
		double CapacityTonnes = 0;
		cs::List<double> Stock = cs::NewArray<double>(GoodInfo::Count);

		double PricePaid = 0, SinceJd = 0;

		/// <summary>What it has brought in (dividends, ore sold off it) and cost to keep (rent, claim fees), credits.</summary>
		double Income = 0, Upkeep = 0;

		/// <summary>Seized (a pirate took the port) or given up.</summary>
		bool Lost = false;
		double LostJd = 0;

		/// <summary>The settlement's trade the dividend last caught up with.</summary>
		double SeenTradeVolume = 0;

		cs::String Name;

		double StoredTonnes() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>A line in the player's books: when, what, and the credits in (+) or out (-).</summary>
	class SPACEGODCORE_API LedgerEntry final
	{
	public:
		double Jd = 0;
		cs::String Text;
		double Amount = 0;

		/// <summary>False for a loss of an asset (a hull, a cargo) rather than of cash from the account.</summary>
		bool Cash = true;

		cs::String ToString() const;
	};

	/// <summary>
	/// The player's own fleet and property (U.SPC-T102). The player can buy hulls at any shipyard that will deal with
	/// them (or put a ship they have taken into their fleet), hire captains to fly them under standing orders -- a
	/// trade route, mining one of their claims, escorting another ship, a patrol -- and own property: a warehouse at
	/// a port, a share in a station, a mining claim. Owned ships fly under the player's house flag
	/// (<see cref="House"/>) and are simulated like any other: they plan real runs, burn real propellant, pay tolls,
	/// land through docking rings, are raided and fight. Every credit they make or lose goes to the house account
	/// (the house's treasury); every captain and crew is paid from it daily, and a captain unpaid for
	/// <see cref="QuitDays"/> walks off. A ship lost is a loss on the books at what it was worth.
	/// </summary>
	class SPACEGODCORE_API Holdings final
	{
		cs::Ref<World> world;
		cs::Random rng;

	public:
		/// <summary>The flag the player's ships fly: lawful, at war with nobody, every pirate's prey.</summary>
		cs::Ref<Faction> House;

		cs::List<cs::Ref<OwnedShip>> Ships = cs::List<cs::Ref<OwnedShip>>();
		cs::List<cs::Ref<Property>> Properties = cs::List<cs::Ref<Property>>();

		/// <summary>The books, newest last (the latest <see cref="LedgerLength"/>).</summary>
		cs::List<cs::Ref<LedgerEntry>> Ledger = cs::List<cs::Ref<LedgerEntry>>();

		/// <summary>Over the house's life, credits: everything its ships and property brought in, everything they cost to
		/// run, and the value of what was lost (hulls and cargoes).</summary>
		double TotalEarned = 0, TotalCosts = 0, TotalLost = 0;

		/// <summary>What the account should hold by the books (every cash line summed), against which the daily round
		/// finds money that moved without a line of its own (a ransom paid, a rescue fee, prize money).</summary>
		double BookBalance = 0;

	private:
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<OwnedShip>> byShip = cs::Dictionary<cs::Ref<Ship>, cs::Ref<OwnedShip>>();
		double nextTickJd = cs::Double::NaN;

	public:
		/// <summary>Raised when an owned ship is lost (destroyed, taken or cast adrift, not gone through a gate): the ship,
		/// what it was worth with its cargo, and the date. Where an underwriter pays out (U.SPC-T426).</summary>
		cs::Event<cs::Ref<OwnedShip>, double, double> ShipLost;

		/// <summary>Raised when a property is lost (seized by pirates, or lost with an abandoned port): the property, what it
		/// was worth with its stock, and the date.</summary>
		cs::Event<cs::Ref<Property>, double, double> PropertyLost;

	private:
		int captainNames = 0;

	public:
		// ---------------------------------------------------------------- tuning

		static constexpr int LedgerLength = 300;

		/// <summary>The books are kept, wages paid and dividends drawn once a day.</summary>
		static constexpr double TickDays = 1;

		/// <summary>A captain's pay a day at skill 1, credits (more for a warship's).</summary>
		static constexpr double CaptainWage = 600;

		/// <summary>Days unpaid before a captain walks off the ship.</summary>
		static constexpr int QuitDays = 5;

		/// <summary>Damage at which a captain takes the ship to a yard, and the share a yard mends a day (at the hull's
		/// price times <see cref="RepairRate"/> for each share).</summary>
		static constexpr double RepairDamage = 0.25;
		static constexpr double RepairPerDay = 0.2;
		static constexpr double RepairRate = 0.3;

		/// <summary>A warehouse: its price, rent a day, and what it holds.</summary>
		static constexpr double WarehousePrice = 2e6;
		static constexpr double WarehouseRentPerDay = 1500;
		static constexpr double WarehouseTonnes = 20000;

		/// <summary>What a settlement is worth per resident (a share is priced at that fraction of it).</summary>
		static constexpr double ValuePerResident = 250;

		/// <summary>A claim: its price per tonne-a-day of yield at the ore's base price, and its fee a day.</summary>
		static constexpr double ClaimPricePerYield = 60;
		static constexpr double ClaimFeePerDay = 300;

		/// <summary>How fast a hauler on a claim cuts ore, tonnes a day, before the ore's richness (rarer ores come slower).</summary>
		static constexpr double BaseYieldPerDay = 4000;

	private:
		static const cs::List<cs::String>& CaptainNames();

	public:
		Holdings(cs::Ref<World> world, int seed);

		/// <summary>The house account, credits (the house's treasury).</summary>
		double Balance() const;

	private:
		cs::Ref<::SpaceGod::Core::Economy> Economy() const;

	public:
		/// <summary>The house's record of <paramref name="ship"/>, while it is still the house's: one taken (or lost) since
		/// the last daily round no longer follows the house's orders, even before that round writes it off.</summary>
		cs::Ref<OwnedShip> Of(const cs::Ref<Ship>& ship) const;

		bool Owns(const cs::Ref<Ship>& ship) const;

		cs::List<cs::Ref<Property>> OfKind(PropertyKind kind) const;

		// ---------------------------------------------------------------- the account

		/// <summary>Moves credits from <paramref name="from"/>'s treasury (the purse of whatever the player flies) into the house account.</summary>
		bool Deposit(const cs::Ref<Faction>& from, double amount, double jd);

		/// <summary>Moves credits out of the house account to <paramref name="to"/>'s treasury.</summary>
		bool Withdraw(const cs::Ref<Faction>& to, double amount, double jd);

		/// <summary>
		/// Credits paid into (+) or out of (-) the house account by the house's other business (charter hire, insurance
		/// premiums and claims, U.SPC-T426), booked as trade income or cost. False (and nothing moved) for a payment out
		/// the account cannot cover, unless <paramref name="overdraw"/>.
		/// </summary>
		bool Transact(double amount, const cs::String& text, double jd, bool overdraw = false);

	private:
		void Book(double jd, const cs::String& text, double amount, bool earned = true, bool cash = true);

	public:
		// ---------------------------------------------------------------- ships

		/// <summary>Whether <paramref name="yard"/> will sell the house a hull.</summary>
		bool CanBuyAt(const cs::Ref<Settlement>& yard) const;

		/// <summary>
		/// Buys a new <paramref name="cls"/> off <paramref name="yard"/>'s slip at its price (<see cref="Economy.HullPrice"/>),
		/// paid from the house account to the yard's owner, and puts it into space there under the house flag, its
		/// crew signed on in port. It lies in port until a captain is hired. Null if the yard will not deal or the
		/// account cannot pay.
		/// </summary>
		cs::Ref<OwnedShip> Buy(const cs::Ref<Settlement>& yard, ShipClass cls, const cs::String& name, double jd);

		/// <summary>
		/// Takes a ship the player already has (the hull they fly, a prize they claimed) into the house fleet: it
		/// changes to the house flag, its cargo and running profit with it. Null if it cannot (lost, adrift, an escort,
		/// a pirate's, or already the house's).
		/// </summary>
		cs::Ref<OwnedShip> Enlist(const cs::Ref<Ship>& ship, double jd);

	private:
		cs::Ref<OwnedShip> Add(const cs::Ref<Ship>& ship, double jd, double price);

	public:
		/// <summary>Signs on a captain for <paramref name="owned"/> (replacing any it had). Their pay starts today.</summary>
		cs::Ref<Captain> Hire(const cs::Ref<OwnedShip>& owned, double jd, double skill = cs::Double::NaN);

		/// <summary>Lets the captain go: the ship lies in port (unless the player flies it) until another is hired. Not
		/// while it is out on charter: a hull is chartered with its crew.</summary>
		void Dismiss(const cs::Ref<OwnedShip>& owned, double jd);

		/// <summary>
		/// Gives <paramref name="owned"/>'s captain standing orders; false if the ship cannot carry them out (a hull
		/// that cannot carry the claim's ore, a route of under two stops, escorting itself). They take effect when it
		/// next plans a trip.
		/// </summary>
		bool Order(const cs::Ref<OwnedShip>& owned, const cs::Ref<StandingOrder>& orders);

		/// <summary>Gives standing orders even to a ship out on charter (the charter's own: free trading, then its old orders back).</summary>
		bool SetOrders(const cs::Ref<OwnedShip>& owned, const cs::Ref<StandingOrder>& orders);

		/// <summary>Why the last order given was refused for want of reach (a trade-route leg its full tanks cannot cover), else null.</summary>
		cs::String LastRefusal;

	private:
		/// <summary>The first leg of the cyclic <paramref name="stops"/> an empty ship on full tanks cannot fly (ignoring
		/// legs it could never fly anyway for want of a drive), described; null if every leg is within reach.</summary>
		cs::String UnreachableLeg(const cs::Ref<Ship>& ship, const cs::List<int>& stops, double jd, int& badFrom) const;

		cs::String LegProblem(const cs::Ref<Ship>& ship, int a, int b, double jd) const;

	public:
		/// <summary>True when every stop of a trade route is a body with a discovered market to trade at (a planet with
		/// only a port moon has none itself, so a route to it would shuttle empty forever).</summary>
		bool HasMarketAtEveryStop(const cs::Ref<StandingOrder>& o) const;

		/// <summary>The body to trade at for <paramref name="body"/>: itself if it has a market, else the first of its
		/// moons that does, else -1.</summary>
		int MarketBodyFor(int body) const;

		static bool CanFollow(const cs::Ref<Ship>& ship, const cs::Ref<StandingOrder>& o);

	private:
		/// <summary>The role the fleet and markets treat a ship under these orders as: a trader or miner is a merchant
		/// (it sells in port and lands through rings); a patrol or escort is not.</summary>
		static ShipRole RoleFor(StandingOrderKind kind, ShipClass cls);

	public:
		// ---------------------------------------------------------------- property

		/// <summary>Buys a warehouse at <paramref name="port"/> (one per port), paid to its owner.</summary>
		cs::Ref<Property> BuyWarehouse(const cs::Ref<Settlement>& port, double jd);

		/// <summary>What <paramref name="share"/> of <paramref name="s"/> costs.</summary>
		static double SharePrice(const cs::Ref<Settlement>& s, double share);

		/// <summary>
		/// Buys <paramref name="share"/> (up to half) of a station or colony from its owner: from then on the house is
		/// paid that share of the port taxes the settlement's trade pays its owner, daily. Null if it will not sell
		/// (a pirate's, a ruin, a haven, more than half in other hands) or the account cannot pay.
		/// </summary>
		cs::Ref<Property> BuyShare(const cs::Ref<Settlement>& s, double share, double jd);

	private:
		static cs::String ShareName(const cs::Ref<Settlement>& s, double share);

	public:
		/// <summary>The tonnes a day a hauler cuts of <paramref name="ore"/> on a claim: rarer, dearer ores come slower
		/// (nickel-iron at the full <see cref="BaseYieldPerDay"/>, platinum-group metals at under a twentieth of it).</summary>
		static double YieldOf(Good ore);

		static double ClaimPrice(Good ore);

		/// <summary>
		/// Stakes a claim to cut <paramref name="ore"/> on <paramref name="body"/> (a world, moon or rock, never a star or a
		/// haven's rock), bought from the registry. Only an ore hauler or freighter that carries it can work it.
		/// Null if it cannot be staked (not a mineral or volatile, already claimed, no money).
		/// </summary>
		cs::Ref<Property> StakeClaim(int body, Good ore, double jd);

	private:
		void AddProperty(const cs::Ref<Property>& p, double jd);

		/// <summary>Whether the house may trade at <paramref name="port"/>: it stands, and no blockade, embargo or war shuts it to the house.</summary>
		bool CanTradeAt(const cs::Ref<Settlement>& port) const;

	public:
		/// <summary>Buys <paramref name="tonnes"/> of <paramref name="good"/> from the warehouse's port into it, at the market's price
		/// with tax. Returns the tonnes bought (fewer if the market, the warehouse or the account runs short).</summary>
		double StoreFromMarket(const cs::Ref<Property>& warehouse, Good good, double tonnes, double jd);

		/// <summary>Sells <paramref name="tonnes"/> of what the warehouse holds into its port's market; returns the credits (after tax).</summary>
		double SellFromStore(const cs::Ref<Property>& warehouse, Good good, double tonnes, double jd);

		// ---------------------------------------------------------------- the daily round

		/// <summary>Keeps the books to <paramref name="jd"/>, a day at a time: each ship's takings and costs, losses, wages, yard
		/// bills, dividends, rent and claim fees.</summary>
		void Update(double jd);

	private:
		void Tick(double jd);

	public:
		/// <summary>Cargo the house's ships have paid for and not yet sold, credits (out of the account, not yet a loss or a gain).</summary>
		double CargoAfloat() const;

		/// <summary>What the ships have made or spent since the books last caught up with them (settled at the next daily round), credits.</summary>
		double Unsettled() const;

	private:
		/// <summary>Money that came or went without a line of its own (a ransom a raider took, a rescue fee for pods brought
		/// in, prize money) goes on the books as it is found, so the account and the books always agree.</summary>
		void Reconcile(double jd);

		/// <summary>The ship's running profit moved since the books last looked: trade and ore sold in, propellant, tolls and tankers out.</summary>
		void Settle(const cs::Ref<OwnedShip>& o, double jd);

		bool CheckLost(const cs::Ref<OwnedShip>& o, double jd);

		void PayWages(const cs::Ref<OwnedShip>& o, double jd);

		/// <summary>A damaged ship in port at a yard that will serve the house is mended a share a day, at the house's expense:
		/// in orbit, or landed through one of the world's rings (a merchant goes down to the surface, and waits there).</summary>
		void RepairInPort(const cs::Ref<OwnedShip>& o, double jd);

		cs::Ref<Settlement> YardAt(int body) const;

		void Charge(const cs::Ref<Property>& p, double amount, double jd, const cs::String& text);

		/// <summary>A pirate took the port: the warehouse and its stock, or the share, are gone.</summary>
		bool Seized(const cs::Ref<Property>& p, double jd);

		/// <summary>The share of the port taxes the settlement's trade has paid since the last dividend, from its owner's treasury.</summary>
		void PayDividend(const cs::Ref<Property>& p, double jd);

	public:
		// ---------------------------------------------------------------- the captains' choices

		/// <summary>
		/// Fleet hook: where an owned ship goes next under its captain's standing orders (a body; its own to stay), or -1
		/// to leave the move to the fleet's own choice as any merchant's (a free trader with an empty hold and no run). With no captain it lies where it is.
		/// </summary>
		int ChooseNext(const cs::Ref<Ship>& ship, int here, double jd);

		/// <summary>Fleet hook: how long an owned ship stays where it is: mining time on a claim, a short wait while
		/// escorting (it keeps checking on its charge), and a better captain turns round faster.</summary>
		double Dwell(const cs::Ref<Ship>& ship, double normal);

	private:
		int NextOnRoute(const cs::Ref<OwnedShip>& o, int here, double jd);

		/// <summary>Throws the hold's cargo away, booking its cost as a loss.</summary>
		void DumpCargo(const cs::Ref<Ship>& ship);

		/// <summary>A free trader's next port: the run that earns most a day from here, as any merchant captain picks it
		/// (<see cref="Economy.PlanTrade"/>; with a load aboard nobody here took, where it sells best), else -1, the fleet's own move to another port as any merchant's.</summary>
		int NextFreeTrade(const cs::Ref<OwnedShip>& o, int here, double jd);

		int NextWaypoint(const cs::Ref<OwnedShip>& o, int here);

		int NextForMining(const cs::Ref<OwnedShip>& o, int here, double jd);

	public:
		/// <summary>The body whose frame the piloted ship is flown in, set each pilot frame by the app; -1 when nobody is at the helm (U.SPC-T552).</summary>
		int PilotFrameBody = -1;

	private:
		int NextForEscort(const cs::Ref<OwnedShip>& o, int here);

		/// <summary>Where a ship is or is going: the fight it is in, the body it holds at, or its trip's end.</summary>
		int WhereBound(const cs::Ref<Ship>& c) const;

		int NearestYard(const cs::Ref<Ship>& ship, int here, double jd) const;
	private:
		Holdings() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Holdings); // ObjectGraph registration (Persistence/Registrations)
	};
}
