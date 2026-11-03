#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/MarketIntel.cs (US-T19).

#include "Cs/Cs.h"
#include "Economy/Goods.h" // GoodInfo::Count (the sheets' lengths), Good (by value in TradeLead)

#include <vector>

namespace SpaceGod::Core
{
	class Settlement;
	class TradeDesk;
	class World;
	class Faction;
	class CargoHold;

	/// <summary>One market's prices as the player last knew them: when, from whom, and what it sold and wanted then.</summary>
	class PriceSheet final
	{
	public:
		cs::Ref<Settlement> Market;

		/// <summary>When the prices were true, and who reported them ("seen in port", "Capt. Ada Okonkwo", "broker at Ceres").</summary>
		double Jd = 0;
		cs::String Source;

		/// <summary>By good: the price a tonne, the tonnes a trader could buy, and the tonnes it would still take.</summary>
		cs::List<double> Price = cs::NewArray<double>(GoodInfo::Count);
		cs::List<double> Available = cs::NewArray<double>(GoodInfo::Count);
		cs::List<double> Wants = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>By good: whether anybody there uses it (a market that only makes a good does not buy it).</summary>
		cs::List<bool> Buys = cs::NewArray<bool>(GoodInfo::Count);

		double AgeDays(double jd) const { return cs::Math::Max(0.0, jd - Jd); }

		cs::String ToString() const;
	};

	/// <summary>A run the player's intelligence says pays: buy here, sell there, what it should make and how far to trust it.</summary>
	class TradeLead final
	{
	public:
		cs::Ref<Settlement> From, To;
		::SpaceGod::Core::Good Good = static_cast<::SpaceGod::Core::Good>(0);

		/// <summary>The prices a tonne as last known at each end, the tonnes the run could carry, and the margin a tonne
		/// (sale less purchase, port tax and the price walking against the trade both ends).</summary>
		double BuyPrice = 0, SellPrice = 0, Tonnes = 0, Margin = 0;

		/// <summary>Margin times tonnes, credits.</summary>
		double Profit = 0;

		/// <summary>The older of the two price sheets, days; and the risk at the far end, 0..1.</summary>
		double AgeDays = 0, Risk = 0;

		/// <summary>The far end is closed to the house (blockaded or embargoed): only its back door buys.</summary>
		bool BackDoor = false;

		/// <summary>Profit weighed by how fresh the prices are and how safe the run is: what the desk ranks runs by.</summary>
		double Score = 0;

		cs::String ToString() const;
	};

	/// <summary>
	/// The player's price intelligence (U.SPC-T426). Prices are not known everywhere at once: the player knows a market's
	/// prices from the last time they were there to see them (the market board at the helm), from the house's own
	/// captains (every owned hull in port reports the markets at its world each day), or from a broker, who sells a
	/// price sheet of every known market in the system at a lawful port. Each sheet ages, and the desk trusts it less as
	/// it does (<see cref="TradeRules.IntelHalfLife"/>). From the sheets it holds, the desk ranks the runs they add up to
	/// (<see cref="Leads"/>): buy where a good is cheap and to spare, sell where it is dear and wanted, weighed by how
	/// fresh the prices are and the risk at the far end.
	/// </summary>
	class MarketIntel final
	{
		cs::Ref<TradeDesk> desk;
		cs::Dictionary<cs::Ref<Settlement>, cs::Ref<PriceSheet>> sheets = cs::Dictionary<cs::Ref<Settlement>, cs::Ref<PriceSheet>>();

	public:
		MarketIntel(cs::Ref<TradeDesk> desk) : desk(desk) {}

	private:
		cs::Ref<::SpaceGod::Core::World> World() const;

	public:
		/// <summary>Every market the player holds prices for.</summary>
		cs::List<cs::Ref<PriceSheet>> Sheets() const { return sheets.Values(); }

		int Count() const { return sheets.Count(); }

		/// <summary>The prices last known for <paramref name="s"/>, or null.</summary>
		cs::Ref<PriceSheet> Of(const cs::Ref<Settlement>& s) const;

		/// <summary>Takes down <paramref name="s"/>'s prices as they stand now (an older report never overwrites a newer one).
		/// Nothing for a market nobody has found.</summary>
		cs::Ref<PriceSheet> Observe(const cs::Ref<Settlement>& s, double jd, const cs::String& source);

		/// <summary>Takes down every market's prices at <paramref name="body"/>; returns how many.</summary>
		int ObserveAt(int body, double jd, const cs::String& source);

		/// <summary>How far the desk trusts a sheet now: 1 when fresh, half after <see cref="TradeRules.IntelHalfLife"/> days.</summary>
		double Confidence(const cs::Ref<PriceSheet>& sheet, double jd) const;

		/// <summary>"today", "3 d old".</summary>
		static cs::String AgeText(double days);

		/// <summary>Every day the house's captains in port report the markets at their world.</summary>
		void Daily(double jd);

		// ---------------------------------------------------------------- brokers

		/// <summary>Whether <paramref name="port"/> has a broker who will sell the house a price sheet: a lawful, standing port that deals with it.</summary>
		bool HasBroker(const cs::Ref<Settlement>& port) const;

		/// <summary>What a price sheet costs at a broker now (less for a trader of rank).</summary>
		double ReportFee() const;

		/// <summary>
		/// Buys a broker's price sheet at <paramref name="port"/>, paid by <paramref name="payer"/> (the house account, or the
		/// purse of the ship the player flies) to the port's owner: the prices of every market in the system anyone has
		/// found, as they stand now. Returns the markets learned, or -1 if there is no broker or the payer cannot pay.
		/// </summary>
		int BuyReport(const cs::Ref<Settlement>& port, const cs::Ref<Faction>& payer, double jd);

		// ---------------------------------------------------------------- the runs

		/// <summary>
		/// The runs the player's prices add up to for a hold of <paramref name="holdTonnes"/>, best first (at most
		/// <paramref name="max"/>): every good to spare at one known market that another known market buys for more, net of
		/// port tax at both ends and the price walking against the trade, sized to what the one can spare, the other
		/// would take and the hold carries. Each is scored by its profit, trusted less the older its prices and the riskier
		/// the far end. Runs from a pirate haven (its goods are stolen) are the back door's business, not the desk's.
		/// </summary>
		cs::List<cs::Ref<TradeLead>> Leads(double holdTonnes, double jd, int max = 12, cs::Ref<Faction> flag = nullptr, cs::Ref<CargoHold> crateHold = nullptr);
	};
}
