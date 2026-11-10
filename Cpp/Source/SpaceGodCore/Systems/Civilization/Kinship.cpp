// US-T277: Kinship (see Systems/Civilization/Kinship.h).

#include "Systems/Civilization/Kinship.h"

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
		const char* const Givens[] = { "Ash", "Belen", "Corin", "Dagny", "Elio", "Fen", "Galen", "Hesper", "Iver", "Juno", "Kasimir", "Lark",
			"Maren", "Nils", "Orla", "Pell", "Quinn", "Rhea", "Silas", "Tamsin", "Ulla", "Vesna", "Wyn", "Xan", "Yusuf", "Zara" };

		cs::Ref<SocialEvent> Ev(SocialKind k, double jd, const cs::Ref<Household>& h, const cs::Ref<Household>& other)
		{
			auto e = Civilization::Make(k, jd, h != nullptr ? h->Home : nullptr, h != nullptr ? h->System : nullptr);
			e->House = h;
			e->OtherHouse = other;
			return e;
		}

		bool Standing(const cs::Ref<Household>& h) { return h != nullptr && !h->Scattered && h->Living() > 0; }

		void Sour(Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, double by)
		{
			if (a->Home == nullptr || b->Home == nullptr || galaxy.Polities == nullptr || galaxy.Polities->Statecraft == nullptr) return;
			auto fa = a->Home->Owner, fb = b->Home->Owner;
			if (fa == nullptr || fb == nullptr || fa == fb) return;
			galaxy.Polities->Statecraft->Sway(fa, fb, by);
			galaxy.Polities->Statecraft->Sway(fb, fa, by);
		}
	}

	cs::Ref<Feud> Kinship::FeudBetween(const cs::Ref<Household>& a, const cs::Ref<Household>& b) const
	{
		for (const auto& f : Feuds)
			if (f->Active() && f->Between(a, b)) return f;
		return nullptr;
	}

	cs::Ref<Feud> Kinship::FeudOf(const cs::Ref<Household>& h) const
	{
		cs::Ref<Feud> best;
		for (const auto& f : Feuds)
			if (f->Active() && (f->A == h || f->B == h) && (best == nullptr || f->Heat > best->Heat)) best = f;
		return best;
	}

	cs::Ref<Feud> Kinship::Start(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, const cs::String& cause, double heat, double jd, bool steered)
	{
		if (!Standing(a) || !Standing(b) || a == b) return nullptr;
		if (auto f = FeudBetween(a, b))
		{
			f->Heat = cs::Math::Min(1.0, f->Heat + heat * 0.5);
			return f;
		}
		auto f = cs::New<Feud>();
		f->Id = civ.NextId++;
		f->A = a;
		f->B = b;
		f->Heat = heat;
		f->Cause = cause;
		f->StartedJd = jd;
		f->Steered = steered;
		Feuds.Add(f);
		// The heads become enemies.
		Civilization::Bind(a->Head(), b->Head(), TieKind::Enemy, heat, jd);
		auto e = Ev(SocialKind::FeudDeclared, jd, a, b);
		e->Who = a->Head();
		e->Whom = b->Head();
		e->Steered = steered;
		civ.Publish(galaxy, e);
		Sour(galaxy, a, b, -1.5);
		return f;
	}

	void Kinship::End(Civilization& civ, Galaxy& galaxy, const cs::Ref<Feud>& f, double jd)
	{
		f->EndedJd = jd;
		if (Standing(f->A) && Standing(f->B))
		{
			Civilization::Unbind(f->A->Head(), f->B->Head());
			civ.Publish(galaxy, Ev(SocialKind::FeudEnded, jd, f->A, f->B));
		}
	}

	void Kinship::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd) { React(civ, galaxy, society, jd); }

	void Kinship::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Feuding(civ, galaxy, jd);
		Straying(civ, galaxy, society, jd);
		Schooling(civ, galaxy, society, jd);
		Renown(civ, galaxy, society, jd);
		// Ended feuds and old affairs leave the books.
		for (int i = 0; i < Feuds.Count();)
		{
			if (!Feuds[i]->Active() && jd - Feuds[i]->EndedJd > 720) Feuds.RemoveAt(i);
			else i++;
		}
		for (int i = 0; i < Affairs.Count();)
		{
			auto& a = Affairs[i];
			bool over = a->Who == nullptr || a->Whom == nullptr || !a->Who->Alive() || !a->Whom->Alive()
				|| (!cs::Double::IsNaN(a->FoundOutJd) && cs::Double::IsNaN(a->HeirDueJd) && jd - a->FoundOutJd > 365);
			if (over) Affairs.RemoveAt(i);
			else i++;
		}
	}

	// ---------------------------------------------------------------- what happened elsewhere: feuds, inheritances, reconciliations

	void Kinship::React(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			std::uint32_t r = H(static_cast<std::uint32_t>(e->Id), 0x4E0Du);
			switch (e->Kind)
			{
				case SocialKind::Buyout:
				case SocialKind::PatentStolen: // US-T283: a stolen invention rankles like a lost firm
				{
					// The bought-out house blames the buyer.
					if (e->House == nullptr || e->OtherHouse == nullptr) break;
					double chance = 0.15 + (Civilization::Has(e->House->Head(), Trait::Vengeful) ? 0.3 : 0) + (Civilization::Has(e->House->Head(), Trait::Cruel) ? 0.1 : 0);
					if (Roll(r, chance)) Start(civ, galaxy, e->House, e->OtherHouse, "a buyout", 0.5, jd, false);
					break;
				}
				case SocialKind::Scandal:
				{
					// The fallen governor's house blames the firm that bought them, and the governor's name is mud.
					if (e->Who != nullptr) e->Who->Reputation = cs::Math::Max(-1.0, e->Who->Reputation - 0.5);
					if (e->House != nullptr && e->OtherHouse != nullptr && Roll(r, 0.3)) Start(civ, galaxy, e->House, e->OtherHouse, "a scandal", 0.45, jd, false);
					break;
				}
				case SocialKind::FallingOut:
				{
					if (e->House == nullptr || e->OtherHouse == nullptr || e->House == e->OtherHouse) break;
					if (Roll(r, Civilization::Has(e->Who, Trait::Vengeful) ? 0.45 : 0.15)) Start(civ, galaxy, e->House, e->OtherHouse, "a falling-out", 0.35, jd, false);
					break;
				}
				case SocialKind::Wedding:
				{
					// A marriage between feuding houses ends the feud.
					if (auto f = FeudBetween(e->House, e->OtherHouse)) End(civ, galaxy, f, jd);
					break;
				}
				case SocialKind::Death:
				{
					if (e->Who != nullptr && e->Who->Role == CitizenRole::Head && e->House != nullptr) Inherit(civ, galaxy, society, e->House, jd);
					break;
				}
				default: break;
			}
		}
	}

	void Kinship::Inherit(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& h, double jd)
	{
		if (!Standing(h)) return;
		auto head = h->Head();
		cs::List<cs::Ref<Citizen>> claimants;
		for (const auto& c : h->Members)
			if (c->Alive() && c != head && c->Role == CitizenRole::Heir && c->AgeYears(jd) >= 18) claimants.Add(c);
		double fortune = h->Wealth + (h->Business != nullptr && h->Business->Trading() ? h->Business->Capital : 0);
		if (claimants.Count() == 0 || head == nullptr || fortune < 5e6) return;
		std::uint32_t r = H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0x1A4Eu);
		auto claimant = Pick(claimants, r);
		double chance = 0.2 + (Civilization::Has(claimant, Trait::Greedy) ? 0.25 : 0) + (Civilization::Has(claimant, Trait::Ambitious) ? 0.15 : 0)
			- (Civilization::Has(claimant, Trait::Loyal) ? 0.15 : 0);
		if (!Roll(r >> 3, chance)) return;
		auto dispute = Ev(SocialKind::InheritanceDispute, jd, h, nullptr);
		dispute->Who = claimant;
		dispute->Whom = head;
		dispute->Amount = fortune;
		civ.Publish(galaxy, dispute);
		Civilization::Bind(claimant, head, TieKind::Rival, 0.6, jd);
		if (!Roll(r >> 9, 0.5)) return;

		// The house splits: the claimant takes a third and founds their own, with a firm of the same trade.
		auto branch = cs::New<Household>();
		branch->Id = society.NextId++;
		branch->Surname = h->Surname;
		branch->Home = h->Home;
		branch->System = h->System;
		branch->FoundedJd = jd;
		branch->NextEventJd = jd + 90 + (r % 120u);
		h->Members.Remove(claimant);
		claimant->House = branch;
		claimant->Role = CitizenRole::Head;
		branch->Members.Add(claimant);
		if (claimant->Spouse != nullptr && claimant->Spouse->House == h)
		{
			h->Members.Remove(claimant->Spouse);
			claimant->Spouse->House = branch;
			claimant->Spouse->Role = CitizenRole::Spouse;
			branch->Members.Add(claimant->Spouse);
		}
		double share = h->Wealth / 3;
		h->Wealth -= share;
		branch->Wealth = share;
		society.Houses.Add(branch);
		if (h->Business != nullptr && h->Business->Trading())
		{
			auto old = h->Business;
			auto b = cs::New<Business>();
			b->Id = society.NextId++;
			b->Kind = old->Kind;
			b->Name = cs::Cat(claimant->Given, " ", h->Surname, Pick({ " & Co.", " Holdings", " Group", " Enterprises" }, r >> 5));
			b->Owner = branch;
			b->Home = branch->Home;
			b->System = branch->System;
			b->Capital = old->Capital / 3;
			old->Capital -= b->Capital;
			b->FoundedJd = jd;
			branch->Business = b;
			society.Businesses.Add(b);
			society.Founded++;
		}
		auto split = Ev(SocialKind::HouseSplit, jd, h, branch);
		split->Who = claimant;
		split->Amount = share;
		civ.Publish(galaxy, split);
		if (Roll(r >> 13, 0.5)) Start(civ, galaxy, branch, h, "an inheritance", 0.4, jd, false);
	}

	// ---------------------------------------------------------------- feuds run their course

	void Kinship::Feuding(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& f : Feuds.ToArray())
		{
			if (!f->Active()) continue;
			if (!Standing(f->A) || !Standing(f->B)) { f->EndedJd = jd; continue; }
			f->Heat -= 0.03 + (Civilization::Has(f->A->Head(), Trait::Loyal) || Civilization::Has(f->B->Head(), Trait::Honest) ? 0.02 : 0);
			if (f->Heat < 0.08) { End(civ, galaxy, f, jd); continue; }
			std::uint32_t r = H(static_cast<std::uint32_t>(f->Id), DayOf(jd), 0x5AB0u);
			if (!Roll(r, f->Heat * 0.3)) continue;
			// One side strikes at the other's business.
			bool aStrikes = Roll(r >> 4, 0.5);
			auto by = aStrikes ? f->A : f->B, at = aStrikes ? f->B : f->A;
			if (at->Business == nullptr || !at->Business->Trading()) continue;
			double loss = at->Business->Capital * (0.03 + 0.05 * Unit(r >> 8));
			at->Business->Capital -= loss;
			f->Incidents++;
			f->Heat = cs::Math::Min(1.0, f->Heat + 0.05);
			auto e = Ev(SocialKind::FeudSabotage, jd, at, by);
			e->Business = at->Business;
			e->Amount = loss;
			civ.Publish(galaxy, e);
			Sour(galaxy, by, at, -0.5);
		}
	}

	// ---------------------------------------------------------------- affairs, divorces, hidden heirs

	void Kinship::Straying(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto adults = Civilization::Adults(society, jd);
		// New affairs: the married charming or reckless, with someone married of another house on the same world.
		for (const auto& a : adults)
		{
			if (a->Spouse == nullptr || a->House == nullptr) continue;
			double chance = (Civilization::Has(a, Trait::Charming) ? 0.006 : 0) + (Civilization::Has(a, Trait::Reckless) ? 0.006 : 0) - (Civilization::Has(a, Trait::Loyal) ? 0.01 : 0);
			std::uint32_t r = H(static_cast<std::uint32_t>(a->Id), DayOf(jd), 0xAFFu);
			if (chance <= 0 || !Roll(r, chance) || Affairs.Count() >= 40) continue;
			cs::List<cs::Ref<Citizen>> near;
			for (const auto& b : adults)
				if (b != a && b != a->Spouse && b->House != a->House && b->House != nullptr && b->House->Home == a->House->Home && b->AgeYears(jd) < 60) near.Add(b);
			if (near.Count() == 0) continue;
			auto b = Pick(near, r >> 6);
			bool already = false;
			for (const auto& x : Affairs)
				if ((x->Who == a || x->Whom == a) && cs::Double::IsNaN(x->FoundOutJd)) already = true;
			if (already) continue;
			auto af = cs::New<Affair>();
			af->Who = a;
			af->Whom = b;
			af->StartedJd = jd;
			Affairs.Add(af);
			Civilization::Bind(a, b, TieKind::Lover, 0.7, jd);
			auto e = Ev(SocialKind::Affair, jd, a->House, b->House);
			e->Who = a;
			e->Whom = b;
			civ.Publish(galaxy, e);
		}
		// Affairs come out; children of them come forward.
		for (const auto& af : Affairs.ToArray())
		{
			std::uint32_t r = H(static_cast<std::uint32_t>(af->Who->Id), static_cast<std::uint32_t>(af->Whom->Id), DayOf(jd));
			if (cs::Double::IsNaN(af->FoundOutJd))
			{
				if (!Roll(r, 0.08)) continue;
				af->FoundOutJd = jd;
				auto betrayed = af->Who->Spouse;
				if (Roll(r >> 3, 0.5) && betrayed != nullptr)
				{
					betrayed->Spouse = nullptr;
					af->Who->Spouse = nullptr;
					auto e = Ev(SocialKind::Divorce, jd, af->Who->House, betrayed->House);
					e->Who = af->Who;
					e->Whom = betrayed;
					civ.Publish(galaxy, e);
					// A marriage across flags had warmed them; its end cools them again.
					if (af->Who->House != nullptr && betrayed->House != nullptr) Sour(galaxy, af->Who->House, betrayed->House, -2);
					Civilization::Bind(af->Who, betrayed, TieKind::Enemy, 0.6, jd);
				}
				if (betrayed != nullptr && betrayed->House != nullptr && af->Whom->House != nullptr && betrayed->House != af->Whom->House
					&& Roll(r >> 7, Civilization::Has(betrayed, Trait::Vengeful) ? 0.6 : 0.25))
					Start(civ, galaxy, betrayed->House, af->Whom->House, "an affair", 0.55, jd, false);
				if (Roll(r >> 11, 0.15)) af->HeirDueJd = jd + 365 * (2 + (r % 4u));
			}
			else if (!cs::Double::IsNaN(af->HeirDueJd) && jd >= af->HeirDueJd)
			{
				af->HeirDueJd = cs::Double::NaN;
				auto h = af->Who->House;
				if (!Standing(h)) continue;
				auto c = cs::New<Citizen>();
				c->Id = society.NextId++;
				c->Given = Pick(Givens, r);
				c->House = h;
				c->Role = CitizenRole::Heir;
				c->BornJd = af->StartedJd;
				h->Members.Add(c);
				Civilization::Bind(c, af->Who, TieKind::Patron, 0.5, jd);
				auto e = Ev(SocialKind::HiddenHeir, jd, h, nullptr);
				e->Who = c;
				e->Whom = af->Who;
				civ.Publish(galaxy, e);
			}
		}
	}

	// ---------------------------------------------------------------- schools

	void Kinship::Schooling(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto schools = civ.OrgsOf(OrgKind::School);
		// The big learned worlds found academies, one each.
		if (schools.Count() < MaxSchools)
			for (const auto& c : civ.Worlds)
			{
				if (c->Learning < 0.4 || c->Where->Population < 1e6) continue;
				bool has = false;
				for (const auto& s : schools)
					if (s->Seat == c->Where) has = true;
				if (has) continue;
				std::uint32_t r = H(HashText(c->Where->Name), DayOf(jd), 0x5C0u);
				if (!Roll(r, 0.05)) continue;
				// A learned member of a house there is its first master.
				cs::Ref<Citizen> master;
				for (const auto& h : society.HousesOf(c->Where))
					for (const auto& m : h->Members)
						if (m->Alive() && m->AgeYears(jd) > 35 && (master == nullptr || m->Skill > master->Skill)) master = m;
				auto o = civ.Found(OrgKind::School, c->Where, c->System, c->Where->Owner, master, 2e6, jd, r);
				o->Strength = c->Learning;
				auto e = Civilization::Make(SocialKind::SchoolFounded, jd, c->Where, c->System);
				e->Org = o;
				e->Who = master;
				civ.Publish(galaxy, e);
				schools.Add(o);
				break;
			}
		if (schools.Count() == 0) return;
		// The young of houses with money go to study; after four years they graduate abler.
		for (const auto& h : society.Houses)
		{
			if (!Standing(h) || h->Wealth < 1e6) continue;
			for (const auto& c : h->Members)
			{
				if (!c->Alive()) continue;
				double age = c->AgeYears(jd);
				cs::Ref<Org> at;
				for (const auto& o : c->Orgs)
					if (o != nullptr && o->Kind == OrgKind::School && o->Active()) at = o;
				if (at == nullptr && age >= 16 && age < 17 && c->Skill < 0.95)
				{
					// The nearest school: on their own world, else the strongest under their flag, else any.
					cs::Ref<Org> pick;
					for (const auto& o : schools)
						if (o->Seat == h->Home) pick = o;
					if (pick == nullptr)
						for (const auto& o : schools)
							if (o->Faction == (h->Home != nullptr ? h->Home->Owner : nullptr) && (pick == nullptr || o->Strength > pick->Strength)) pick = o;
					if (pick == nullptr) pick = Pick(schools, H(static_cast<std::uint32_t>(c->Id), 0x5C1u));
					pick->Members.Add(c);
					c->Orgs.Add(pick);
					double fees = cs::Math::Min(h->Wealth * 0.05, 2e5);
					h->Wealth -= fees;
					pick->Funds += fees;
				}
				else if (at != nullptr && age >= 16 + StudyYears)
				{
					c->Skill = cs::Math::Min(1.0, c->Skill + 0.1 + 0.15 * at->Strength);
					c->Orgs.Remove(at);
					at->Members.Remove(c);
					at->Score++;
					if (at->Leader != nullptr && at->Leader->Alive()) Civilization::Bind(at->Leader, c, TieKind::Mentor, 0.5, jd);
					auto e = Civilization::Make(SocialKind::Graduation, jd, at->Seat, at->System);
					e->Who = c;
					e->House = h;
					e->Org = at;
					civ.Publish(galaxy, e);
				}
			}
			// An able head runs a better business: a small monthly edge (or drag) on its capital.
			auto head = h->Head();
			if (head != nullptr && h->Business != nullptr && h->Business->Trading()) h->Business->Capital *= 1 + (head->Skill - 0.5) * 0.002;
		}
	}

	// ---------------------------------------------------------------- the famous

	void Kinship::Renown(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : Civilization::Adults(society, jd))
		{
			std::uint32_t r = H(static_cast<std::uint32_t>(c->Id), DayOf(jd), 0xFA3Eu);
			double before = c->Reputation;
			double gain = (Civilization::Has(c, Trait::Charming) ? 0.01 : 0) + (Civilization::Has(c, Trait::Gifted) ? 0.01 : 0) + (c->Goal == Ambition::Fame ? 0.015 : 0)
				+ (c->Skill > 0.8 ? 0.005 : 0);
			c->Reputation = cs::Math::Clamp(c->Reputation + gain * (0.5 + Unit(r)) - (c->Reputation > 0 ? 0.004 : -0.004), -1.0, 1.0);
			auto home = c->House != nullptr ? c->House->Home : nullptr;
			if (before < FameThreshold && c->Reputation >= FameThreshold)
			{
				auto e = Civilization::Make(SocialKind::CelebrityRise, jd, home, c->House->System);
				e->Who = c;
				e->House = c->House;
				civ.Publish(galaxy, e);
			}
			if (c->Reputation < FameThreshold) continue;
			// The famous fall now and then.
			if (Roll(r >> 4, 0.015 + (Civilization::Has(c, Trait::Reckless) ? 0.03 : 0)))
			{
				c->Reputation -= 0.7;
				auto e = Civilization::Make(SocialKind::CelebrityScandal, jd, home, c->House->System);
				e->Who = c;
				e->House = c->House;
				civ.Publish(galaxy, e);
				continue;
			}
			// And lend their names to the commercials.
			if (!Roll(r >> 9, 0.2)) continue;
			cs::List<cs::Ref<Business>> brands;
			for (const auto& b : society.Businesses)
				if (b->Trading() && b->Home == home && (b->Kind == BusinessKind::Hospitality || b->Kind == BusinessKind::Media || b->Kind == BusinessKind::TradingHouse || b->Kind == BusinessKind::Shipwright)) brands.Add(b);
			if (brands.Count() == 0) continue;
			auto b = Pick(brands, r >> 13);
			double fee = cs::Math::Min(b->Capital * 0.01, 5e5);
			b->Capital += fee * 2; // the custom it brings outweighs the fee
			if (c->House != nullptr) c->House->Wealth += fee;
			auto e = Civilization::Make(SocialKind::Endorsement, jd, home, c->House->System);
			e->Who = c;
			e->Business = b;
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Kinship::Declare(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, double jd)
	{
		if (!Standing(a) || !Standing(b)) return "no such house standing";
		if (a == b) return "a house cannot feud with itself";
		auto f = Start(civ, galaxy, a, b, "a whisper in the right ear", 0.7, jd, true);
		return f != nullptr ? cs::String() : cs::String("they would not quarrel");
	}

	cs::String Kinship::Reconcile(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& a, const cs::Ref<Household>& b, double jd)
	{
		auto f = FeudBetween(a, b);
		if (f == nullptr) return "those houses are not feuding";
		End(civ, galaxy, f, jd);
		return "";
	}

	cs::String Kinship::Fame(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& c, double jd)
	{
		if (c == nullptr || !c->Alive()) return "no such person living";
		if (c->Reputation >= FameThreshold) return "they are famous already";
		c->Reputation = 0.85;
		auto e = Civilization::Make(SocialKind::CelebrityRise, jd, c->House != nullptr ? c->House->Home : nullptr, c->House != nullptr ? c->House->System : nullptr);
		e->Who = c;
		e->House = c->House;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Kinship::Seduce(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b, double jd)
	{
		if (a == nullptr || b == nullptr || !a->Alive() || !b->Alive() || a == b) return "no such pair living";
		if (a->AgeYears(jd) < 18 || b->AgeYears(jd) < 18) return "only grown people";
		if (a->Spouse == b) return "they are married to each other";
		auto af = cs::New<Affair>();
		af->Who = a->Spouse != nullptr ? a : b;
		af->Whom = af->Who == a ? b : a;
		af->StartedJd = jd;
		Affairs.Add(af);
		Civilization::Bind(a, b, TieKind::Lover, 0.8, jd);
		auto e = Ev(SocialKind::Affair, jd, af->Who->House, af->Whom->House);
		e->Who = af->Who;
		e->Whom = af->Whom;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Feud, "SpaceGod.Core.Feud",
		SG_F(Id), SG_F(A), SG_F(B), SG_F(Heat), SG_F(Cause), SG_F(Incidents), SG_F(StartedJd), SG_F(EndedJd), SG_F(Steered))

	SG_SERIALIZE(Affair, "SpaceGod.Core.Affair",
		SG_F(Who), SG_F(Whom), SG_F(StartedJd), SG_F(FoundOutJd), SG_F(HeirDueJd))

	SG_SERIALIZE(Kinship, "SpaceGod.Core.Kinship",
		SG_F(Feuds), SG_F(Affairs), SG_F(seenEvent))
}
