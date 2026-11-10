// US-T283: Commerce (see Systems/Civilization/Commerce.h).

#include "Systems/Civilization/Commerce.h"

#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Strife.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		const char* const Landmarks[] = { "the Glass Canyons", "the Singing Dunes", "the Aurora Coast", "the Drowned City", "the Thousand Falls",
			"the Ember Fields", "the Ringrise Terraces", "the Ice Cathedral", "the Old Colony Ark", "the Mirror Lakes", "the Storm Spire", "the Founders' Arch" };

		cs::Ref<Org> UnionOf(Civilization& civ, const cs::Ref<Settlement>& s)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Union))
				if (o->Seat == s) return o;
			return nullptr;
		}

		cs::Ref<Org> BankOf(Civilization& civ, const cs::Ref<Settlement>& s)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Bank))
				if (o->Seat == s) return o;
			return nullptr;
		}

		bool PirateHeld(const cs::Ref<StarSystem>& sys)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return false;
			for (const auto& s : sys->World->Economy->Settlements)
				if (s->Kind == SettlementKind::Haven && !s->Fallen) return true;
			return false;
		}
	}

	double Commerce::ShareOf(const cs::Ref<Household>& h, const cs::Ref<Business>& b) const
	{
		for (const auto& s : Stakes)
			if (s->Holder == h && s->Business == b) return s->Share;
		return 0;
	}

	void Commerce::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)society;
		React(civ, galaxy, jd);
	}

	void Commerce::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Unions(civ, galaxy, jd);
		Refugees(civ, galaxy, jd);
		Shares(civ, galaxy, society, jd);
		Banks(civ, galaxy, society, jd);
		Insurance(civ, galaxy, society, jd);
		Adverts(civ, galaxy, society, jd);
		Tourism(civ, galaxy, society, jd);
		Inventions(civ, galaxy, society, jd);
	}

	// ---------------------------------------------------------------- unions

	void Commerce::Unions(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			if (s->Population < 1e5 || s->WageIndex <= 0) continue;
			double gap = s->CostOfLiving() / s->WageIndex;
			auto u = UnionOf(civ, s);
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x0A10u);
			if (u == nullptr)
			{
				if ((gap < 1.05 && c->Liberty < 0.3) || !Roll(h, 0.04)) continue;
				u = civ.Found(OrgKind::Union, s, c->System, s->Owner, nullptr, 1e5, jd, h);
				u->Strength = 0.2;
				auto e = Civilization::Make(SocialKind::UnionFormed, jd, s, c->System);
				e->Org = u;
				civ.Publish(galaxy, e);
				continue;
			}
			u->Strength = cs::Math::Clamp(u->Strength + (gap - 1.0) * 0.1 + (c->Liberty > 0 ? 0.01 : -0.01), 0.05, 1.0);
			if (gap > 1.15 && Roll(h >> 4, u->Strength * 0.3) && civ.Strife != nullptr)
				civ.Strife->CallStrike(civ, galaxy, s, u, 10 + (h % 16u), jd);
			else if (gap > 1.03 && Roll(h >> 8, u->Strength * 0.2))
			{
				s->WageIndex *= 1.02;
				auto e = Civilization::Make(SocialKind::WageDeal, jd, s, c->System);
				e->Org = u;
				civ.Publish(galaxy, e);
			}
		}
	}

	// ---------------------------------------------------------------- refugees

	void Commerce::Refugees(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			auto s = c->Where;
			bool fleeing = c->Infected > 0.2 || s->Ration < 0.85 || (Civilization::AtWar(galaxy, s->Owner) && s->Unrest > 0.5) || s->OccupiedFrom != nullptr;
			if (!fleeing || s->Population < 5000) continue;
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0x2EF0u);
			if (!Roll(h, 0.15)) continue;
			// The best world their flag holds (the flag they would rather be under, if occupied).
			auto flag = s->OccupiedFrom != nullptr ? s->OccupiedFrom : s->Owner;
			cs::Ref<Civics> haven;
			for (const auto& d : civ.Worlds)
				if (d != c && d->Where->Owner == flag && d->Infected < 0.1 && (haven == nullptr || d->Where->Attraction * Civilization::Wellbeing(d->Where) > haven->Where->Attraction * Civilization::Wellbeing(haven->Where))) haven = d;
			if (haven == nullptr) continue;
			double n = cs::Math::Floor(s->Population * (0.01 + 0.02 * Unit(h >> 5)));
			s->Population -= n;
			haven->Where->Population += n;
			civ.Stir(haven->Where, 0.02, -0.02);
			// The sick carry it with them.
			if (c->Infected > 0.2) haven->Infected = cs::Math::Min(1.0, haven->Infected + 0.02);
			auto e = Civilization::Make(SocialKind::Refugees, jd, s, c->System);
			e->Amount = n;
			e->Faction = flag;
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- shares and takeovers

	void Commerce::Shares(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		// Dividends.
		for (const auto& st : Stakes)
			if (st->Business->Trading() && st->Business->LastMonth > 0)
			{
				double d = st->Business->LastMonth * 0.5 * st->Share;
				st->Business->Capital -= d;
				st->Holder->Wealth += d;
			}
		for (int i = 0; i < Stakes.Count();)
		{
			if (!Stakes[i]->Business->Trading() || Stakes[i]->Holder->Scattered || Stakes[i]->Holder->Living() == 0) Stakes.RemoveAt(i);
			else i++;
		}
		// Buying in.
		for (const auto& h : society.HousesOf(nullptr))
		{
			auto head = h->Head();
			if (head == nullptr || h->Wealth < 5e7) continue;
			bool hungry = head->Goal == Ambition::Wealth || head->Goal == Ambition::Power || Civilization::Has(head, Trait::Greedy);
			std::uint32_t r = H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0x5A2Eu);
			if (!hungry || !Roll(r, 0.15)) continue;
			cs::List<cs::Ref<Business>> targets;
			for (const auto& b : society.Businesses)
				if (b->Trading() && b->Owner != h && b->Home == h->Home && b->Capital > 1e6) targets.Add(b);
			if (targets.Count() == 0) continue;
			auto b = Pick(targets, r >> 5);
			double add = 0.05 + 0.1 * Unit(r >> 9);
			double cost = add * b->Capital;
			if (cost > h->Wealth * 0.5) continue;
			h->Wealth -= cost;
			b->Capital += cost * 0.5; // new shares raise money for the firm
			cs::Ref<Stake> st;
			for (const auto& x : Stakes)
				if (x->Holder == h && x->Business == b) st = x;
			if (st == nullptr)
			{
				st = cs::New<Stake>();
				st->Holder = h;
				st->Business = b;
				Stakes.Add(st);
			}
			st->Share = cs::Math::Min(1.0, st->Share + add);
			if (st->Share > 0.5) Seize(civ, galaxy, society, h, b, jd, false);
		}
		// A collapsing firm's shares crash.
		for (const auto& b : society.Businesses)
			if (b->Trading() && b->LastMonth < -0.05 * cs::Math::Max(1.0, b->Capital))
			{
				bool held = false;
				for (const auto& st : Stakes)
					if (st->Business == b) held = true;
				if (held && Roll(H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0xC2A5u), 0.3)) Crash(civ, galaxy, b, jd);
			}
	}

	void Commerce::Crash(Civilization& civ, Galaxy& galaxy, const cs::Ref<Business>& b, double jd)
	{
		for (const auto& st : Stakes)
			if (st->Business == b) st->Share *= 0.5; // what they hold is worth half
		b->Capital *= 0.8;
		auto e = Civilization::Make(SocialKind::SharesCrash, jd, b->Home, b->System);
		e->Business = b;
		civ.Publish(galaxy, e);
	}

	void Commerce::Seize(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& raider, const cs::Ref<Business>& b, double jd, bool steered)
	{
		auto victim = b->Owner;
		// The raider's firm swallows it (as Society's buyouts do); a raider without a firm takes it over outright.
		auto into = raider->Business != nullptr && raider->Business->Trading() ? raider->Business : nullptr;
		if (into != nullptr)
		{
			into->Capital += b->Capital * 0.8;
			for (const auto& br : b->Branches)
				if (!into->Branches.Contains(br) && br != into->Home) into->Branches.Add(br);
			b->Status = BusinessStatus::Acquired;
			b->Buyer = into;
			b->EndedJd = jd;
			b->Capital = 0;
			if (victim != nullptr && victim->Business == b) victim->Business = nullptr;
			society.Acquisitions++;
		}
		else
		{
			if (victim != nullptr && victim->Business == b) victim->Business = nullptr;
			b->Owner = raider;
			raider->Business = b;
		}
		for (int i = 0; i < Stakes.Count();)
		{
			if (Stakes[i]->Business == b) Stakes.RemoveAt(i);
			else i++;
		}
		auto e = Civilization::Make(SocialKind::Takeover, jd, b->Home, b->System);
		e->Business = b;
		e->House = victim;
		e->OtherHouse = raider;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		// Felt as a buyout by those who lost it (Kinship may answer with a feud).
		auto bo = Civilization::Make(SocialKind::Buyout, jd, b->Home, b->System);
		bo->Business = b;
		bo->House = victim;
		bo->OtherHouse = raider;
		civ.Publish(galaxy, bo, false, false);
	}

	// ---------------------------------------------------------------- banks

	void Commerce::Banks(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		// Each big world keeps one, founded by its richest house.
		for (const auto& c : civ.Worlds)
		{
			if (c->Where->Population < 2e6 || BankOf(civ, c->Where) != nullptr) continue;
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0xBA2Cu);
			if (!Roll(h, 0.05)) continue;
			cs::Ref<Household> richest;
			for (const auto& hh : society.HousesOf(c->Where))
				if (richest == nullptr || hh->Wealth > richest->Wealth) richest = hh;
			auto o = civ.Found(OrgKind::Bank, c->Where, c->System, c->Where->Owner, richest != nullptr ? richest->Head() : nullptr, 5e7, jd, h);
			o->Strength = 0.5;
			auto e = Civilization::Make(SocialKind::BankFounded, jd, c->Where, c->System);
			e->Org = o;
			e->House = richest;
			civ.Publish(galaxy, e);
		}
		for (const auto& bank : civ.OrgsOf(OrgKind::Bank))
		{
			// Deposits from the rich houses of its world, interest paid back on them.
			for (const auto& hh : society.HousesOf(bank->Seat))
				if (hh->Wealth > 1e7) { double d = hh->Wealth * 0.005; hh->Wealth -= d; bank->Funds += d; }
			// Lend to struggling firms on its world.
			for (const auto& b : society.Businesses)
			{
				if (!b->Trading() || b->Home != bank->Seat || b->Capital > 2e6 || bank->Funds < 5e6) continue;
				bool has = false;
				for (const auto& l : Loans)
					if (l->Business == b) has = true;
				if (has) continue;
				auto l = cs::New<Loan>();
				l->Bank = bank;
				l->Business = b;
				l->Principal = 2e6;
				l->DueJd = jd + 720;
				bank->Funds -= l->Principal;
				b->Capital += l->Principal;
				bank->Score++;
				Loans.Add(l);
			}
		}
		// Interest, repayment, default.
		for (const auto& l : Loans.ToArray())
		{
			auto b = l->Business;
			if (!l->Bank->Active()) { Loans.Remove(l); continue; }
			if (!b->Trading() || b->Capital < 0)
			{
				Loans.Remove(l);
				auto e = Civilization::Make(SocialKind::LoanDefault, jd, b->Home, b->System);
				e->Business = b;
				e->Org = l->Bank;
				e->Amount = l->Principal;
				civ.Publish(galaxy, e);
				continue;
			}
			double interest = l->Principal * MonthlyRate;
			b->Capital -= interest;
			l->Bank->Funds += interest;
			if (jd >= l->DueJd && b->Capital > l->Principal * 1.5)
			{
				b->Capital -= l->Principal;
				l->Bank->Funds += l->Principal;
				Loans.Remove(l);
			}
		}
		for (const auto& bank : civ.OrgsOf(OrgKind::Bank))
			if (bank->Funds < 0) Fail(civ, galaxy, society, bank, jd, false);
	}

	void Commerce::Fail(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& bank, double jd, bool steered)
	{
		bank->EndedJd = jd;
		for (const auto& hh : society.HousesOf(bank->Seat)) hh->Wealth *= 0.95;
		civ.Stir(bank->Seat, 0.05, -0.1);
		auto e = Civilization::Make(SocialKind::BankRun, jd, bank->Seat, bank->System);
		e->Org = bank;
		e->Steered = steered;
		civ.Publish(galaxy, e);
	}

	// ---------------------------------------------------------------- insurance

	void Commerce::Insurance(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			if (!PirateHeld(c->System)) continue;
			for (const auto& b : society.Businesses)
				if (b->Trading() && b->Home == c->Where && (b->Kind == BusinessKind::Shipping || b->Kind == BusinessKind::TradingHouse)) b->Capital *= 0.99;
			if (Roll(H(HashText(c->Where->Name), DayOf(jd), 0x1A5u), 0.08)) civ.Publish(galaxy, Civilization::Make(SocialKind::PremiumsRise, jd, c->Where, c->System));
		}
	}

	// ---------------------------------------------------------------- ad wars

	void Commerce::Adverts(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& h : society.HousesOf(nullptr))
		{
			auto head = h->Head();
			if (head == nullptr || h->Business == nullptr || !h->Business->Trading()) continue;
			for (const auto& t : head->Ties)
			{
				if (t->Kind != TieKind::Rival || t->Other == nullptr || !t->Other->Alive() || t->Other->House == nullptr) continue;
				auto other = t->Other->House->Business;
				if (other == nullptr || !other->Trading() || other->Kind != h->Business->Kind || h->Id > t->Other->House->Id) continue;
				if (!Roll(H(static_cast<std::uint32_t>(h->Id), static_cast<std::uint32_t>(t->Other->House->Id), DayOf(jd)), 0.1)) continue;
				auto mine = h->Business;
				mine->Capital *= 0.99;
				other->Capital *= 0.99;
				auto winner = head->Skill >= t->Other->Skill ? mine : other;
				winner->Capital *= 1.03;
				auto e = Civilization::Make(SocialKind::AdWar, jd, winner->Home, winner->System);
				e->Business = winner;
				civ.Publish(galaxy, e);
				break;
			}
		}
	}

	// ---------------------------------------------------------------- tourism

	void Commerce::Tourism(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0x7022u);
			if (c->Landmark.empty())
			{
				if (c->Where->Population < 1e6 || !Roll(h, 0.01)) continue;
				Landmark(civ, galaxy, c->Where, jd);
				continue;
			}
			// Visitors fill the hotels, unless war or plague keeps them away.
			if (Civilization::AtWar(galaxy, c->Where->Owner) || c->Quarantined(jd) || c->Infected > 0.1) continue;
			for (const auto& b : society.Businesses)
				if (b->Trading() && b->Home == c->Where && b->Kind == BusinessKind::Hospitality) b->Capital *= 1.02;
			if (Roll(h >> 6, 0.15))
			{
				auto e = Civilization::Make(SocialKind::TouristBoom, jd, c->Where, c->System);
				for (const auto& b : society.Businesses)
					if (b->Trading() && b->Home == c->Where && b->Kind == BusinessKind::Hospitality) { e->Business = b; break; }
				civ.Publish(galaxy, e);
			}
		}
	}

	// ---------------------------------------------------------------- patents

	void Commerce::Inventions(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto cmd = galaxy.Polities != nullptr ? galaxy.Polities->Command : nullptr;
		for (const auto& b : society.Businesses)
		{
			if (!b->Trading() || b->Owner == nullptr) continue;
			auto head = b->Owner->Head();
			if (head == nullptr || head->Skill < 0.75) continue;
			std::uint32_t h = H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0x9A7Eu);
			if (!Roll(h, 0.03)) continue;
			Patents++;
			b->Capital *= 1.05;
			// What one firm invents, its state's labs learn from.
			if (cmd != nullptr && b->Home != nullptr && b->Home->Owner != nullptr)
				if (auto d = cmd->Of(b->Home->Owner); d != nullptr && d->Research != nullptr) d->ResearchPoints += d->Research->CostOf(d->Level(d->Research)) * 0.05;
			auto e = Civilization::Make(SocialKind::PatentFiled, jd, b->Home, b->System);
			e->Business = b;
			e->Who = head;
			civ.Publish(galaxy, e);
			// A greedy rival in the trade may steal it.
			for (const auto& o : society.Businesses)
			{
				if (o == b || !o->Trading() || o->Kind != b->Kind || o->Owner == nullptr) continue;
				auto oh = o->Owner->Head();
				if (!(Civilization::Has(oh, Trait::Greedy) || Civilization::Has(oh, Trait::Cruel)) || !Roll(H(h, static_cast<std::uint32_t>(o->Id)), 0.1)) continue;
				double gain = b->Capital * 0.03;
				b->Capital -= gain;
				o->Capital += gain;
				auto s = Civilization::Make(SocialKind::PatentStolen, jd, b->Home, b->System);
				s->Business = b;
				s->House = b->Owner;
				s->OtherHouse = o->Owner;
				civ.Publish(galaxy, s);
				break;
			}
		}
	}

	void Commerce::React(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			if (e->Kind == SocialKind::FraudExposed && e->Business != nullptr) Crash(civ, galaxy, e->Business, jd);
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Commerce::Takeover(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Household>& raider, const cs::Ref<Business>& b, double jd)
	{
		if (raider == nullptr || raider->Scattered || raider->Living() == 0) return "no such house standing";
		if (b == nullptr || !b->Trading()) return "no such business trading";
		if (b->Owner == raider) return "they own it already";
		Seize(civ, galaxy, society, raider, b, jd, true);
		return "";
	}

	cs::String Commerce::Run(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& bank, double jd)
	{
		if (bank == nullptr || bank->Kind != OrgKind::Bank || !bank->Active()) return "no such bank";
		Fail(civ, galaxy, society, bank, jd, true);
		return "";
	}

	cs::String Commerce::Landmark(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		auto c = civ.CivicsOf(s);
		if (c == nullptr) return "nobody watches that world yet";
		if (!c->Landmark.empty()) return "it is famous already";
		c->Landmark = Pick(Landmarks, H(HashText(s->Name), 0x7023u));
		auto e = Civilization::Make(SocialKind::LandmarkFound, jd, s, c->System);
		civ.Publish(galaxy, e);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Stake, "SpaceGod.Core.Stake",
		SG_F(Holder), SG_F(Business), SG_F(Share))

	SG_SERIALIZE(Loan, "SpaceGod.Core.Loan",
		SG_F(Bank), SG_F(Business), SG_F(Principal), SG_F(DueJd))

	SG_SERIALIZE(Commerce, "SpaceGod.Core.Commerce",
		SG_F(Stakes), SG_F(Loans), SG_F(Patents), SG_F(seenEvent))
}
