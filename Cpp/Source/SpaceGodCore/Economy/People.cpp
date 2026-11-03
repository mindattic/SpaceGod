// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/People.cs (US-T19).

#include "Economy/People.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Combat/Diplomacy.h"

namespace SpaceGod::Core
{
	double People::GrossWaterKg(SettlementKind kind) { return kind == SettlementKind::Colony ? 50 : 5.03; }

	double People::WaterRecovery(SettlementKind kind)
	{
		switch (kind)
		{
			case SettlementKind::Station: return 0.98;
			case SettlementKind::Haven: return 0.85;
			default: return 0.935;
		}
	}

	double People::WaterLossKg(SettlementKind kind)
	{
		double oxygen = OxygenKg + LeakAirKg * AirOxygenShare;
		return GrossWaterKg(kind) * (1 - WaterRecovery(kind)) + cs::Math::Max(0.0, oxygen * 9.0 / 8.0 - SabatierWaterKg);
	}

	const cs::List<cs::List<std::tuple<Good, double>>>& People::needs()
	{
		static const cs::List<cs::List<std::tuple<Good, double>>> n = BuildNeeds();
		return n;
	}

	cs::List<cs::List<std::tuple<Good, double>>> People::BuildNeeds()
	{
		using T = std::tuple<Good, double>;
		// Enum.GetValues(typeof(SettlementKind)): Colony, Station, MiningOutpost, Haven.
		const int kinds = 4;
		auto all = cs::NewArray<cs::List<T>>(kinds);
		for (int k = 0; k < kinds; k++)
		{
			auto kind = static_cast<SettlementKind>(k);
			all[static_cast<int>(kind)] = cs::List<T>
			{
				T(Good::Food, FoodKg / 1000),
				T(Good::Water, WaterLossKg(kind) / 1000),
				T(Good::Nitrogen, NitrogenKg / 1000),
				T(Good::MedicalSupplies, MedicalKg / 1000),
				// The habitat's own upkeep (fusion fuel, spares, controls), shared per head: assumed.
				T(Good::HeliumThree, 0.0000025),
				T(Good::Machinery, 0.000008),
				T(Good::Electronics, 0.0000015),
			};
		}
		return all;
	}

	const cs::List<std::tuple<Good, double>>& People::NeedsOf(SettlementKind kind) { return needs()[static_cast<int>(kind)]; }

	double People::NeedOf(SettlementKind kind, Good g)
	{
		for (const auto& [good, perDay] : NeedsOf(kind))
			if (good == g) return perDay;
		return 0;
	}

	double People::GrowthRate(double sustenance, double health)
	{
		return sustenance >= 0.98
			? GrowthPerDay * (0.5 + 0.5 * health) - (1 - health) * SicknessPerDay
			: (sustenance - 0.98) * FaminePerDay;
	}

	double People::Labour(const cs::Ref<Settlement>& s) { return s->Workforce() * IndustrialShare; }

	void People::AssignLabour(const cs::Ref<Settlement>& s)
	{
		double labour = Labour(s), lifeJobs = 0, otherJobs = 0;
		for (const auto& f : s->Facilities)
			if (f->LifeSupport()) lifeJobs += f->Workers();
			else otherJobs += f->Workers();
		double life = lifeJobs > 0 ? cs::Math::Min(1.0, labour / lifeJobs) : 1;
		double left = cs::Math::Max(0.0, labour - lifeJobs);
		double other = otherJobs > 0 ? cs::Math::Min(1.0, left / otherJobs) : 1;
		for (const auto& f : s->Facilities) f->Staffing = f->LifeSupport() ? life : other;
		double jobs = lifeJobs + otherJobs;
		s->Staffing = jobs > 0 ? cs::Math::Min(1.0, labour / jobs) : 1;
	}

	double People::CrewFactor(double staffing) { return staffing < MinimumCrew ? 0 : cs::Math::Min(1.0, staffing); }

	double People::ValueAdded(const cs::Ref<Settlement>& s, const cs::Ref<Facility>& f)
	{
		double v = 0;
		for (const auto& [g, rate] : f->Recipe->Outputs) v += rate * s->Price[static_cast<int>(g)];
		for (const auto& [g, rate] : f->Recipe->Inputs) v -= rate * s->Price[static_cast<int>(g)];
		// A yard's product is hulls, not a good: count its materials as worth half again once built.
		if (f->Recipe->Kind == IndustryKind::Shipyard) v = -v * 0.5;
		return cs::Math::Max(0.0, v) * f->Level;
	}

	double People::Wage(const cs::Ref<Settlement>& s)
	{
		double employed = 0;
		for (const auto& f : s->Facilities) employed += f->Workers() * cs::Math::Min(1.0, f->Staffing);
		return employed > 0 ? s->WageBill / employed : 0;
	}

	double People::ExpectedEarnings(const cs::Ref<Settlement>& s, double serviceWage)
	{
		double labour = Labour(s);
		double employed = 0;
		for (const auto& f : s->Facilities) employed += f->Workers() * cs::Math::Min(1.0, f->Staffing);
		double chance = labour > 0 ? cs::Math::Min(1.0, employed / labour) : 0;
		return chance * Wage(s) + (1 - chance) * serviceWage;
	}

	double People::Crowding(const cs::Ref<Settlement>& s)
	{
		double cap = s->InitialPopulation * MaxGrowth;
		if (cap <= 0) return 0;
		return cs::Math::Max(0.0, cs::Math::Min(1.0, (s->Population / cap - CrowdedShare) / (1 - CrowdedShare)));
	}

	void People::AddOverflow(const cs::Ref<Settlement>& s, double people)
	{
		if (people <= 0 || s->Kind == SettlementKind::Haven) return;
		s->Overflow = cs::Math::Min(s->Overflow + people, s->Population * OverflowShare);
	}

	double People::Attraction(const cs::Ref<Settlement>& s, double meanEarnings)
	{
		double living = s->Wellbeing * (0.6 + 0.4 * cs::Math::Min(1.0, s->HealthWellbeing));
		double pay = meanEarnings > 0 && s->Earnings > 0 ? cs::Math::Pow(s->Earnings / meanEarnings, 0.25) : 1;
		return living * cs::Math::Max(0.8, cs::Math::Min(1.25, pay));
	}

	bool People::CanMigrate(const cs::Ref<Diplomacy>& diplomacy, const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b)
	{
		if (a == b || a->Fallen || b->Fallen) return false;
		bool ah = a->Kind == SettlementKind::Haven, bh = b->Kind == SettlementKind::Haven;
		if (ah || bh) return ah && bh && a->Owner == b->Owner;
		return diplomacy == nullptr || !diplomacy->Hostile(a->Owner, b->Owner);
	}

	void People::Migrate(const cs::List<cs::Ref<Settlement>>& settlements, const cs::Ref<Diplomacy>& diplomacy, double days)
	{
		int n = settlements.Count();
		if (n == 0) return;

		// The service wage: half the system's average facility wage, weighted by who earns it.
		double wageSum = 0, workers = 0;
		for (const auto& s : settlements)
		{
			if (s->Fallen) continue;
			double employed = 0;
			for (const auto& f : s->Facilities) employed += f->Workers() * cs::Math::Min(1.0, f->Staffing);
			wageSum += s->WageBill;
			workers += employed;
		}
		double serviceWage = workers > 0 ? wageSum / workers * ServiceWageShare : 0;

		double earnSum = 0, people = 0;
		for (const auto& s : settlements)
		{
			s->NetMigration = 0;
			if (s->Fallen || s->Kind == SettlementKind::Haven) { s->Earnings = 0; continue; }
			s->Earnings = ExpectedEarnings(s, serviceWage);
			earnSum += s->Earnings * s->Population;
			people += s->Population;
		}
		double meanEarnings = people > 0 ? earnSum / people : 0;
		for (const auto& s : settlements) s->Attraction = s->Fallen ? 0 : Attraction(s, meanEarnings);
		if (days <= 0) return;

		// new double[n, n]: row-major, moves[i, j] at i * n + j.
		std::vector<double> moves(static_cast<size_t>(n) * static_cast<size_t>(n), 0.0);
		auto M = [&](int i, int j) -> double& { return moves[static_cast<size_t>(i) * static_cast<size_t>(n) + static_cast<size_t>(j)]; };
		auto leaving = cs::NewArray<double>(n);
		for (int i = 0; i < n; i++)
		{
			auto from = settlements[i];
			if (from->Fallen || from->Population <= 0) continue;
			double reach = 0;
			for (int j = 0; j < n; j++)
				if (CanMigrate(diplomacy, from, settlements[j])) reach += settlements[j]->Population;
			if (reach <= 0) continue;
			double rate = 1 - cs::Math::Exp(-MigrationPerDay * days);
			for (int j = 0; j < n; j++)
			{
				auto to = settlements[j];
				if (!CanMigrate(diplomacy, from, to)) continue;
				double gap = to->Attraction - from->Attraction - MigrationThreshold;
				if (gap <= 0) continue;
				double m = from->Population * rate * cs::Math::Min(1.0, gap) * (to->Population / reach);
				M(i, j) = m;
				leaving[i] += m;
			}
			// Those who already left on contract passage were part of this flow: that many fewer go on their own.
			if (from->Emigrated > 0 && leaving[i] > 0)
			{
				double counted = cs::Math::Min(from->Emigrated, leaving[i]);
				from->Emigrated -= counted;
				double k = (leaving[i] - counted) / leaving[i];
				for (int j = 0; j < n; j++) M(i, j) *= k;
				leaving[i] -= counted;
			}
			// Nobody leaves below the floor.
			double spare = cs::Math::Max(0.0, from->Population - from->InitialPopulation * MinShare);
			if (leaving[i] > spare)
			{
				double k = spare / leaving[i];
				for (int j = 0; j < n; j++) M(i, j) *= k;
			}
		}
		for (int j = 0; j < n; j++)
		{
			auto to = settlements[j];
			double arriving = 0;
			for (int i = 0; i < n; i++) arriving += M(i, j);
			if (arriving <= 0) continue;
			// ... and nobody arrives past the cap: the rest stay home.
			double room = cs::Math::Max(0.0, to->InitialPopulation * MaxGrowth - to->Population);
			if (arriving > room)
			{
				double k = room / arriving;
				for (int i = 0; i < n; i++)
				{
					// Those turned away still want to leave: people with nowhere to go (U.SPC-T98).
					AddOverflow(settlements[i], M(i, j) * (1 - k));
					M(i, j) *= k;
				}
			}
		}
		for (int i = 0; i < n; i++)
			for (int j = 0; j < n; j++)
			{
				double m = M(i, j);
				if (m <= 0) continue;
				settlements[i]->Population -= m;
				settlements[j]->Population += m;
				settlements[i]->NetMigration -= m / days;
				settlements[j]->NetMigration += m / days;
			}
	}
}
