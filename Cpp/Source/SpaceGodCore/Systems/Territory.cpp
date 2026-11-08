// US-T190: faction territory driven by the economy, reviewed daily (see Systems/Territory.h).

#include "Systems/Territory.h"

#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Colonisation.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/GalaxyLife.h" // US-T100
#include "Systems/LastingConsequences.h" // US-T105
#include "Systems/Exploration.h" // US-T260
#include "Systems/Polities.h"
#include "World.h"

#include <unordered_map>

namespace SpaceGod::Core
{
	namespace
	{
		/// The most daily reviews one update catches up on: a longer jump is reviewed once at its end.
		constexpr int MaxCatchUp = 31;

		bool Stands(const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && s->Stage != ColonyStage::Ruin && s->Kind != SettlementKind::Haven; }

		cs::String NameOf(const cs::Ref<Faction>& f) { return f != nullptr ? cs::Cat("the ", f->Name) : cs::String("nobody"); }
	}

	double FactionStanding::Trend() const
	{
		int n = History.Count();
		if (n < 2) return 0;
		int from = cs::Math::Max(0, n - 1 - Territory::TrendDays);
		return History[n - 1] - History[from];
	}

	cs::Ref<Territory> Territory::Of(Galaxy& galaxy)
	{
		if (galaxy.Territory == nullptr) galaxy.Territory = cs::New<Territory>();
		return galaxy.Territory;
	}

	cs::Ref<FactionStanding> Territory::StandingOf(const cs::Ref<Faction>& f) const
	{
		for (const auto& s : Standings)
			if (s->Faction == f) return s;
		return nullptr;
	}

	cs::Ref<TerritoryChange> Territory::LastChange(const cs::Ref<StarSystem>& s, double asOfJd) const
	{
		for (int i = Changes.Count() - 1; i >= 0; i--)
			if (Changes[i]->System == s && Changes[i]->Jd <= asOfJd) return Changes[i];
		return nullptr;
	}

	double Territory::StrengthOf(int systems, double population, double treasury, double cargoLost)
	{
		return population / 1e6 + 5.0 * systems + treasury / 1e8 - cargoLost / 1e7;
	}

	cs::Ref<FactionStanding> Territory::Standing(const cs::Ref<Faction>& f)
	{
		auto s = StandingOf(f);
		if (s != nullptr) return s;
		s = cs::New<FactionStanding>();
		s->Faction = f;
		Standings.Add(s);
		return s;
	}

	// ---------------------------------------------------------------- the review

	void Territory::Update(Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(jd)) return;
		// Every economy's lost cargo is heard of from the moment its system runs (and again after a load: not saved).
		Wire(galaxy);
		if (cs::Double::IsNaN(nextDailyJd))
		{
			// The first look only takes stock: who holds what, and every polity's strength.
			Watch(galaxy, jd, true);
			Measure(galaxy, jd);
			nextDailyJd = cs::Math::Floor(jd) + 1;
			return;
		}
		if (jd < nextDailyJd) return;
		// Each review runs on its whole-day mark, whatever the frame, so the borders never depend on the frame rate.
		if (jd - nextDailyJd > MaxCatchUp) nextDailyJd = cs::Math::Floor(jd);
		while (nextDailyJd <= jd)
		{
			double day = nextDailyJd;
			Recruit(galaxy, day);
			Sell(galaxy, day);
			Watch(galaxy, day, false);
			Measure(galaxy, day);
			nextDailyJd = day + 1;
		}
	}

	void Territory::Wire(Galaxy& galaxy)
	{
		for (auto it = wired.begin(); it != wired.end();)
			if (it->second.expired()) it = wired.erase(it);
			else ++it;
		auto* self = this;
		for (const auto& s : galaxy.Systems)
		{
			auto economy = s->World ? s->World->Economy : nullptr;
			if (economy == nullptr || wired.count(economy.get()) > 0) continue;
			wired[economy.get()] = economy;
			economy->CargoLost += [self](cs::Ref<Ship> ship, Good good, double tonnes, cs::String /*cause*/, cs::Ref<Faction> /*by*/) { self->OnCargoLost(ship, good, tonnes); };
		}
	}

	void Territory::OnCargoLost(const cs::Ref<Ship>& ship, Good good, double tonnes)
	{
		if (ship == nullptr || ship->Faction == nullptr || ship->Faction->IsPirate || tonnes <= 0) return;
		Standing(ship->Faction)->CargoLost += tonnes * GoodInfo::Of(good)->BasePrice;
	}

	void Territory::Measure(Galaxy& galaxy, double jd)
	{
		(void)jd;
		std::unordered_map<const Faction*, double> people;
		std::unordered_map<const Faction*, int> held;
		for (const auto& s : galaxy.Systems)
		{
			if (s->World == nullptr || s->World->Economy == nullptr) continue;
			for (const auto& st : s->World->Economy->Settlements)
				if (Stands(st) && st->Owner != nullptr) people[st->Owner.get()] += st->Population;
		}
		for (const auto& h : holders)
			if (h->Holder != nullptr) held[h->Holder.get()]++;
		double fade = cs::Math::Pow(0.5, 1.0 / LossHalfLifeDays);
		for (const auto& f : galaxy.Polities->Factions())
		{
			if (f->IsPirate) continue;
			auto st = Standing(f);
			auto p = people.find(f.get());
			auto h = held.find(f.get());
			st->Population = p != people.end() ? p->second : 0;
			st->Systems = h != held.end() ? h->second : 0;
			st->Treasury = f->Treasury;
			st->Strength = StrengthOf(st->Systems, st->Population, st->Treasury, st->CargoLost);
			st->History.Add(st->Strength);
			if (st->History.Count() > HistoryDays) st->History.RemoveAt(0);
			st->CargoLost *= fade;
		}
	}

	void Territory::Recruit(Galaxy& galaxy, double jd)
	{
		auto polities = galaxy.Polities;
		for (const auto& f : polities->Factions())
		{
			if (f->IsPirate || f->Kind != PolityKind::State || f->Treasury < RichAt) continue;
			auto st = Standing(f);
			if (!cs::Double::IsNaN(st->RecruitedJd) && jd - st->RecruitedJd < RecruitEveryDays) continue;
			auto w = polities->CapitalSystemOf(f);
			if (w == nullptr || w->Economy == nullptr || w->Economy->Colonisation == nullptr) continue;
			auto economy = w->Economy;
			auto colonisation = economy->Colonisation;
			// Crowded enough to send its own: the colonisation sees to it at its next review.
			double pressure = Colonisation::Pressure(economy, f);
			if (pressure >= Colonisation::PartySize) continue;
			cs::Ref<Settlement> port = nullptr;
			for (const auto& s : economy->Settlements)
				if (s->Owner == f && Stands(s) && (port == nullptr || s->Population > port->Population)) port = s;
			if (port == nullptr) continue;
			st->RecruitedJd = jd;
			double before = port->Overflow;
			port->Overflow += Colonisation::PartySize - pressure;
			double at = cs::Double::IsNaN(w->Jd) ? jd : cs::Math::Max(jd, w->Jd);
			auto ship = colonisation->TryLaunch(economy, f, at);
			// Whatever the party did not take goes home again: nobody waits on the docks for a ship that is not coming.
			port->Overflow = cs::Math::Min(port->Overflow, before);
			if (ship == nullptr) continue;
			double cost = ship->Colonists * RecruitCostPerPerson;
			f->Treasury -= cost;
			w->Log(at, cs::Cat("The ", f->Name, " pays the passage of ", cs::Fmt(ship->Colonists, "N0"), " colonists from ", ship->From->Name,
				" (", cs::Fmt(cost, "N0"), " cr) to carry its flag to new ground"), WorldEventKind::Economy, ship->From->Body);
		}
	}

	void Territory::Sell(Galaxy& galaxy, double jd)
	{
		auto polities = galaxy.Polities;
		const auto& factions = polities->Factions();
		for (const auto& f : factions)
		{
			if (f->IsPirate) continue;
			auto st = Standing(f);
			if (f->Treasury >= -DebtLimit)
			{
				st->InDebtSinceJd = cs::Double::NaN;
				continue;
			}
			if (cs::Double::IsNaN(st->InDebtSinceJd)) st->InDebtSinceJd = jd;
			if (jd - st->InDebtSinceJd < DebtDays) continue;

			// Its smallest station that is not its seat of government.
			auto capital = polities->CapitalOf(f);
			cs::Ref<Settlement> station = nullptr;
			for (const auto& s : polities->SettlementsOf(f))
			{
				if (!Stands(s) || s == capital || (s->Kind != SettlementKind::Station && s->Kind != SettlementKind::MiningOutpost)) continue;
				if (station == nullptr || s->Population < station->Population) station = s;
			}
			if (station == nullptr) continue;
			auto w = polities->WorldOf(station);
			if (w == nullptr || w->Economy == nullptr) continue;
			double price = cs::Math::Max(MinStationPrice, station->Population * StationPricePerPerson);

			// The richest solvent state at peace with it, one already trading in that system first.
			cs::Ref<Faction> buyer = nullptr;
			bool buyerThere = false;
			for (const auto& b : factions)
			{
				if (b == f || b->IsPirate || b->Kind != PolityKind::State || b->Treasury < 2 * price) continue;
				if (polities->Diplomacy != nullptr && polities->Diplomacy->Hostile(f, b)) continue;
				if (polities->SettlementsOf(b).Count() == 0) continue;
				bool there = false;
				for (const auto& s : w->Economy->Settlements)
					if (s->Owner == b && Stands(s)) { there = true; break; }
				if (buyer == nullptr || (there && !buyerThere) || (there == buyerThere && b->Treasury > buyer->Treasury))
				{
					buyer = b;
					buyerThere = there;
				}
			}
			if (buyer == nullptr) continue;

			buyer->Treasury -= price;
			f->Treasury += price;
			w->Economy->Capture(station, buyer);
			station->OccupiedFrom = nullptr;
			station->Garrison = cs::Double::NaN;
			station->Unrest = cs::Math::Max(station->Unrest, 0.15);
			if (w->War != nullptr) w->War->RevokeBookings();
			st->InDebtSinceJd = jd; // another station only after as long again in debt
			double at = cs::Double::IsNaN(w->Jd) ? jd : cs::Math::Max(jd, w->Jd);
			w->Log(at, cs::Cat("Deep in debt, the ", f->Name, " sell ", station->Name, " to the ", buyer->Name, " for ", cs::Fmt(price, "N0"), " cr"),
				WorldEventKind::Economy, station->Body);
			sold.Add(station);
		}
	}

	void Territory::Watch(Galaxy& galaxy, double jd, bool quiet)
	{
		// Forget the systems the galaxy has let go.
		holders.RemoveAll([&](const cs::Ref<SystemHolder>& h) { return !galaxy.Systems.Contains(h->System); });
		Changes.RemoveAll([&](const cs::Ref<TerritoryChange>& c) { return !galaxy.Systems.Contains(c->System); });
		auto polities = galaxy.Polities;
		for (const auto& s : galaxy.Systems)
		{
			if (s->World == nullptr || s->World->Economy == nullptr) continue;
			auto holder = s->Holder();
			cs::Ref<SystemHolder> h = nullptr;
			for (const auto& x : holders)
				if (x->System == s) { h = x; break; }
			if (h == nullptr)
			{
				// A system the review has not seen before is taken as it stands: it joined, it did not change hands.
				h = cs::New<SystemHolder>();
				h->System = s;
				h->Holder = holder;
				holders.Add(h);
				continue;
			}
			if (h->Holder == holder) continue;
			auto c = cs::New<TerritoryChange>();
			c->System = s;
			c->From = h->Holder;
			c->To = holder;
			c->Jd = jd;
			h->Holder = holder;
			if (quiet) continue;
			bool bought = false;
			for (const auto& st : s->World->Economy->Settlements)
				if (st->Owner == holder && sold.Contains(st)) bought = true;
			bool war = false;
			for (const auto& st : s->World->Economy->Settlements)
				if (st->Owner == holder && st->OccupiedFrom != nullptr && st->OccupiedFrom == c->From) war = true;
			if (!war && c->From != nullptr && holder != nullptr && polities->Diplomacy != nullptr && polities->Diplomacy->Hostile(c->From, holder)) war = true;
			c->How = c->From == nullptr ? cs::String("first settled")
				: holder == nullptr ? cs::String("abandoned")
				: bought ? cs::String("bought")
				: war ? cs::String("taken in war")
				: cs::String("changed hands");
			Changes.Add(c);
			if (Changes.Count() > MaxChanges) Changes.RemoveAt(0);
			cs::String name = s->Name;
			cs::String text = holder == nullptr ? cs::Cat(name, " is held by nobody now: ", NameOf(c->From), " have lost their hold on it")
				: c->From == nullptr ? cs::Cat(name, " is the ", holder->Name, "'s now: first settled")
				: cs::Cat(name, " passes from ", NameOf(c->From), " to the ", holder->Name, " (", c->How, ")");
			double at = cs::Double::IsNaN(s->World->Jd) ? jd : cs::Math::Max(jd, s->World->Jd);
			// Diplomacy is notable news: the relays carry it out through the gates (SystemReport::Notable).
			s->World->Log(at, text, WorldEventKind::Diplomacy, -1);
		}
		sold.Clear();
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(TerritoryChange, "SpaceGod.Core.TerritoryChange",
		SG_F(System), SG_F(From), SG_F(To), SG_F(Jd), SG_F(How))

	SG_SERIALIZE(FactionStanding, "SpaceGod.Core.FactionStanding",
		SG_F(Faction), SG_F(Systems), SG_F(Population), SG_F(Treasury), SG_F(CargoLost), SG_F(Strength), SG_F(History),
		SG_F(InDebtSinceJd), SG_F(RecruitedJd))

	SG_SERIALIZE(SystemHolder, "SpaceGod.Core.SystemHolder",
		SG_F(System), SG_F(Holder))

	SG_SERIALIZE(Territory, "SpaceGod.Core.Territory",
		SG_F(Standings), SG_F(Changes), SG_F(holders), SG_F(nextDailyJd),
		SG_OPT(Life), // US-T100, absent from older saves
		SG_OPT(Consequences), // US-T105, absent from older saves
		SG_OPT(Exploration)) // US-T260, absent from older saves
}
