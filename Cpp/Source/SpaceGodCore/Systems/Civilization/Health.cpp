// US-T286: Health (see Systems/Civilization/Health.h).

#include "Systems/Civilization/Health.h"

#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/Statecraft.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		int NameKey(const cs::Ref<Settlement>& s) { return static_cast<int>(HashText(s->Name) & 0x7fffffffu); }
	}

	void Health::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd) { (void)civ; (void)galaxy; (void)society; (void)jd; }

	void Health::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Sickness(civ, galaxy, society, jd);
		Care(civ, galaxy, society, jd);
		Births(civ, galaxy, society, jd);
		Disasters(civ, galaxy, society, jd);
		Hunger(civ, galaxy, jd);
	}

	void Health::Kill(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double share, double jd)
	{
		s->Population = cs::Math::Max(0.0, s->Population * (1 - share));
		// Named people die too, now and then.
		for (const auto& h : society.HousesOf(s))
			for (const auto& c : h->Members.ToArray())
			{
				if (!c->Alive() || !Roll(H(static_cast<std::uint32_t>(c->Id), DayOf(jd), 0xDEA7u), share * 3)) continue;
				c->DiedJd = jd;
				society.Deaths++;
				if (c->Spouse != nullptr && c->Spouse->Spouse == c) c->Spouse->Spouse = nullptr;
				if (c->Role == CitizenRole::Head)
				{
					cs::Ref<Citizen> heir;
					for (const auto& m : h->Members)
						if (m->Alive() && m->Role == CitizenRole::Heir && (heir == nullptr || m->BornJd < heir->BornJd)) heir = m;
					if (heir != nullptr) heir->Role = CitizenRole::Head;
				}
				auto d = Civilization::Make(SocialKind::Death, jd, s, h->System);
				d->Who = c;
				d->House = h;
				civ.Publish(galaxy, d);
			}
	}

	// ---------------------------------------------------------------- epidemics

	void Health::Sickness(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Active = 0;
		for (const auto& c : civ.Worlds)
			if (c->Infected > 0.005) Active++;
		// A new fever, now and then, where care is poor and crime high.
		if (Active < MaxOutbreaks * 3)
			for (const auto& c : civ.Worlds)
			{
				if (c->Infected > 0) continue;
				double chance = 0.0015 + c->Crime * 0.004 + (1 - c->Care) * 0.002;
				if (!Roll(H(HashText(c->Where->Name), DayOf(jd), 0x0B7Au), chance)) continue;
				c->Infected = 0.02;
				Outbreaks++;
				civ.Publish(galaxy, Civilization::Make(SocialKind::Outbreak, jd, c->Where, c->System));
				break;
			}
		// It spreads through a world, burns out into immunity, and kills.
		for (const auto& c : civ.Worlds)
		{
			if (c->Infected <= 0) { c->Immune = cs::Math::Max(0.0, c->Immune - 0.01); continue; }
			double susceptible = cs::Math::Max(0.0, 1 - c->Infected - c->Immune);
			double catching = c->Infected * susceptible * 1.2 * (1 - c->Care * 0.5);
			double recovering = c->Infected * (0.3 + c->Care * 0.2);
			c->Infected = cs::Math::Clamp(c->Infected + catching - recovering, 0.0, 1.0);
			c->Immune = cs::Math::Min(1.0, c->Immune + recovering);
			Kill(civ, galaxy, society, c->Where, c->Infected * 0.02 * (1 - c->Care * 0.6), jd);
			// Quarantine a badly hit world.
			if (c->Infected > 0.1 && !c->Quarantined(jd) && Roll(H(HashText(c->Where->Name), DayOf(jd), 0x0A2Au), 0.6))
			{
				c->QuarantineUntil = jd + QuarantineDays;
				civ.Publish(galaxy, Civilization::Make(SocialKind::Quarantine, jd, c->Where, c->System));
			}
			if (c->Infected < 0.005)
			{
				c->Infected = 0;
				c->QuarantineUntil = cs::Double::NaN;
				civ.Publish(galaxy, Civilization::Make(SocialKind::Cured, jd, c->Where, c->System));
			}
		}
		// The travellers carry it from a sick world to a clean one.
		for (const auto& t : society.Trips)
		{
			if (t->From == nullptr || t->To == nullptr || t->ArriveJd <= jd - Civilization::MonthDays || t->ArriveJd > jd) continue;
			auto from = civ.CivicsOf(t->From), to = civ.CivicsOf(t->To);
			if (from == nullptr || to == nullptr || from->Infected < 0.05 || to->Quarantined(jd)) continue;
			if (!Roll(H(static_cast<std::uint32_t>(t->Id), 0x5BEAu), from->Infected * 2)) continue;
			bool fresh = to->Infected <= 0;
			to->Infected = cs::Math::Min(1.0, to->Infected + 0.01);
			if (fresh) civ.Publish(galaxy, Civilization::Make(SocialKind::Spread, jd, to->Where, to->System));
		}
	}

	// ---------------------------------------------------------------- hospitals and medicine

	void Health::Care(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0xCA2Eu);
			double wealth = 0;
			cs::Ref<Household> richest;
			for (const auto& hh : society.HousesOf(c->Where))
			{
				wealth += hh->Wealth;
				if (richest == nullptr || hh->Wealth > richest->Wealth) richest = hh;
			}
			if (wealth > 1e8 && c->Care < 0.8 && Roll(h, 0.03 + (c->Infected > 0.05 ? 0.1 : 0)))
			{
				c->Care = cs::Math::Min(1.0, c->Care + 0.15);
				richest->Wealth *= 0.98;
				auto e = Civilization::Make(SocialKind::HospitalBuilt, jd, c->Where, c->System);
				e->House = richest;
				civ.Publish(galaxy, e);
			}
			if (c->Infected > 0.15 && c->Care < 0.4 && Roll(h >> 5, 0.3))
			{
				civ.Stir(c->Where, 0.03, -0.05);
				civ.Publish(galaxy, Civilization::Make(SocialKind::MedicineShortage, jd, c->Where, c->System));
			}
			c->Care = cs::Math::Max(0.1, c->Care - 0.002); // hospitals age
		}
	}

	// ---------------------------------------------------------------- births and age

	void Health::Births(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		cs::List<int> atWar;
		for (const auto& c : civ.Worlds)
		{
			int key = NameKey(c->Where);
			bool war = c->WarDays > 0;
			if (war) atWar.Add(key);
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0xB0B1u);
			if (!war && lastWar.Contains(key) && c->Approval > 0)
			{
				c->Where->Population *= 1.01;
				// The houses there have news sooner.
				for (const auto& hh : society.HousesOf(c->Where))
					if (!cs::Double::IsNaN(hh->NextEventJd)) hh->NextEventJd = cs::Math::Min(hh->NextEventJd, jd + 10);
				civ.Publish(galaxy, Civilization::Make(SocialKind::BabyBoom, jd, c->Where, c->System));
			}
			else if (c->Approval < -0.2 && Civilization::Wellbeing(c->Where) < 0.5 && Roll(h, 0.02))
			{
				c->Where->Population *= 0.995;
				civ.Publish(galaxy, Civilization::Make(SocialKind::Ageing, jd, c->Where, c->System));
			}
		}
		lastWar = atWar;
	}

	// ---------------------------------------------------------------- disasters and relief

	bool Health::Help(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, bool food, double jd, const cs::Ref<Faction>& forced)
	{
		auto owner = s->Owner;
		auto sc = galaxy.Polities != nullptr ? galaxy.Polities->Statecraft : nullptr;
		cs::Ref<Faction> helper = forced;
		if (helper == nullptr && sc != nullptr && galaxy.Polities != nullptr)
		{
			double best = 25;
			for (const auto& f : galaxy.Polities->Factions())
				if (f != owner && !f->IsPirate && Statecraft::Party(f) && f->Treasury > 5e7 && sc->Opinion(f, owner) > best) { best = sc->Opinion(f, owner); helper = f; }
		}
		if (helper == nullptr) return false;
		double cost = cs::Math::Min(helper->Treasury * 0.02, 2e7);
		helper->Treasury -= cost;
		if (sc != nullptr && helper != owner) { sc->Sway(owner, helper, 6); sc->Sway(helper, owner, 2); }
		civ.Stir(s, -0.03, 0.05);
		auto e = Civilization::Make(food ? SocialKind::FoodAid : SocialKind::Relief, jd, s, Civilization::SystemOf(galaxy, s));
		e->Faction = helper;
		e->Amount = cost;
		e->Steered = forced != nullptr;
		civ.Publish(galaxy, e);
		return true;
	}

	void Health::Disaster(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd, bool steered)
	{
		std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0xD15Au);
		double lost = 0.01 + 0.04 * Unit(h);
		Kill(civ, galaxy, society, s, lost, jd);
		for (const auto& b : society.Businesses)
			if (b->Trading() && b->Home == s) b->Capital *= 0.9;
		civ.Stir(s, 0.05, 0);
		auto e = Civilization::Make(SocialKind::Disaster, jd, s, Civilization::SystemOf(galaxy, s));
		e->Amount = lost;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		Help(civ, galaxy, s, false, jd, nullptr);
	}

	void Health::Disasters(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			double chance = 0.001 + (c->Where->Kind == SettlementKind::MiningOutpost ? 0.002 : 0);
			if (Roll(H(HashText(c->Where->Name), DayOf(jd), 0xD15Bu), chance)) Disaster(civ, galaxy, society, c->Where, jd, false);
		}
	}

	// ---------------------------------------------------------------- hunger

	void Health::Hunger(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			if (c->Where->Ration >= 0.8) continue;
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0xFA31u);
			if (!Roll(h, 0.3)) continue;
			civ.Stir(c->Where, 0.03, -0.05);
			civ.Publish(galaxy, Civilization::Make(SocialKind::Famine, jd, c->Where, c->System));
			Help(civ, galaxy, c->Where, true, jd, nullptr);
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Health::Plague(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		auto c = civ.CivicsOf(s);
		if (c == nullptr) return "nobody watches that world yet";
		bool fresh = c->Infected <= 0;
		c->Infected = cs::Math::Min(1.0, c->Infected + 0.08);
		Outbreaks += fresh ? 1 : 0;
		auto e = Civilization::Make(SocialKind::Outbreak, jd, s, c->System);
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Health::Cure(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		auto c = civ.CivicsOf(s);
		if (c == nullptr) return "nobody watches that world yet";
		if (c->Infected <= 0) return "it is not sick";
		c->Immune = cs::Math::Min(1.0, c->Immune + c->Infected);
		c->Infected = 0;
		c->QuarantineUntil = cs::Double::NaN;
		auto e = Civilization::Make(SocialKind::Cured, jd, s, c->System);
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Health::Strike(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd)
	{
		if (civ.CivicsOf(s) == nullptr) return "nobody watches that world yet";
		Disaster(civ, galaxy, society, s, jd, true);
		return "";
	}

	cs::String Health::Aid(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& by, const cs::Ref<Settlement>& s, double jd)
	{
		if (by == nullptr) return "no such state";
		if (civ.CivicsOf(s) == nullptr) return "nobody watches that world yet";
		Help(civ, galaxy, s, s->Ration < 0.8, jd, by);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Health, "SpaceGod.Core.Health",
		SG_F(Active), SG_F(Outbreaks), SG_F(lastWar))
}
