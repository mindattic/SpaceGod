// US-T280: Strife (see Systems/Civilization/Strife.h).

#include "Systems/Civilization/Strife.h"

#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Galaxy.h"
#include "Systems/GalaxyLife.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "Systems/Territory.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		std::uint32_t FH(const cs::Ref<Faction>& f) { return f != nullptr ? HashText(f->Name) : 0u; }
		cs::Ref<Statecraft> StatecraftOf(Galaxy& galaxy) { return galaxy.Polities != nullptr ? galaxy.Polities->Statecraft : nullptr; }

		cs::Ref<Org> InsurgentsOf(Civilization& civ, const cs::Ref<Settlement>& s)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Insurgents))
				if (o->Seat == s) return o;
			return nullptr;
		}

		cs::Ref<Org> AgencyOf(Civilization& civ, const cs::Ref<Faction>& f)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Agency))
				if (o->Faction == f) return o;
			return nullptr;
		}

		double AvgWeariness(Civilization& civ, const cs::Ref<Faction>& f)
		{
			double w = 0;
			int n = 0;
			for (const auto& c : civ.Worlds)
				if (c->Where->Owner == f) { w += c->Weariness; n++; }
			return n > 0 ? w / n : 0;
		}

		void Weary(Civilization& civ, const cs::Ref<Faction>& f, double by)
		{
			for (const auto& c : civ.Worlds)
				if (c->Where->Owner == f) c->Weariness = cs::Math::Clamp(c->Weariness + by, 0.0, 1.0);
		}
	}

	bool Strife::OnStrike(const cs::Ref<Settlement>& s) const
	{
		for (const auto& w : Walkouts)
			if (w->Settlement == s) return true;
		return false;
	}

	cs::Ref<Admiral> Strife::AdmiralOf(const cs::Ref<Faction>& f) const
	{
		for (const auto& a : Admirals)
			if (a->Faction == f) return a;
		return nullptr;
	}

	bool Strife::CallStrike(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, const cs::Ref<Org>& by, double days, double jd)
	{
		// A world already out (ours, or a strike MarketShocks called over wages) is not called out again.
		if (s == nullptr || OnStrike(s) || s->IndustryFactor < 1) return false;
		auto w = cs::New<Walkout>();
		w->Settlement = s;
		w->Union = by;
		w->StartJd = jd;
		w->EndJd = jd + days;
		Walkouts.Add(w);
		s->IndustryFactor = 0.15;
		auto e = Civilization::Make(SocialKind::StrikeCalled, jd, s, Civilization::SystemOf(galaxy, s));
		e->Org = by;
		e->Amount = days;
		civ.Publish(galaxy, e);
		return true;
	}

	void Strife::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)society;
		React(civ, galaxy, jd);
		// Strikes end: industry back, wages a little higher for it.
		for (int i = 0; i < Walkouts.Count();)
		{
			auto w = Walkouts[i];
			if (jd < w->EndJd) { i++; continue; }
			if (w->Settlement != nullptr)
			{
				if (w->Settlement->IndustryFactor < 1) w->Settlement->IndustryFactor = 1;
				w->Settlement->WageIndex *= 1.04;
				auto e = Civilization::Make(SocialKind::WageDeal, jd, w->Settlement, Civilization::SystemOf(galaxy, w->Settlement));
				e->Org = w->Union;
				civ.Publish(galaxy, e);
			}
			Walkouts.RemoveAt(i);
		}
	}

	void Strife::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Streets(civ, galaxy, society, jd);
		Insurgency(civ, galaxy, society, jd);
		Spies(civ, galaxy, jd);
		Weariness(civ, galaxy, jd);
		Mercenaries(civ, galaxy, society, jd);
		Admiralty(civ, galaxy, society, jd);
	}

	void Strife::Burn(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double share, double jd)
	{
		(void)civ; (void)galaxy; (void)jd;
		for (const auto& b : society.Businesses)
			if (b->Trading() && b->Home == s) b->Capital *= 1 - share;
	}

	// ---------------------------------------------------------------- the streets

	void Strife::Streets(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x57EEu);
			if ((c->Approval < -0.25 || s->Unrest > 0.4) && Roll(h, 0.15))
			{
				civ.Stir(s, 0.02, 0);
				civ.Publish(galaxy, Civilization::Make(SocialKind::Protest, jd, s, c->System));
			}
			if (c->Approval < -0.5 && s->Unrest > 0.55 && Roll(h >> 5, 0.12))
			{
				civ.Stir(s, 0.05, -0.05);
				Burn(civ, galaxy, society, s, 0.05, jd);
				civ.Publish(galaxy, Civilization::Make(SocialKind::Riot, jd, s, c->System));
				if (Roll(h >> 9, 0.3)) CallStrike(civ, galaxy, s, nullptr, 7 + (h % 14u), jd);
			}
		}
	}

	// ---------------------------------------------------------------- the armed cells

	void Strife::Insurgency(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			auto cell = InsurgentsOf(civ, s);
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x1A5Fu);
			bool fertile = c->Drift > 0.4 || s->OccupiedFrom != nullptr || c->Approval < -0.5;
			if (cell == nullptr)
			{
				if (!fertile || !Roll(h, 0.06)) continue;
				// Led by a brave or vengeful soul of a house there, if one will.
				cs::Ref<Citizen> leader;
				for (const auto& hh : society.HousesOf(s))
					for (const auto& m : hh->Members)
						if (m->Alive() && m->AgeYears(jd) >= 18 && m->AgeYears(jd) < 50 && (Civilization::Has(m, Trait::Brave) || Civilization::Has(m, Trait::Vengeful)) && leader == nullptr) leader = m;
				cell = civ.Found(OrgKind::Insurgents, s, c->System, s->OccupiedFrom, leader, 1e5, jd, h);
				cell->Strength = 0.1;
				auto e = Civilization::Make(SocialKind::CellFormed, jd, s, c->System);
				e->Org = cell;
				e->Who = leader;
				civ.Publish(galaxy, e);
				continue;
			}
			// The cell grows on discontent and crime, starves on contentment and a strong garrison.
			double garrison = cs::Double::IsNaN(s->Garrison) ? 0.5 : cs::Math::Min(1.0, s->Garrison / cs::Math::Max(1.0, s->Population * 0.001));
			cell->Strength = cs::Math::Clamp(cell->Strength + (-c->Approval) * 0.05 + c->Crime * 0.02 + c->Drift * 0.02 - garrison * 0.03, 0.0, 1.0);
			if (cell->Strength < 0.02)
			{
				cell->EndedJd = jd;
				auto e = Civilization::Make(SocialKind::Crackdown, jd, s, c->System);
				e->Org = cell;
				civ.Publish(galaxy, e);
				continue;
			}
			if (Roll(h >> 4, cell->Strength * 0.3))
			{
				civ.Stir(s, 0.03, -0.03);
				Burn(civ, galaxy, society, s, 0.02, jd);
				if (s->Owner != nullptr) s->Owner->Treasury = cs::Math::Max(0.0, s->Owner->Treasury - s->Population * 2);
				auto e = Civilization::Make(SocialKind::Sabotage, jd, s, c->System);
				e->Org = cell;
				civ.Publish(galaxy, e);
			}
			// Strong enough on a restless enough world, it rises and breaks the world away.
			if (cell->Strength > 0.8 && s->Unrest > 0.6 && Roll(h >> 9, 0.25) && galaxy.Territory != nullptr && galaxy.Territory->Life != nullptr && c->System != nullptr)
			{
				auto e = Civilization::Make(SocialKind::Uprising, jd, s, c->System);
				e->Org = cell;
				civ.Publish(galaxy, e);
				auto free = galaxy.Territory->Life->Secede(galaxy, c->System, s, jd);
				cell->EndedJd = jd;
				if (free != nullptr) c->Approval = 0.5;
			}
		}
	}

	// ---------------------------------------------------------------- spies

	bool Strife::Operation(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& agency, const cs::Ref<Faction>& on, double jd, bool steered)
	{
		auto sc = StatecraftOf(galaxy);
		auto by = agency->Faction;
		if (sc == nullptr || by == nullptr || on == nullptr) return false;
		std::uint32_t h = H(static_cast<std::uint32_t>(agency->Id), FH(on), DayOf(jd));
		auto capital = galaxy.Polities->CapitalOf(on);
		auto sys = Civilization::SystemOf(galaxy, capital);
		// Caught.
		if (!steered && Roll(h, 0.25 - agency->Strength * 0.1))
		{
			sc->Sway(on, by, -6);
			agency->Strength = cs::Math::Max(0.05, agency->Strength - 0.1);
			auto e = Civilization::Make(SocialKind::SpyCaught, jd, capital, sys);
			e->Faction = on;
			e->Rival = by;
			e->Org = agency;
			civ.Publish(galaxy, e);
			return false;
		}
		agency->Strength = cs::Math::Min(1.0, agency->Strength + 0.03);
		int op = static_cast<int>((h >> 5) % 3u);
		if (op == 0)
		{
			// Leak a pact of theirs with a third state, which cools on them and may break it.
			for (const auto& p : sc->Pacts().ToArray())
			{
				if (!p->Involves(on) || p->Involves(by)) continue;
				auto third = p->Other(on);
				sc->Sway(third, on, -8);
				auto e = Civilization::Make(SocialKind::PactLeaked, jd, capital, sys);
				e->Faction = on;
				e->Rival = third;
				e->Org = agency;
				civ.Publish(galaxy, e);
				if (Roll(h >> 9, 0.3)) sc->Break(p, third, jd, "a leak");
				return true;
			}
			op = 1;
		}
		if (op == 1)
		{
			// Steal research: a level they have that we lack gives our labs a third of its cost.
			auto cmd = galaxy.Polities->Command;
			if (cmd != nullptr)
			{
				auto mine = cmd->Of(by);
				auto theirs = cmd->Find(on);
				if (mine != nullptr && theirs != nullptr && mine->Research != nullptr && theirs->Level(mine->Research) > mine->Level(mine->Research))
				{
					mine->ResearchPoints += mine->Research->CostOf(mine->Level(mine->Research)) * 0.3;
					auto e = Civilization::Make(SocialKind::TechStolen, jd, capital, sys);
					e->Faction = on;
					e->Rival = by;
					e->Org = agency;
					civ.Publish(galaxy, e);
					return true;
				}
			}
		}
		// Turn a governor: their world grows restless and sour on its rulers.
		cs::List<cs::Ref<Civics>> theirs;
		for (const auto& c : civ.Worlds)
			if (c->Where->Owner == on) theirs.Add(c);
		if (theirs.Count() == 0) return false;
		auto w = Pick(theirs, h >> 11);
		civ.Stir(w->Where, 0.04, -0.1);
		agency->Funds = cs::Math::Max(0.0, agency->Funds - 1e6);
		auto e = Civilization::Make(SocialKind::Sabotage, jd, w->Where, w->System);
		e->Org = agency;
		e->Faction = on;
		e->Rival = by;
		civ.Publish(galaxy, e);
		return true;
	}

	void Strife::Spies(Civilization& civ, Galaxy& galaxy, double jd)
	{
		auto sc = StatecraftOf(galaxy);
		if (sc == nullptr || galaxy.Polities == nullptr) return;
		for (const auto& f : galaxy.Polities->Factions())
		{
			if (f->IsPirate || !Statecraft::Party(f) || galaxy.Polities->CapitalOf(f) == nullptr) continue;
			// Its foe: an enemy at war, else the state it likes least (if it likes it little).
			auto foe = Civilization::EnemyOf(galaxy, f);
			if (foe == nullptr)
			{
				double least = -30;
				for (const auto& g : galaxy.Polities->Factions())
					if (g != f && !g->IsPirate && Statecraft::Party(g) && sc->Opinion(f, g) < least) { least = sc->Opinion(f, g); foe = g; }
			}
			if (foe == nullptr || foe->IsPirate) continue;
			auto agency = AgencyOf(civ, f);
			std::uint32_t h = H(FH(f), DayOf(jd), 0x5B1u);
			if (agency == nullptr)
			{
				if (!Roll(h, 0.2)) continue;
				auto capital = galaxy.Polities->CapitalOf(f);
				agency = civ.Found(OrgKind::Agency, capital, Civilization::SystemOf(galaxy, capital), f, nullptr, 1e7, jd, h);
				agency->Strength = 0.3;
				continue;
			}
			if (Roll(h >> 3, 0.15)) Operation(civ, galaxy, agency, foe, jd, false);
		}
	}

	// ---------------------------------------------------------------- weariness and the draft

	void Strife::Weariness(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (galaxy.Wars == nullptr) return;
		auto sc = StatecraftOf(galaxy);
		cs::List<cs::Ref<Faction>> done;
		for (const auto& war : galaxy.Wars->Active().ToArray())
			for (const auto& f : { war->Aggressor, war->Defender })
			{
				if (f == nullptr || f->IsPirate || done.Contains(f)) continue;
				done.Add(f);
				double weary = AvgWeariness(civ, f);
				std::uint32_t h = H(FH(f), DayOf(jd), 0x3EA7u);
				auto capital = galaxy.Polities != nullptr ? galaxy.Polities->CapitalOf(f) : nullptr;
				if (weary > 0.6 && jd >= nextWearyNewsJd)
				{
					nextWearyNewsJd = jd + 60;
					auto e = Civilization::Make(SocialKind::WarWeariness, jd, capital, Civilization::SystemOf(galaxy, capital));
					e->Faction = f;
					e->Amount = weary;
					civ.Publish(galaxy, e);
				}
				if (weary > 0.8 && sc != nullptr && Roll(h, 0.15))
				{
					auto enemy = war->Enemy(f);
					auto e = Civilization::Make(SocialKind::PeaceForced, jd, capital, Civilization::SystemOf(galaxy, capital));
					e->Faction = f;
					e->Rival = enemy;
					civ.Publish(galaxy, e);
					sc->MakePeace(f, enemy, jd, "war weariness at home");
					break;
				}
				// Hawks call up the reserves.
				double hawk = 0;
				int n = 0;
				for (const auto& c : civ.Worlds)
					if (c->Where->Owner == f) { hawk += c->Hawk; n++; }
				if (n > 0 && hawk / n > 0.2 && weary < 0.5 && Roll(h >> 6, 0.05))
				{
					if (f->Manpower < 1e300)
					{
						double pop = 0;
						for (const auto& c : civ.Worlds)
							if (c->Where->Owner == f) pop += c->Where->Population;
						f->Manpower += pop * 0.0005;
					}
					Weary(civ, f, 0.08);
					for (const auto& c : civ.Worlds)
						if (c->Where->Owner == f) civ.Stir(c->Where, 0.02, -0.05);
					auto e = Civilization::Make(SocialKind::Conscription, jd, capital, Civilization::SystemOf(galaxy, capital));
					e->Faction = f;
					civ.Publish(galaxy, e);
				}
			}
	}

	// ---------------------------------------------------------------- free companies

	void Strife::Mercenaries(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto mercs = civ.OrgsOf(OrgKind::Mercenaries);
		// Free companies gather where the law is thin or the wars are.
		if (mercs.Count() < MaxMercs)
			for (const auto& c : civ.Worlds)
			{
				if (!(c->Where->Lawless || c->Crime > 0.35 || c->WarDays > 180)) continue;
				std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0x3E5Cu);
				if (!Roll(h, 0.03)) continue;
				auto o = civ.Found(OrgKind::Mercenaries, c->Where, c->System, nullptr, nullptr, 5e5, jd, h);
				o->Strength = 0.2 + 0.4 * Unit(h >> 7);
				break;
			}
		for (const auto& o : civ.OrgsOf(OrgKind::Mercenaries))
		{
			std::uint32_t h = H(static_cast<std::uint32_t>(o->Id), DayOf(jd), 0x3E5Du);
			double fee = 2e7 * (0.5 + o->Strength);
			if (o->Faction == nullptr)
			{
				// Hired by a state at war with the money to pay.
				cs::Ref<Faction> buyer;
				if (galaxy.Wars != nullptr)
					for (const auto& w : galaxy.Wars->Active())
						for (const auto& f : { w->Aggressor, w->Defender })
							if (f != nullptr && !f->IsPirate && f->Treasury > fee * 3 && (buyer == nullptr || f->Treasury > buyer->Treasury)) buyer = f;
				if (buyer != nullptr && Roll(h, 0.3))
				{
					buyer->Treasury -= fee;
					o->Funds += fee;
					o->Faction = buyer;
					Weary(civ, buyer, -0.05);
					auto e = Civilization::Make(SocialKind::MercsHired, jd, o->Seat, o->System);
					e->Org = o;
					e->Faction = buyer;
					e->Amount = fee;
					civ.Publish(galaxy, e);
					continue;
				}
				// Or by a house in a hot feud.
				if (civ.Kinship != nullptr)
					for (const auto& f : civ.Kinship->Feuds)
						if (f->Active() && f->Heat > 0.7 && f->A->Wealth > fee && Roll(H(h, static_cast<std::uint32_t>(f->Id)), 0.2))
						{
							f->A->Wealth -= fee * 0.25;
							o->Funds += fee * 0.25;
							f->Heat = cs::Math::Min(1.0, f->Heat + 0.1);
							if (f->B->Business != nullptr && f->B->Business->Trading()) f->B->Business->Capital *= 0.92;
							auto e = Civilization::Make(SocialKind::MercsHired, jd, f->A->Home, f->A->System);
							e->Org = o;
							e->House = f->A;
							e->OtherHouse = f->B;
							e->Amount = fee * 0.25;
							civ.Publish(galaxy, e);
							break;
						}
				continue;
			}
			// In service: paid off at the war's end, or bought away by a richer enemy.
			auto enemy = Civilization::EnemyOf(galaxy, o->Faction);
			if (enemy == nullptr) { o->Faction = nullptr; continue; }
			if (!enemy->IsPirate && enemy->Treasury > o->Faction->Treasury * 1.5 && enemy->Treasury > fee * 4 && Roll(h >> 4, 0.08))
			{
				auto old = o->Faction;
				enemy->Treasury -= fee * 1.5;
				o->Funds += fee * 1.5;
				o->Faction = enemy;
				Weary(civ, old, 0.06);
				Weary(civ, enemy, -0.05);
				auto e = Civilization::Make(SocialKind::MercsTurned, jd, o->Seat, o->System);
				e->Org = o;
				e->Faction = old;
				e->Rival = enemy;
				civ.Publish(galaxy, e);
			}
		}
		(void)society;
	}

	// ---------------------------------------------------------------- admirals

	void Strife::Admiralty(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (int i = 0; i < Admirals.Count();)
		{
			if (Admirals[i]->Who == nullptr || !Admirals[i]->Who->Alive()) Admirals.RemoveAt(i);
			else i++;
		}
		if (galaxy.Wars == nullptr) return;
		auto adults = Civilization::Adults(society, jd);
		for (const auto& w : galaxy.Wars->Active())
			for (const auto& f : { w->Aggressor, w->Defender })
			{
				if (f == nullptr || f->IsPirate) continue;
				auto a = AdmiralOf(f);
				if (a != nullptr)
				{
					a->Who->Reputation = cs::Math::Min(1.0, a->Who->Reputation + 0.03 + (Civilization::Has(a->Who, Trait::Brave) ? 0.02 : 0));
					continue;
				}
				cs::Ref<Citizen> best;
				for (const auto& c : adults)
				{
					if (c->House == nullptr || c->House->Home == nullptr || c->House->Home->Owner != f || c->AgeYears(jd) > 65) continue;
					double score = c->Skill + (Civilization::Has(c, Trait::Brave) ? 0.3 : 0) + (Civilization::Has(c, Trait::Ambitious) ? 0.1 : 0);
					double bestScore = best == nullptr ? -1 : best->Skill + (Civilization::Has(best, Trait::Brave) ? 0.3 : 0) + (Civilization::Has(best, Trait::Ambitious) ? 0.1 : 0);
					if (score > bestScore) best = c;
				}
				if (best == nullptr) continue;
				a = cs::New<Admiral>();
				a->Faction = f;
				a->Who = best;
				a->SinceJd = jd;
				Admirals.Add(a);
				best->Reputation = cs::Math::Min(1.0, best->Reputation + 0.2);
				auto e = Civilization::Make(SocialKind::AdmiralRises, jd, best->House->Home, best->House->System);
				e->Who = best;
				e->Faction = f;
				civ.Publish(galaxy, e);
			}
	}

	// ---------------------------------------------------------------- peace sends the prisoners home

	void Strife::React(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			if ((e->Kind != SocialKind::PeaceSummit && e->Kind != SocialKind::PeaceForced) || e->Faction == nullptr || e->Rival == nullptr) continue;
			auto x = Civilization::Make(SocialKind::PrisonersExchanged, jd, e->Where, e->System);
			x->Faction = e->Faction;
			x->Rival = e->Rival;
			x->Amount = 50 + (H(static_cast<std::uint32_t>(e->Id), 0x9A1u) % 1950u);
			civ.Publish(galaxy, x);
			if (auto sc = StatecraftOf(galaxy))
			{
				sc->Sway(e->Faction, e->Rival, 2);
				sc->Sway(e->Rival, e->Faction, 2);
			}
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Strife::Incite(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		if (!Civilization::Stands(s)) return "no such world standing";
		civ.Stir(s, 0.12, -0.25);
		auto e = Civilization::Make(SocialKind::Riot, jd, s, Civilization::SystemOf(galaxy, s));
		e->Steered = true;
		civ.Publish(galaxy, e);
		CallStrike(civ, galaxy, s, nullptr, 10, jd);
		return "";
	}

	cs::String Strife::Arm(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd)
	{
		(void)society;
		if (!Civilization::Stands(s)) return "no such world standing";
		auto cell = InsurgentsOf(civ, s);
		if (cell == nullptr)
		{
			cell = civ.Found(OrgKind::Insurgents, s, Civilization::SystemOf(galaxy, s), s->OccupiedFrom, nullptr, 5e6, jd, H(HashText(s->Name), DayOf(jd)));
			auto e = Civilization::Make(SocialKind::CellFormed, jd, s, cell->System);
			e->Org = cell;
			e->Steered = true;
			civ.Publish(galaxy, e);
		}
		cell->Strength = cs::Math::Min(1.0, cell->Strength + 0.4);
		cell->Funds += 5e6;
		return "";
	}

	cs::String Strife::Spy(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& by, const cs::Ref<Faction>& on, double jd)
	{
		if (by == nullptr || on == nullptr || by == on) return "name two states";
		if (galaxy.Polities == nullptr || galaxy.Polities->CapitalOf(by) == nullptr) return "that state has no capital to run spies from";
		auto agency = AgencyOf(civ, by);
		if (agency == nullptr)
		{
			auto capital = galaxy.Polities->CapitalOf(by);
			agency = civ.Found(OrgKind::Agency, capital, Civilization::SystemOf(galaxy, capital), by, nullptr, 1e7, jd, H(FH(by), DayOf(jd)));
			agency->Strength = 0.4;
		}
		return Operation(civ, galaxy, agency, on, jd, true) ? cs::String() : cs::String("the operation found nothing to take");
	}

	cs::String Strife::Hire(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& company, const cs::Ref<Faction>& by, double jd)
	{
		if (company == nullptr || company->Kind != OrgKind::Mercenaries || !company->Active()) return "no such free company";
		if (by == nullptr) return "no such state";
		auto old = company->Faction;
		company->Faction = by;
		Weary(civ, by, -0.05);
		auto e = Civilization::Make(old != nullptr ? SocialKind::MercsTurned : SocialKind::MercsHired, jd, company->Seat, company->System);
		e->Org = company;
		e->Faction = old != nullptr ? old : by;
		e->Rival = old != nullptr ? by : nullptr;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Walkout, "SpaceGod.Core.Walkout",
		SG_F(Settlement), SG_F(Union), SG_F(StartJd), SG_F(EndJd))

	SG_SERIALIZE(Admiral, "SpaceGod.Core.Admiral",
		SG_F(Faction), SG_F(Who), SG_F(SinceJd))

	SG_SERIALIZE(Strife, "SpaceGod.Core.Strife",
		SG_F(Walkouts), SG_F(Admirals), SG_F(seenEvent), SG_F(nextWearyNewsJd))
}
