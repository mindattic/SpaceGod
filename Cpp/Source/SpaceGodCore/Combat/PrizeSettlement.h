#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/PrizeSettlement.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Faction;
	class BoardingParty;

	/// <summary>One allied faction's claim on a hull its marines took together with others (U.SPC-T392), and what it
	/// comes away with once the claims are settled.</summary>
	class SPACEGODCORE_API PrizeClaim final
	{
	public:
		cs::Ref<Faction> Faction;

		/// <summary>Its part of the prize, 0 to 1: the fighting strength it committed over everyone's.</summary>
		double Share = 0;

		/// <summary>Its tonnes of the cargo aboard (the hold is always shared by the claims).</summary>
		double CargoTonnes = 0;

		/// <summary>Credits it is paid for its part of the hull: by the keeper buying it out, or from the breakers' price.</summary>
		double HullPaid = 0;
	};

	/// <summary>
	/// How allied factions who boarded one hull together settle the prize (U.SPC-T392, see docs/BOARDING.md "Combined
	/// allied boarding"). Each faction's claim is the fighting strength its marines brought aboard (marines landed times
	/// their quality), over everyone's, whether or not they lived: the ally who took the losses breaking in is not cut out
	/// by the one who walked in after. Then:
	/// <list type="bullet">
	/// <item><b>The hold is salvage, shared.</b> The cargo aboard is split by claim, whoever keeps the hull.</item>
	/// <item><b>The hull is negotiated.</b> The claimants are offered it largest claim first (the first to land breaking a
	/// tie): the first who can buy the others out keeps it, paying each its claim's part of the prize's worth (the hull's
	/// price, by how much of it is left, at <see cref="PrizeValueShare"/>).</item>
	/// <item><b>No one can pay: the breakers.</b> The hull is sold for scrap at <see cref="BreakersShare"/> of its price
	/// and the money is split by claim.</item>
	/// </list>
	/// A boarding by one faction alone is no negotiation: it keeps everything, as it always has.
	/// </summary>
	class SPACEGODCORE_API PrizeSettlement final
	{
	public:
		/// <summary>What a taken hull is worth to the allies who share it, as a part of its price new (by how much of it
		/// is left): the prize-court value a keeper buys its partners out at.</summary>
		static constexpr double PrizeValueShare = 0.5;

		/// <summary>What the breakers pay for a hull nobody could buy, as a part of its price new (the raiders' rate).</summary>
		static constexpr double BreakersShare = 0.25;

		cs::List<cs::Ref<PrizeClaim>> Claims = cs::List<cs::Ref<PrizeClaim>>();

		/// <summary>The faction that keeps the hull, or null when it goes to the breakers.</summary>
		cs::Ref<Faction> Keeper;

		/// <summary>The prize's worth the keeper bought its partners out at, and what the breakers paid (when they did).</summary>
		double HullValue = 0, BreakersValue = 0;

		bool SoldToBreakers() const;

		/// <summary>The claim of <paramref name="f"/>, or null.</summary>
		cs::Ref<PrizeClaim> Of(const cs::Ref<Faction>& f) const;

		/// <summary>What the keeper pays its partners in all.</summary>
		double BuyOut() const;

		/// <summary>
		/// Settles a prize taken by <paramref name="parties"/>: each claim from what its faction committed, the cargo
		/// (<paramref name="cargoTonnes"/>) split by claim, and the hull to the largest claimant that can pay its partners
		/// their parts of <paramref name="hullValue"/> from its <paramref name="treasury"/>, or to the breakers for
		/// <paramref name="breakersValue"/> shared by claim. Pure: the caller books the money and the cargo.
		/// </summary>
		static cs::Ref<PrizeSettlement> Negotiate(const cs::List<cs::Ref<BoardingParty>>& parties, double hullValue, double breakersValue, double cargoTonnes, cs::Func<double, cs::Ref<Faction>> treasury);
	};
}
