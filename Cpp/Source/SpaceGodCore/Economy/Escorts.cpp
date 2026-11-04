// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Escorts.cs (US-T19).

#include "Economy/Escorts.h"
#include "Economy/Goods.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Holdings.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	bool MerchantEscorts::IsHired(const cs::Ref<Ship>& s) const { return hired.Contains(s); }

	void MerchantEscorts::MarkHired(const cs::Ref<Ship>& s) { hired.Add(s); }

	void MerchantEscorts::UnmarkHired(const cs::Ref<Ship>& s) { hired.Remove(s); }

	MerchantEscorts::MerchantEscorts(cs::Ref<World> world, int seed) : rng(seed)
	{
		this->world = world;
	}

	int MerchantEscorts::Wanted(double cargoValue, double danger)
	{
		double worth = cs::Math::Max(0.0, cs::Math::Min(2.0, cs::Math::Log10(cs::Math::Max(1.0, cargoValue)) - 5));
		double score = worth + 4 * cs::Math::Max(0.0, danger);
		return score < 0.6 ? 0 : score < 1.4 ? 1 : score < 2.2 ? 2 : 3;
	}

	cs::List<ShipClass> MerchantEscorts::Wing(int count, cs::Random& rng)
	{
		auto Light = [&]() { return rng.NextDouble() < 0.6 ? ShipClass::Fighter : ShipClass::Courier; };
		// Each draw hoisted in C#'s left-to-right order (see docs/MIGRATION.md).
		switch (count)
		{
			case 1: { auto a = Light(); return cs::List<ShipClass>{ a }; }
			case 2: { auto a = Light(); auto b = Light(); return cs::List<ShipClass>{ a, b }; }
			case 3: { auto a = Light(); auto b = Light(); return cs::List<ShipClass>{ ShipClass::Gunship, a, b }; }
			default: return cs::List<ShipClass>();
		}
	}

	bool MerchantEscorts::IsFighter(ShipClass c) { return c == ShipClass::Fighter || c == ShipClass::Interceptor || c == ShipClass::Courier || c == ShipClass::Gunship; }

	double MerchantEscorts::HireFee(ShipClass c) { return c == ShipClass::Gunship ? HeavyHireFee : LightHireFee; }

	void MerchantEscorts::Update(double jd)
	{
		if (jd < nextJd) return;
		double days = nextJd > 0 ? jd - nextJd + CheckDays : CheckDays;
		nextJd = jd + CheckDays;
		PayOffOrphans();

		auto wings = cs::Dictionary<cs::Ref<Ship>, int>();
		int total = 0;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || !s->IsEscort() || s->Leader == nullptr || !world->IsTrader(s->Leader)) continue;
			int n = 0;
			wings.TryGetValue(s->Leader, n);
			wings[s->Leader] = n + 1;
			// The cap is on the fighter wings hired here, not on the older escorts some merchants sail with.
			if (IsFighter(s->Spec->Class)) total++;
		}
		// Wages first: a wing flies for as long as it is paid.
		for (auto [key, value] : wings) Pay(key, value * DailyWage * cs::Math::Min(days, 2.0));

		// The richest cargoes into the worst waters first: the fleet's escorts go where they are needed most.
		struct Candidate { cs::Ref<Ship> ship; int want = 0; int have = 0; double stake = 0; };
		auto candidates = cs::List<Candidate>();
		for (const auto& s : world->Fleet->Ships())
		{
			if (!Wants(s)) continue;
			int have = 0;
			wings.TryGetValue(s, have);
			double value = s->HasCargo ? s->CargoTonnes * GoodInfo::Of(s->CargoGood)->BasePrice : 0;
			double danger = cs::Math::Max(DangerAt(s->Orbit->Body), DangerAt(s->DestinationBody));
			int want = Wanted(value, danger);
			if (want > have) candidates.Add(Candidate{ s, want, have, value * (1 + 4 * danger) });
		}
		candidates.Sort([](const Candidate& x, const Candidate& y) { return cs::Compare(y.stake, x.stake); });

		struct Hire { cs::Ref<Ship> leader; cs::List<ShipClass> wing; };
		auto hire = cs::List<Hire>();
		for (const auto& c0 : candidates)
		{
			const auto& s = c0.ship; int want = c0.want, have = c0.have;
			if (total + want - have > MaxEscorts) continue;
			auto wing = Wing(want, rng);
			// Only what the freighter has not got yet: the first places of the wing are the ones it already flies with.
			auto missing = cs::NewArray<ShipClass>(want - have);
			for (int i = 0; i < missing.Count(); i++) missing[i] = wing[have + i];
			double cost = 0;
			for (auto c : missing) cost += HireFee(c);
			if (Purse(s) < cost * AffordFactor) continue;
			hire.Add(Hire{ s, missing });
			total += missing.Count();
		}
		for (const auto& h : hire)
		{
			const auto& leader = h.leader;
			int have = 0;
			wings.TryGetValue(leader, have);
			for (auto c : h.wing)
			{
				auto e = world->Fleet->AddShip(cs::Cat(leader->Name, " escort ", ++named), c, leader->Faction, ShipRole::Escort);
				world->Fleet->AddEscort(leader, e, Fleet::SlotFor(have++));
				Pay(leader, HireFee(c));
				world->NotifyAdded(e);
				hired.Add(e);
			}
		}
	}

	void MerchantEscorts::PayOffOrphans()
	{
		auto gone = cs::List<cs::Ref<Ship>>();
		for (const auto& s : hired)
		{
			if (s->Destroyed) { gone.Add(s); continue; }
			auto lead = s->Leader;
			bool guarding = lead != nullptr && !lead->Destroyed && world->IsTrader(lead);
			// Still with a merchant, or following the one of its wing that is (the wing flies on as a group).
			if (guarding || (lead != nullptr && hired.Contains(lead) && !lead->Destroyed && lead->Leader != nullptr && world->IsTrader(lead->Leader))) continue;
			auto head = lead != nullptr && hired.Contains(lead) ? lead : s;
			if (head->Phase != ShipPhase::Parked || head->Battle != nullptr || s->Battle != nullptr || head == world->PilotedShip || s == world->PilotedShip) continue;
			gone.Add(s);
			s->Destroyed = true;
			s->GateDeparted = true;
			// Gone where its wing lay, not parked with no orbit of its own (an escort has none, US-T59).
			if (s->Leader != nullptr) s->TakeMotionOf(s->Leader);
			s->Leader = nullptr;
		}
		for (const auto& s : gone) hired.Remove(s);
	}

	bool MerchantEscorts::Wants(const cs::Ref<Ship>& s) const
	{
		return !s->Destroyed && !s->IsEscort() && world->IsTrader(s) && s->Phase == ShipPhase::Parked && s->Battle == nullptr && s != world->PilotedShip &&
			!(world->Holdings != nullptr && world->Holdings->Owns(s)) &&
			s->Faction != nullptr && !s->Faction->IsPirate && !s->Derelict && s->Orbit != nullptr &&
			(s->Spec->Class == ShipClass::Freighter || s->Spec->Class == ShipClass::Miner);
	}

	double MerchantEscorts::DangerAt(int body) const { return world->Contracts != nullptr ? world->Contracts->DangerAt(body) : 0; }

	double MerchantEscorts::Purse(const cs::Ref<Ship>& s) { return s->Company != nullptr ? s->Company->Cash : s->Faction->Treasury; }

	void MerchantEscorts::Pay(const cs::Ref<Ship>& s, double credits)
	{
		if (s->Company != nullptr) s->Company->Cash -= credits;
		else if (s->Faction != nullptr) s->Faction->Treasury -= credits;
	}
}
