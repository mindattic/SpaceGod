// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Companies.cs (US-T19).

#include "Economy/Companies.h"

#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "Systems/InterstellarTraffic.h"
#include "World.h"

#include <cctype>
#include <cmath>

namespace SpaceGod::Core
{
	const char* ToString(CompanyFate v)
	{
		switch (v)
		{
			case CompanyFate::Trading: return "Trading";
			case CompanyFate::Merged: return "Merged";
			case CompanyFate::Bankrupt: return "Bankrupt";
		}
		return "";
	}

	// ---------------------------------------------------------------- TradeRoute

	cs::String TradeRoute::ToString() const { return cs::Cat(From->Name, " - ", To->Name); }

	// ---------------------------------------------------------------- Company

	double Company::StandingWith(const cs::Ref<Faction>& f) const
	{
		double v = 0;
		return f != nullptr && Standing.TryGetValue(f, v) ? v : 0;
	}

	void Company::Adjust(const cs::Ref<Faction>& f, double by)
	{
		if (f == nullptr) return;
		Standing[f] = cs::Math::Max(-1.0, cs::Math::Min(1.0, StandingWith(f) + by));
	}

	double Company::Value() const
	{
		double v = Cash + (OnOrder.has_value() ? OrderPrice : 0);
		for (const auto& s : Ships) v += Companies::ResaleShare * Economy::HullPrice(s->Spec->Class);
		return v;
	}

	cs::Ref<TradeRoute> Company::RouteOf(const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to) const
	{
		for (const auto& r : Routes)
			if (r->From == from && r->To == to) return r;
		return nullptr;
	}

	double Company::ExpectedYearly(ShipClass cls, double marketPerDay) const
	{
		double rate = ClassPerDay[static_cast<int>(cls)];
		if (cs::Double::IsNaN(rate)) rate = marketPerDay;
		if (cs::Double::IsNaN(rate))
		{
			double f = ClassPerDay[static_cast<int>(ShipClass::Freighter)];
			if (cs::Double::IsNaN(f)) return 0;
			rate = f * Economy::CapacityOf(cls) / Economy::CapacityOf(ShipClass::Freighter) * 0.5;
		}
		return (rate - Economy::RunningCostPerDay(cls)) * 365.25 * (1 - Companies::CorporateTax);
	}

	// ---------------------------------------------------------------- Companies

	Companies::Companies(cs::Ref<World> world, int seed)
		: world(world), rng(seed), FoundingCapital(2.2 * Economy::HullPrice(ShipClass::Freighter))
	{
	}

	cs::List<cs::Ref<Company>> Companies::Live() const
	{
		cs::List<cs::Ref<Company>> r;
		for (const auto& c : All)
			if (c->Trading()) r.Add(c);
		return r;
	}

	int Companies::LiveCount() const
	{
		int n = 0;
		for (const auto& c : All)
			if (c->Trading()) n++;
		return n;
	}

	// ---------------------------------------------------------------- founding

	cs::Ref<Company> Companies::Found(const cs::String& name, const cs::String& tag, Rgb livery, const cs::Ref<Settlement>& home, const cs::Ref<Faction>& flag, double cash, double jd)
	{
		auto c = cs::New<Company>();
		c->Name = name; c->Tag = tag; c->Livery = livery; c->HomePort = home; c->Flag = flag; c->Cash = cash; c->FoundedJd = jd;
		for (const auto& f : world->Fleet->Factions())
		{
			double s = f == flag ? 0.5 : f->IsPirate ? -1 : world->Diplomacy()->Get(flag, f) == Relation::War ? -0.6 : 0.1;
			c->Standing[f] = s;
		}
		All.Add(c);
		foundedCount++;
		if (cs::Double::IsNaN(nextReviewJd)) nextReviewJd = jd + ReviewDays;
		if (cs::Double::IsNaN(lastReviewJd)) lastReviewJd = jd;
		if (cs::Double::IsNaN(nextFoundingJd)) nextFoundingJd = jd + FoundingDays;
		return c;
	}

	void Companies::Enlist(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship)
	{
		if (ship->Company != nullptr && ship->Company != c)
		{
			SettleOut(ship->Company, ship);
			ship->Company->Ships.Remove(ship);
		}
		ship->Company = c;
		// A load aboard was bought by the flag it flew: the new flag, which will take the whole of its sale, pays for it.
		if (ship->HasCargo && ship->Faction != nullptr && ship->Faction != c->Flag && ship->CargoCost > 0)
		{
			ship->Faction->Treasury += ship->CargoCost;
			c->Flag->Treasury -= ship->CargoCost;
		}
		ship->Faction = c->Flag;
		// A hull taken over abroad (its company merged into another) carries on with its run under the new flag, rather
		// than flying on with a voyage still in the old one's name.
		if (ship->Voyage != nullptr && ship->Voyage->Faction != nullptr) ship->Voyage->Faction = c->Flag;
		ship->CompanyBookedCredits = ship->Credits;
		ship->LoadedFrom = nullptr;
		if (!c->Ships.Contains(ship)) c->Ships.Add(ship);
	}

	void Companies::SettleOut(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship)
	{
		if (c == nullptr || ship == nullptr) return;
		double delta = ship->Credits - ship->CompanyBookedCredits;
		ship->CompanyBookedCredits = ship->Credits;
		if (delta == 0) return;
		double tax = cs::Math::Max(0.0, delta) * CorporateTax;
		c->Flag->Treasury += tax - delta;
		c->Cash += delta - tax;
		c->Earned += delta;
		c->TaxPaid += tax;
	}

	void Companies::OnShipAdded(const cs::Ref<Ship>& ship)
	{
		auto c = ship->Company;
		if (c == nullptr || c->Ships.Contains(ship)) return;
		if (!c->Trading())
		{
			// Bought out while this hull was inside a gate (in no system's fleet, so Merge never saw it): it carries
			// on under the buyer, as every sister hull did, and its takings follow the old books over as Merge's do.
			cs::Ref<Company> buyer = c->Fate == CompanyFate::Merged ? c->MergedInto : nullptr;
			while (buyer != nullptr && buyer->Fate == CompanyFate::Merged && buyer->MergedInto != nullptr) buyer = buyer->MergedInto;
			if (buyer != nullptr && buyer->Trading())
			{
				Enlist(buyer, ship);
				buyer->Cash += c->Cash;
				c->Cash = 0;
				return;
			}
			ship->Company = nullptr;
			return;
		}
		c->Ships.Add(ship);
	}

	// ---------------------------------------------------------------- what captains are told

	double Companies::RouteFactor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to)
	{
		auto c = ship->Company;
		if (c == nullptr || to == nullptr) return 1;
		if (c->StandingWith(to->Owner) < ShunnedStanding) return 0;
		if (from != nullptr && c->StandingWith(from->Owner) < ShunnedStanding) return 0;
		double f = 1;
		if (to == c->HomePort || from == c->HomePort) f += HomeBonus;
		if (from != nullptr)
		{
			for (const auto& r : c->Chosen)
				if (r->From == from && r->To == to) { f += ChosenBonus; break; }
			auto known = c->RouteOf(from, to);
			if (known != nullptr && known->Runs >= 2 && known->Profit < 0) f *= 0.5;
		}
		return f;
	}

	void Companies::OnLoaded(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from)
	{
		if (ship->Company == nullptr) return;
		ship->LoadedFrom = from;
		ship->LoadedJd = world->Economy->Jd;
		ship->LoadedCredits = ship->Credits;
	}

	void Companies::OnSold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& to, Good good, double tonnes)
	{
		auto c = ship->Company;
		if (c == nullptr) return;
		c->Adjust(to->Owner, GoodInfo::Of(good)->Category == GoodCategory::Essential ? 0.012 : 0.006);
		auto from = ship->LoadedFrom;
		ship->LoadedFrom = nullptr;
		if (from == nullptr || from == to) return;
		double jd = world->Economy->Jd;
		Record(c, from, to, ship->Credits - ship->LoadedCredits, cs::Math::Max(0.25, jd - ship->LoadedJd), jd);
	}

	void Companies::Record(const cs::Ref<Company>& c, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, double profit, double days, double jd)
	{
		auto r = c->RouteOf(from, to);
		if (r == nullptr)
		{
			auto lanes = world->Fleet->Lanes;
			const auto& here = world->Economy->Settlements;
			// Bought in another star system (an interstellar run), or in another planetary system of this one.
			bool cross = !here.Contains(from) || !here.Contains(to) || (lanes != nullptr && lanes->SystemOf(from->Body) != lanes->SystemOf(to->Body));
			r = cs::New<TradeRoute>();
			r->From = from; r->To = to; r->CrossSystem = cross;
			c->Routes.Add(r);
			r->Profit = profit;
			r->Days = days;
		}
		else
		{
			r->Profit += (profit - r->Profit) * RouteMemory;
			r->Days += (days - r->Days) * RouteMemory;
		}
		r->Runs++;
		r->LastJd = jd;
	}

	// ---------------------------------------------------------------- the review

	void Companies::Update(double jd)
	{
		if (!Enabled() || cs::Double::IsNaN(nextReviewJd) || jd < nextReviewJd) return;
		nextReviewJd = jd + ReviewDays;
		Review(jd);
		if (jd >= nextFoundingJd)
		{
			nextFoundingJd = jd + FoundingDays;
			TryFound(jd);
		}
	}

	void Companies::Review(double jd)
	{
		double days = cs::Double::IsNaN(lastReviewJd) ? 0 : cs::Math::Max(0.0, jd - lastReviewJd);
		lastReviewJd = jd;
		// A list copy: mergers and bankruptcies change who trades.
		auto trading = Live();
		for (const auto& c : trading) Book(c, days, jd);
		for (const auto& c : trading)
		{
			if (!c->Trading()) continue;
			Standings(c);
			ChooseRoutes(c, jd);
			if (Trouble(c, jd)) continue;
			Expand(c, jd);
		}
	}

	void Companies::Book(const cs::Ref<Company>& c, double days, double jd)
	{
		double trade = 0, running = 0;
		auto classSum = cs::NewArray<double>(c->ClassPerDay.Count());
		auto classCount = cs::NewArray<int>(c->ClassPerDay.Count());
		for (int i = c->Ships.Count() - 1; i >= 0; i--)
		{
			auto s = c->Ships[i];
			if (s->GateDeparted)
			{
				// Gone on through a gate: its copy over there is still ours, carries the same unbooked takings
				// and books them (and its own trade) itself.
				c->Ships.RemoveAt(i);
				continue;
			}
			double delta = s->Credits - s->CompanyBookedCredits;
			s->CompanyBookedCredits = s->Credits;
			trade += delta;
			// Its takings went to the flag it flew when it earned them: the company's, even if it has since been taken.
			c->Flag->Treasury -= delta;
			if (s->Destroyed || s->Derelict || s->Faction != c->Flag)
			{
				c->Ships.RemoveAt(i);
				c->HullsLost++;
				if (s->Company == c) s->Company = nullptr;
				world->Log(jd, cs::Cat(c->Name, " writes off the ", cs::Strings::ToLowerInvariant(s->Spec->DisplayName), " ", s->Name), WorldEventKind::Economy, s->Orbit ? s->Orbit->Body : -1);
				continue;
			}
			running += Economy::RunningCostPerDay(s->Spec->Class) * days;
			int k = static_cast<int>(s->Spec->Class);
			classSum[k] += delta;
			classCount[k]++;
		}
		double tax = cs::Math::Max(0.0, trade) * CorporateTax;
		c->Flag->Treasury += tax;
		c->Cash += trade - running - tax;
		c->Earned += trade;
		c->RunningCosts += running;
		c->TaxPaid += tax;
		if (days <= 0) return;
		double k2 = 1 - cs::Math::Exp(-days / SmoothingDays);
		c->IncomePerDay += ((trade - running - tax) / days - c->IncomePerDay) * k2;
		for (int i = 0; i < classSum.Count(); i++)
		{
			if (classCount[i] == 0) continue;
			double rate = classSum[i] / classCount[i] / days;
			c->ClassPerDay[i] = cs::Double::IsNaN(c->ClassPerDay[i]) ? rate : c->ClassPerDay[i] + (rate - c->ClassPerDay[i]) * k2;
		}
	}

	void Companies::Standings(const cs::Ref<Company>& c)
	{
		for (const auto& f : world->Fleet->Factions())
		{
			if (f == c->Flag) { c->Standing[f] = cs::Math::Max(c->StandingWith(f), 0.5); continue; }
			if (f->IsPirate) { c->Standing[f] = -1; continue; }
			double s = c->StandingWith(f);
			double toward = world->Diplomacy()->Get(c->Flag, f) == Relation::War ? -0.7 : 0;
			if (s < toward || toward < 0) c->Standing[f] = s + (toward - s) * 0.05;
			else c->Standing[f] = s - cs::Math::Min(0.002, s - toward);
		}
	}

	void Companies::ChooseRoutes(const cs::Ref<Company>& c, double jd)
	{
		c->Routes.RemoveAll([&](const cs::Ref<TradeRoute>& r) { return jd - r->LastJd > 365; });
		c->Chosen.Clear();
		cs::List<cs::Ref<TradeRoute>> candidates;
		for (const auto& r : c->Routes)
			if (r->Profit > 0 && jd - r->LastJd < 120 && c->StandingWith(r->From->Owner) >= ShunnedStanding && c->StandingWith(r->To->Owner) >= ShunnedStanding)
				candidates.Add(r);
		candidates.Sort([](const cs::Ref<TradeRoute>& a, const cs::Ref<TradeRoute>& b) { return cs::Compare(b->PerDay(), a->PerDay()); });
		for (int i = 0; i < candidates.Count() && i < ChosenRoutes; i++) c->Chosen.Add(candidates[i]);
	}

	bool Companies::Trouble(const cs::Ref<Company>& c, double jd)
	{
		if (c->Cash >= 0)
		{
			c->InDebtSinceJd = cs::Double::NaN;
			return false;
		}
		if (cs::Double::IsNaN(c->InDebtSinceJd)) c->InDebtSinceJd = jd;
		CancelOrder(c);
		if (c->Cash >= 0) { c->InDebtSinceJd = cs::Double::NaN; return false; }
		if (TryMerge(c, jd)) return true;
		bool longInDebt = jd - c->InDebtSinceJd >= BankruptDays;
		if (c->Ships.Count() == 0 || longInDebt)
		{
			Bankrupt(c, jd);
			return true;
		}
		if (c->Cash < -DebtLimit) SellHull(c, jd);
		return true;
	}

	// ---------------------------------------------------------------- growth

	void Companies::Expand(const cs::Ref<Company>& c, double jd)
	{
		if (c->OnOrder.has_value())
		{
			if (jd - c->OrderJd < OrderPatienceDays && YardWillBuild(c, c->OrderYard)) return;
			CancelOrder(c);
		}
		if (world->TraderCount() + Ordered() >= world->MerchantCap) return;
		cs::Nullable<ShipClass> best;
		double bestMargin = 0;
		for (ShipClass cls : { ShipClass::Freighter, ShipClass::Miner })
		{
			double price = Economy::HullPrice(cls);
			double yearly = c->ExpectedYearly(cls, MarketPerDay(cls));
			double margin = yearly * PaybackYears / price;
			if (margin >= 1 && margin > bestMargin && c->Cash >= price * (1 + ReserveShare))
			{
				bestMargin = margin;
				best = cls;
			}
		}
		if (!best.has_value()) return;
		auto yard = YardFor(c);
		if (yard == nullptr) return;
		c->OnOrder = best;
		c->OrderYard = yard;
		c->OrderJd = jd;
		c->OrderPrice = Economy::HullPrice(*best) * (1 - 0.1 * cs::Math::Max(0.0, c->StandingWith(yard->Owner)));
		c->Cash -= c->OrderPrice;
	}

	double Companies::MarketPerDay(ShipClass cls) const
	{
		double sum = 0;
		int n = 0;
		for (const auto& c : All)
		{
			double r = c->ClassPerDay[static_cast<int>(cls)];
			if (!c->Trading() || cs::Double::IsNaN(r)) continue;
			int hulls = 0;
			for (const auto& s : c->Ships)
				if (s->Spec->Class == cls) hulls++;
			sum += r * hulls;
			n += hulls;
		}
		return n > 0 ? sum / n : cs::Double::NaN;
	}

	int Companies::Ordered() const
	{
		int n = 0;
		for (const auto& c : All)
			if (c->Trading() && c->OnOrder.has_value()) n++;
		return n;
	}

	bool Companies::YardWillBuild(const cs::Ref<Company>& c, const cs::Ref<Settlement>& yard) const
	{
		return yard != nullptr && !yard->Fallen && yard->HasShipyard() && yard->Owner != nullptr && !yard->Owner->IsPirate &&
			world->Diplomacy()->CanDockAt(c->Flag, yard) && c->StandingWith(yard->Owner) >= YardStanding;
	}

	cs::Ref<Settlement> Companies::YardFor(const cs::Ref<Company>& c) const
	{
		if (YardWillBuild(c, c->HomePort)) return c->HomePort;
		auto lanes = world->Fleet->Lanes;
		cs::Ref<Settlement> best = nullptr;
		double bestScore = cs::Double::MinValue;
		for (const auto& s : world->Economy->Settlements)
		{
			if (!YardWillBuild(c, s)) continue;
			double score = c->StandingWith(s->Owner) + s->BuildPoints / 1e6;
			if (lanes != nullptr && c->HomePort != nullptr && lanes->SystemOf(s->Body) == lanes->SystemOf(c->HomePort->Body)) score += 1;
			if (score > bestScore)
			{
				bestScore = score;
				best = s;
			}
		}
		return best;
	}

	void Companies::CancelOrder(const cs::Ref<Company>& c)
	{
		if (!c->OnOrder.has_value()) return;
		c->Cash += c->OrderPrice;
		c->OnOrder.reset();
		c->OrderYard = nullptr;
		c->OrderPrice = 0;
	}

	cs::Nullable<ShipClass> Companies::OrderAt(const cs::Ref<Settlement>& yard) const
	{
		cs::Ref<Company> first = FirstOrderAt(yard, std::nullopt);
		return first ? first->OnOrder : cs::Nullable<ShipClass>();
	}

	cs::Ref<Company> Companies::FirstOrderAt(const cs::Ref<Settlement>& yard, cs::Nullable<ShipClass> cls) const
	{
		cs::Ref<Company> first = nullptr;
		for (const auto& c : All)
			if (c->Trading() && c->OnOrder.has_value() && c->OrderYard == yard && (!cls.has_value() || c->OnOrder == cls) && (first == nullptr || c->OrderJd < first->OrderJd))
				first = c;
		return first;
	}

	bool Companies::PaidFor(const cs::Ref<Settlement>& yard, ShipClass cls, double price) const { return FirstOrderAt(yard, cls) != nullptr; }

	cs::Ref<Company> Companies::TakeDelivery(const cs::Ref<Settlement>& yard, ShipClass cls, double jd)
	{
		auto c = FirstOrderAt(yard, cls);
		if (c == nullptr) return nullptr;
		yard->Owner->Treasury += c->OrderPrice;
		c->HullsSpent += c->OrderPrice;
		c->HullsBought++;
		c->OnOrder.reset();
		c->OrderYard = nullptr;
		c->OrderPrice = 0;
		return c;
	}

	cs::String Companies::HullName(const cs::Ref<Company>& c, const cs::List<cs::String>& names)
	{
		cs::String word = cs::Strings::Split(c->Name, ' ')[0];
		int round = hullsNamed / names.Count();
		cs::String name = cs::Cat(word, " ", names[hullsNamed % names.Count()], (round > 0 ? cs::Cat(" ", round + 1) : cs::String()));
		hullsNamed++;
		return name;
	}

	// ---------------------------------------------------------------- trouble

	cs::Ref<Ship> Companies::Weakest(const cs::Ref<Company>& c) const
	{
		cs::Ref<Ship> worst = nullptr;
		double worstRate = cs::Double::MaxValue;
		for (const auto& s : c->Ships)
		{
			if (!Saleable(s)) continue;
			double rate = c->ClassPerDay[static_cast<int>(s->Spec->Class)];
			if (cs::Double::IsNaN(rate)) rate = 0;
			rate -= Economy::RunningCostPerDay(s->Spec->Class);
			if (rate < worstRate)
			{
				worstRate = rate;
				worst = s;
			}
		}
		return worst;
	}

	bool Companies::Saleable(const cs::Ref<Ship>& s) const
	{
		return !s->Destroyed && s->Battle == nullptr && s != world->PilotedShip && s->Voyage == nullptr && !s->IsEscort() &&
			(s->Phase == ShipPhase::Parked || s->Phase == ShipPhase::Landed);
	}

	void Companies::SellHull(const cs::Ref<Company>& c, double jd)
	{
		auto ship = Weakest(c);
		if (ship == nullptr) return;
		double price = ResaleShare * Economy::HullPrice(ship->Spec->Class);
		cs::Ref<Company> buyer = nullptr;
		for (const auto& o : Live())
		{
			if (o == c || !Friendly(o, c) || o->Cash < price * (1 + ReserveShare) || o->ExpectedYearly(ship->Spec->Class, MarketPerDay(ship->Spec->Class)) * PaybackYears < price) continue;
			if (buyer == nullptr || o->Cash > buyer->Cash) buyer = o;
		}
		if (buyer != nullptr)
		{
			buyer->Cash -= price;
			c->Cash += price;
			c->Ships.Remove(ship);
			Enlist(buyer, ship);
			c->HullsSold++;
			world->Log(jd, cs::Cat(c->Name, " sells the ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), " ", ship->Name, " to ", buyer->Name, " to pay its debts"), WorldEventKind::Economy, ship->Orbit ? ship->Orbit->Body : -1);
			return;
		}
		Scrap(c, ship, jd);
	}

	void Companies::Scrap(const cs::Ref<Company>& c, const cs::Ref<Ship>& ship, double jd)
	{
		// Its takings since the last review are its own too, same as a hull sold or merged away.
		SettleOut(c, ship);
		c->Cash += ScrapShare * Economy::HullPrice(ship->Spec->Class);
		c->Ships.Remove(ship);
		c->HullsScrapped++;
		world->Economy->Release(ship);
		ship->Company = nullptr;
		// Its escorts (a hired fighter wing, U.SPC-T275) do not follow it to the breakers: they carry on under the strongest.
		if (world->War) world->War->HandOverCommand(ship);
		ship->Scrapped = true;
		// Gone from the fleet without a wreck: the hull goes to the breakers' yard.
		ship->Destroyed = true;
		ship->GateDeparted = true;
		world->Log(jd, cs::Cat(c->Name, " sends the ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), " ", ship->Name, " to the breakers"), WorldEventKind::Economy, ship->Orbit ? ship->Orbit->Body : -1);
	}

	bool Companies::Friendly(const cs::Ref<Company>& a, const cs::Ref<Company>& b) const
	{
		return a->Flag == b->Flag || (!a->Flag->IsPirate && !b->Flag->IsPirate && world->Diplomacy()->Get(a->Flag, b->Flag) == Relation::Peace);
	}

	bool Companies::TryMerge(const cs::Ref<Company>& target, double jd)
	{
		if (!target->Trading()) return false;
		double price = cs::Math::Max(0.0, target->Value());
		cs::Ref<Company> buyer = nullptr;
		for (const auto& o : Live())
		{
			if (o == target || !Friendly(o, target) || o->IncomePerDay <= 0) continue;
			if (o->Cash + cs::Math::Min(0.0, target->Cash) < price + ReserveShare * Economy::HullPrice(ShipClass::Freighter)) continue;
			if (buyer == nullptr || o->Cash > buyer->Cash) buyer = o;
		}
		if (buyer == nullptr) return false;
		Merge(buyer, target, price, jd);
		return true;
	}

	void Companies::Merge(const cs::Ref<Company>& buyer, const cs::Ref<Company>& target, double price, double jd)
	{
		CancelOrder(target);
		// Its hulls' takings since the last review are its own, and go over with its cash.
		for (const auto& s : cs::List<cs::Ref<Ship>>(target->Ships)) Enlist(buyer, s);
		target->Ships.Clear();
		buyer->Cash += target->Cash - price;
		target->Flag->Treasury += price;
		target->Cash = 0;
		for (const auto& r : target->Routes)
			if (buyer->RouteOf(r->From, r->To) == nullptr) buyer->Routes.Add(r);
		target->Fate = CompanyFate::Merged;
		target->FateJd = jd;
		target->MergedInto = buyer;
		world->Log(jd, cs::Cat(buyer->Name, " buys out ", target->Name) + (price > 0 ? cs::Cat(" for ", Credits(price)) : cs::String(" for its debts")), WorldEventKind::Economy, target->HomePort ? target->HomePort->Body : -1);
	}

	void Companies::Bankrupt(const cs::Ref<Company>& c, double jd)
	{
		CancelOrder(c);
		// Rivals buy what hulls they can use, the breakers take the rest of those in port ...
		for (auto s = Weakest(c); s != nullptr; s = Weakest(c)) SellHull(c, jd);
		// ... and one out on a run goes to the flag, whose merchant service carries on with it.
		for (const auto& s : cs::List<cs::Ref<Ship>>(c->Ships))
		{
			c->Ships.Remove(s);
			s->Company = nullptr;
		}
		c->Fate = CompanyFate::Bankrupt;
		c->FateJd = jd;
		world->Log(jd, cs::Cat(c->Name, " of ", (c->HomePort ? c->HomePort->Name : cs::String("nowhere")), " goes bankrupt") + (c->Cash < 0 ? cs::Cat(" owing ", Credits(-c->Cash)) : cs::String()), WorldEventKind::Economy, c->HomePort ? c->HomePort->Body : -1);
		// What the sales left over goes back where its money came from, rather than out of the world with the company.
		if (c->Cash > 0)
		{
			c->Flag->Treasury += c->Cash;
			c->Cash = 0;
		}
	}

	// ---------------------------------------------------------------- new companies

	cs::Ref<Company> Companies::TryFound(double jd)
	{
		if (LiveCount() >= MaxCompanies || world->TraderCount() >= world->MerchantCap * 0.9) return nullptr;
		double perHull = 0;
		int hulls = 0;
		for (const auto& c : Live())
		{
			if (c->Ships.Count() == 0) continue;
			perHull += c->ExpectedYearly(ShipClass::Freighter, MarketPerDay(ShipClass::Freighter)) * c->Ships.Count();
			hulls += c->Ships.Count();
		}
		if (hulls == 0 || perHull / hulls * PaybackYears < Economy::HullPrice(ShipClass::Freighter) * 1.2) return nullptr;
		cs::Ref<Settlement> best = nullptr;
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Fallen || s->Owner == nullptr || s->Owner->IsPirate || s->Kind == SettlementKind::Haven || s->Owner->Treasury < FoundingCapital * 5) continue;
			bool taken = false;
			for (const auto& c : Live()) taken |= c->HomePort == s;
			if (taken) continue;
			if (best == nullptr || s->TradeVolumeCredits > best->TradeVolumeCredits) best = s;
		}
		if (best == nullptr) return nullptr;
		best->Owner->Treasury -= FoundingCapital;
		auto [name, tag] = NewName(best);
		auto co = Found(name, tag, LiveryFor(foundedCount), best, best->Owner, FoundingCapital, jd);
		world->Log(jd, cs::Cat(co->Name, " is founded at ", best->Name, ", under the ", best->Owner->Name, " flag"), WorldEventKind::Economy, best->Body);
		return co;
	}

	const cs::List<cs::String>& Companies::Houses()
	{
		static const cs::List<cs::String> houses = { "Lines", "Carrying Company", "Freight", "Haulage", "Shipping", "Transport", "Bulk Carriers", "Trading Company" };
		return houses;
	}

	std::tuple<cs::String, cs::String> Companies::NewName(const cs::Ref<Settlement>& port) const
	{
		cs::String place = cs::Strings::Split(port->Name, ' ')[0];
		for (int tries = 0; tries < Houses().Count(); tries++)
		{
			cs::String house = Houses()[(foundedCount + tries) % Houses().Count()];
			cs::String name = cs::Cat(place, " ", house);
			bool used = false;
			for (const auto& c : All) used |= c->Name == name;
			if (!used) return { name, TagOf(name) };
		}
		cs::String n = cs::Cat(place, " Venture ", foundedCount + 1);
		return { n, TagOf(n) };
	}

	cs::String Companies::TagOf(const cs::String& name)
	{
		cs::String tag = "";
		for (const auto& w : cs::Strings::Split(name, ' '))
			// char.IsLetter / char.ToUpperInvariant on ASCII (company names are built from settlement names).
			if (static_cast<int>(w.size()) > 0 && std::isalpha(static_cast<unsigned char>(w[0]))) tag += static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
		return static_cast<int>(tag.size()) > 4 ? cs::Strings::Substring(tag, 0, 4) : tag;
	}

	Rgb Companies::LiveryFor(int n)
	{
		// C# `%` on doubles is fmod (truncated remainder), not IEEERemainder.
		double h = std::fmod(n * 0.618033988749895 + 0.11, 1.0);
		return Hsv(h, 0.55, 0.95);
	}

	Rgb Companies::Hsv(double h, double s, double v)
	{
		double r = Channel(5, h, s, v), g = Channel(3, h, s, v), b = Channel(1, h, s, v);
		return Rgb(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
	}

	double Companies::Channel(double n, double h, double s, double v)
	{
		double k = std::fmod(n + h * 6, 6.0);
		return v - v * s * cs::Math::Max(0.0, cs::Math::Min(cs::Math::Min(k, 4 - k), 1.0));
	}

	cs::String Companies::Credits(double c)
	{
		return cs::Math::Abs(c) >= 1e9 ? cs::Fmt(c / 1e9, "0.0") + " B cr"
			: cs::Math::Abs(c) >= 1e6 ? cs::Fmt(c / 1e6, "0.0") + " M cr"
			: cs::Fmt(c, "N0") + " cr";
	}
}
