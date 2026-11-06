// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Smuggling.cs (US-T19).

#include "Economy/Smuggling.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Settings/GameSetting.h"
#include "Combat/Diplomacy.h"
#include "Law/Law.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(BackDoorReason v)
	{
		switch (v)
		{
			case BackDoorReason::None: return "None";
			case BackDoorReason::Stolen: return "Stolen";
			case BackDoorReason::Contraband: return "Contraband";
			case BackDoorReason::Blockade: return "Blockade";
			case BackDoorReason::Embargo: return "Embargo";
		}
		return "";
	}

	const char* ToString(Smuggling::Outcome v)
	{
		switch (v)
		{
			case Smuggling::Outcome::Sold: return "Sold";
			case Smuggling::Outcome::Caught: return "Caught";
			case Smuggling::Outcome::NoBackDoor: return "NoBackDoor";
			case Smuggling::Outcome::NothingToSmuggle: return "NothingToSmuggle";
		}
		return "";
	}

	Smuggling::Smuggling(cs::Ref<TradeDesk> desk, int seed) : rng(seed)
	{
		this->desk = desk;
	}

	cs::Ref<::SpaceGod::Core::World> Smuggling::World() const { return desk->World(); }

	bool Smuggling::HasBackDoor(const cs::Ref<Settlement>& s)
	{
		return s != nullptr && !s->Fallen && s->Owner != nullptr && !s->Owner->IsPirate && s->Kind != SettlementKind::Haven && !s->Lawless;
	}

	BackDoorReason Smuggling::ReasonFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, const cs::Ref<CargoItem>& item) const
	{
		if (!HasBackDoor(s) || item == nullptr || ship == nullptr) return BackDoorReason::None;
		if (World()->Diplomacy()->BlockadedBy(s) != nullptr) return BackDoorReason::Blockade;
		if (World()->Diplomacy()->IsEmbargoed(s->Owner, ship->Faction)) return BackDoorReason::Embargo;
		if (s->Owner->Contraband.Contains(item->Good)) return BackDoorReason::Contraband;
		if (item->Stolen) return BackDoorReason::Stolen;
		return BackDoorReason::None;
	}

	BackDoorReason Smuggling::ReasonFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good g) const
	{
		auto best = BackDoorReason::None;
		if (ship == nullptr) return best;
		for (const auto& item : ship->PersonalHold()->Items())
		{
			if (item->Good != g) continue;
			auto r = ReasonFor(ship, s, item);
			if (r > best) best = r;
		}
		return best;
	}

	double Smuggling::PriceFor(const cs::Ref<Settlement>& s, Good g, BackDoorReason reason)
	{
		double local = cs::Math::Max(s->Price[static_cast<int>(g)], 0.5 * GoodInfo::Of(g)->BasePrice), premium = TradeRules::BackDoorPremium()->Value;
		switch (reason)
		{
			case BackDoorReason::Blockade: return local * premium * 1.2;
			case BackDoorReason::Contraband: return cs::Math::Max(local, GoodInfo::Of(g)->BasePrice) * premium * 1.3;
			case BackDoorReason::Embargo: return local * premium;
			case BackDoorReason::Stolen: return local * 0.7;
			default: return 0;
		}
	}

	double Smuggling::CatchChance(const cs::Ref<Settlement>& s, BackDoorReason reason) const
	{
		double c = TradeRules::CustomsChance()->Value;
		switch (reason)
		{
			case BackDoorReason::Blockade: c *= 1.6; break;
			case BackDoorReason::Stolen: c *= 0.75; break;
			case BackDoorReason::None: return 0;
			default: break;
		}
		auto law = World()->Law;
		auto watcher = Watcher(s, reason);
		// The pilot the player is embodied in (US-T148): a clean one draws no extra eyes.
		auto rec = law != nullptr ? law->PlayerRecord() : nullptr;
		if (rec != nullptr && rec->WantedBy(watcher)) c *= 1.5;
		return cs::Math::Max(0.0, cs::Math::Min(0.95, c));
	}

	cs::Ref<Faction> Smuggling::Watcher(const cs::Ref<Settlement>& s, BackDoorReason reason) const
	{
		if (reason == BackDoorReason::Blockade)
		{
			auto by = World()->Diplomacy()->BlockadedBy(s);
			return by ? by : s->Owner;
		}
		return s->Owner;
	}

	Smuggling::Outcome Smuggling::SellBackDoor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good g, int crates, double jd, int& sold, double& credits, double roll)
	{
		sold = 0;
		credits = 0;
		if (!HasBackDoor(s)) return Outcome::NoBackDoor;
		auto lot = cs::List<cs::Ref<CargoItem>>();
		auto reason = BackDoorReason::None;
		for (const auto& item : ship->PersonalHold()->Items())
		{
			if (lot.Count() >= crates || item->Good != g) continue;
			auto r = ReasonFor(ship, s, item);
			if (r == BackDoorReason::None) continue;
			lot.Add(item);
			if (r > reason) reason = r;
		}
		if (lot.Count() == 0) return Outcome::NothingToSmuggle;
		if (cs::Double::IsNaN(roll)) roll = rng.NextDouble();
		double tonnes = 0;
		for (const auto& item : lot)
		{
			ship->PersonalHold()->Remove(item);
			tonnes += item->Tonnes();
		}
		if (roll < CatchChance(s, reason))
		{
			auto by = Watcher(s, reason);
			desk->TimesCaught++;
			cs::String what = cs::Cat(lot.Count(), " crate", lot.Count() == 1 ? "" : "s", " of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name));
			if (World()->Law != nullptr) World()->Law->Commit(World()->Law->RecordOf(ship), CrimeKind::Smuggling, by, by, s->Body, jd, cs::Cat("customs at ", s->Name), cs::Cat(what, " through the back door"));
			desk->Notice(cs::Cat("Customs at ", s->Name, " seize ", what, ": you are charged with smuggling"));
			return Outcome::Caught;
		}
		credits = PriceFor(s, g, reason) * tonnes;
		s->Stock[static_cast<int>(g)] += tonnes;
		s->RefreshPrices();
		ship->Faction->Treasury += credits;
		sold = lot.Count();
		desk->Smuggled++;
		desk->SmugglingTakings += credits;
		return Outcome::Sold;
	}

	cs::String Smuggling::Describe(BackDoorReason r)
	{
		switch (r)
		{
			case BackDoorReason::Stolen: return "stolen";
			case BackDoorReason::Contraband: return "banned here";
			case BackDoorReason::Blockade: return "blockade run";
			case BackDoorReason::Embargo: return "embargoed";
			default: return "";
		}
	}
}
