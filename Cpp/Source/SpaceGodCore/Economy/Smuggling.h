#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Smuggling.cs (US-T19).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class TradeDesk;
	class World;
	class Settlement;
	class Ship;
	class CargoItem;
	class Faction;
	enum class Good : int;

	/// <summary>Why a crate can only go in by a port's back door.</summary>
	enum class BackDoorReason : int
	{
		/// <summary>The open market takes it: no need of the back door.</summary>
		None,

		/// <summary>Stolen goods (bought at a pirate haven): a lawful market will not touch them, but a fence will.</summary>
		Stolen,

		/// <summary>A good the port's owner bans (<see cref="Faction.Contraband"/>).</summary>
		Contraband,

		/// <summary>The port is blockaded: nothing is landed but what slips past the pickets.</summary>
		Blockade,

		/// <summary>The port's owner has closed its ports to the ship's flag.</summary>
		Embargo,
	};
	SPACEGODCORE_API const char* ToString(BackDoorReason v);

	/// <summary>
	/// Smuggling (U.SPC-T426): every lawful port has a back door. What its open market will not take from the player's
	/// hold, or cannot (stolen goods, a good its owner bans, anything at all when the port is blockaded or its owner has
	/// embargoed the ship's flag), its smugglers will, off the books: no port tax, and at a premium over the port's own
	/// price (<see cref="TradeRules.BackDoorPremium"/>; a blockaded or banned good pays most, a fence takes stolen goods
	/// for less than they are worth). The goods land in the port's stores (a blockade run relieves the port). But customs
	/// may be waiting (<see cref="TradeRules.CustomsChance"/>, more under a blockade's pickets, more for a captain with a
	/// price on their head): caught, the crates are seized and the law charges smuggling, with a witness, against the
	/// port's owner (or the blockader).
	/// </summary>
	class SPACEGODCORE_API Smuggling final
	{
		cs::Ref<TradeDesk> desk;
		cs::Random rng;

	public:
		Smuggling(cs::Ref<TradeDesk> desk, int seed);

	private:
		cs::Ref<::SpaceGod::Core::World> World() const;

	public:
		/// <summary>Whether <paramref name="s"/> has a back door at all: a lawful port that stands (a haven or lawless station
		/// trades in the open, see <see cref="BlackMarket"/>).</summary>
		static bool HasBackDoor(const cs::Ref<Settlement>& s);

		/// <summary>Why <paramref name="item"/> would have to go in by <paramref name="s"/>'s back door from <paramref name="ship"/>, or None.</summary>
		BackDoorReason ReasonFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, const cs::Ref<CargoItem>& item) const;

		/// <summary>The worst reason any crate of <paramref name="g"/> in the hold has (a blockade or embargo covers them all), or None.</summary>
		BackDoorReason ReasonFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good g) const;

		/// <summary>What the back door pays a tonne of <paramref name="g"/> at <paramref name="s"/> for crates going in for <paramref name="reason"/>.</summary>
		static double PriceFor(const cs::Ref<Settlement>& s, Good g, BackDoorReason reason);

		/// <summary>
		/// The chance customs catches a back-door sale of crates going in for <paramref name="reason"/> at <paramref name="s"/>:
		/// the vigilance setting, more under a blockade's pickets, less for a fence, and more again for a captain the
		/// port's owner (or the blockader) wants; at most 95%.
		/// </summary>
		double CatchChance(const cs::Ref<Settlement>& s, BackDoorReason reason) const;

	private:
		/// <summary>Who catches a back-door sale: the blockader under a blockade, else the port's owner.</summary>
		cs::Ref<Faction> Watcher(const cs::Ref<Settlement>& s, BackDoorReason reason) const;

	public:
		enum class Outcome : int
		{
			/// <summary>Sold: paid, and the crates are in the port.</summary>
			Sold,

			/// <summary>Customs caught it: the crates are seized and smuggling charged.</summary>
			Caught,

			/// <summary>There is no back door here (a haven, a lawless station, a ruin).</summary>
			NoBackDoor,

			/// <summary>No crate of that good needs the back door (or none is aboard): sell it on the open market.</summary>
			NothingToSmuggle,
		};
		friend const char* ToString(Outcome v);

		/// <summary>
		/// Sells up to <paramref name="crates"/> crates of <paramref name="g"/> out of <paramref name="ship"/>'s hold (the one
		/// the player flies) at <paramref name="s"/>'s back door: only crates the open market will not take. One roll against
		/// customs for the lot (<paramref name="roll"/> in 0..1 for a given roll; random otherwise). Sold, the ship's purse is
		/// paid off the books and the goods go into the port's stores; caught, the crates are seized and the player charged
		/// with smuggling. Reports the crates and credits in <paramref name="sold"/> and <paramref name="credits"/>.
		/// </summary>
		Outcome SellBackDoor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good g, int crates, double jd, int& sold, double& credits, double roll = cs::Double::NaN);

		static cs::String Describe(BackDoorReason r);
	private:
		Smuggling() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Smuggling); // ObjectGraph registration (Persistence/Registrations)
	};

	SPACEGODCORE_API const char* ToString(Smuggling::Outcome v);
}
