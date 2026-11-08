// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/MarketIntel.cs (US-T19).

#include "Economy/MarketIntel.h"

#include "Combat/Diplomacy.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	cs::String PriceSheet::ToString() const
	{
		return cs::Cat(Market ? Market->Name : cs::String(), " (", Source, ")");
	}

	void PriceSheet::Remember()
	{
		// One sample a day: a later report the same day replaces the earlier.
		int n = SeenJd.Count();
		if (n > 0 && cs::Math::Floor(SeenJd[n - 1]) == cs::Math::Floor(Jd))
		{
			SeenJd.RemoveAt(n - 1);
			SeenPrice.RemoveRange((n - 1) * GoodInfo::Count, GoodInfo::Count);
		}
		SeenJd.Add(Jd);
		for (int g = 0; g < GoodInfo::Count; g++) SeenPrice.Add(static_cast<float>(Price[g]));
		// Kept as long as a market keeps its own board.
		int old = 0;
		while (old < SeenJd.Count() && SeenJd[old] < Jd - Settlement::HistoryDays) old++;
		if (old > 0)
		{
			SeenJd.RemoveRange(0, old);
			SeenPrice.RemoveRange(0, old * GoodInfo::Count);
		}
	}

	PriceRange PriceSheet::RangeOver(Good g, double jd, double days) const
	{
		PriceRange r;
		double sum = 0;
		if (SeenPrice.Count() != SeenJd.Count() * GoodInfo::Count) return r;
		for (int k = 0; k < SeenJd.Count(); k++)
		{
			if (SeenJd[k] < jd - days || SeenJd[k] > jd) continue;
			double p = SeenPrice[k * GoodInfo::Count + static_cast<int>(g)];
			if (r.Samples == 0) r.Low = r.High = p;
			r.Low = cs::Math::Min(r.Low, p);
			r.High = cs::Math::Max(r.High, p);
			sum += p;
			r.Samples++;
		}
		if (r.Samples > 0) r.Mean = sum / r.Samples;
		return r;
	}

	cs::String TradeLead::ToString() const
	{
		return cs::Cat(GoodInfo::Of(Good)->Name, ": ", From->Name, " -> ", To->Name, ", ", cs::Fmt(Profit, "N0"), " cr");
	}

	cs::Ref<::SpaceGod::Core::World> MarketIntel::World() const { return desk->World(); }

	cs::Ref<PriceSheet> MarketIntel::Of(const cs::Ref<Settlement>& s) const
	{
		cs::Ref<PriceSheet> sheet;
		return s != nullptr && sheets.TryGetValue(s, sheet) ? sheet : nullptr;
	}

	cs::Ref<PriceSheet> MarketIntel::Observe(const cs::Ref<Settlement>& s, double jd, const cs::String& source)
	{
		if (s == nullptr || !s->Discovered()) return nullptr;
		cs::Ref<PriceSheet> sheet;
		if (sheets.TryGetValue(s, sheet))
		{
			if (sheet->Jd > jd) return sheet;
		}
		else
		{
			sheet = cs::New<PriceSheet>();
			sheet->Market = s;
			sheets[s] = sheet;
		}
		sheet->Jd = jd;
		sheet->Source = source;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto good = static_cast<Good>(g);
			sheet->Price[g] = s->Price[g];
			sheet->Available[g] = s->Fallen ? 0 : s->Available(good);
			sheet->Wants[g] = s->Fallen ? 0 : s->Absorb(good);
			sheet->Buys[g] = !s->Fallen && s->Demand[g] > 0;
		}
		sheet->Remember(); // US-T263
		return sheet;
	}

	int MarketIntel::ObserveAt(int body, double jd, const cs::String& source)
	{
		int n = 0;
		for (const auto& s : World()->Economy->At(body))
			if (Observe(s, jd, source) != nullptr) n++;
		return n;
	}

	double MarketIntel::Confidence(const cs::Ref<PriceSheet>& sheet, double jd) const
	{
		return sheet == nullptr ? 0 : cs::Math::Pow(0.5, sheet->AgeDays(jd) / cs::Math::Max(0.1, TradeRules::IntelHalfLife()->Value));
	}

	cs::String MarketIntel::AgeText(double days)
	{
		return days < 0.5 ? cs::String("today") : cs::Cat(cs::Fmt(days, "0"), " d old");
	}

	void MarketIntel::Daily(double jd)
	{
		for (const auto& o : desk->Holdings()->Ships)
		{
			if (o->Lost || o->Captain == nullptr) continue;
			auto ship = o->Ship;
			if (ship->Orbit == nullptr || (ship->Phase != ShipPhase::Parked && ship->Phase != ShipPhase::Landed)) continue;
			ObserveAt(ship->Orbit->Body, jd, cs::Cat("Capt. ", o->Captain->Name));
		}
	}

	// ---------------------------------------------------------------- brokers

	bool MarketIntel::HasBroker(const cs::Ref<Settlement>& port) const
	{
		return port != nullptr && !port->Fallen && port->Owner != nullptr && !port->Owner->IsPirate && port->Kind != SettlementKind::Haven &&
			World()->Diplomacy()->CanDockAt(desk->Flag(), port);
	}

	double MarketIntel::ReportFee() const { return TradeRules::BrokerFee()->Value * (1 - desk->BrokerDiscount()); }

	int MarketIntel::BuyReport(const cs::Ref<Settlement>& port, const cs::Ref<Faction>& payer, double jd)
	{
		if (!HasBroker(port) || payer == nullptr) return -1;
		double fee = ReportFee();
		if (payer == desk->Flag())
		{
			if (!desk->Holdings()->Transact(-fee, cs::Cat("A broker's price sheet at ", port->Name), jd)) return -1;
		}
		else
		{
			if (payer->Treasury < fee) return -1;
			payer->Treasury -= fee;
		}
		port->Owner->Treasury += fee;
		int n = 0;
		for (const auto& s : World()->Economy->Settlements)
			if (Observe(s, jd, cs::Cat("broker at ", port->Name)) != nullptr) n++;
		desk->ReportsBought++;
		return n;
	}

	// ---------------------------------------------------------------- the runs

	cs::List<cs::Ref<TradeLead>> MarketIntel::Leads(double holdTonnes, double jd, int max, cs::Ref<Faction> flag, cs::Ref<CargoHold> crateHold)
	{
		auto leads = cs::List<cs::Ref<TradeLead>>();
		// A player flying the crate hold carries only what its free cells take, good by good.
		// (C# null is an empty list here: room has GoodInfo.Count entries whenever it is set.)
		cs::List<double> room;
		if (crateHold != nullptr)
		{
			room = cs::NewArray<double>(GoodInfo::Count);
			holdTonnes = 0;
			for (int g = 0; g < room.Count(); g++) { room[g] = crateHold->RoomTonnes(static_cast<Good>(g)); holdTonnes = cs::Math::Max(holdTonnes, room[g]); }
		}
		bool hasRoom = crateHold != nullptr;
		if (holdTonnes <= 0) return leads;
		auto known = sheets.Values();
		for (const auto& a : known)
		{
			auto from = a->Market;
			if (from->Fallen || from->Kind == SettlementKind::Haven || (from->Owner != nullptr && from->Owner->IsPirate)) continue;
			auto fromRisk = desk->Assess(from, flag);
			// Nobody sells the house anything at a port closed to it.
			if (fromRisk.Closed()) continue;
			double ca = Confidence(a, jd);
			for (const auto& b : known)
			{
				if (b == a || b->Market->Fallen) continue;
				auto toRisk = desk->Assess(b->Market, flag);
				double cb = Confidence(b, jd);
				for (int g = 0; g < GoodInfo::Count; g++)
				{
					if (!b->Buys[g] || a->Available[g] < 1) continue;
					double buy = a->Price[g], sell = b->Price[g];
					double margin = sell * 0.97 * (1 - Economy::PortTax) - buy * 1.03 * (1 + Economy::PortTax);
					if (margin <= 0) continue;
					double tonnes = cs::Math::Min(hasRoom ? room[g] : holdTonnes, cs::Math::Min(a->Available[g], b->Wants[g]));
					if (tonnes < 1) continue;
					double profit = margin * tonnes;
					double risk = cs::Math::Max(toRisk.Total, fromRisk.Total);
					auto lead = cs::New<TradeLead>();
					lead->From = from;
					lead->To = b->Market;
					lead->Good = static_cast<Good>(g);
					lead->BuyPrice = buy;
					lead->SellPrice = sell;
					lead->Tonnes = tonnes;
					lead->Margin = margin;
					lead->Profit = profit;
					lead->AgeDays = cs::Math::Max(a->AgeDays(jd), b->AgeDays(jd));
					lead->Risk = risk;
					lead->BackDoor = toRisk.Closed();
					lead->Score = profit * ca * cb * (1 - risk);
					// Against what the player has seen these prices do over the last month (US-T263).
					auto buyRange = a->RangeOver(lead->Good, jd, 30), sellRange = b->RangeOver(lead->Good, jd, 30);
					lead->BuyHasHistory = buyRange.Samples > 1;
					lead->SellHasHistory = sellRange.Samples > 1;
					lead->BuyVsAvg = buyRange.VsMean(buy);
					lead->SellVsAvg = sellRange.VsMean(sell);
					leads.Add(lead);
				}
			}
		}
		leads.Sort([](const cs::Ref<TradeLead>& x, const cs::Ref<TradeLead>& y) { return cs::Compare(y->Score, x->Score); });
		if (leads.Count() > max) leads.RemoveRange(max, leads.Count() - max);
		return leads;
	}
}
