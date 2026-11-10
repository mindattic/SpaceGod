// US-T282: Underworld (see Systems/Civilization/Underworld.h).

#include "Systems/Civilization/Underworld.h"

#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "World.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		cs::Ref<Org> SyndicateOf(Civilization& civ, const cs::Ref<Settlement>& s)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Syndicate))
				if (o->Seat == s) return o;
			return nullptr;
		}

		/// The pirate band keeping a haven in the system, if any.
		cs::Ref<Faction> PiratesOf(const cs::Ref<StarSystem>& sys)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return nullptr;
			for (const auto& s : sys->World->Economy->Settlements)
				if (s->Kind == SettlementKind::Haven && !s->Fallen && s->Owner != nullptr) return s->Owner;
			return nullptr;
		}

		double Garrisoned(const cs::Ref<Settlement>& s)
		{
			if (cs::Double::IsNaN(s->Garrison)) return 0.5;
			return cs::Math::Min(1.0, s->Garrison / cs::Math::Max(1.0, s->Population * 0.001));
		}
	}

	cs::Ref<Fraud> Underworld::FraudOf(const cs::Ref<Business>& b) const
	{
		for (const auto& f : Frauds)
			if (f->Business == b) return f;
		return nullptr;
	}

	bool Underworld::Jailed(const cs::Ref<Citizen>& c) const
	{
		for (const auto& p : Prisons)
			if (p->Inmates.Contains(c)) return true;
		return false;
	}

	void Underworld::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		React(civ, galaxy, jd);
		for (const auto& k : Contracts.ToArray())
			if (jd >= k->DueJd)
			{
				Contracts.Remove(k);
				Carry(civ, galaxy, society, k, jd);
			}
	}

	void Underworld::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Crime(civ, galaxy, jd);
		Syndicates(civ, galaxy, society, jd);
		Smuggling(civ, galaxy, jd);
		Books(civ, galaxy, society, jd);
		Grudges(civ, galaxy, society, jd);
		Jails(civ, galaxy, jd);
	}

	// ---------------------------------------------------------------- crime

	void Underworld::Crime(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			auto syn = SyndicateOf(civ, s);
			double target = 0.05 + (1 - cs::Math::Min(1.0, Civilization::Wellbeing(s))) * 0.3 + s->Unrest * 0.2 + (syn != nullptr ? syn->Strength * 0.3 : 0)
				+ (s->Lawless ? 0.3 : 0) - Garrisoned(s) * 0.1;
			double before = c->Crime;
			c->Crime = cs::Math::Clamp(c->Crime + (target - c->Crime) * 0.1, 0.0, 1.0);
			if (before < 0.45 && c->Crime >= 0.45) civ.Publish(galaxy, Civilization::Make(SocialKind::CrimeWave, jd, s, c->System));
			// A strongman government cracks down.
			auto regime = civ.Politics != nullptr ? civ.Politics->RegimeOf(s->Owner) : nullptr;
			if (regime != nullptr && regime->Liberty < -0.3 && c->Crime > 0.4 && Roll(H(HashText(s->Name), DayOf(jd), 0xC2Au), 0.2)) Crackdown(civ, galaxy, s, jd);
		}
	}

	// ---------------------------------------------------------------- syndicates

	void Underworld::Syndicates(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			auto syn = SyndicateOf(civ, s);
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x5D1u);
			if (syn == nullptr)
			{
				if (c->Crime < 0.3 || !Roll(h, 0.05)) continue;
				cs::Ref<Citizen> boss;
				for (const auto& hh : society.HousesOf(s))
					for (const auto& m : hh->Members)
						if (m->Alive() && m->AgeYears(jd) >= 21 && (Civilization::Has(m, Trait::Cruel) || Civilization::Has(m, Trait::Greedy)) && boss == nullptr) boss = m;
				syn = civ.Found(OrgKind::Syndicate, s, c->System, PiratesOf(c->System), boss, 5e5, jd, h);
				syn->Strength = 0.2;
				auto e = Civilization::Make(SocialKind::SyndicateRises, jd, s, c->System);
				e->Org = syn;
				e->Who = boss;
				civ.Publish(galaxy, e);
				continue;
			}
			syn->Strength = cs::Math::Clamp(syn->Strength + (c->Crime - 0.3) * 0.05, 0.0, 1.0);
			if (syn->Strength < 0.03) { syn->EndedJd = jd; continue; }
			// Protection: every business on the world pays, a little, the more the stronger the syndicate.
			cs::Ref<Business> refuser;
			for (const auto& b : society.Businesses)
			{
				if (!b->Trading() || b->Home != s) continue;
				auto head = b->Owner != nullptr ? b->Owner->Head() : nullptr;
				if ((Civilization::Has(head, Trait::Brave) || Civilization::Has(head, Trait::Honest)) && Roll(H(h, static_cast<std::uint32_t>(b->Id)), 0.3))
				{
					refuser = b;
					continue;
				}
				double take = b->Capital * 0.01 * syn->Strength;
				b->Capital -= take;
				syn->Funds += take;
			}
			if (Roll(h >> 3, 0.15))
			{
				auto e = Civilization::Make(SocialKind::Protection, jd, s, c->System);
				e->Org = syn;
				for (const auto& b : society.Businesses)
					if (b->Trading() && b->Home == s) { e->Business = b; break; }
				if (e->Business != nullptr) civ.Publish(galaxy, e);
			}
			// One who would not pay may pay another way.
			if (refuser != nullptr && Roll(h >> 7, 0.1) && refuser->Owner != nullptr) Mark(civ, refuser->Owner->Head(), nullptr, syn, 20 + (h % 40u), jd);
		}
	}

	// ---------------------------------------------------------------- smuggling

	void Underworld::Smuggling(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			bool gap = c->Quarantined(jd) || Civilization::AtWar(galaxy, s->Owner) || s->IndustryFactor < 1;
			if (!gap) continue;
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x5306u);
			if (!Roll(h, 0.05 + c->Crime * 0.2)) continue;
			auto syn = SyndicateOf(civ, s);
			if (Roll(h >> 5, cs::Math::Max(0.05, 0.3 - c->Crime * 0.3)))
			{
				if (syn != nullptr) syn->Strength = cs::Math::Max(0.0, syn->Strength - 0.05);
				auto e = Civilization::Make(SocialKind::SmugglerCaught, jd, s, c->System);
				e->Org = syn;
				civ.Publish(galaxy, e);
				continue;
			}
			double take = 5e5 + s->Population * 0.5;
			if (syn != nullptr) syn->Funds += take;
			s->Unrest = cs::Math::Max(0.0, s->Unrest - 0.01); // the goods get through
			auto e = Civilization::Make(SocialKind::SmugglingRun, jd, s, c->System);
			e->Org = syn;
			e->Amount = take;
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- cooked books

	void Underworld::Books(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& b : society.Businesses)
		{
			if (!b->Trading() || b->Owner == nullptr) continue;
			auto head = b->Owner->Head();
			auto f = FraudOf(b);
			std::uint32_t h = H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0xF2Au);
			if (f == nullptr)
			{
				if (b->LastMonth >= 0 || !Civilization::Has(head, Trait::Greedy) || Civilization::Has(head, Trait::Honest) || !Roll(h, 0.05)) continue;
				f = cs::New<Fraud>();
				f->Business = b;
				f->SinceJd = jd;
				Frauds.Add(f);
				auto e = Civilization::Make(SocialKind::Fraud, jd, b->Home, b->System);
				e->Business = b;
				e->Who = head;
				civ.Publish(galaxy, e);
				continue;
			}
			f->Inflated += cs::Math::Abs(b->LastMonth) + b->Capital * 0.01;
			if (Roll(h >> 4, 0.06 + cs::Math::Min(0.2, (jd - f->SinceJd) / 3650))) Expose(civ, galaxy, society, b, jd);
		}
		for (int i = 0; i < Frauds.Count();)
		{
			if (Frauds[i]->Business == nullptr || !Frauds[i]->Business->Trading()) Frauds.RemoveAt(i);
			else i++;
		}
	}

	bool Underworld::Expose(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Business>& b, double jd, bool steered)
	{
		auto f = FraudOf(b);
		if (f == nullptr) return false;
		Frauds.Remove(f);
		b->Capital *= 0.4;
		// The world's investors (its other houses) lose a share of their money.
		double lost = 0;
		for (const auto& h : society.HousesOf(b->Home))
			if (h != b->Owner) { double l = h->Wealth * 0.03; h->Wealth -= l; lost += l; }
		auto head = b->Owner != nullptr ? b->Owner->Head() : nullptr;
		if (head != nullptr) head->Reputation = cs::Math::Max(-1.0, head->Reputation - 0.5);
		auto e = Civilization::Make(SocialKind::FraudExposed, jd, b->Home, b->System);
		e->Business = b;
		e->Who = head;
		e->Amount = f->Inflated + lost;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		if (civ.Politics != nullptr && head != nullptr) civ.Politics->Charge(civ, galaxy, head, SocialKind::FraudExposed, jd);
		return true;
	}

	// ---------------------------------------------------------------- killings

	cs::Ref<Assassination> Underworld::Mark(Civilization& civ, const cs::Ref<Citizen>& target, const cs::Ref<Household>& client, const cs::Ref<Org>& killers, double days, double jd, bool steered)
	{
		if (target == nullptr || !target->Alive()) return nullptr;
		for (const auto& k : Contracts)
			if (k->Target == target) return k;
		auto k = cs::New<Assassination>();
		k->Id = civ.NextId++;
		k->Target = target;
		k->Client = client;
		k->Killers = killers;
		k->DueJd = jd + days;
		k->Steered = steered;
		Contracts.Add(k);
		return k;
	}

	void Underworld::Grudges(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)galaxy;
		// Hot feuds hire killers.
		if (civ.Kinship != nullptr)
			for (const auto& f : civ.Kinship->Feuds)
			{
				if (!f->Active() || f->Heat < 0.75) continue;
				auto client = f->A->Head();
				if (!(Civilization::Has(client, Trait::Cruel) || Civilization::Has(client, Trait::Vengeful))) continue;
				if (!Roll(H(static_cast<std::uint32_t>(f->Id), DayOf(jd), 0xA55u), 0.1)) continue;
				Mark(civ, f->B->Head(), f->A, nullptr, 15 + (DayOf(jd) % 45u), jd);
			}
		// The cruel settle deep enmities.
		for (const auto& c : Civilization::Adults(society, jd))
		{
			if (!Civilization::Has(c, Trait::Cruel)) continue;
			for (const auto& t : c->Ties)
				if (t->Kind == TieKind::Enemy && t->Strength > 0.8 && t->Other != nullptr && t->Other->Alive()
					&& Roll(H(static_cast<std::uint32_t>(c->Id), static_cast<std::uint32_t>(t->Other->Id), DayOf(jd)), 0.03))
				{
					Mark(civ, t->Other, c->House, nullptr, 20 + (DayOf(jd) % 30u), jd);
					break;
				}
		}
	}

	void Underworld::Carry(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Assassination>& k, double jd)
	{
		auto t = k->Target;
		if (t == nullptr || !t->Alive() || t->House == nullptr) return;
		auto h = t->House;
		std::uint32_t r = H(static_cast<std::uint32_t>(k->Id), DayOf(jd), 0xD3Au);
		double odds = 0.6 - (Civilization::Has(t, Trait::Brave) ? 0.1 : 0) - cs::Math::Min(0.2, h->Wealth / 5e8) + (k->Killers != nullptr ? k->Killers->Strength * 0.2 : 0);
		auto sys = h->System;
		if (!k->Steered && !Roll(r, odds))
		{
			auto e = Civilization::Make(SocialKind::AssassinationFoiled, jd, h->Home, sys);
			e->Who = t;
			e->House = k->Client;
			civ.Publish(galaxy, e);
			if (civ.Kinship != nullptr && k->Client != nullptr)
				if (auto f = civ.Kinship->FeudBetween(k->Client, h)) f->Heat = cs::Math::Min(1.0, f->Heat + 0.2);
			return;
		}
		t->DiedJd = jd;
		society.Deaths++;
		if (t->Spouse != nullptr && t->Spouse->Spouse == t) t->Spouse->Spouse = nullptr;
		// A murdered head is succeeded at once by the eldest heir.
		if (t->Role == CitizenRole::Head)
		{
			cs::Ref<Citizen> heir;
			for (const auto& m : h->Members)
				if (m->Alive() && m->Role == CitizenRole::Heir && (heir == nullptr || m->BornJd < heir->BornJd)) heir = m;
			if (heir != nullptr) heir->Role = CitizenRole::Head;
		}
		auto e = Civilization::Make(SocialKind::Assassination, jd, h->Home, sys);
		e->Who = t;
		e->House = k->Client;
		e->Org = k->Killers;
		e->Steered = k->Steered;
		civ.Publish(galaxy, e);
		// Mirrored as a death, so heirs inherit and successions follow.
		auto d = Civilization::Make(SocialKind::Death, jd, h->Home, sys);
		d->Who = t;
		d->House = h;
		civ.Publish(galaxy, d, false, false);
		if (civ.Kinship != nullptr && k->Client != nullptr && k->Client != h)
		{
			auto f = civ.Kinship->FeudBetween(k->Client, h);
			if (f != nullptr) f->Heat = 1;
		}
		// The client may be found out.
		if (k->Client != nullptr && civ.Politics != nullptr && Roll(r >> 6, 0.4)) civ.Politics->Charge(civ, galaxy, k->Client->Head(), SocialKind::Assassination, jd);
	}

	// ---------------------------------------------------------------- prisons

	void Underworld::Imprison(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& c, double jd)
	{
		if (c == nullptr || !c->Alive() || c->House == nullptr || c->House->Home == nullptr || Jailed(c)) return;
		auto f = c->House->Home->Owner;
		cs::Ref<Prison> p;
		for (const auto& x : Prisons)
			if (x->Faction == f && Civilization::Stands(x->Where)) p = x;
		if (p == nullptr)
		{
			// The state's smallest standing world takes the convicts.
			cs::Ref<Settlement> site;
			for (const auto& w : civ.Worlds)
				if (w->Where->Owner == f && (site == nullptr || w->Where->Population < site->Population)) site = w->Where;
			if (site == nullptr) return;
			p = cs::New<Prison>();
			p->Where = site;
			p->Faction = f;
			Prisons.Add(p);
			auto e = Civilization::Make(SocialKind::PrisonOpened, jd, site, Civilization::SystemOf(galaxy, site));
			e->Faction = f;
			civ.Publish(galaxy, e);
		}
		p->Inmates.Add(c);
		p->ReleaseJd.Add(jd + 365 * (1 + (H(static_cast<std::uint32_t>(c->Id), DayOf(jd)) % 5u)));
	}

	void Underworld::Jails(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& p : Prisons)
		{
			for (int i = 0; i < p->Inmates.Count();)
			{
				if (!p->Inmates[i]->Alive() || jd >= p->ReleaseJd[i]) { p->Inmates.RemoveAt(i); p->ReleaseJd.RemoveAt(i); }
				else i++;
			}
			if (p->Inmates.Count() == 0 || !Civilization::Stands(p->Where)) continue;
			auto c = civ.CivicsOf(p->Where);
			double crime = c != nullptr ? c->Crime : 0.2;
			if (!Roll(H(HashText(p->Where->Name), DayOf(jd), 0xB2Eu), 0.02 + crime * 0.05)) continue;
			p->Breakouts++;
			auto e = Civilization::Make(SocialKind::PrisonBreak, jd, p->Where, Civilization::SystemOf(galaxy, p->Where));
			e->Faction = p->Faction;
			e->Amount = p->Inmates.Count();
			e->Who = p->Inmates[0];
			civ.Publish(galaxy, e);
			if (c != nullptr) c->Crime = cs::Math::Min(1.0, c->Crime + 0.05);
			p->Inmates.Clear();
			p->ReleaseJd.Clear();
		}
	}

	void Underworld::React(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			if (e->Kind == SocialKind::Verdict) Imprison(civ, galaxy, e->Who, jd);
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Underworld::Hit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& target, double jd)
	{
		(void)galaxy;
		if (target == nullptr || !target->Alive()) return "no such person living";
		auto k = Mark(civ, target, nullptr, nullptr, 1, jd, true);
		if (k != nullptr) k->Steered = true;
		return "";
	}

	cs::String Underworld::Racket(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd)
	{
		(void)society;
		if (!Civilization::Stands(s)) return "no such world standing";
		auto syn = SyndicateOf(civ, s);
		auto sys = Civilization::SystemOf(galaxy, s);
		if (syn == nullptr)
		{
			syn = civ.Found(OrgKind::Syndicate, s, sys, PiratesOf(sys), nullptr, 5e6, jd, H(HashText(s->Name), DayOf(jd), 0x5D2u));
			auto e = Civilization::Make(SocialKind::SyndicateRises, jd, s, sys);
			e->Org = syn;
			e->Steered = true;
			civ.Publish(galaxy, e);
		}
		syn->Strength = cs::Math::Min(1.0, syn->Strength + 0.4);
		if (auto c = civ.CivicsOf(s)) c->Crime = cs::Math::Min(1.0, c->Crime + 0.2);
		return "";
	}

	cs::String Underworld::Crackdown(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		if (!Civilization::Stands(s)) return "no such world standing";
		auto c = civ.CivicsOf(s);
		if (c == nullptr) return "nobody watches that world yet";
		c->Crime = cs::Math::Max(0.0, c->Crime - 0.2);
		if (auto syn = SyndicateOf(civ, s)) syn->Strength = cs::Math::Max(0.0, syn->Strength - 0.3);
		civ.Stir(s, 0.03, -0.03);
		civ.Publish(galaxy, Civilization::Make(SocialKind::Crackdown, jd, s, c->System));
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Assassination, "SpaceGod.Core.Assassination",
		SG_F(Id), SG_F(Target), SG_F(Client), SG_F(Killers), SG_F(DueJd), SG_F(Steered))

	SG_SERIALIZE(Prison, "SpaceGod.Core.Prison",
		SG_F(Where), SG_F(Faction), SG_F(Inmates), SG_F(ReleaseJd), SG_F(Breakouts))

	SG_SERIALIZE(Fraud, "SpaceGod.Core.Fraud",
		SG_F(Business), SG_F(Inflated), SG_F(SinceJd))

	SG_SERIALIZE(Underworld, "SpaceGod.Core.Underworld",
		SG_F(Contracts), SG_F(Prisons), SG_F(Frauds), SG_F(seenEvent))
}
