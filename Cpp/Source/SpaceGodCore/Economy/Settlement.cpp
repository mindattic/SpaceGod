// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Settlement.cs (US-T19).

#include "Economy/Settlement.h"
#include "Economy/Industry.h"
#include "Economy/People.h"

namespace SpaceGod::Core
{
	const char* ToString(SettlementKind v)
	{
		switch (v)
		{
			case SettlementKind::Colony: return "Colony";
			case SettlementKind::Station: return "Station";
			case SettlementKind::MiningOutpost: return "MiningOutpost";
			case SettlementKind::Haven: return "Haven";
		}
		return "";
	}

	double Settlement::RationOf(Good g) const { return GoodInfo::Of(g)->Category == GoodCategory::Essential ? Ration : 1; }

	double Settlement::CostOfLiving() const
	{
		double now = 0, normal = 0;
		for (const auto& [g, perPerson] : PersonNeeds())
		{
			now += perPerson * Price[static_cast<int>(g)];
			normal += perPerson * GoodInfo::Of(g)->BasePrice;
		}
		return normal > 0 ? now / normal : 1;
	}

	cs::Ref<Facility> Settlement::Shipyard() const
	{
		for (const auto& f : Facilities)
			if (f->Recipe->Kind == IndustryKind::Shipyard) return f;
		return nullptr;
	}

	bool Settlement::HasShipyard() const { return Shipyard() != nullptr; }

	bool Settlement::Discovered() const { return !Hidden || KnownTo.Count() > 0; }

	bool Settlement::KnownBy(const cs::Ref<Faction>& f) const { return !Hidden || (f != nullptr && KnownTo.Contains(f)); }

	const cs::List<std::tuple<Good, double>>& Settlement::PersonNeeds() const { return People::NeedsOf(Kind); }

	double Settlement::Workforce() const { return Population * People::LabourForceShare; }

	double Settlement::Jobs() const
	{
		double jobs = 0;
		for (const auto& f : Facilities) jobs += f->Workers();
		return jobs;
	}

	double Settlement::Flow(Good g) const { return cs::Math::Max(Demand[static_cast<int>(g)], Supply[static_cast<int>(g)]); }

	double Settlement::Target(Good g) const { return Flow(g) * CoverDays; }

	double Settlement::Capacity(Good g) const { return cs::Math::Max(Flow(g) * StorageDays, 1.0); }

	double Settlement::PriceAt(Good g, double stock) const
	{
		int i = static_cast<int>(g);
		double basePrice = GoodInfo::Of(g)->BasePrice;
		double target = Target(g);
		if (target <= 0) return basePrice * 0.25;
		double ratio = target / (cs::Math::Max(stock, 0.0) + 0.12 * target);
		double p = basePrice * cs::Math::Pow(ratio, 0.75);
		// A market that only produces a good sells it at a discount: nobody local wants it.
		if (Demand[i] <= 0) p *= 0.6;
		return cs::Math::Max(basePrice * 0.2, cs::Math::Min(basePrice * 6, p));
	}

	void Settlement::RefreshPrices()
	{
		for (int i = 0; i < GoodInfo::Count; i++) Price[i] = PriceAt(static_cast<Good>(i), Stock[i]);
	}

	double Settlement::Available(Good g) const
	{
		int i = static_cast<int>(g);
		double reserve = Demand[i] > 0 ? Target(g) * 0.35 : 0;
		return cs::Math::Max(0.0, Stock[i] - reserve);
	}

	double Settlement::Absorb(Good g) const
	{
		int i = static_cast<int>(g);
		if (Demand[i] <= 0) return 0;
		return cs::Math::Max(0.0, Target(g) * 1.6 - Stock[i] - Inbound[i]);
	}

	double Settlement::SaleValue(Good g, double tonnes) const
	{
		const int steps = 6;
		double value = 0, stock = Stock[static_cast<int>(g)];
		for (int k = 0; k < steps; k++)
		{
			double chunk = tonnes / steps;
			value += PriceAt(g, stock + chunk * 0.5) * chunk;
			stock += chunk;
		}
		return value * 0.97;
	}

	double Settlement::PurchaseCost(Good g, double tonnes) const
	{
		const int steps = 6;
		double value = 0, stock = Stock[static_cast<int>(g)];
		for (int k = 0; k < steps; k++)
		{
			double chunk = tonnes / steps;
			value += PriceAt(g, stock - chunk * 0.5) * chunk;
			stock -= chunk;
		}
		return value * 1.03;
	}

	cs::Ref<Settlement> Settlement::Snapshot() const
	{
		auto s = cs::New<Settlement>();
		s->Id = Id; s->Name = Name; s->Body = Body; s->Kind = Kind; s->Owner = Owner; s->Population = Population; s->InitialPopulation = InitialPopulation;
		s->Hidden = Hidden; s->Fallen = Fallen; s->RebuildJd = RebuildJd; s->DaysSinceCapture = DaysSinceCapture;
		// Array.Copy: element-wise into the new settlement's own arrays.
		for (int i = 0; i < Stock.Count(); i++) s->Stock[i] = Stock[i];
		for (int i = 0; i < Price.Count(); i++) s->Price[i] = Price[i];
		for (int i = 0; i < Inbound.Count(); i++) s->Inbound[i] = Inbound[i];
		for (int i = 0; i < Demand.Count(); i++) s->Demand[i] = Demand[i];
		for (int i = 0; i < Supply.Count(); i++) s->Supply[i] = Supply[i];
		return s;
	}

	// ---------------------------------------------------------------- price history (US-T263)

	namespace
	{
		int HistorySlot(int day) { return ((day % Settlement::HistoryDays) + Settlement::HistoryDays) % Settlement::HistoryDays; }
	}

	void Settlement::SamplePrices(double jd)
	{
		int day = static_cast<int>(cs::Math::Floor(jd));
		if (PriceLog.Count() != HistoryDays * GoodInfo::Count)
		{
			PriceLog = cs::NewArray<float>(HistoryDays * GoodInfo::Count);
			PriceLogDays = 0;
		}
		if (PriceLogDays > 0 && day <= PriceLogDay) return;
		// Days a coarse tick jumped over take today's prices (the best the board knows of them).
		int first = PriceLogDays > 0 ? cs::Math::Max(PriceLogDay + 1, day - HistoryDays + 1) : day;
		for (int d = first; d <= day; d++)
		{
			int at = HistorySlot(d) * GoodInfo::Count;
			for (int g = 0; g < GoodInfo::Count; g++) PriceLog[at + g] = static_cast<float>(Price[g]);
		}
		PriceLogDays = cs::Math::Min(HistoryDays, PriceLogDays + (day - first + 1));
		PriceLogDay = day;
	}

	cs::List<double> Settlement::PriceTrail(Good g, int days) const
	{
		cs::List<double> trail;
		if (PriceLog.Count() != HistoryDays * GoodInfo::Count) return trail;
		int n = cs::Math::Min(cs::Math::Min(days, HistoryDays), PriceLogDays);
		for (int k = 0; k < n; k++)
			trail.Add(PriceLog[HistorySlot(PriceLogDay - (n - 1) + k) * GoodInfo::Count + static_cast<int>(g)]);
		return trail;
	}

	PriceRange Settlement::PriceRangeOver(Good g, int days) const
	{
		PriceRange r;
		double sum = 0;
		for (double p : PriceTrail(g, days))
		{
			if (r.Samples == 0) r.Low = r.High = p;
			r.Low = cs::Math::Min(r.Low, p);
			r.High = cs::Math::Max(r.High, p);
			sum += p;
			r.Samples++;
		}
		if (r.Samples > 0) r.Mean = sum / r.Samples;
		return r;
	}
}
