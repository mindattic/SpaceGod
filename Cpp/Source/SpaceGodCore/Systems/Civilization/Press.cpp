// US-T288: Press (see Systems/Civilization/Press.h).

#include "Systems/Civilization/Press.h"

#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Civilization/Underworld.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		cs::Ref<Org> CharityOf(Civilization& civ, const cs::Ref<Household>& h)
		{
			for (const auto& o : civ.OrgsOf(OrgKind::Charity))
				if (o->Owner == h) return o;
			return nullptr;
		}
	}

	void Press::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)society;
		Run(civ, galaxy, jd);
	}

	void Press::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Outlets(civ, galaxy, society, jd);
		Boards(civ, galaxy, society, jd);
		Charities(civ, galaxy, society, jd);
		for (const auto& o : civ.OrgsOf(OrgKind::Outlet))
			if (Roll(H(static_cast<std::uint32_t>(o->Id), DayOf(jd), 0x1E5Bu), 0.1 + o->Strength * 0.1)) Dig(civ, galaxy, society, o, jd, false);
		// Press trust: worn by slanted outlets and strongmen, mended slowly.
		for (const auto& c : civ.Worlds)
		{
			double slant = 0;
			for (const auto& o : civ.OrgsOf(OrgKind::Outlet))
				if (o->System == c->System) slant += cs::Math::Abs(o->Stance);
			double target = 0.65 - slant * 0.1 + c->Liberty * 0.1 + c->Learning * 0.1;
			c->PressTrust = cs::Math::Clamp(c->PressTrust + (target - c->PressTrust) * 0.05, 0.05, 1.0);
		}
		for (int i = 0; i < Marked.Count();)
		{
			if (!Marked[i]->Alive()) Marked.RemoveAt(i);
			else i++;
		}
	}

	// ---------------------------------------------------------------- outlets

	void Press::Outlets(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto outlets = civ.OrgsOf(OrgKind::Outlet);
		if (outlets.Count() >= MaxOutlets) return;
		for (const auto& b : society.Businesses)
		{
			if (!b->Trading() || b->Kind != BusinessKind::Media || b->Owner == nullptr) continue;
			bool has = false;
			for (const auto& o : outlets)
				if (o->Owner == b->Owner) has = true;
			if (has || !Roll(H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0x0471u), 0.1)) continue;
			Found(civ, galaxy, b->Owner, jd, false);
			break;
		}
	}

	cs::Ref<Org> Press::Found(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& h, double jd, bool steered)
	{
		auto head = h->Head();
		std::uint32_t r = H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0x0472u);
		auto o = civ.Found(OrgKind::Outlet, h->Home, h->System, h->Home != nullptr ? h->Home->Owner : nullptr, head, 1e6, jd, r);
		// Its owners' loyalties set its line: the loyal cheer their flag, the vengeful and those wronged by it carp.
		o->Stance = Unit(r >> 4) * 0.6 - 0.3 + (Civilization::Has(head, Trait::Loyal) ? 0.5 : 0) - (Civilization::Has(head, Trait::Vengeful) ? 0.3 : 0)
			- (Civilization::Has(head, Trait::Honest) ? cs::Math::Abs(Unit(r >> 4) * 0.6 - 0.3) : 0);
		o->Stance = cs::Math::Clamp(o->Stance, -1.0, 1.0);
		o->Strength = 0.3;
		// (Its reporters are taken on when it first digs, see Dig.)
		auto e = Civilization::Make(SocialKind::OutletFounded, jd, h->Home, h->System);
		e->Org = o;
		e->House = h;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		return o;
	}

	void Press::Run(Civilization& civ, Galaxy& galaxy, double jd)
	{
		auto outlets = civ.OrgsOf(OrgKind::Outlet);
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			// A silenced reporter is news.
			if (e->Kind == SocialKind::Assassination && e->Who != nullptr && Marked.Contains(e->Who))
			{
				auto s = Civilization::Make(SocialKind::JournalistSilenced, jd, e->Where, e->System);
				s->Who = e->Who;
				s->House = e->House;
				civ.Publish(galaxy, s);
				for (const auto& c : civ.Worlds)
					if (c->System == e->System) c->PressTrust = cs::Math::Max(0.05, c->PressTrust - 0.05);
				continue;
			}
			if (!Narrator::Notable(e->Kind) || e->Faction == nullptr || e->RunBy != nullptr || e->Unverified) continue;
			// The outlets of the system run it, their way.
			for (const auto& o : outlets)
			{
				if (o->System != e->System || cs::Math::Abs(o->Stance) < 0.2) continue;
				if (!Roll(H(static_cast<std::uint32_t>(e->Id), static_cast<std::uint32_t>(o->Id), 0x0473u), 0.4)) continue;
				bool theirs = o->Faction == e->Faction;
				e->RunBy = o;
				e->Slant = (o->Stance > 0) == theirs ? 1 : -1;
				// Readers believe it as far as they trust the press.
				for (const auto& c : civ.Worlds)
					if (c->System == o->System && c->Where->Owner == e->Faction)
						c->Approval = cs::Math::Clamp(c->Approval + 0.01 * e->Slant * c->PressTrust, -1.0, 1.0);
				o->Strength = cs::Math::Min(1.0, o->Strength + 0.005);
				break;
			}
		}
	}

	// ---------------------------------------------------------------- the boards

	void Press::Boards(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		cs::List<cs::Ref<Settlement>> busy;
		for (const auto& e : civ.Latest(80))
			if (e->Jd > jd - Civilization::MonthDays && e->Where != nullptr && Narrator::Notable(e->Kind) && !busy.Contains(e->Where)) busy.Add(e->Where);
		for (const auto& s : busy)
		{
			std::uint32_t h = H(HashText(s->Name), DayOf(jd), 0xB0A2u);
			if (!Roll(h, 0.5)) continue;
			cs::List<cs::Ref<Citizen>> locals;
			for (const auto& hh : society.HousesOf(s))
				for (const auto& m : hh->Members)
					if (m->Alive() && m->AgeYears(jd) >= 16) locals.Add(m);
			auto e = Civilization::Make(SocialKind::BoardPost, jd, s, Civilization::SystemOf(galaxy, s));
			if (locals.Count() > 0) e->Who = Pick(locals, h >> 4);
			civ.Publish(galaxy, e);
		}
	}

	// ---------------------------------------------------------------- the muckrakers

	bool Press::Dig(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& outlet, double jd, bool steered)
	{
		// A reporter: one already on the staff, else the curious or honest of its world.
		cs::Ref<Citizen> reporter;
		for (const auto& m : outlet->Members)
			if (m->Alive() && m != outlet->Leader) { reporter = m; break; }
		if (reporter == nullptr)
			for (const auto& hh : society.HousesOf(outlet->Seat))
				for (const auto& m : hh->Members)
					if (reporter == nullptr && m->Alive() && m->AgeYears(jd) >= 21 && (Civilization::Has(m, Trait::Curious) || Civilization::Has(m, Trait::Honest)))
					{
						reporter = m;
						outlet->Members.Add(m);
						m->Orgs.Add(outlet);
					}
		if (reporter == nullptr) reporter = outlet->Leader;
		// A fraud in its system first, else a bribe still hidden.
		cs::Ref<Business> target;
		if (civ.Underworld != nullptr)
			for (const auto& f : civ.Underworld->Frauds)
				if (f->Business->System == outlet->System && f->Business->Owner != outlet->Owner) { target = f->Business; break; }
		cs::Ref<Bribe> bribe;
		if (target == nullptr)
			for (const auto& b : society.Bribes)
				if (!b->Exposed && b->Briber != nullptr && b->Briber->System == outlet->System && b->Briber->Owner != outlet->Owner) { bribe = b; break; }
		if (target == nullptr && bribe == nullptr) return false;
		auto e = Civilization::Make(SocialKind::Investigation, jd, outlet->Seat, outlet->System);
		e->Org = outlet;
		e->Who = reporter;
		e->Business = target != nullptr ? target : bribe->Briber;
		e->Steered = steered;
		civ.Publish(galaxy, e);
		outlet->Strength = cs::Math::Min(1.0, outlet->Strength + 0.05);
		if (reporter != nullptr) reporter->Reputation = cs::Math::Min(1.0, reporter->Reputation + 0.1);
		cs::Ref<Household> exposed;
		if (target != nullptr) { civ.Underworld->Expose(civ, galaxy, society, target, jd); exposed = target->Owner; }
		else { society.Reveal(galaxy, bribe, jd); exposed = bribe->Briber->Owner; }
		// A cruel head may have the reporter killed.
		auto head = exposed != nullptr ? exposed->Head() : nullptr;
		if (reporter != nullptr && civ.Underworld != nullptr && Civilization::Has(head, Trait::Cruel)
			&& Roll(H(static_cast<std::uint32_t>(reporter->Id), DayOf(jd), 0x51Eu), 0.4))
		{
			civ.Underworld->Mark(civ, reporter, exposed, nullptr, 20 + (DayOf(jd) % 30u), jd);
			Marked.Add(reporter);
		}
		return true;
	}

	// ---------------------------------------------------------------- charities

	void Press::Charities(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& h : society.HousesOf(nullptr))
		{
			auto head = h->Head();
			if (head == nullptr) continue;
			auto charity = CharityOf(civ, h);
			std::uint32_t r = H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0xC4A2u);
			if (charity == nullptr)
			{
				bool giving = Civilization::Has(head, Trait::Honest) || Civilization::Has(head, Trait::Pious) || head->Reputation < -0.2;
				if (h->Wealth < 2e8 || !giving || !Roll(r, 0.02)) continue;
				charity = civ.Found(OrgKind::Charity, h->Home, h->System, nullptr, head, h->Wealth * 0.02, jd, r);
				h->Wealth *= 0.98;
				auto e = Civilization::Make(SocialKind::CharityFounded, jd, h->Home, h->System);
				e->Org = charity;
				e->House = h;
				civ.Publish(galaxy, e);
				continue;
			}
			double gift = h->Wealth * 0.005;
			h->Wealth -= gift;
			charity->Funds += gift;
			// What it spends: care and cheer on its world, a better name for its house.
			double spend = charity->Funds * 0.5;
			charity->Funds -= spend;
			if (auto c = civ.CivicsOf(charity->Seat))
			{
				c->Care = cs::Math::Min(1.0, c->Care + cs::Math::Min(0.02, spend / 5e8));
				c->Approval = cs::Math::Min(1.0, c->Approval + 0.01);
			}
			head->Reputation = cs::Math::Min(1.0, head->Reputation + 0.02);
			if (Roll(r >> 5, 0.2))
			{
				auto e = Civilization::Make(SocialKind::Donation, jd, charity->Seat, charity->System);
				e->Org = charity;
				e->House = h;
				e->Amount = spend;
				civ.Publish(galaxy, e);
			}
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Press::Slant(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& outlet, int way, double jd)
	{
		(void)galaxy; (void)jd; (void)civ;
		if (outlet == nullptr || outlet->Kind != OrgKind::Outlet || !outlet->Active()) return "no such outlet";
		outlet->Stance = way > 0 ? 0.9 : (way < 0 ? -0.9 : 0);
		return "";
	}

	cs::String Press::Investigate(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& outlet, double jd)
	{
		if (outlet == nullptr || outlet->Kind != OrgKind::Outlet || !outlet->Active()) return "no such outlet";
		return Dig(civ, galaxy, society, outlet, jd, true) ? cs::String() : cs::String("there is nothing in its system to dig up");
	}

	cs::String Press::Donate(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& h, double jd)
	{
		if (h == nullptr || h->Scattered || h->Living() == 0) return "no such house standing";
		auto charity = CharityOf(civ, h);
		if (charity == nullptr) charity = civ.Found(OrgKind::Charity, h->Home, h->System, nullptr, h->Head(), 0, jd, H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0xC4A3u));
		double gift = h->Wealth * 0.2;
		h->Wealth -= gift;
		if (auto c = civ.CivicsOf(h->Home))
		{
			c->Care = cs::Math::Min(1.0, c->Care + 0.1);
			c->Approval = cs::Math::Min(1.0, c->Approval + 0.1);
		}
		if (auto head = h->Head()) head->Reputation = cs::Math::Min(1.0, head->Reputation + 0.3);
		auto e = Civilization::Make(SocialKind::Donation, jd, h->Home, h->System);
		e->Org = charity;
		e->House = h;
		e->Amount = gift;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Press, "SpaceGod.Core.Press",
		SG_F(Marked), SG_F(seenEvent))
}
