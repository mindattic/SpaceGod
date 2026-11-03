// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Charters.cs (US-T19).

#include "Economy/Charters.h"

#include "Economy/Companies.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	cs::String CharterOffer::Title() const
	{
		return cs::Cat(Charterer->Name, ": a ", cs::Strings::ToLowerInvariant(ShipClassSpec::Of(Class)->DisplayName), " for ", cs::Fmt(Days, "0"), " d at ",
			cs::Fmt(RatePerDay, "N0"), " cr a day");
	}

	cs::String Charter::ToString() const
	{
		return cs::Cat(Ship ? Ship->Name() : cs::String(), " to ", Charterer ? Charterer->Name : cs::String());
	}

	const cs::List<ShipClass>& Charters::Classes()
	{
		static const cs::List<ShipClass> value = { ShipClass::Freighter, ShipClass::Miner, ShipClass::Courier };
		return value;
	}

	Charters::Charters(cs::Ref<TradeDesk> desk, int seed)
		: desk(desk), rng(seed)
	{
	}

	cs::Ref<::SpaceGod::Core::World> Charters::World() const { return desk->World(); }
	cs::Ref<::SpaceGod::Core::Holdings> Charters::Holdings() const { return desk->Holdings(); }

	cs::Ref<Charter> Charters::Of(const cs::Ref<OwnedShip>& o) const { return o != nullptr && o->OnCharter() ? o->Charter : nullptr; }

	cs::List<cs::Ref<Charter>> Charters::Active() const
	{
		cs::List<cs::Ref<Charter>> result;
		for (const auto& c : All)
			if (c->Active) result.Add(c);
		return result;
	}

	double Charters::RateFor(const cs::Ref<Company>& c, ShipClass cls) const
	{
		double perDay = c != nullptr ? c->ClassPerDay[static_cast<int>(cls)] : cs::Double::NaN;
		if (cs::Double::IsNaN(perDay)) perDay = World()->Companies ? World()->Companies->MarketPerDay(cls) : cs::Double::NaN;
		if (cs::Double::IsNaN(perDay) || perDay <= 0) perDay = 2.5 * Economy::RunningCostPerDay(cls);
		double rate = cs::Math::Max(1.2 * Economy::RunningCostPerDay(cls), perDay * TradeRules::CharterHire()->Value);
		return cs::Math::Round(rate * (1 + 0.05 * static_cast<int>(desk->Rank)) / 10) * 10;
	}

	void Charters::Post(double jd)
	{
		Offers.RemoveAll([&](const cs::Ref<CharterOffer>& o) { return jd >= o->ExpiresJd || o->Charterer == nullptr || !o->Charterer->Trading(); });
		nextPostJd = jd + PostEveryDays;
		auto companies = World()->Companies;
		if (companies == nullptr) return;
		cs::List<cs::Ref<Company>> live = companies->Live();
		// A shuffle, so the first companies listed do not always post first.
		for (int i = live.Count() - 1; i > 0; i--)
		{
			int k = rng.Next(i + 1);
			auto t = live[i];
			live[i] = live[k];
			live[k] = t;
		}
		for (const auto& c : live)
		{
			if (Offers.Count() >= MaxOffers) break;
			if (c->HomePort == nullptr || c->HomePort->Fallen) continue;
			bool posted = false;
			for (const auto& o : Offers)
				if (o->Charterer == c) posted = true;
			if (posted || rng.NextDouble() > 0.5) continue;
			auto cls = Classes()[rng.Next(Classes().Count())];
			double days = cs::Math::Round(MinDays + rng.NextDouble() * (MaxDays - MinDays));
			double rate = RateFor(c, cls);
			// It posts only what it could pay for a month at least.
			if (c->Cash < rate * 30) continue;
			auto offer = cs::New<CharterOffer>();
			offer->Id = nextId++;
			offer->Charterer = c;
			offer->Class = cls;
			offer->RatePerDay = rate;
			offer->Days = days;
			offer->PostedJd = jd;
			offer->ExpiresJd = jd + OfferDays;
			offer->Port = c->HomePort;
			Offers.Add(offer);
		}
	}

	const char* ToString(Charters::TakeOutcome v)
	{
		switch (v)
		{
		case Charters::TakeOutcome::Ok: return "Ok";
		case Charters::TakeOutcome::Gone: return "Gone";
		case Charters::TakeOutcome::NotYours: return "NotYours";
		case Charters::TakeOutcome::WrongClass: return "WrongClass";
		case Charters::TakeOutcome::NoCaptain: return "NoCaptain";
		case Charters::TakeOutcome::Chartered: return "Chartered";
		}
		return "";
	}

	Charters::TakeOutcome Charters::CanTake(const cs::Ref<CharterOffer>& offer, const cs::Ref<OwnedShip>& ship, double jd) const
	{
		if (offer == nullptr || !Offers.Contains(offer) || jd >= offer->ExpiresJd || !offer->Charterer->Trading()) return TakeOutcome::Gone;
		if (ship == nullptr || ship->Lost || !Holdings()->Owns(ship->Ship) || ship->Ship == World()->PilotedShip) return TakeOutcome::NotYours;
		if (ship->OnCharter()) return TakeOutcome::Chartered;
		if (ship->Ship->Spec->Class != offer->Class) return TakeOutcome::WrongClass;
		if (ship->Captain == nullptr) return TakeOutcome::NoCaptain;
		return TakeOutcome::Ok;
	}

	cs::Ref<OwnedShip> Charters::Candidate(const cs::Ref<CharterOffer>& offer, double jd) const
	{
		for (const auto& o : Holdings()->Ships)
			if (CanTake(offer, o, jd) == TakeOutcome::Ok) return o;
		return nullptr;
	}

	cs::Ref<Charter> Charters::Take(const cs::Ref<CharterOffer>& offer, const cs::Ref<OwnedShip>& ship, double jd)
	{
		if (CanTake(offer, ship, jd) != TakeOutcome::Ok) return nullptr;
		auto previous = ship->Orders;
		if (!Holdings()->SetOrders(ship, StandingOrder::FreeTrade())) return nullptr;
		auto c = cs::New<Charter>();
		c->Ship = ship;
		c->Charterer = offer->Charterer;
		c->RatePerDay = offer->RatePerDay;
		c->StartJd = jd;
		c->PaidThroughJd = jd;
		c->EndJd = jd + offer->Days;
		c->PreviousOrders = previous;
		ship->Charter = c;
		All.Add(c);
		Offers.Remove(offer);
		Holdings()->Transact(0, cs::Cat("The ", ship->Name(), " goes out on charter to ", c->Charterer->Name, " for ", cs::Fmt(offer->Days, "0"), " d at ", TradeDesk::Cr(c->RatePerDay), " a day"), jd);
		World()->Log(jd, cs::Cat(c->Charterer->Name, " charters your ", cs::Strings::ToLowerInvariant(ship->Ship->Spec->DisplayName), " ", ship->Name()), WorldEventKind::Economy,
			ship->Ship->Orbit ? ship->Ship->Orbit->Body : -1);
		return c;
	}

	bool Charters::Recall(const cs::Ref<Charter>& c, double jd)
	{
		if (c == nullptr || !c->Active) return false;
		double penalty = c->RatePerDay * RecallPenaltyDays;
		if (!Holdings()->Transact(-penalty, cs::Cat("Recalled the ", c->Ship->Name(), " from charter to ", c->Charterer->Name), jd)) return false;
		c->Charterer->Cash += penalty;
		End(c, jd, "recalled", false);
		return true;
	}

	void Charters::Daily(double jd)
	{
		for (const auto& c : Active())
		{
			if (c->Ship->Lost || !Holdings()->Owns(c->Ship->Ship))
			{
				End(c, jd, "the hull was lost", false);
				continue;
			}
			if (c->Ship->Captain == nullptr)
			{
				End(c, jd, "its captain walked off", false);
				continue;
			}
			if (!c->Charterer->Trading())
			{
				End(c, jd, cs::Cat(c->Charterer->Name, " has gone under"), false);
				continue;
			}
			// Hire is for the days served, so the last tick pays only the part of a day left in the term.
			double owed = c->RatePerDay * cs::Math::Max(0.0, cs::Math::Min(jd, c->EndJd) - c->PaidThroughJd);
			if (c->Charterer->Cash < owed)
			{
				End(c, jd, cs::Cat(c->Charterer->Name, " defaulted on the hire"), false);
				continue;
			}
			c->PaidThroughJd = cs::Math::Min(jd, c->EndJd);
			c->Charterer->Cash -= owed;
			c->HirePaid += owed;
			desk->HireEarned += owed;
			if (owed > 0) Holdings()->Transact(owed, cs::Cat("Charter hire for the ", c->Ship->Name(), " from ", c->Charterer->Name), jd);
			if (jd >= c->EndJd) End(c, jd, "term served", true);
		}
		if (cs::Double::IsNaN(nextPostJd) || jd >= nextPostJd) Post(jd);
	}

	void Charters::End(const cs::Ref<Charter>& c, double jd, const cs::String& how, bool served)
	{
		c->Active = false;
		c->EndedHow = how;
		c->EndedJd = jd;
		c->Served = served;
		auto ship = c->Ship;
		if (served) desk->ChartersServed++;
		if (!ship->Lost)
		{
			ship->Charter = nullptr;
			if (!Holdings()->SetOrders(ship, c->PreviousOrders ? c->PreviousOrders : StandingOrder::Hold())) Holdings()->SetOrders(ship, StandingOrder::Hold());
		}
		cs::String text = cs::Cat("The ", ship->Name(), "'s charter to ", c->Charterer->Name, " ends: ", how, " (", TradeDesk::Cr(c->HirePaid), " hire paid)");
		desk->Notice(text);
		World()->Log(jd, text, WorldEventKind::Economy, ship->Ship->Orbit ? ship->Ship->Orbit->Body : -1);
		int ended = 0;
		for (const auto& x : All)
			if (!x->Active) ended++;
		for (int i = 0; i < All.Count() && ended > HistoryCap; i++)
			if (!All[i]->Active)
			{
				All.RemoveAt(i--);
				ended--;
			}
	}
}
