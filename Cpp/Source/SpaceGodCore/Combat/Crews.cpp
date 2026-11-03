// Port of Assets/UnitySpace/Core/Combat/Crews.cs (see docs/MIGRATION.md).

#include "Combat/Crews.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Diplomacy.h"
#include "Combat/DriftingRounds.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(PodState v)
	{
		switch (v)
		{
			case PodState::Drifting: return "Drifting";
			case PodState::Rescued: return "Rescued";
			case PodState::Hostage: return "Hostage";
			case PodState::Prisoner: return "Prisoner";
			case PodState::Lost: return "Lost";
		}
		return "";
	}

	const char* ToString(AbandonCause v)
	{
		switch (v)
		{
			case AbandonCause::Destroyed: return "Destroyed";
			case AbandonCause::ReactorLost: return "ReactorLost";
			case AbandonCause::Scuttled: return "Scuttled";
		}
		return "";
	}

	// ---------------------------------------------------------------- EscapePod

	double EscapePod::AirLeftDays(double jd) const { return cs::Math::Max(0.0, LaunchedJd + LifeSupportDays - jd); }

	double EscapePod::DriftKm(double jd) const { return MaxDriftKm * (1 - cs::Math::Exp(-cs::Math::Max(0.0, jd - LaunchedJd) / DriftDays)); }

	DVec3 EscapePod::LocalAt(double jd) const { return Local + DriftDir * DriftKm(jd); }

	DVec3 EscapePod::PosAt(double jd) const { return Anchor + LocalAt(jd); }

	// ---------------------------------------------------------------- pods

	int Crews::PodsFitted(double complement)
	{
		return cs::Math::Max(1, cs::ToInt(cs::Math::Ceiling(cs::Math::Max(0.0, complement) / PodCapacity - 1e-9)));
	}

	int Crews::PodsFor(double people, double complement)
	{
		return cs::Math::Min(PodsFitted(cs::Math::Max(complement, people)), cs::Math::Max(1, cs::ToInt(cs::Math::Ceiling(people / PodCapacity - 1e-9))));
	}

	double Crews::EscapeShare(AbandonCause cause, double roll)
	{
		roll = cs::Math::Clamp(roll, 0.0, 1.0);
		switch (cause)
		{
			case AbandonCause::Scuttled: return 0.97;
			case AbandonCause::ReactorLost: return 0.2 + 0.25 * roll;
			default: return 0.45 + 0.35 * roll;
		}
	}

	cs::Ref<EscapePod> Crews::Launch(const cs::String& name, const cs::Ref<Faction>& owners, double aboard, double complement, AbandonCause cause,
		int body, DVec3 anchor, DVec3 local, const cs::Ref<LaneSite>& site, double jd, const cs::String& mergeKey, const cs::Ref<Settlement>& haven)
	{
		if (owners == nullptr || aboard < 0.5) return nullptr;
		double escaped = cs::Math::Round(aboard * EscapeShare(cause, rng.NextDouble()));
		RecordDeaths(owners, aboard - escaped);
		if (escaped < 1) return nullptr;
		if (!mergeKey.empty()) // C# `mergeKey != null`
		{
			auto into = Pods.Find([&](const cs::Ref<EscapePod>& p) { return p->Drifting() && p->MergeKey == mergeKey; });
			if (into != nullptr)
			{
				into->People += escaped;
				into->Pods += PodsFor(escaped, complement);
				return into;
			}
		}
		double z = 2 * rng.NextDouble() - 1, a = 2 * cs::Math::PI * rng.NextDouble(), r = cs::Math::Sqrt(1 - z * z);
		auto pod = cs::New<EscapePod>();
		pod->Id = nextId++;
		pod->Name = name;
		pod->Owners = owners;
		pod->People = escaped;
		pod->Pods = PodsFor(escaped, complement);
		pod->Body = body;
		pod->Anchor = anchor;
		pod->Local = local;
		pod->Site = site;
		pod->DriftDir = DVec3(r * cs::Math::Cos(a), r * cs::Math::Sin(a), z);
		pod->LaunchedJd = jd;
		pod->MergeKey = mergeKey;
		pod->Haven = haven;
		Pods.Add(pod);
		Launched.Invoke(pod);
		return pod;
	}

	cs::Ref<EscapePod> Crews::Adrift(const cs::String& name, const cs::Ref<Faction>& owners, double marines, const cs::Ref<Faction>& enemy, int body, DVec3 anchor, DVec3 local,
		DVec3 heading, const cs::Ref<LaneSite>& site, double jd)
	{
		if (owners == nullptr || marines < 0.5) return nullptr;
		auto pod = cs::New<EscapePod>();
		pod->Id = nextId++;
		pod->Name = name;
		pod->Owners = owners;
		pod->People = marines;
		pod->Pods = 1;
		pod->Body = body;
		pod->Anchor = anchor;
		pod->Local = local;
		pod->Site = site;
		pod->DriftDir = heading.LengthSquared() > 1e-18 ? heading.Normalized() : DVec3::UnitX;
		pod->LaunchedJd = jd;
		pod->LifeSupportDays = DriftingRounds::AirDays;
		pod->Breacher = true;
		pod->Enemy = enemy;
		Pods.Add(pod);
		return pod;
	}

	PodState Crews::Collect(const cs::Ref<EscapePod>& pod, const cs::Ref<Faction>& rescuer, const cs::Ref<Ship>& ship, const cs::Ref<Diplomacy>& diplomacy, double jd, bool payRescuer)
	{
		if (pod == nullptr || !pod->Drifting() || rescuer == nullptr) return pod ? pod->State : PodState::Lost;
		pod->TakenBy = rescuer;
		pod->ResolvedJd = jd;
		bool hostile = rescuer != pod->Owners && (diplomacy ? diplomacy->Hostile(rescuer, pod->Owners) : (rescuer->IsPirate || pod->Owners->IsPirate));
		if (!hostile)
		{
			pod->State = PodState::Rescued;
			Return(pod->Owners, pod->People);
			if (payRescuer && rescuer != pod->Owners)
			{
				double reward = cs::Math::Max(0.0, cs::Math::Min(pod->People * RescueRewardPerHead, pod->Owners->Treasury));
				pod->Owners->Treasury -= reward;
				rescuer->Treasury += reward;
			}
		}
		else if (rescuer->IsPirate)
		{
			pod->State = PodState::Hostage;
			auto held = Hold(pod->Owners, rescuer, pod->People, false, ship != nullptr ? HavenOf(ship) : nullptr, jd, pod->Name);
			// Marines salvaged from a Breacher round may be sold to the side they were fighting (U.SPC-T519).
			if (held != nullptr && pod->Breacher)
			{
				held->Marines = true;
				held->Buyer = pod->Enemy;
			}
		}
		else
		{
			pod->State = PodState::Prisoner;
			if (pod->Owners->IsPirate)
			{
				// Raiders in irons are lost to the Banner; they may talk.
				Lose(pod->Owners, pod->People);
				if (RaidersTaken) RaidersTaken(pod, rescuer, jd);
			}
			else Hold(pod->Owners, rescuer, pod->People, true, nullptr, jd, pod->Name);
		}
		auto keep = pod; // the caller's reference may be the list's own element
		Pods.Remove(keep);
		Resolved.Invoke(keep, ship);
		return keep->State;
	}

	void Crews::Sweep(const cs::List<cs::Ref<Ship>>& ships, const cs::Ref<Diplomacy>& diplomacy, const cs::Ref<Ship>& piloted, double jd, double days)
	{
		if (Pods.Count() == 0 || days <= 0) return;
		auto snapshot = Pods.ToArray();
		for (const auto& pod : snapshot)
		{
			if (!pod->Drifting()) continue;
			for (const auto& s : ships)
			{
				if (s == nullptr || s == piloted || s->Destroyed || s->Derelict || s->GateDeparted || s->Battle != nullptr || s->Faction == nullptr) continue;
				// A drifting Breacher round is towed in, so only a tow unit can take it (U.SPC-T519).
				if (pod->Breacher && !DriftingRounds::CanTow(s)) continue;
				bool here = pod->Site == nullptr
					? Fleet::IsAt(s, pod->Body)
					: s->Phase == ShipPhase::Lane && s->LaneLeg.Lane != nullptr && s->LaneLeg.Lane == pod->Site->Hop.Lane;
				if (!here) continue;
				double rate = pod->Breacher ? (s->Faction->IsPirate ? DriftingRounds::PirateFindRate : DriftingRounds::TowFindRate)
					: s->Faction->IsPirate ? PirateFindRate : s->Spec->IsMilitary ? PatrolFindRate : MerchantFindRate;
				// On a lane a passer-by has only moments to see them.
				if (pod->Site != nullptr) rate *= 0.25;
				if (rng.NextDouble() >= 1 - cs::Math::Exp(-rate * days)) continue;
				Collect(pod, s->Faction, s, diplomacy, jd);
				break;
			}
		}
	}

	void Crews::Expire(double jd)
	{
		for (int i = Pods.Count() - 1; i >= 0; i--)
		{
			auto pod = Pods[i];
			if (!pod->Drifting() || pod->AirLeftDays(jd) > 0) continue;
			pod->State = PodState::Lost;
			pod->ResolvedJd = jd;
			RecordDeaths(pod->Owners, pod->People);
			Pods.RemoveAt(i);
			Resolved.Invoke(pod, nullptr);
		}
	}

	// ---------------------------------------------------------------- captives

	cs::Ref<Captives> Crews::Hold(const cs::Ref<Faction>& owners, const cs::Ref<Faction>& captors, double people, bool prisoners, const cs::Ref<Settlement>& haven, double jd, const cs::String& from)
	{
		if (owners == nullptr || captors == nullptr || people < 0.5) return nullptr;
		auto c = cs::New<Captives>();
		c->Owners = owners; c->Captors = captors; c->People = people; c->Prisoners = prisoners; c->Haven = haven;
		c->SinceJd = jd; c->RansomJd = jd + 8 + 12 * rng.NextDouble(); c->From = from;
		Held.Add(c);
		return c;
	}

	double Crews::HeldFrom(const cs::Ref<Faction>& owners, const cs::Ref<Faction>& captors) const
	{
		double n = 0;
		for (const auto& c : Held)
			if (c->Owners == owners && (captors == nullptr || c->Captors == captors)) n += c->People;
		return n;
	}

	void Crews::Daily(const cs::Ref<Diplomacy>& diplomacy, double jd)
	{
		for (int i = Held.Count() - 1; i >= 0; i--)
		{
			auto c = Held[i];
			cs::String how;
			if (c->Haven != nullptr && c->Haven->Fallen)
			{
				Return(c->Owners, c->People);
				how = cs::Cat(cs::Fmt(c->People, "0"), " hostages from the ", c->From, " are freed when ", c->Haven->Name, " falls");
			}
			else if (c->Prisoners)
			{
				if (diplomacy != nullptr && diplomacy->Hostile(c->Owners, c->Captors)) continue;
				Return(c->Owners, c->People);
				how = cs::Cat("The ", c->Captors->Name, " send ", cs::Fmt(c->People, "0"), " prisoners from the ", c->From, " home to the ", c->Owners->Name);
			}
			else
			{
				if (jd < c->RansomJd) continue;
				double ransom = c->People * RansomPerHead;
				// Marines salvaged from a Breacher round may go to the side they were fighting, as slaves (U.SPC-T519).
				double price = 0;
				auto buyer = DriftingRounds::Buyer(c, diplomacy, price);
				if (buyer != nullptr && (c->Owners->Treasury < ransom || rng.NextDouble() < DriftingRounds::SellChance))
				{
					buyer->Treasury -= price;
					c->Captors->Treasury += price;
					Lose(c->Owners, c->People);
					how = cs::Cat("The ", c->Captors->Name, " sell ", cs::Fmt(c->People, "0"), " marines from the ", c->From, " to the ", buyer->Name, " as slaves for ", cs::Fmt(price, "N0"), " cr");
				}
				else if (c->Owners->Treasury >= ransom)
				{
					c->Owners->Treasury -= ransom;
					c->Captors->Treasury += ransom;
					Return(c->Owners, c->People);
					how = cs::Cat("The ", c->Owners->Name, " ransom ", cs::Fmt(c->People, "0"), " hostages from the ", c->From, " for ", cs::Fmt(ransom, "N0"), " cr");
				}
				else
				{
					Lose(c->Owners, c->People);
					if (!cs::Double::IsPositiveInfinity(c->Captors->Manpower)) c->Captors->Manpower += c->People;
					how = cs::Cat("Nobody pays for ", cs::Fmt(c->People, "0"), " hostages from the ", c->From, ": the ", c->Captors->Name, " press them into their crews");
				}
			}
			Held.RemoveAt(i);
			CaptivesReleased.Invoke(c, how, jd);
		}
	}

	// ---------------------------------------------------------------- manpower and morale

	void Crews::RecordDeaths(const cs::Ref<Faction>& f, double people)
	{
		if (f == nullptr || people <= 0) return;
		Lose(f, people);
		f->CrewMorale = cs::Math::Clamp(f->CrewMorale - people * MoraleLossPerDeath, MinMorale, MaxMorale);
	}

	void Crews::Lose(const cs::Ref<Faction>& f, double people)
	{
		if (f != nullptr && people > 0) f->CrewLost += people;
	}

	void Crews::Return(const cs::Ref<Faction>& f, double people)
	{
		if (f == nullptr || people <= 0) return;
		f->CrewRescued += people;
		if (!cs::Double::IsPositiveInfinity(f->Manpower)) f->Manpower += people;
		f->CrewMorale = cs::Math::Clamp(f->CrewMorale + people * MoraleGainPerRescue, MinMorale, MaxMorale);
	}

	double Crews::Complement(ShipClass cls)
	{
		auto spec = CombatSpec::Of(cls);
		return spec->Crew + spec->Marines;
	}

	bool Crews::CanCrew(const cs::Ref<Faction>& f, ShipClass cls) { return f != nullptr && f->Manpower >= Complement(cls); }

	double Crews::Draft(const cs::Ref<Faction>& f, double people)
	{
		if (f == nullptr || people <= 0) return 0;
		double got = cs::Math::Min(people, cs::Math::Max(0.0, f->Manpower));
		if (!cs::Double::IsPositiveInfinity(f->Manpower)) f->Manpower -= got;
		return got;
	}

	double Crews::ReserveFor(const cs::Ref<Faction>& f, const cs::List<cs::Ref<Settlement>>& settlements)
	{
		if (f->IsPirate) return PirateReserve;
		double people = 0;
		for (const auto& s : settlements)
			if (s->Owner == f && !s->Fallen) people += s->Population;
		return people * ReserveShare;
	}

	void Crews::MusterReserves(const cs::List<cs::Ref<Faction>>& factions, const cs::List<cs::Ref<Settlement>>& settlements)
	{
		for (const auto& f : factions) f->Manpower = ReserveFor(f, settlements);
	}

	void Crews::Recruit(const cs::List<cs::Ref<Faction>>& factions, const cs::List<cs::Ref<Settlement>>& settlements, double days)
	{
		if (days <= 0) return;
		for (const auto& f : factions)
		{
			f->CrewMorale += (1 - f->CrewMorale) * (1 - cs::Math::Pow(1 - MoraleRecoveryPerDay, days));
			if (cs::Double::IsPositiveInfinity(f->Manpower)) continue;
			double cap = ReserveFor(f, settlements), intake;
			if (f->IsPirate)
			{
				bool haven = false;
				for (const auto& s : settlements)
					if (s->Owner == f && s->Kind == SettlementKind::Haven && !s->Fallen) haven = true;
				intake = haven ? PirateRecruitPerDay * days : 0;
			}
			else
			{
				double people = 0;
				for (const auto& s : settlements)
					if (s->Owner == f && !s->Fallen) people += s->Population;
				intake = people * RecruitPerPersonPerDay * days;
			}
			if (f->Manpower < cap) f->Manpower = cs::Math::Min(cap, f->Manpower + intake);
		}
	}
}
