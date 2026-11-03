// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/PrizeSettlement.cs (see docs/MIGRATION.md).

#include "Combat/PrizeSettlement.h"

#include "Ships.h"
#include "Combat/Boarding.h"

namespace SpaceGod::Core
{
	bool PrizeSettlement::SoldToBreakers() const { return Keeper == nullptr; }

	cs::Ref<PrizeClaim> PrizeSettlement::Of(const cs::Ref<Faction>& f) const
	{
		for (const auto& c : Claims) if (c->Faction == f) return c;
		return nullptr;
	}

	double PrizeSettlement::BuyOut() const
	{
		if (Keeper == nullptr) return 0;
		double n = 0;
		for (const auto& c : Claims) if (c->Faction != Keeper) n += c->HullPaid;
		return n;
	}

	cs::Ref<PrizeSettlement> PrizeSettlement::Negotiate(const cs::List<cs::Ref<BoardingParty>>& parties, double hullValue, double breakersValue, double cargoTonnes, cs::Func<double, cs::Ref<Faction>> treasury)
	{
		auto deal = cs::New<PrizeSettlement>();
		deal->HullValue = cs::Math::Max(0.0, hullValue);
		// (C#'s `parties == null` cannot arise: the list is passed by reference.)
		if (parties.Count() == 0) return deal;

		// Strength committed is the claim; failing that (nobody brought any), marines landed; failing that, equal parts.
		double strength = 0, landed = 0;
		for (const auto& p : parties)
		{
			strength += cs::Math::Max(0.0, p->Strength);
			landed += cs::Math::Max(0.0, p->Landed);
		}
		auto order = cs::List<cs::Ref<BoardingParty>>(parties);
		for (const auto& p : parties)
		{
			double share = strength > 1e-9 ? cs::Math::Max(0.0, p->Strength) / strength
				: landed > 1e-9 ? cs::Math::Max(0.0, p->Landed) / landed
				: 1.0 / parties.Count();
			auto claim = cs::New<PrizeClaim>();
			claim->Faction = p->Faction;
			claim->Share = share;
			claim->CargoTonnes = cs::Math::Max(0.0, cargoTonnes) * share;
			deal->Claims.Add(claim);
		}

		// The hull is offered largest claim first; the first aboard breaks a tie.
		order.Sort([&](const cs::Ref<BoardingParty>& a, const cs::Ref<BoardingParty>& b)
		{
			int byStrength = cs::Compare(deal->Of(b->Faction)->Share, deal->Of(a->Faction)->Share);
			return byStrength != 0 ? byStrength : cs::Compare(a->Order, b->Order);
		});
		if (parties.Count() == 1)
		{
			deal->Keeper = parties[0]->Faction;
			return deal;
		}
		for (const auto& p : order)
		{
			auto claim = deal->Of(p->Faction);
			double owed = (1 - claim->Share) * deal->HullValue;
			double purse = treasury != nullptr && p->Faction != nullptr ? treasury(p->Faction) : 0;
			if (purse + 1e-6 < owed) continue;
			deal->Keeper = p->Faction;
			for (const auto& c : deal->Claims)
				if (c->Faction != deal->Keeper) c->HullPaid = c->Share * deal->HullValue;
			return deal;
		}

		// Nobody can buy the others out: the hull is scrapped and the price shared.
		deal->BreakersValue = cs::Math::Max(0.0, breakersValue);
		for (const auto& c : deal->Claims) c->HullPaid = c->Share * deal->BreakersValue;
		return deal;
	}
}
