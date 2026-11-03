#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/BlackMarket.cs (US-T19).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Settlement;
	enum class Good : int;

	/// <summary>
	/// The black markets (U.SPC-T94): where stolen goods are sold, at prices of their own rather than the open board's.
	/// Every haven is one, for its clan's loot (kept in its <see cref="Settlement.Stock"/>; what it holds over its own
	/// needs is loot waiting to move on, and its fences pay half the going rate for it, see <c>Economy.Fence</c>); so is
	/// a lawless station (<see cref="Settlement.Lawless"/>), whose middlemen buy stolen goods into a store of their own
	/// (<see cref="Settlement.BlackStock"/>) and pass them on into the station's open stores a little at a time.
	///
	/// A black market's price for a good starts from its base price, cut by what the market can get away with paying
	/// (<see cref="HavenShare"/> at a haven, <see cref="StationShare"/> at a station, which can launder it), and rises
	/// as the market runs short of it and falls as it fills: double at an empty market, the share itself when it holds
	/// as much as it can move in <see cref="AppetiteDays"/>, and on down to a quarter of that when glutted. So a clan's
	/// smugglers carry loot from havens that are full of it to stations that are short.
	/// </summary>
	class SPACEGODCORE_API BlackMarket
	{
	public:
		/// <summary>What a haven's fences pay for loot, as a share of the base price, when they hold a normal amount.</summary>
		static constexpr double HavenShare = 0.5;

		/// <summary>What a lawless station's middlemen pay for stolen goods, as a share of the base price, at a normal holding.</summary>
		static constexpr double StationShare = 0.8;

		/// <summary>Days of a good's flow through the place a black market can move: holding that much, it pays its share.</summary>
		static constexpr double AppetiteDays = 15;

		/// <summary>The least a black market expects to move of any good, tonnes.</summary>
		static constexpr double MinAppetiteTonnes = 500;

		/// <summary>Share of a lawless station's black-market store passed into its open stores a day.</summary>
		static constexpr double LaunderPerDay = 0.04;

		/// <summary>Whether <paramref name="s"/> runs a black market: a standing haven, or a lawless station.</summary>
		static bool Takes(const cs::Ref<Settlement>& s);

		/// <summary>Stolen goods of <paramref name="g"/> it holds, tonnes: a haven's stock over its own needs, a station's black store.</summary>
		static double Held(const cs::Ref<Settlement>& s, Good g);

		/// <summary>How much of <paramref name="g"/> the market can move, tonnes (see <see cref="AppetiteDays"/>).</summary>
		static double Appetite(const cs::Ref<Settlement>& s, Good g);

		/// <summary>What it pays a tonne for <paramref name="g"/> holding <paramref name="held"/> tonnes of it.</summary>
		static double PriceAt(const cs::Ref<Settlement>& s, Good g, double held);

		/// <summary>What it pays a tonne for <paramref name="g"/> now.</summary>
		static double Price(const cs::Ref<Settlement>& s, Good g);

		/// <summary>Credits for selling it <paramref name="tonnes"/> of <paramref name="g"/>, the price walking down as it fills.</summary>
		static double SaleValue(const cs::Ref<Settlement>& s, Good g, double tonnes);

		/// <summary>
		/// Sells it <paramref name="tonnes"/> of stolen <paramref name="g"/>: the goods go into its store (a haven's stock, a
		/// station's black store) and the price is paid out of its owner's treasury. Returns the credits paid.
		/// </summary>
		static double Sell(const cs::Ref<Settlement>& s, Good g, double tonnes);

		/// <summary>A lawless station's middlemen pass part of their store into the station's open stores (they paid for it).</summary>
		static void Launder(const cs::Ref<Settlement>& s, double dt);

		/// <summary>The value at base prices of the loot a haven holds over its own needs, credits.</summary>
		static double LootValue(const cs::Ref<Settlement>& haven);
	};
}
