// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/BlackMarket.cs (US-T19).

#include "Economy/BlackMarket.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	bool BlackMarket::Takes(const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && (s->Kind == SettlementKind::Haven || s->Lawless); }

	double BlackMarket::Held(const cs::Ref<Settlement>& s, Good g)
	{
		return s->Kind == SettlementKind::Haven ? cs::Math::Max(0.0, s->Stock[static_cast<int>(g)] - s->Target(g)) : s->BlackStock[static_cast<int>(g)];
	}

	double BlackMarket::Appetite(const cs::Ref<Settlement>& s, Good g) { return cs::Math::Max(s->Flow(g) * AppetiteDays, MinAppetiteTonnes); }

	double BlackMarket::PriceAt(const cs::Ref<Settlement>& s, Good g, double held)
	{
		double share = s->Kind == SettlementKind::Haven ? HavenShare : StationShare;
		double scarcity = 2 / (1 + cs::Math::Max(0.0, held) / Appetite(s, g));
		return GoodInfo::Of(g)->BasePrice * share * cs::Math::Max(0.25, scarcity);
	}

	double BlackMarket::Price(const cs::Ref<Settlement>& s, Good g) { return PriceAt(s, g, Held(s, g)); }

	double BlackMarket::SaleValue(const cs::Ref<Settlement>& s, Good g, double tonnes)
	{
		const int steps = 6;
		double value = 0, held = Held(s, g);
		for (int k = 0; k < steps; k++)
		{
			double chunk = tonnes / steps;
			value += PriceAt(s, g, held + chunk * 0.5) * chunk;
			held += chunk;
		}
		return value;
	}

	double BlackMarket::Sell(const cs::Ref<Settlement>& s, Good g, double tonnes)
	{
		if (!Takes(s) || tonnes <= 0) return 0;
		double value = SaleValue(s, g, tonnes);
		if (s->Kind == SettlementKind::Haven) s->Stock[static_cast<int>(g)] += tonnes;
		else s->BlackStock[static_cast<int>(g)] += tonnes;
		if (s->Owner != nullptr) s->Owner->Treasury -= value;
		s->TradeVolumeCredits += value;
		return value;
	}

	void BlackMarket::Launder(const cs::Ref<Settlement>& s, double dt)
	{
		double k = 1 - cs::Math::Exp(-LaunderPerDay * dt);
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			double moved = s->BlackStock[i] * k;
			if (moved <= 0) continue;
			s->BlackStock[i] -= moved;
			s->Stock[i] += moved;
		}
	}

	double BlackMarket::LootValue(const cs::Ref<Settlement>& haven)
	{
		double v = 0;
		for (int i = 0; i < GoodInfo::Count; i++) v += Held(haven, static_cast<Good>(i)) * GoodInfo::Of(static_cast<Good>(i))->BasePrice;
		return v;
	}
}
