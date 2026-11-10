// US-T274/US-T275: the Civilization layer (see Systems/Civilization/Civilization.h).

#include "Systems/Civilization/Civilization.h"

#include "Combat/Diplomacy.h"
#include "Economy/Colonisation.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Civilization/Strife.h"
#include "Systems/Civilization/Underworld.h"
#include "Systems/Civilization/Commerce.h"
#include "Systems/Civilization/Culture.h"
#include "Systems/Civilization/Health.h"
#include "Systems/Civilization/Press.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/Territory.h"
#include "World.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		static const char* const SocialKindNames[] = {
			"None", "Birth", "Death", "Wedding", "BusinessFounded", "Bankruptcy", "Buyout", "Scandal",
			"Friendship", "Rivalry", "FallingOut", "Mood", "FeudDeclared", "FeudSabotage", "FeudEnded", "InheritanceDispute",
			"HouseSplit", "Affair", "Divorce", "HiddenHeir", "Graduation", "SchoolFounded", "CelebrityRise", "CelebrityScandal",
			"Endorsement", "IdeologyShift", "PartyFounded", "ElectionCalled", "ElectionWon", "ElectionRigged", "Coup", "CoupFailed",
			"SuccessionCrisis", "TrialOpened", "Verdict", "JudgeBribed", "PeaceSummit", "SummitFailed", "PollPublished", "Protest",
			"Riot", "CellFormed", "Sabotage", "Uprising", "SpyCaught", "PactLeaked", "TechStolen", "WarWeariness",
			"Conscription", "PeaceForced", "MercsHired", "MercsTurned", "AdmiralRises", "PrisonersExchanged", "SyndicateRises", "Protection",
			"SmugglingRun", "SmugglerCaught", "PrisonOpened", "PrisonBreak", "Assassination", "AssassinationFoiled", "Fraud", "FraudExposed",
			"CrimeWave", "Crackdown", "UnionFormed", "WageDeal", "StrikeCalled", "Refugees", "Takeover", "SharesCrash",
			"BankFounded", "LoanDefault", "BankRun", "PremiumsRise", "AdWar", "LandmarkFound", "TouristBoom", "PatentFiled",
			"PatentStolen", "FaithFounded", "Conversion", "Schism", "Pilgrimage", "HolyWar", "Festival", "MatchPlayed",
			"MatchRigged", "Championship", "TrendSweeps", "CulturalDrift", "MonumentRaised", "Chronicle", "Rumour", "Propaganda",
			"Outbreak", "Spread", "Quarantine", "Cured", "HospitalBuilt", "MedicineShortage", "BabyBoom", "Ageing",
			"Disaster", "Relief", "Famine", "FoodAid", "OutletFounded", "SlantedStory", "BoardPost", "Investigation",
			"JournalistSilenced", "CharityFounded", "Donation",
		};
		static_assert(sizeof(SocialKindNames) / sizeof(SocialKindNames[0]) == static_cast<std::size_t>(SocialKind::Count_), "a SocialKind without a name");
	}

	const char* ToString(SocialKind v)
	{
		int i = static_cast<int>(v);
		return i >= 0 && i < static_cast<int>(SocialKind::Count_) ? SocialKindNames[i] : "";
	}

	const char* ToString(OrgKind v)
	{
		switch (v)
		{
			case OrgKind::Party: return "Party";
			case OrgKind::Faith: return "Faith";
			case OrgKind::Syndicate: return "Syndicate";
			case OrgKind::Union: return "Union";
			case OrgKind::Bank: return "Bank";
			case OrgKind::Outlet: return "Outlet";
			case OrgKind::Team: return "Team";
			case OrgKind::Charity: return "Charity";
			case OrgKind::Mercenaries: return "Mercenaries";
			case OrgKind::School: return "School";
			case OrgKind::Agency: return "Agency";
			case OrgKind::Insurgents: return "Insurgents";
		}
		return "";
	}

	const char* ToString(Trait v)
	{
		switch (v)
		{
			case Trait::Greedy: return "Greedy";
			case Trait::Pious: return "Pious";
			case Trait::Reckless: return "Reckless";
			case Trait::Ambitious: return "Ambitious";
			case Trait::Loyal: return "Loyal";
			case Trait::Vengeful: return "Vengeful";
			case Trait::Charming: return "Charming";
			case Trait::Honest: return "Honest";
			case Trait::Cruel: return "Cruel";
			case Trait::Curious: return "Curious";
			case Trait::Brave: return "Brave";
			case Trait::Gifted: return "Gifted";
		}
		return "";
	}

	const char* ToString(Ambition v)
	{
		switch (v)
		{
			case Ambition::Wealth: return "Wealth";
			case Ambition::Power: return "Power";
			case Ambition::Fame: return "Fame";
			case Ambition::Faith: return "Faith";
			case Ambition::Family: return "Family";
			case Ambition::Revenge: return "Revenge";
			case Ambition::Knowledge: return "Knowledge";
			case Ambition::Peace: return "Peace";
		}
		return "";
	}

	const char* ToString(TieKind v)
	{
		switch (v)
		{
			case TieKind::Friend: return "Friend";
			case TieKind::Rival: return "Rival";
			case TieKind::Lover: return "Lover";
			case TieKind::Patron: return "Patron";
			case TieKind::Enemy: return "Enemy";
			case TieKind::Mentor: return "Mentor";
		}
		return "";
	}

	cs::String SocialEvent::ToString() const { return Narrator::Render(*this, true); }

	// ---------------------------------------------------------------- the container

	cs::Ref<Civilization> Civilization::Of(Galaxy& galaxy)
	{
		auto t = Territory::Of(galaxy);
		if (t->Civilization == nullptr) t->Civilization = cs::New<Civilization>();
		return t->Civilization;
	}

	cs::Ref<Civilization> Civilization::Find(const Galaxy& galaxy) { return galaxy.Territory != nullptr ? galaxy.Territory->Civilization : nullptr; }

	bool Civilization::Stands(const cs::Ref<Settlement>& s)
	{
		return s != nullptr && !s->Fallen && s->Stage != ColonyStage::Ruin && s->Kind != SettlementKind::Haven && s->Owner != nullptr;
	}

	cs::Ref<StarSystem> Civilization::SystemOf(Galaxy& galaxy, const cs::Ref<Settlement>& s)
	{
		if (s == nullptr) return nullptr;
		for (const auto& sys : galaxy.Systems)
			if (sys->World != nullptr && sys->World->Economy != nullptr && sys->World->Economy->Settlements.Contains(s)) return sys;
		return nullptr;
	}

	double Civilization::Wellbeing(const cs::Ref<Settlement>& s) { return s != nullptr ? cs::Math::Clamp(s->Wellbeing, 0.0, 1.5) : 0; }

	bool Civilization::Hostile(Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
	{
		if (a == nullptr || b == nullptr || a == b) return false;
		if (a->IsPirate || b->IsPirate) return true;
		auto d = galaxy.Polities != nullptr ? galaxy.Polities->Diplomacy : nullptr;
		return d != nullptr && d->Hostile(a, b);
	}

	cs::Ref<Faction> Civilization::EnemyOf(Galaxy& galaxy, const cs::Ref<Faction>& f)
	{
		if (f == nullptr || galaxy.Wars == nullptr) return nullptr;
		for (const auto& w : galaxy.Wars->Active())
			if (w->Involves(f)) return w->Enemy(f);
		return nullptr;
	}

	bool Civilization::AtWar(Galaxy& galaxy, const cs::Ref<Faction>& f) { return EnemyOf(galaxy, f) != nullptr; }

	cs::Ref<SocialEvent> Civilization::Make(SocialKind kind, double jd, const cs::Ref<Settlement>& where, const cs::Ref<StarSystem>& sys)
	{
		auto e = cs::New<SocialEvent>();
		e->Kind = kind;
		e->Jd = jd;
		e->Where = where;
		e->System = sys;
		if (where != nullptr) e->Faction = where->Owner;
		return e;
	}

	cs::Ref<SocialEvent> Civilization::Publish(Galaxy& galaxy, const cs::Ref<SocialEvent>& e, bool feed, bool log)
	{
		e->Id = NextId++;
		if (e->Seed == 0) e->Seed = H(static_cast<std::uint32_t>(e->Id), DayOf(e->Jd), static_cast<std::uint32_t>(e->Kind));
		if (e->System == nullptr && e->Where != nullptr) e->System = SystemOf(galaxy, e->Where);
		Events.Add(e);
		int k = static_cast<int>(e->Kind);
		while (Tally.Count() <= k) Tally.Add(0);
		Tally[k] = Tally[k] + 1;

		if (feed)
			if (auto society = Society::Find(galaxy))
			{
				auto f = cs::New<FeedItem>();
				f->Jd = e->Jd;
				f->Kind = Narrator::FeedOf(e->Kind);
				f->System = e->System;
				f->Event = e;
				if (e->Who != nullptr) f->From = e->Who->FullName();
				if (e->Whom != nullptr) f->To = e->Whom->FullName();
				society->Feed.Add(f);
			}
		// Notable happenings go on the system's log in the template's words (never Prose's: the simulation's state must not
		// depend on what Prose has written), so the relays carry them out as news (SystemReport::Notable).
		if (log && Narrator::Notable(e->Kind) && e->System != nullptr && e->System->World != nullptr && !e->Unverified)
		{
			auto w = e->System->World;
			w->Log(cs::Double::IsNaN(w->Jd) ? e->Jd : cs::Math::Max(e->Jd, w->Jd), Narrator::Render(*e, false), WorldEventKind::Society, -1);
		}
		return e;
	}

	cs::Ref<Civics> Civilization::CivicsOf(const cs::Ref<Settlement>& s) const
	{
		for (const auto& c : Worlds)
			if (c->Where == s) return c;
		return nullptr;
	}

	cs::Ref<Org> Civilization::OrgById(int id) const
	{
		for (const auto& o : Orgs)
			if (o->Id == id) return o;
		return nullptr;
	}

	cs::List<cs::Ref<Org>> Civilization::OrgsOf(OrgKind kind, bool activeOnly) const
	{
		cs::List<cs::Ref<Org>> r;
		for (const auto& o : Orgs)
			if (o->Kind == kind && (!activeOnly || o->Active())) r.Add(o);
		return r;
	}

	cs::List<cs::Ref<SocialEvent>> Civilization::Latest(int n, SocialKind kind) const
	{
		cs::List<cs::Ref<SocialEvent>> r;
		for (int i = Events.Count() - 1; i >= 0 && r.Count() < n; i--)
			if (kind == SocialKind::None || Events[i]->Kind == kind) r.Insert(0, Events[i]);
		return r;
	}

	cs::Ref<Org> Civilization::Found(OrgKind kind, const cs::Ref<Settlement>& seat, const cs::Ref<StarSystem>& sys, const cs::Ref<Faction>& faction,
		const cs::Ref<Citizen>& leader, double funds, double jd, std::uint32_t seed)
	{
		auto o = cs::New<Org>();
		o->Id = NextId++;
		o->Kind = kind;
		o->NameSeed = seed;
		o->Seat = seat;
		o->System = sys;
		o->Faction = faction;
		o->Leader = leader;
		o->Funds = funds;
		o->FoundedJd = jd;
		o->Owner = leader != nullptr ? leader->House : nullptr;
		o->Name = Narrator::FallbackName(kind, seed, seat != nullptr ? seat->Name : (sys != nullptr ? sys->Name : cs::String("Frontier")),
			o->Owner != nullptr ? o->Owner->Surname : cs::String("Commons"));
		if (leader != nullptr)
		{
			o->Members.Add(leader);
			if (kind == OrgKind::Faith) leader->Faith = o;
			else if (!leader->Orgs.Contains(o)) leader->Orgs.Add(o);
		}
		Orgs.Add(o);
		return o;
	}

	// ---------------------------------------------------------------- persona helpers

	bool Civilization::Has(const cs::Ref<Citizen>& c, Trait t) { return c != nullptr && (c->Traits & static_cast<int>(t)) != 0; }

	cs::Ref<Tie> Civilization::TieOf(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b)
	{
		if (a == nullptr || b == nullptr) return nullptr;
		for (const auto& t : a->Ties)
			if (t->Other == b) return t;
		return nullptr;
	}

	namespace
	{
		void BindOne(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b, TieKind kind, double strength, double jd)
		{
			for (const auto& t : a->Ties)
				if (t->Other == b)
				{
					t->Kind = kind;
					t->Strength = cs::Math::Clamp(strength, 0.0, 1.0);
					t->SinceJd = jd;
					return;
				}
			// Room is made by dropping the weakest tie, or the tie to someone dead.
			if (a->Ties.Count() >= Civilization::MaxTies)
			{
				int weakest = 0;
				for (int i = 1; i < a->Ties.Count(); i++)
				{
					bool deadI = a->Ties[i]->Other == nullptr || !a->Ties[i]->Other->Alive();
					bool deadW = a->Ties[weakest]->Other == nullptr || !a->Ties[weakest]->Other->Alive();
					if ((deadI && !deadW) || (deadI == deadW && a->Ties[i]->Strength < a->Ties[weakest]->Strength)) weakest = i;
				}
				a->Ties.RemoveAt(weakest);
			}
			auto t = cs::New<Tie>();
			t->Other = b;
			t->Kind = kind;
			t->Strength = cs::Math::Clamp(strength, 0.0, 1.0);
			t->SinceJd = jd;
			a->Ties.Add(t);
		}
	}

	void Civilization::Bind(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b, TieKind kind, double strength, double jd)
	{
		if (a == nullptr || b == nullptr || a == b) return;
		BindOne(a, b, kind, strength, jd);
		// A patron's protégé, a mentor's student: the other side reads it plainly as a friend.
		BindOne(b, a, kind == TieKind::Patron || kind == TieKind::Mentor ? TieKind::Friend : kind, strength, jd);
	}

	void Civilization::Unbind(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b)
	{
		if (a == nullptr || b == nullptr) return;
		for (int i = 0; i < a->Ties.Count(); i++)
			if (a->Ties[i]->Other == b) { a->Ties.RemoveAt(i); break; }
		for (int i = 0; i < b->Ties.Count(); i++)
			if (b->Ties[i]->Other == a) { b->Ties.RemoveAt(i); break; }
	}

	cs::List<cs::Ref<Citizen>> Civilization::Adults(const Society& society, double jd)
	{
		cs::List<cs::Ref<Citizen>> r;
		for (const auto& h : society.Houses)
		{
			if (h->Scattered) continue;
			for (const auto& c : h->Members)
				if (c->Alive() && c->AgeYears(jd) >= 18) r.Add(c);
		}
		return r;
	}

	void Civilization::Stir(const cs::Ref<Settlement>& s, double unrest, double approval)
	{
		if (s == nullptr) return;
		s->Unrest = cs::Math::Clamp(s->Unrest + unrest, 0.0, 1.0);
		if (auto c = CivicsOf(s)) c->Approval = cs::Math::Clamp(c->Approval + approval, -1.0, 1.0);
	}

	// ---------------------------------------------------------------- the step

	void Civilization::Update(Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(jd)) return;
		if (cs::Double::IsNaN(nextDayJd))
		{
			nextDayJd = cs::Math::Floor(jd);
			nextMonthJd = nextDayJd;
		}
		if (jd < nextDayJd) return;
		if (jd - nextDayJd > MaxCatchUpDays) nextDayJd = cs::Math::Floor(jd);
		while (nextDayJd <= jd)
		{
			double day = nextDayJd;
			if (day >= nextMonthJd)
			{
				Month(galaxy, day);
				nextMonthJd = day + MonthDays;
			}
			Day(galaxy, day);
			nextDayJd = day + 1;
		}
		Prune();
	}

	void Civilization::Day(Galaxy& galaxy, double jd)
	{
		auto society = Society::Find(galaxy);
		if (society == nullptr) return;
		Personas(*society, jd);
		if (Kinship == nullptr) Kinship = cs::New<SpaceGod::Core::Kinship>();
		Kinship->Day(*this, galaxy, *society, jd);
		if (Politics == nullptr) Politics = cs::New<SpaceGod::Core::Politics>();
		Politics->Day(*this, galaxy, *society, jd);
		if (Strife == nullptr) Strife = cs::New<SpaceGod::Core::Strife>();
		Strife->Day(*this, galaxy, *society, jd);
		if (Underworld == nullptr) Underworld = cs::New<SpaceGod::Core::Underworld>();
		Underworld->Day(*this, galaxy, *society, jd);
		if (Commerce == nullptr) Commerce = cs::New<SpaceGod::Core::Commerce>();
		Commerce->Day(*this, galaxy, *society, jd);
		if (Culture == nullptr) Culture = cs::New<SpaceGod::Core::Culture>();
		Culture->Day(*this, galaxy, *society, jd);
		if (Health == nullptr) Health = cs::New<SpaceGod::Core::Health>();
		Health->Day(*this, galaxy, *society, jd);
		if (Press == nullptr) Press = cs::New<SpaceGod::Core::Press>();
		Press->Day(*this, galaxy, *society, jd);
	}

	void Civilization::Month(Galaxy& galaxy, double jd)
	{
		auto society = Society::Find(galaxy);
		Census(galaxy, jd);
		Moods(galaxy, jd);
		if (society == nullptr) return;
		Ties(galaxy, *society, jd);
		if (Kinship != nullptr) Kinship->Month(*this, galaxy, *society, jd);
		if (Politics != nullptr) Politics->Month(*this, galaxy, *society, jd);
		if (Strife != nullptr) Strife->Month(*this, galaxy, *society, jd);
		if (Underworld != nullptr) Underworld->Month(*this, galaxy, *society, jd);
		if (Commerce != nullptr) Commerce->Month(*this, galaxy, *society, jd);
		if (Culture != nullptr) Culture->Month(*this, galaxy, *society, jd);
		if (Health != nullptr) Health->Month(*this, galaxy, *society, jd);
		if (Press != nullptr) Press->Month(*this, galaxy, *society, jd);
	}

	void Civilization::Census(Galaxy& galaxy, double jd)
	{
		(void)jd;
		for (const auto& sys : galaxy.Systems)
		{
			auto w = sys->World;
			if (w == nullptr || w->Economy == nullptr) continue;
			for (const auto& s : w->Economy->Settlements)
			{
				if (!Stands(s) || s->Population < 200 || CivicsOf(s) != nullptr) continue;
				auto c = cs::New<Civics>();
				c->Where = s;
				c->System = sys;
				std::uint32_t h = H(HashText(s->Name), HashText(sys->Name), 0xC1F1u);
				c->Hawk = Unit(h) * 1.2 - 0.6;
				c->Liberty = Unit(h >> 7) * 1.6 - 0.8;
				c->Crime = 0.05 + 0.15 * Unit(h >> 11) + (s->Lawless ? 0.3 : 0);
				c->Care = 0.15 + 0.4 * Unit(h >> 13);
				c->Learning = 0.15 + 0.4 * Unit(h >> 17);
				c->Trend = static_cast<int>((h >> 3) % 12u);
				Worlds.Add(c);
			}
		}
		// A fallen world's record goes; its people are elsewhere now.
		for (int i = 0; i < Worlds.Count();)
		{
			if (!Stands(Worlds[i]->Where)) Worlds.RemoveAt(i);
			else i++;
		}
	}

	void Civilization::Moods(Galaxy& galaxy, double jd)
	{
		for (const auto& c : Worlds)
		{
			auto s = c->Where;
			bool war = AtWar(galaxy, s->Owner);
			if (war)
			{
				c->WarDays += MonthDays;
				c->Weariness = cs::Math::Min(1.0, c->Weariness + 0.025 + (c->Hawk < 0 ? 0.02 : 0));
			}
			else
			{
				c->WarDays = 0;
				c->Weariness *= 0.85;
			}
			double target = (Wellbeing(s) - 0.6) * 1.2 - s->Unrest * 0.9 - c->Weariness * 0.5 - c->Crime * 0.3 - c->Infected * 0.8
				+ (s->OccupiedFrom != nullptr ? -0.5 : 0);
			double before = c->Approval;
			c->Approval = cs::Math::Clamp(c->Approval + (target - c->Approval) * 0.15, -1.0, 1.0);
			// Discontent feeds unrest a little each month, contentment calms it.
			if (c->Approval < -0.3) s->Unrest = cs::Math::Min(1.0, s->Unrest + 0.01 * (-c->Approval));
			else if (c->Approval > 0.3) s->Unrest = cs::Math::Max(0.0, s->Unrest - 0.005);
			if (before >= -0.4 && c->Approval < -0.4)
			{
				auto e = Make(SocialKind::Mood, jd, s, c->System);
				e->Amount = c->Approval;
				Publish(galaxy, e);
			}
		}
	}

	void Civilization::Personas(Society& society, double jd)
	{
		(void)jd;
		if (society.NextId - 1 <= personaUpTo) return;
		static const Trait All[] = { Trait::Greedy, Trait::Pious, Trait::Reckless, Trait::Ambitious, Trait::Loyal, Trait::Vengeful,
			Trait::Charming, Trait::Honest, Trait::Cruel, Trait::Curious, Trait::Brave, Trait::Gifted };
		for (const auto& h : society.Houses)
			for (const auto& c : h->Members)
			{
				if (c->Persona) continue;
				std::uint32_t r = H(static_cast<std::uint32_t>(c->Id), 0x9E450u);
				int n = 2 + static_cast<int>(r % 2u);
				for (int i = 0; i < n; i++) c->Traits |= static_cast<int>(All[H(r, static_cast<std::uint32_t>(i)) % 12u]);
				// Children take after the head of the house now and then.
				if (auto head = h->Head(); head != nullptr && head != c && head->Persona && Roll(r >> 5, 0.4)) c->Traits |= head->Traits & -head->Traits;
				// Honest and Greedy, Cruel and Charming sit badly: the later roll wins.
				if ((c->Traits & static_cast<int>(Trait::Honest)) && (c->Traits & static_cast<int>(Trait::Greedy))) c->Traits &= ~static_cast<int>(Trait::Honest);
				c->Goal = static_cast<Ambition>((r >> 9) % 8u);
				if (Has(c, Trait::Pious) && Roll(r >> 13, 0.5)) c->Goal = Ambition::Faith;
				if (Has(c, Trait::Greedy) && Roll(r >> 15, 0.5)) c->Goal = Ambition::Wealth;
				c->Skill = 0.2 + 0.6 * Unit(r >> 11) + (Has(c, Trait::Gifted) ? 0.2 : 0);
				c->Persona = true;
			}
		personaUpTo = society.NextId - 1;
	}

	void Civilization::Ties(Galaxy& galaxy, Society& society, double jd)
	{
		auto adults = Adults(society, jd);
		if (adults.Count() < 2) return;
		// People who share a world meet; the charming make friends faster, the vengeful and cruel fall out.
		cs::List<cs::Ref<Citizen>> byWorld;
		for (int i = 0; i < adults.Count(); i++)
		{
			auto a = adults[i];
			if (a->House == nullptr || a->House->Home == nullptr) continue;
			std::uint32_t r = H(static_cast<std::uint32_t>(a->Id), DayOf(jd), 0x71E5u);
			double friendly = 0.05 + (Has(a, Trait::Charming) ? 0.06 : 0) + (Has(a, Trait::Curious) ? 0.02 : 0);
			if (Roll(r, friendly))
			{
				// Someone else on the same world, of another house.
				byWorld.Clear();
				for (const auto& b : adults)
					if (b != a && b->House != a->House && b->House != nullptr && b->House->Home == a->House->Home) byWorld.Add(b);
				if (byWorld.Count() > 0)
				{
					auto b = Pick(byWorld, r >> 8);
					auto existing = TieOf(a, b);
					if (existing == nullptr)
					{
						bool rival = a->House->Business != nullptr && b->House->Business != nullptr && a->House->Business->Kind == b->House->Business->Kind
							&& a->House->Business->Trading() && b->House->Business->Trading() && Roll(r >> 4, 0.5);
						Bind(a, b, rival ? TieKind::Rival : TieKind::Friend, 0.3 + 0.3 * Unit(r >> 12), jd);
						auto e = Make(rival ? SocialKind::Rivalry : SocialKind::Friendship, jd, a->House->Home, a->House->System);
						e->Who = a;
						e->Whom = b;
						e->House = a->House;
						e->OtherHouse = b->House;
						if (rival || Roll(r >> 16, 0.3)) Publish(galaxy, e);
					}
					else existing->Strength = cs::Math::Min(1.0, existing->Strength + 0.1);
				}
			}
			// A friendship soured.
			for (const auto& t : a->Ties.ToArray())
			{
				if (t->Kind != TieKind::Friend || t->Other == nullptr || !t->Other->Alive()) continue;
				double sour = 0.004 + (Has(a, Trait::Vengeful) ? 0.01 : 0) + (Has(a, Trait::Cruel) ? 0.01 : 0) + (Has(t->Other, Trait::Greedy) ? 0.005 : 0);
				if (!Roll(H(r, static_cast<std::uint32_t>(t->Other->Id)), sour)) continue;
				Bind(a, t->Other, TieKind::Enemy, 0.5, jd);
				auto e = Make(SocialKind::FallingOut, jd, a->House->Home, a->House->System);
				e->Who = a;
				e->Whom = t->Other;
				e->House = a->House;
				e->OtherHouse = t->Other->House;
				Publish(galaxy, e);
				break;
			}
		}
		// A trip to see someone binds the two closer.
		for (const auto& t : society.Trips)
			if (t->Stage == TripStage::Done && t->Meeting != nullptr && t->Traveller != nullptr && t->HomeJd > jd - MonthDays && t->HomeJd <= jd
				&& t->Meeting->House != t->Traveller->House)
			{
				auto tie = TieOf(t->Traveller, t->Meeting);
				if (tie == nullptr) Bind(t->Traveller, t->Meeting, TieKind::Friend, 0.4, jd);
				else if (tie->Kind == TieKind::Friend || tie->Kind == TieKind::Patron) tie->Strength = cs::Math::Min(1.0, tie->Strength + 0.15);
			}
	}

	void Civilization::Prune()
	{
		if (Events.Count() > MaxEvents) Events.RemoveRange(0, Events.Count() - MaxEvents);
		// Organisations long gone leave the books after a while.
		int ended = 0;
		for (const auto& o : Orgs)
			if (!o->Active()) ended++;
		for (int i = 0; i < Orgs.Count() && ended > 60;)
		{
			if (!Orgs[i]->Active()) { Orgs.RemoveAt(i); ended--; }
			else i++;
		}
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(SocialKind, "SpaceGod.Core.SocialKind")
	SG_ENUM(OrgKind, "SpaceGod.Core.OrgKind")
	SG_ENUM(Ambition, "SpaceGod.Core.Ambition")
	SG_ENUM(TieKind, "SpaceGod.Core.TieKind")

	SG_SERIALIZE(Tie, "SpaceGod.Core.Tie",
		SG_F(Other), SG_F(Kind), SG_F(Strength), SG_F(SinceJd))

	SG_SERIALIZE(SocialEvent, "SpaceGod.Core.SocialEvent",
		SG_F(Id), SG_F(Kind), SG_F(Jd), SG_F(System), SG_F(Where), SG_F(Faction), SG_F(Rival), SG_F(Who), SG_F(Whom), SG_F(House), SG_F(OtherHouse),
		SG_F(Business), SG_F(Org), SG_F(OtherOrg), SG_F(Amount), SG_F(Seed), SG_F(Unverified), SG_F(RunBy), SG_F(Slant), SG_F(Steered))

	SG_SERIALIZE(Civics, "SpaceGod.Core.Civics",
		SG_F(Where), SG_F(System), SG_F(Approval), SG_F(Hawk), SG_F(Liberty), SG_F(Crime), SG_F(Infected), SG_F(Immune), SG_F(Weariness), SG_F(PressTrust),
		SG_F(Care), SG_F(Learning), SG_F(Trend), SG_F(TrendHeat), SG_F(Drift), SG_F(Faith), SG_F(FaithShare), SG_F(QuarantineUntil), SG_F(WarDays),
		SG_F(Landmark), SG_F(Monument))

	SG_SERIALIZE(Org, "SpaceGod.Core.Org",
		SG_F(Id), SG_F(Kind), SG_F(Name), SG_F(NameSeed), SG_F(Seat), SG_F(System), SG_F(Faction), SG_F(Leader), SG_F(Members), SG_F(Owner), SG_F(Funds),
		SG_F(Strength), SG_F(Stance), SG_F(Rival), SG_F(Parent), SG_F(Score), SG_F(FoundedJd), SG_F(EndedJd))

	SG_SERIALIZE(Civilization, "SpaceGod.Core.Civilization",
		SG_F(NextId), SG_F(Events), SG_F(Worlds), SG_F(Orgs), SG_F(Tally), SG_F(nextDayJd), SG_F(nextMonthJd), SG_F(personaUpTo),
		SG_OPT(Kinship), // US-T277, absent from saves made before it
		SG_OPT(Politics), // US-T278
		SG_OPT(Strife), // US-T280
		SG_OPT(Underworld), // US-T282
		SG_OPT(Commerce), // US-T283
		SG_OPT(Culture), // US-T285
		SG_OPT(Health), // US-T286
		SG_OPT(Press)) // US-T288
}
