// US-T278: Politics (see Systems/Civilization/Politics.h).

#include "Systems/Civilization/Politics.h"

#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/Statecraft.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		cs::Ref<Statecraft> StatecraftOf(Galaxy& galaxy) { return galaxy.Polities != nullptr ? galaxy.Polities->Statecraft : nullptr; }

		cs::Ref<Settlement> CapitalOf(Galaxy& galaxy, const cs::Ref<Faction>& f) { return galaxy.Polities != nullptr ? galaxy.Polities->CapitalOf(f) : nullptr; }

		std::uint32_t FactionHash(const cs::Ref<Faction>& f) { return f != nullptr ? HashText(f->Name) : 0u; }

		cs::Ref<SocialEvent> Ev(Civilization& civ, Galaxy& galaxy, SocialKind k, double jd, const cs::Ref<Faction>& f)
		{
			auto capital = CapitalOf(galaxy, f);
			auto e = Civilization::Make(k, jd, capital, Civilization::SystemOf(galaxy, capital));
			e->Faction = f;
			(void)civ;
			return e;
		}

		/// Every state that rules people: owns a standing settlement and is no pirate band.
		bool Ruling(const cs::Ref<Faction>& f) { return f != nullptr && !f->IsPirate; }
	}

	cs::Ref<Regime> Politics::RegimeOf(const cs::Ref<Faction>& f) const
	{
		for (const auto& r : Regimes)
			if (r->Faction == f) return r;
		return nullptr;
	}

	cs::Ref<Trial> Politics::TrialById(int id) const
	{
		for (const auto& t : Trials)
			if (t->Id == id) return t;
		return nullptr;
	}

	cs::Ref<Citizen> Politics::Heir(Galaxy& galaxy, Society& society, const cs::Ref<Faction>& f, double jd) const
	{
		// The head of the richest house at its capital (else anywhere under its flag).
		auto capital = CapitalOf(galaxy, f);
		cs::Ref<Household> best;
		for (const auto& h : society.HousesOf(nullptr))
		{
			if (h->Home == nullptr || h->Home->Owner != f) continue;
			bool atCapital = h->Home == capital;
			bool bestAtCapital = best != nullptr && best->Home == capital;
			double w = h->Wealth + (h->Business != nullptr && h->Business->Trading() ? h->Business->Capital : 0);
			double bw = best == nullptr ? -1 : best->Wealth + (best->Business != nullptr && best->Business->Trading() ? best->Business->Capital : 0);
			if (best == nullptr || (atCapital && !bestAtCapital) || (atCapital == bestAtCapital && w > bw)) best = h;
		}
		(void)jd;
		return best != nullptr ? best->Head() : nullptr;
	}

	void Politics::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)society;
		React(civ, galaxy, jd);
		Courts(civ, galaxy, jd);
	}

	void Politics::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Census(civ, galaxy, society, jd);
		Ideology(civ, galaxy, jd);
		Parties(civ, galaxy, society, jd);
		Elections(civ, galaxy, jd);
		Coups(civ, galaxy, society, jd);
		PeaceTalks(civ, galaxy, jd);
		Polls(civ, galaxy, jd);
		if (Trials.Count() > 150) Trials.RemoveRange(0, Trials.Count() - 150);
		for (int i = 0; i < Summits.Count();)
		{
			if (jd - Summits[i]->Jd > TalksCooldownDays) Summits.RemoveAt(i);
			else i++;
		}
	}

	// ---------------------------------------------------------------- the states and their people's mood

	void Politics::Census(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		cs::List<cs::Ref<Faction>> seen;
		for (const auto& c : civ.Worlds)
		{
			auto f = c->Where->Owner;
			if (!Ruling(f) || seen.Contains(f)) continue;
			seen.Add(f);
			auto r = RegimeOf(f);
			if (r == nullptr)
			{
				r = cs::New<Regime>();
				r->Faction = f;
				Regimes.Add(r);
			}
			// Weighted by people: the big worlds speak loudest.
			double pop = 0, hawk = 0, liberty = 0, approval = 0;
			for (const auto& w : civ.Worlds)
				if (w->Where->Owner == f)
				{
					double p = cs::Math::Max(1.0, w->Where->Population);
					pop += p;
					hawk += w->Hawk * p;
					liberty += w->Liberty * p;
					approval += w->Approval * p;
				}
			r->Hawk = hawk / pop;
			r->Liberty = liberty / pop;
			r->Approval = approval / pop;
			bool crisis = !cs::Double::IsNaN(r->CrisisUntil);
			if ((r->Ruler == nullptr || !r->Ruler->Alive()) && !crisis) r->Ruler = Heir(galaxy, society, f, jd);
			if (crisis && jd >= r->CrisisUntil)
			{
				r->CrisisUntil = cs::Double::NaN;
				r->Ruler = r->Ruling != nullptr && r->Ruling->Leader != nullptr && r->Ruling->Leader->Alive() ? r->Ruling->Leader : Heir(galaxy, society, f, jd);
			}
			if (r->Liberty > 0.1 && cs::Double::IsNaN(r->NextElectionJd)) r->NextElectionJd = jd + 180 + (FactionHash(f) % 900u);
			if (r->Liberty < -0.1) r->NextElectionJd = cs::Double::NaN;
		}
	}

	void Politics::Ideology(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			bool war = Civilization::AtWar(galaxy, c->Where->Owner);
			double dHawk = war ? (c->Approval > 0 ? 0.02 : 0) - c->Weariness * 0.04 : -0.01 * c->Hawk;
			double dLiberty = (Civilization::Wellbeing(c->Where) - 0.7) * 0.03 - c->Where->Unrest * 0.03 + c->Learning * 0.005;
			c->Hawk = cs::Math::Clamp(c->Hawk + dHawk, -1.0, 1.0);
			c->Liberty = cs::Math::Clamp(c->Liberty + dLiberty, -1.0, 1.0);
		}
		auto sc = StatecraftOf(galaxy);
		for (const auto& r : Regimes)
		{
			int leaning = r->Hawk > 0.35 ? 1 : (r->Hawk < -0.35 ? -1 : 0);
			if (leaning != r->Leaning && leaning != 0)
			{
				auto e = Ev(civ, galaxy, SocialKind::IdeologyShift, jd, r->Faction);
				e->Amount = r->Hawk;
				civ.Publish(galaxy, e);
			}
			r->Leaning = leaning;
			if (sc == nullptr || galaxy.Polities == nullptr || leaning == 0) continue;
			// A hawkish state cools toward everyone, a dovish one warms.
			cs::Ref<Faction> leastLiked;
			double least = 1e9;
			for (const auto& g : galaxy.Polities->Factions())
			{
				if (g == r->Faction || !Statecraft::Party(g)) continue;
				sc->Sway(r->Faction, g, leaning > 0 ? -0.4 : 0.4);
				double o = sc->Opinion(r->Faction, g);
				if (!g->IsPirate && o < least && !Civilization::Hostile(galaxy, r->Faction, g)) { least = o; leastLiked = g; }
			}
			// A strongly hawkish state, already loathing a neighbour, goes to war with it now and then.
			if (r->Hawk > 0.6 && leastLiked != nullptr && least < -50 && !Civilization::AtWar(galaxy, r->Faction)
				&& Roll(H(FactionHash(r->Faction), DayOf(jd), 0x4A3Bu), 0.05))
				sc->DeclareWar(r->Faction, leastLiked, jd);
		}
	}

	// ---------------------------------------------------------------- parties and elections

	void Politics::Parties(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& r : Regimes)
		{
			if (r->Liberty <= 0.1) continue;
			int have = 0;
			for (const auto& o : civ.OrgsOf(OrgKind::Party))
				if (o->Faction == r->Faction) have++;
			if (have >= 3) continue;
			std::uint32_t h = H(FactionHash(r->Faction), DayOf(jd), 0x9A27u);
			if (have >= 2 && !Roll(h, 0.03)) continue;
			// Founded by a power-hungry head of a house under its flag.
			cs::Ref<Citizen> founder;
			for (const auto& hh : society.HousesOf(nullptr))
			{
				auto head = hh->Head();
				if (hh->Home == nullptr || hh->Home->Owner != r->Faction || head == nullptr) continue;
				bool inParty = false;
				for (const auto& o : head->Orgs)
					if (o != nullptr && o->Kind == OrgKind::Party && o->Active()) inParty = true;
				if (inParty) continue;
				if (founder == nullptr || (head->Goal == Ambition::Power && founder->Goal != Ambition::Power) || (head->Goal == founder->Goal && hh->Wealth > founder->House->Wealth)) founder = head;
			}
			if (founder == nullptr) continue;
			auto seat = founder->House->Home;
			auto o = civ.Found(OrgKind::Party, seat, founder->House->System, r->Faction, founder, founder->House->Wealth * 0.1, jd, h);
			founder->House->Wealth *= 0.9;
			// The first party takes the people's side; the next the other.
			o->Stance = have == 0 ? r->Hawk : -r->Hawk + (Civilization::Has(founder, Trait::Brave) ? 0.3 : -0.1);
			o->Stance = cs::Math::Clamp(o->Stance, -1.0, 1.0);
			o->Strength = 0.3;
			auto e = Civilization::Make(SocialKind::PartyFounded, jd, seat, o->System);
			e->Org = o;
			e->Who = founder;
			civ.Publish(galaxy, e);
		}
		// Rich houses whose heads want power join and fund a party under their flag.
		for (const auto& hh : society.HousesOf(nullptr))
		{
			auto head = hh->Head();
			if (head == nullptr || head->Goal != Ambition::Power || hh->Home == nullptr) continue;
			bool inParty = false;
			for (const auto& o : head->Orgs)
				if (o != nullptr && o->Kind == OrgKind::Party && o->Active()) { inParty = true; double gift = hh->Wealth * 0.02; hh->Wealth -= gift; o->Funds += gift; }
			if (inParty) continue;
			cs::List<cs::Ref<Org>> mine;
			for (const auto& o : civ.OrgsOf(OrgKind::Party))
				if (o->Faction == hh->Home->Owner) mine.Add(o);
			if (mine.Count() == 0) continue;
			auto o = Pick(mine, H(static_cast<std::uint32_t>(head->Id), 0x9A28u));
			o->Members.Add(head);
			head->Orgs.Add(o);
		}
	}

	void Politics::Elections(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& r : Regimes)
		{
			if (cs::Double::IsNaN(r->NextElectionJd)) continue;
			if (!r->Called && jd >= r->NextElectionJd - 30)
			{
				r->Called = true;
				civ.Publish(galaxy, Ev(civ, galaxy, SocialKind::ElectionCalled, jd, r->Faction));
			}
			if (jd < r->NextElectionJd) continue;
			r->Called = false;
			r->NextElectionJd = jd + TermDays;
			cs::List<cs::Ref<Org>> parties;
			for (const auto& o : civ.OrgsOf(OrgKind::Party))
				if (o->Faction == r->Faction) parties.Add(o);
			if (parties.Count() == 0) continue;
			// The vote: how near each party stands to the people, how much money it spends, how the people feel about the rulers.
			double total = 0;
			cs::List<double> score;
			std::uint32_t h = H(FactionHash(r->Faction), DayOf(jd), 0xE1EC7u);
			cs::Ref<Org> rigger;
			for (int i = 0; i < parties.Count(); i++)
			{
				auto& o = parties[i];
				double s = 1.2 - cs::Math::Abs(o->Stance - r->Hawk) + cs::Math::Min(1.0, o->Funds / 5e7) * 0.6 + 0.4 * Unit(H(h, static_cast<std::uint32_t>(i)));
				if (o == r->Ruling) s += r->Approval * 0.5;
				// A party led by the greedy or the cruel may buy the count.
				if (o->Leader != nullptr && (Civilization::Has(o->Leader, Trait::Greedy) || Civilization::Has(o->Leader, Trait::Cruel)) && o->Funds > 1e6 && Roll(H(h, 0xB0Bu, static_cast<std::uint32_t>(i)), 0.25))
				{
					s += 0.6;
					o->Funds *= 0.7;
					rigger = o;
				}
				s = cs::Math::Max(0.05, s);
				score.Add(s);
				total += s;
			}
			int best = 0;
			for (int i = 1; i < parties.Count(); i++)
				if (score[i] > score[best]) best = i;
			for (int i = 0; i < parties.Count(); i++) parties[i]->Strength = score[i] / total;
			r->Elections++;
			Win(civ, galaxy, r, parties[best], score[best] / total, jd, false);
			// A bought count may come out: the people take to the streets.
			if (rigger != nullptr && Roll(h >> 7, 0.4))
			{
				auto e = Ev(civ, galaxy, SocialKind::ElectionRigged, jd, r->Faction);
				e->Org = rigger;
				civ.Publish(galaxy, e);
				for (const auto& w : civ.Worlds)
					if (w->Where->Owner == r->Faction) civ.Stir(w->Where, 0.06, -0.15);
				if (rigger->Leader != nullptr) Charge(civ, galaxy, rigger->Leader, SocialKind::ElectionRigged, jd);
			}
		}
	}

	void Politics::Win(Civilization& civ, Galaxy& galaxy, const cs::Ref<Regime>& r, const cs::Ref<Org>& party, double share, double jd, bool steered)
	{
		r->Ruling = party;
		party->Score++;
		if (party->Leader != nullptr && party->Leader->Alive()) r->Ruler = party->Leader;
		auto e = Ev(civ, galaxy, SocialKind::ElectionWon, jd, r->Faction);
		e->Org = party;
		e->Who = party->Leader;
		e->Amount = share;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		// The winners' line: doves warm the state to its enemies, hawks cool it.
		auto sc = StatecraftOf(galaxy);
		if (sc == nullptr || galaxy.Wars == nullptr) return;
		for (const auto& w : galaxy.Wars->Active())
			if (w->Involves(r->Faction)) sc->Sway(r->Faction, w->Enemy(r->Faction), party->Stance < 0 ? 6 : -4);
		// The people's approval takes heart from a fresh government.
		for (const auto& c : civ.Worlds)
			if (c->Where->Owner == r->Faction) c->Approval = cs::Math::Min(1.0, c->Approval + 0.1);
	}

	// ---------------------------------------------------------------- coups and successions

	void Politics::Coups(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& r : Regimes)
		{
			double unrest = 0, n = 0;
			for (const auto& c : civ.Worlds)
				if (c->Where->Owner == r->Faction) { unrest += c->Where->Unrest; n++; }
			if (n == 0) continue;
			unrest /= n;
			double risk = unrest * 0.5 + (r->Approval < -0.3 ? 0.3 : 0) + (r->Liberty < 0 ? 0.1 : -0.1) + (!cs::Double::IsNaN(r->CrisisUntil) ? 0.3 : 0);
			std::uint32_t h = H(FactionHash(r->Faction), DayOf(jd), 0xC0Bu);
			if (risk <= 0 || !Roll(h, risk * 0.03)) continue;
			// The plotter: someone under the flag who wants power (an admiral if Strife has made one, else a power-hungry head).
			cs::Ref<Citizen> plotter;
			for (const auto& c : Civilization::Adults(society, jd))
			{
				if (c == r->Ruler || c->House == nullptr || c->House->Home == nullptr || c->House->Home->Owner != r->Faction) continue;
				if (c->Goal != Ambition::Power && !Civilization::Has(c, Trait::Ambitious)) continue;
				if (plotter == nullptr || c->Reputation + c->Skill > plotter->Reputation + plotter->Skill) plotter = c;
			}
			if (plotter == nullptr) continue;
			double odds = 0.45 + plotter->Skill * 0.3 - r->Approval * 0.4 + (Civilization::Has(plotter, Trait::Brave) ? 0.1 : 0);
			if (Roll(h >> 5, odds)) Seize(civ, galaxy, r, plotter, jd, false);
			else
			{
				auto e = Ev(civ, galaxy, SocialKind::CoupFailed, jd, r->Faction);
				e->Who = plotter;
				civ.Publish(galaxy, e);
				Charge(civ, galaxy, plotter, SocialKind::CoupFailed, jd);
				if (r->Ruler != nullptr) Civilization::Bind(r->Ruler, plotter, TieKind::Enemy, 0.9, jd);
			}
		}
	}

	void Politics::Seize(Civilization& civ, Galaxy& galaxy, const cs::Ref<Regime>& r, const cs::Ref<Citizen>& plotter, double jd, bool steered)
	{
		auto old = r->Ruler;
		r->Ruler = plotter;
		r->Ruling = nullptr;
		r->CrisisUntil = cs::Double::NaN;
		r->NextElectionJd = cs::Double::NaN;
		r->Called = false;
		r->Coups++;
		plotter->Reputation = cs::Math::Min(1.0, plotter->Reputation + 0.3);
		if (old != nullptr) Civilization::Bind(old, plotter, TieKind::Enemy, 1.0, jd);
		for (const auto& c : civ.Worlds)
			if (c->Where->Owner == r->Faction)
			{
				c->Liberty = cs::Math::Max(-1.0, c->Liberty - 0.3);
				civ.Stir(c->Where, 0.08, -0.1);
			}
		auto e = Ev(civ, galaxy, SocialKind::Coup, jd, r->Faction);
		e->Who = plotter;
		e->Whom = old;
		e->Steered = steered;
		civ.Publish(galaxy, e);
	}

	// ---------------------------------------------------------------- the courts

	cs::Ref<Trial> Politics::Charge(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& accused, SocialKind charge, double jd, bool steered)
	{
		if (accused == nullptr || !accused->Alive()) return nullptr;
		for (const auto& t : Trials)
			if (t->Accused == accused && !t->Decided) return t;
		auto t = cs::New<Trial>();
		t->Id = civ.NextId++;
		t->Accused = accused;
		t->Where = accused->House != nullptr ? accused->House->Home : nullptr;
		t->Charge = charge;
		t->OpenedJd = jd;
		t->VerdictJd = jd + 30 + (H(static_cast<std::uint32_t>(t->Id), 0x7E1u) % 60u);
		t->Steered = steered;
		Trials.Add(t);
		auto e = Civilization::Make(SocialKind::TrialOpened, jd, t->Where, accused->House != nullptr ? accused->House->System : nullptr);
		e->Who = accused;
		e->House = accused->House;
		civ.Publish(galaxy, e);
		return t;
	}

	void Politics::Courts(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& t : Trials)
		{
			if (t->Decided || jd < t->VerdictJd) continue;
			t->Decided = true;
			auto c = t->Accused;
			if (c == nullptr || !c->Alive()) continue;
			std::uint32_t h = H(static_cast<std::uint32_t>(t->Id), DayOf(jd), 0x7E2u);
			auto house = c->House;
			auto sys = house != nullptr ? house->System : nullptr;
			// A rich house whose accused is no honest soul buys the judge, now and then.
			bool canBuy = house != nullptr && house->Wealth > 2e6 && !Civilization::Has(c, Trait::Honest);
			if (canBuy && Roll(h, 0.3 + (Civilization::Has(c, Trait::Greedy) ? 0.2 : 0)))
			{
				t->Bought = true;
				house->Wealth -= cs::Math::Min(house->Wealth * 0.15, 2e7);
				if (auto w = civ.CivicsOf(t->Where)) w->Crime = cs::Math::Min(1.0, w->Crime + 0.03);
				auto e = Civilization::Make(SocialKind::JudgeBribed, jd, t->Where, sys);
				e->Who = c;
				e->House = house;
				civ.Publish(galaxy, e);
				continue;
			}
			t->Guilty = Roll(h >> 6, 0.7);
			if (!t->Guilty) continue;
			c->Reputation = cs::Math::Max(-1.0, c->Reputation - 0.4);
			if (house != nullptr) house->Wealth *= 0.85;
			auto e = Civilization::Make(SocialKind::Verdict, jd, t->Where, sys);
			e->Who = c;
			e->House = house;
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- peace talks

	bool Politics::Meet(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, bool force)
	{
		auto sc = StatecraftOf(galaxy);
		if (sc == nullptr) return false;
		auto talks = cs::New<Talks>();
		talks->A = a;
		talks->B = b;
		talks->Jd = jd;
		Summits.Add(talks);
		// A neutral host: the most populous world of a third state at war with neither, else the defender's capital.
		cs::Ref<Settlement> host;
		for (const auto& c : civ.Worlds)
		{
			auto o = c->Where->Owner;
			if (o == a || o == b || Civilization::Hostile(galaxy, o, a) || Civilization::Hostile(galaxy, o, b)) continue;
			if (host == nullptr || c->Where->Population > host->Population) host = c->Where;
		}
		if (host == nullptr) host = CapitalOf(galaxy, b);
		auto ra = RegimeOf(a), rb = RegimeOf(b);
		double weary = 0, hawk = 0;
		int n = 0;
		for (const auto& c : civ.Worlds)
			if (c->Where->Owner == a || c->Where->Owner == b) { weary += c->Weariness; n++; }
		if (n > 0) weary /= n;
		hawk = ((ra != nullptr ? ra->Hawk : 0) + (rb != nullptr ? rb->Hawk : 0)) / 2;
		double odds = 0.35 + weary * 0.6 - hawk * 0.4;
		bool ok = force || Roll(H(FactionHash(a), FactionHash(b), DayOf(jd)), odds);
		auto e = Civilization::Make(ok ? SocialKind::PeaceSummit : SocialKind::SummitFailed, jd, host, Civilization::SystemOf(galaxy, host));
		e->Faction = a;
		e->Rival = b;
		e->Who = ra != nullptr ? ra->Ruler : nullptr;
		e->Whom = rb != nullptr ? rb->Ruler : nullptr;
		e->Steered = force;
		civ.Publish(galaxy, e);
		if (ok) sc->MakePeace(a, b, jd, cs::Cat("a peace summit on ", host != nullptr ? host->Name : cs::String("neutral ground")));
		else
		{
			sc->Sway(a, b, -3);
			sc->Sway(b, a, -3);
		}
		return ok;
	}

	void Politics::PeaceTalks(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (galaxy.Wars == nullptr) return;
		for (const auto& w : galaxy.Wars->Active().ToArray())
		{
			auto a = w->Aggressor, b = w->Defender;
			if (!Ruling(a) || !Ruling(b)) continue;
			bool recent = false;
			for (const auto& t : Summits)
				if ((t->A == a && t->B == b) || (t->A == b && t->B == a)) recent = true;
			if (recent) continue;
			double weary = 0;
			int n = 0;
			for (const auto& c : civ.Worlds)
				if (c->Where->Owner == a || c->Where->Owner == b) { weary += c->Weariness; n++; }
			if (n == 0) continue;
			weary /= n;
			auto ra = RegimeOf(a), rb = RegimeOf(b);
			bool doves = (ra != nullptr && ra->Ruling != nullptr && ra->Ruling->Stance < 0) || (rb != nullptr && rb->Ruling != nullptr && rb->Ruling->Stance < 0);
			if (weary < 0.35 && !doves) continue;
			if (!Roll(H(FactionHash(a), FactionHash(b), DayOf(jd), 0x9EACEu), 0.12 + (doves ? 0.1 : 0))) continue;
			Meet(civ, galaxy, a, b, jd, false);
		}
	}

	// ---------------------------------------------------------------- polls

	void Politics::Polls(Civilization& civ, Galaxy& galaxy, double jd)
	{
		int quarter = static_cast<int>(cs::Math::Floor(jd / 91.0));
		for (const auto& r : Regimes)
		{
			if ((static_cast<std::uint32_t>(quarter) + FactionHash(r->Faction)) % 3u != 0) continue;
			if (static_cast<int>(cs::Math::Floor(jd / 30.0)) % 3 != 0) continue;
			auto e = Ev(civ, galaxy, SocialKind::PollPublished, jd, r->Faction);
			e->Amount = (r->Approval + 1) / 2;
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- what happened elsewhere

	void Politics::React(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			switch (e->Kind)
			{
				case SocialKind::Scandal: if (e->Who != nullptr) Charge(civ, galaxy, e->Who, SocialKind::Scandal, jd); break;
				case SocialKind::Death:
					for (const auto& r : Regimes)
						if (r->Ruler == e->Who && e->Who != nullptr)
						{
							r->Ruler = nullptr;
							r->CrisisUntil = jd + 30 + (H(static_cast<std::uint32_t>(e->Id), 0x5CCu) % 60u);
							civ.Publish(galaxy, Ev(civ, galaxy, SocialKind::SuccessionCrisis, jd, r->Faction));
							for (const auto& c : civ.Worlds)
								if (c->Where->Owner == r->Faction) civ.Stir(c->Where, 0.03, -0.05);
						}
					break;
				default: break;
			}
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Politics::Elect(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& party, double jd)
	{
		if (party == nullptr || party->Kind != OrgKind::Party || !party->Active()) return "no such party";
		auto r = RegimeOf(party->Faction);
		if (r == nullptr) return "its state has no government to win";
		r->Elections++;
		r->NextElectionJd = jd + TermDays;
		r->Called = false;
		Win(civ, galaxy, r, party, 0.5 + 0.2 * Unit(H(static_cast<std::uint32_t>(party->Id), DayOf(jd))), jd, true);
		return "";
	}

	cs::String Politics::Overthrow(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& plotter, double jd)
	{
		if (plotter == nullptr || !plotter->Alive() || plotter->House == nullptr || plotter->House->Home == nullptr) return "no such person living";
		auto r = RegimeOf(plotter->House->Home->Owner);
		if (r == nullptr) return "their state has no government to seize";
		if (r->Ruler == plotter) return "they rule already";
		Seize(civ, galaxy, r, plotter, jd, true);
		return "";
	}

	cs::String Politics::Acquit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Trial>& t, double jd)
	{
		if (t == nullptr) return "no such trial";
		if (t->Decided) return "the verdict is in already";
		t->Decided = true;
		t->Guilty = false;
		t->Steered = true;
		auto e = Civilization::Make(SocialKind::JudgeBribed, jd, t->Where, t->Accused != nullptr && t->Accused->House != nullptr ? t->Accused->House->System : nullptr);
		e->Who = t->Accused;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Politics::Summit(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd)
	{
		if (a == nullptr || b == nullptr || a == b) return "name two states";
		if (galaxy.Wars == nullptr) return "no wars";
		bool atWar = false;
		for (const auto& w : galaxy.Wars->Active())
			if (w->Involves(a) && w->Involves(b)) atWar = true;
		if (!atWar && !Civilization::Hostile(galaxy, a, b)) return "they are not at war";
		Meet(civ, galaxy, a, b, jd, true);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Regime, "SpaceGod.Core.Regime",
		SG_F(Faction), SG_F(Hawk), SG_F(Liberty), SG_F(Approval), SG_F(Ruler), SG_F(Ruling), SG_F(NextElectionJd), SG_F(Called), SG_F(CrisisUntil),
		SG_F(Coups), SG_F(Elections), SG_F(Leaning))

	SG_SERIALIZE(Trial, "SpaceGod.Core.Trial",
		SG_F(Id), SG_F(Accused), SG_F(Where), SG_F(Charge), SG_F(OpenedJd), SG_F(VerdictJd), SG_F(Decided), SG_F(Guilty), SG_F(Bought), SG_F(Steered))

	SG_SERIALIZE(Talks, "SpaceGod.Core.Talks",
		SG_F(A), SG_F(B), SG_F(Jd))

	SG_SERIALIZE(Politics, "SpaceGod.Core.Politics",
		SG_F(Regimes), SG_F(Trials), SG_F(Summits), SG_F(seenEvent))
}
