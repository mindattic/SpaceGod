#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/TradeDesk.cs (US-T19).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class World;
	class Settlement;
	class Faction;
	class Holdings;
	class MarketIntel;
	class Charters;
	class TradeInsurance;
	class Smuggling;
	class RangeSetting;
	enum class Good : int;

	/// <summary>The trading game's tunables (U.SPC-T426), in Settings > Economy.</summary>
	// [SettingsSource]: the integration ticket registers TradeRules (RunClassConstructor) with the settings registry.
	class TradeRules
	{
		static cs::String Percent(double v);

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		static const cs::Ref<RangeSetting>& CharterHire();

		static const cs::Ref<RangeSetting>& InsuranceRate();

		static const cs::Ref<RangeSetting>& InsuranceCover();

		static const cs::Ref<RangeSetting>& RiskLoading();

		static const cs::Ref<RangeSetting>& IntelHalfLife();

		static const cs::Ref<RangeSetting>& BrokerFee();

		static const cs::Ref<RangeSetting>& CustomsChance();

		static const cs::Ref<RangeSetting>& BackDoorPremium();
	};

	/// <summary>What a trader faces at a port: raiders, a blockade or an embargo closing it, and the place falling apart.</summary>
	struct PortRisk
	{
		/// <summary>The war's tally of fights and raids at its body, 0..1 (<see cref="Contracts.DangerAt"/>).</summary>
		double Piracy = 0;

		/// <summary>How near the settlement is to collapse, 0..1: unrest, hunger and want, people leaving; 1 for a fallen one.</summary>
		double Collapse = 0;

		/// <summary>Who holds it blockaded, or null.</summary>
		cs::Ref<Faction> BlockadedBy;

		/// <summary>Whether its owner has closed its ports to the trader's flag.</summary>
		bool Embargoed = false;

		/// <summary>All of it in one figure, 0..1: the chance-weighted share of a run's value a trader should expect to lose.</summary>
		double Total = 0;

		PortRisk() = default;
		PortRisk(double piracy, double collapse, cs::Ref<Faction> blockadedBy, bool embargoed);

		/// <summary>Shut to the trader's open business: only the back door (see <see cref="Smuggling"/>) is left.</summary>
		bool Closed() const { return BlockadedBy != nullptr || Embargoed; }

		/// <summary>A few words for the desk: "raiders, near collapse", "blockaded by the Outer Reach Compact".</summary>
		cs::String Summary() const;

	private:
		static double Clamp01(double v);
	};

	/// <summary>A trader's standing in the trade, by what their house is worth (<see cref="TradeDesk.NetWorth"/>).</summary>
	enum class TraderRank : int
	{
		Peddler,
		Hauler,
		Merchant,
		Shipowner,
		Magnate,
		MerchantPrince,
	};
	const char* ToString(TraderRank v);

	/// <summary>One piece of trade news for the desk: where, what, and why it matters to a trader.</summary>
	class TradeTip final
	{
	public:
		double Jd = 0;
		cs::String Text;
		cs::Ref<Settlement> Market;
		cs::Nullable<::SpaceGod::Core::Good> Good;

		/// <summary>How much it matters, for ordering the desk's list (higher first).</summary>
		double Weight = 0;

		cs::String ToString() const { return Text; }
	};

	/// <summary>
	/// The trading game (U.SPC-T426): a full trader's career on the live economy, beside the market boards (buying and
	/// selling at the helm), the contracts boards and the player's own fleet and property (<see cref="Holdings"/>). It
	/// keeps the player's price intelligence (<see cref="MarketIntel"/>: prices seen in port, reported by the house's
	/// captains, bought from brokers, and the runs they add up to), the trade news that moves markets (shocks,
	/// shortages, ports at risk), time charters of the house's hulls to the trading companies (<see cref="Charters"/>),
	/// insurance on hulls, cargoes and warehouses (<see cref="TradeInsurance"/>), the back door of every lawful port for
	/// contraband, blockade runs and embargoes broken (<see cref="Smuggling"/>), and the trader's rank by what the house
	/// is worth. Everything prices risk the same way (<see cref="Assess"/>): raiders, blockades, embargoes and collapse.
	/// New kinds of trade news plug in by registering a source in <see cref="TipSources"/>.
	/// </summary>
	// TradeDesk hands itself to its parts (new MarketIntel(this), ...), hence enable_shared_from_this; its C# constructor
	// body runs as Construct() right after the Ref is made: create it as `auto d = cs::New<TradeDesk>(world, seed); d->Construct();`.
	class TradeDesk final : public std::enable_shared_from_this<TradeDesk>
	{
		cs::Ref<::SpaceGod::Core::World> world;

	public:
		cs::Ref<MarketIntel> Intel;
		cs::Ref<::SpaceGod::Core::Charters> Charters;
		cs::Ref<TradeInsurance> Insurance;
		cs::Ref<::SpaceGod::Core::Smuggling> Smuggling;

		/// <summary>The trader's rank as last reckoned (it is reckoned daily, and announced when it rises).</summary>
		TraderRank Rank = TraderRank::Peddler;

		/// <summary>The best rank reached.</summary>
		TraderRank BestRank = TraderRank::Peddler;

	private:
		double nextDailyJd = cs::Double::NaN;
		cs::List<cs::String> notices = cs::List<cs::String>();

	public:
		/// <summary>Net worth, credits, at which each rank is reached (index by <see cref="TraderRank"/>).</summary>
		static const cs::List<double>& RankWorth();

		/// <summary>
		/// Where the desk's trade news comes from: each source is asked for its tips in turn (the market shocks, shortages
		/// the player's intelligence shows, risks to what the house owns). A new kind of trade news registers a source.
		/// </summary>
		static cs::List<cs::Func<cs::List<cs::Ref<TradeTip>>, cs::Ref<TradeDesk>, double>>& TipSources();

		TradeDesk(cs::Ref<::SpaceGod::Core::World> world, int seed);

		/// <summary>The C# constructor's body (it passes `this` to the desk's parts); call once right after construction.</summary>
		void Construct();

	private:
		int seed = 0; // the constructor's seed, kept for Construct()

	public:
		cs::Ref<::SpaceGod::Core::World> World() const { return world; }
		cs::Ref<::SpaceGod::Core::Holdings> Holdings() const;

		/// <summary>The flag the trader's business sails under (the house's).</summary>
		cs::Ref<Faction> Flag() const;

		// ---------------------------------------------------------------- the clock

		/// <summary>Once a day: captains report prices, charters pay their hire and end, premiums are paid, the rank is reckoned.</summary>
		void Update(double jd);

		// ---------------------------------------------------------------- risk

		/// <summary>What a trader under <paramref name="flag"/> faces at <paramref name="s"/> now (see <see cref="PortRisk"/>).</summary>
		PortRisk Assess(const cs::Ref<Settlement>& s, cs::Ref<Faction> flag = nullptr) const;

		/// <summary>The worst a trader faces at any port on <paramref name="body"/> (just the raiding where there is none).</summary>
		double RiskAt(int body) const;

		/// <summary>
		/// How near <paramref name="s"/> is to collapse, 0..1: unrest under an occupier, its people going hungry, thirsty or
		/// untreated (as remembered over a month), and its people leaving it below what it was founded with. 1 if fallen.
		/// </summary>
		static double CollapseOf(const cs::Ref<Settlement>& s);

		// ---------------------------------------------------------------- the career

		/// <summary>
		/// What the house is worth, credits: the account, its hulls at second-hand prices (less damage) and the cargo they
		/// carry at cost, its warehouses and what is in them at base prices, its shares at their price today, half what
		/// its claims cost; and <paramref name="purse"/>'s treasury if the player flies another flag's ship.
		/// </summary>
		double NetWorth(const cs::Ref<Faction>& purse = nullptr) const;

		/// <summary>The rank a house worth <paramref name="worth"/> holds.</summary>
		static TraderRank RankFor(double worth);

		static cs::String RankName(TraderRank r);

		/// <summary>The worth the next rank needs, or NaN at the top.</summary>
		double NextRankWorth() const;

		/// <summary>A broker's discount for the trader's rank (a tenth a rank).</summary>
		double BrokerDiscount() const;

		/// <summary>Underwriters' discount for the trader's rank (a twentieth a rank: a known name is a better risk).</summary>
		double InsuranceDiscount() const;

		/// <summary>Over the career: back-door sales, their takings and the times customs caught them; charters served and
		/// their hire; premiums paid and claims paid out; price sheets bought.</summary>
		int Smuggled = 0, TimesCaught = 0, ChartersServed = 0, ReportsBought = 0;
		double SmugglingTakings = 0, HireEarned = 0, PremiumsPaid = 0, ClaimsPaid = 0;

	private:
		void Reckon(double jd);

	public:
		// ---------------------------------------------------------------- trade news

		/// <summary>The trade news the desk knows of now, most important first (at most <paramref name="max"/>).</summary>
		cs::List<cs::Ref<TradeTip>> Tips(double jd, int max = 12);

	private:
		/// <summary>Each economic event in force, and what it means for a trader.</summary>
		static cs::List<cs::Ref<TradeTip>> ShockTips(const cs::Ref<TradeDesk>& desk, double jd);

		/// <summary>Markets the player's intelligence shows paying well over the odds for something (at least twice its base price).</summary>
		static cs::List<cs::Ref<TradeTip>> ShortageTips(const cs::Ref<TradeDesk>& desk, double jd);

		/// <summary>What the house owns at a port that is in danger: a warehouse or share where it is blockaded, raided or collapsing.</summary>
		static cs::List<cs::Ref<TradeTip>> RiskTips(const cs::Ref<TradeDesk>& desk, double jd);

	public:
		// ---------------------------------------------------------------- messages

		/// <summary>Records a message for the player (shown as a toast by the HUD).</summary>
		void Notice(const cs::String& text);

		/// <summary>The player's messages since the last call, oldest first.</summary>
		cs::List<cs::String> TakeNotices();

		static cs::String Cr(double v);
	};
}
