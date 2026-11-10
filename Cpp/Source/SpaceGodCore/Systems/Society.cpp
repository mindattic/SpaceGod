// US-T273: the people of the galaxy, living with nobody at the controls (see Systems/Society.h).

#include "Systems/Society.h"

#include "Combat/Diplomacy.h"
#include "Economy/Colonisation.h"
#include "Economy/Settlement.h"
#include "Economy/Economy.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/Civilization.h" // US-T275
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/Territory.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(CitizenRole v)
	{
		switch (v)
		{
			case CitizenRole::Head: return "Head";
			case CitizenRole::Spouse: return "Spouse";
			case CitizenRole::Heir: return "Heir";
			case CitizenRole::Child: return "Child";
			case CitizenRole::Governor: return "Governor";
		}
		return "";
	}

	const char* ToString(BusinessKind v)
	{
		switch (v)
		{
			case BusinessKind::TradingHouse: return "TradingHouse";
			case BusinessKind::Shipwright: return "Shipwright";
			case BusinessKind::MiningConcern: return "MiningConcern";
			case BusinessKind::Refinery: return "Refinery";
			case BusinessKind::Brokerage: return "Brokerage";
			case BusinessKind::Shipping: return "Shipping";
			case BusinessKind::Media: return "Media";
			case BusinessKind::Hospitality: return "Hospitality";
		}
		return "";
	}

	const char* ToString(BusinessStatus v)
	{
		switch (v)
		{
			case BusinessStatus::Trading: return "Trading";
			case BusinessStatus::Acquired: return "Acquired";
			case BusinessStatus::Bankrupt: return "Bankrupt";
		}
		return "";
	}

	const char* ToString(TripPurpose v)
	{
		switch (v)
		{
			case TripPurpose::Business: return "Business";
			case TripPurpose::Conference: return "Conference";
			case TripPurpose::FamilyVisit: return "FamilyVisit";
			case TripPurpose::Holiday: return "Holiday";
			case TripPurpose::Honeymoon: return "Honeymoon";
			case TripPurpose::Pilgrimage: return "Pilgrimage";
			case TripPurpose::Study: return "Study";
			case TripPurpose::Festival: return "Festival";
			case TripPurpose::Match: return "Match";
		}
		return "";
	}

	const char* ToString(TripStage v)
	{
		switch (v)
		{
			case TripStage::Planned: return "Planned";
			case TripStage::Outbound: return "Outbound";
			case TripStage::There: return "There";
			case TripStage::Homeward: return "Homeward";
			case TripStage::Done: return "Done";
			case TripStage::Cancelled: return "Cancelled";
		}
		return "";
	}

	const char* ToString(FeedKind v)
	{
		switch (v)
		{
			case FeedKind::News: return "News";
			case FeedKind::Commercial: return "Commercial";
			case FeedKind::Spam: return "Spam";
			case FeedKind::Message: return "Message";
		}
		return "";
	}

	namespace
	{
		/// The most days one update catches up on: a longer jump runs the last of them only.
		constexpr int MaxCatchUpDays = 400;

		/// A fixed mix of numbers (a 32-bit finaliser), for the choices that must come out the same on every peer.
		std::uint32_t Mix(std::uint32_t x)
		{
			x ^= x >> 16; x *= 0x7feb352du;
			x ^= x >> 15; x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		std::uint32_t H(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0, std::uint32_t d = 0)
		{
			return Mix(a ^ Mix(b + 0x9e3779b9u) ^ Mix(c + 0x85ebca6bu) ^ Mix(d + 0xc2b2ae35u));
		}

		std::uint32_t DayOf(double jd) { return static_cast<std::uint32_t>(static_cast<long long>(cs::Math::Floor(jd))); }

		/// A share of 0..1 from a hash.
		double Unit(std::uint32_t h) { return (h % 100003u) / 100003.0; }

		template <class T> const T& Pick(const cs::List<T>& list, std::uint32_t h) { return list[static_cast<int>(h % static_cast<std::uint32_t>(list.Count()))]; }
		template <class T, std::size_t N> T Pick(const T (&arr)[N], std::uint32_t h) { return arr[h % N]; }

		bool Stands(const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && s->Stage != ColonyStage::Ruin && s->Kind != SettlementKind::Haven && s->Owner != nullptr; }

		const char* const Given[] = {
			"Ama", "Dev", "Ilse", "Tomas", "Mei", "Kofi", "Sana", "Pavel", "Noor", "Rafael", "Yuki", "Odile", "Bram", "Leila", "Arjun",
			"Zofia", "Emeka", "Ines", "Haruto", "Freya", "Malik", "Elena", "Teo", "Aiyana", "Soren", "Priya", "Joaquin", "Nadia", "Kai",
			"Amara", "Lucien", "Wren", "Idris", "Mira", "Bastian", "Talia", "Oren", "Saoirse", "Hamza", "Linnea", "Dario", "Chiamaka",
			"Ezra", "Ayesha", "Viktor", "Imani", "Callum", "Rosalind", "Tariq", "Junie", "Anselm", "Yara", "Niko", "Ottilie",
		};
		const char* const Surnames[] = {
			"Okoro", "Halvorsen", "Vasquez", "Tanaka", "Abara", "Lindqvist", "Moreau", "Chen", "Petrov", "Achterberg", "Nakamura",
			"Oyelaran", "Brandt", "Castellanos", "Iyer", "Kowalczyk", "Mbeki", "Sorensen", "Delacroix", "Haddad", "Novak", "Quispe",
			"Ferreira", "Albescu", "Kwan", "Rasmussen", "Adeyemi", "Volkov", "Marchetti", "Osei", "Thorne", "Valdivia", "Kaur",
			"Lachance", "Szabo", "Mwangi", "Calder", "Yilmaz", "Arceneaux", "Bergstrom", "Okafor", "Ishikawa", "Drummond", "Zeller",
		};

		cs::String Suffix(BusinessKind k, std::uint32_t h)
		{
			switch (k)
			{
				case BusinessKind::TradingHouse: return Pick({ "& Daughters", "Trading", "Mercantile", "& Sons", "Exchange" }, h);
				case BusinessKind::Shipwright: return Pick({ "Yards", "Shipwrights", "Hullworks", "Drydocks" }, h);
				case BusinessKind::MiningConcern: return Pick({ "Mining Co.", "Extraction", "Deep Ore", "Prospecting" }, h);
				case BusinessKind::Refinery: return Pick({ "Refining", "Smelters", "Foundry", "Processing" }, h);
				case BusinessKind::Brokerage: return Pick({ "Brokerage", "Capital", "Insurance", "Bank" }, h);
				case BusinessKind::Shipping: return Pick({ "Lines", "Freight", "Haulage", "Couriers" }, h);
				case BusinessKind::Media: return Pick({ "Broadcasting", "Media", "Newswire", "Signal" }, h);
				case BusinessKind::Hospitality: return Pick({ "Hotels", "Resorts", "Grand Hotel", "Hospitality" }, h);
			}
			return "Holdings";
		}

		const char* Line(BusinessKind k)
		{
			switch (k)
			{
				case BusinessKind::TradingHouse: return "Trade";
				case BusinessKind::Shipwright: return "Shipbuilding";
				case BusinessKind::MiningConcern: return "Mining";
				case BusinessKind::Refinery: return "Refining";
				case BusinessKind::Brokerage: return "Finance";
				case BusinessKind::Shipping: return "Shipping";
				case BusinessKind::Media: return "Media";
				case BusinessKind::Hospitality: return "Tourism";
			}
			return "Trade";
		}

		BusinessKind KindFor(const cs::Ref<Settlement>& s, std::uint32_t h)
		{
			switch (s->Kind)
			{
				case SettlementKind::MiningOutpost: return Pick({ BusinessKind::MiningConcern, BusinessKind::Refinery, BusinessKind::Shipping, BusinessKind::MiningConcern }, h);
				case SettlementKind::Station: return Pick({ BusinessKind::Shipping, BusinessKind::Shipwright, BusinessKind::Brokerage, BusinessKind::Hospitality, BusinessKind::TradingHouse }, h);
				default: return static_cast<BusinessKind>(h % 8u);
			}
		}

		cs::Ref<StarSystem> SystemOf(Galaxy& galaxy, const cs::Ref<Settlement>& s)
		{
			if (s == nullptr) return nullptr;
			for (const auto& sys : galaxy.Systems)
				if (sys->World != nullptr && sys->World->Economy != nullptr && sys->World->Economy->Settlements.Contains(s)) return sys;
			return nullptr;
		}

		cs::Ref<Diplomacy> DiplomacyOf(Galaxy& galaxy) { return galaxy.Polities != nullptr ? galaxy.Polities->Diplomacy : nullptr; }

		bool Hostile(Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
		{
			if (a == nullptr || b == nullptr || a == b) return false;
			if (a->IsPirate || b->IsPirate) return true;
			auto d = DiplomacyOf(galaxy);
			return d != nullptr && d->Hostile(a, b);
		}

		/// Whether pirates keep a haven in a system (trips there may be robbed).
		bool PirateHeld(const cs::Ref<StarSystem>& sys)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return false;
			for (const auto& s : sys->World->Economy->Settlements)
				if (s->Kind == SettlementKind::Haven && !s->Fallen) return true;
			return false;
		}

		double TravelDays(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b, std::uint32_t h)
		{
			if (a == b || a == nullptr || b == nullptr) return Society::InSystemDays + (h % 5u);
			return 2 + Society::DaysPerLy * (a->PositionLy - b->PositionLy).Length() + (h % 4u);
		}

		int Year(double jd) { return 2000 + static_cast<int>(cs::Math::Floor((jd - 2451544.5) / 365.25)); }

		cs::String Money(double credits)
		{
			double a = cs::Math::Abs(credits);
			if (a >= 1e9) return cs::Cat(cs::Fmt(credits / 1e9, "0.#"), " billion credits");
			if (a >= 1e6) return cs::Cat(cs::Fmt(credits / 1e6, "0.#"), " million credits");
			return cs::Cat(cs::Fmt(credits, "#,0"), " credits");
		}

		/// US-T275: mirrors a society happening as a Civilization event (no feed item and no log: Society posts and logs its own),
		/// so the modules on top can react to it (a feud to a buyout, an inheritance quarrel to a death).
		void Mirror(Galaxy& galaxy, SocialKind kind, double jd, const cs::Ref<Settlement>& where, const cs::Ref<StarSystem>& sys,
			const cs::Ref<Citizen>& who, const cs::Ref<Citizen>& whom, const cs::Ref<Household>& house, const cs::Ref<Household>& other,
			const cs::Ref<Business>& business, double amount)
		{
			auto civ = Civilization::Find(galaxy);
			if (civ == nullptr) return;
			auto e = Civilization::Make(kind, jd, where, sys);
			e->Faction = where != nullptr ? where->Owner : nullptr;
			e->Who = who;
			e->Whom = whom;
			e->House = house;
			e->OtherHouse = other;
			e->Business = business;
			e->Amount = amount;
			civ->Publish(galaxy, e, false, false);
		}

		cs::String SystemName(const cs::Ref<StarSystem>& s) { return s != nullptr ? s->Name : cs::String("deep space"); }

		bool Adult(const cs::Ref<Citizen>& c, double jd) { return c != nullptr && c->Alive() && c->AgeYears(jd) >= 18; }
	}

	// ---------------------------------------------------------------- the people

	cs::String Citizen::FullName() const { return House != nullptr ? cs::Cat(Given, " ", House->Surname) : Given; }

	cs::Ref<Citizen> Household::Head() const
	{
		for (const auto& c : Members)
			if (c->Alive() && c->Role == CitizenRole::Head) return c;
		return nullptr;
	}

	int Household::Living() const
	{
		int n = 0;
		for (const auto& c : Members)
			if (c->Alive()) n++;
		return n;
	}

	cs::String FeedItem::ToString() const
	{
		if (Event != nullptr && Text.empty())
		{
			cs::String text = Event->ToString(); // US-T275: worded by the Narrator when shown
			switch (Kind)
			{
				case FeedKind::Message: return cs::Cat("[Message] ", text);
				case FeedKind::Commercial: return cs::Cat("[Ad] ", text);
				case FeedKind::Spam: return cs::Cat("[Spam] ", text);
				default: return cs::Cat("[News] ", text);
			}
		}
		switch (Kind)
		{
			case FeedKind::Message: return cs::Cat("[Message] ", From, " to ", To, ": ", Text);
			case FeedKind::Commercial: return cs::Cat("[Ad] ", From, ": ", Text);
			case FeedKind::Spam: return cs::Cat("[Spam] ", From, ": ", Text);
			default: return cs::Cat("[News] ", Text);
		}
	}

	// ---------------------------------------------------------------- the society

	cs::Ref<Society> Society::Of(Galaxy& galaxy)
	{
		auto t = Territory::Of(galaxy);
		if (t->Society == nullptr) t->Society = cs::New<Society>();
		return t->Society;
	}

	cs::Ref<Society> Society::Find(const Galaxy& galaxy) { return galaxy.Territory != nullptr ? galaxy.Territory->Society : nullptr; }

	cs::Ref<Citizen> Society::CitizenById(int id) const
	{
		for (const auto& h : Houses)
			for (const auto& c : h->Members)
				if (c->Id == id) return c;
		return nullptr;
	}

	cs::Ref<Business> Society::BusinessById(int id) const
	{
		for (const auto& b : Businesses)
			if (b->Id == id) return b;
		return nullptr;
	}

	cs::Ref<Bribe> Society::BribeById(int id) const
	{
		for (const auto& b : Bribes)
			if (b->Id == id) return b;
		return nullptr;
	}

	cs::Ref<Household> Society::HouseById(int id) const
	{
		for (const auto& h : Houses)
			if (h->Id == id) return h;
		return nullptr;
	}

	cs::List<cs::Ref<Household>> Society::HousesOf(const cs::Ref<Settlement>& s) const
	{
		cs::List<cs::Ref<Household>> r;
		for (const auto& h : Houses)
			if (!h->Scattered && h->Living() > 0 && (s == nullptr || h->Home == s)) r.Add(h);
		return r;
	}

	cs::List<cs::Ref<Trip>> Society::ActiveTrips() const
	{
		cs::List<cs::Ref<Trip>> r;
		for (const auto& t : Trips)
			if (t->Active()) r.Add(t);
		return r;
	}

	cs::List<cs::Ref<FeedItem>> Society::Latest(int n) const
	{
		cs::List<cs::Ref<FeedItem>> r;
		for (int i = cs::Math::Max(0, Feed.Count() - n); i < Feed.Count(); i++) r.Add(Feed[i]);
		return r;
	}

	void Society::Update(Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(jd)) return;
		if (cs::Double::IsNaN(nextDayJd))
		{
			// The first look founds every world's houses at once, so a new game opens with its people already living.
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

	void Society::Day(Galaxy& galaxy, double jd)
	{
		for (const auto& t : Trips.ToArray())
			if (t->Active()) Travel(galaxy, t, jd);
		for (const auto& h : Houses.ToArray())
			if (!h->Scattered && !cs::Double::IsNaN(h->NextEventJd) && jd >= h->NextEventJd) Life(galaxy, h, jd);
		PlanTrips(galaxy, jd);
		Media(galaxy, jd);
	}

	void Society::Month(Galaxy& galaxy, double jd)
	{
		Census(galaxy, jd);
		for (const auto& b : Businesses.ToArray())
			if (b->Trading()) Books(galaxy, b, jd);
		for (const auto& b : Businesses.ToArray())
			if (b->Trading()) Branches(galaxy, b, jd);
		Conferences_(galaxy, jd);
		Bribery(galaxy, jd);
	}

	// ---------------------------------------------------------------- census: houses founded, houses driven off

	void Society::Census(Galaxy& galaxy, double jd)
	{
		bool first = Houses.Count() == 0;
		for (const auto& sys : galaxy.Systems)
		{
			auto w = sys->World;
			if (w == nullptr || w->Economy == nullptr) continue;
			for (const auto& s : w->Economy->Settlements.ToArray())
			{
				if (!Stands(s) || s->Population < 200) continue;
				int wanted = cs::Math::Clamp(static_cast<int>(cs::Math::Floor(cs::Math::Log10(s->Population))) - 2, 1, MaxHouses);
				int have = HousesOf(s).Count();
				// A new world gets its houses one a month; the opening census founds them all.
				for (int i = have; i < wanted && (first || i == have); i++) FoundHouse(galaxy, sys, s, jd, i);
			}
		}
		for (const auto& h : Houses.ToArray())
			if (!h->Scattered && h->Living() > 0 && !Stands(h->Home)) Rehome(galaxy, h, jd);
	}

	void Society::Rehome(Galaxy& galaxy, const cs::Ref<Household>& h, double jd)
	{
		auto old = h->Home;
		auto flag = old != nullptr ? (old->OccupiedFrom != nullptr ? old->OccupiedFrom : old->Owner) : nullptr;
		cs::Ref<Settlement> best;
		if (flag != nullptr && galaxy.Polities != nullptr)
			for (const auto& s : galaxy.Polities->SettlementsOf(flag))
				if (s != old && Stands(s) && (best == nullptr || s->Population > best->Population)) best = s;
		auto oldSys = h->System;
		if (best == nullptr)
		{
			h->Scattered = true;
			if (h->Business != nullptr && h->Business->Trading()) Close(galaxy, h->Business, jd, true);
			Post(FeedKind::News, jd, oldSys, "", "", cs::Cat("House ", h->Surname, " of ", old != nullptr ? old->Name : cs::String("?"), " is scattered: nowhere of its flag would take it in"));
			return;
		}
		h->Home = best;
		h->System = SystemOf(galaxy, best);
		if (h->Business != nullptr && h->Business->Trading())
		{
			h->Business->Home = best;
			h->Business->System = h->System;
			h->Business->Capital *= 0.6;
		}
		Post(FeedKind::News, jd, h->System, "", "", cs::Cat("House ", h->Surname, " flees ", old != nullptr ? old->Name : cs::String("its old home"), " for ", best->Name, " in ", SystemName(h->System)));
	}

	cs::Ref<Household> Society::FoundHouse(Galaxy& galaxy, const cs::Ref<StarSystem>& sys, const cs::Ref<Settlement>& s, double jd, int seed)
	{
		(void)galaxy;
		std::uint32_t h = H(static_cast<std::uint32_t>(s->Id), static_cast<std::uint32_t>(NextId), static_cast<std::uint32_t>(seed), DayOf(jd));
		auto house = cs::New<Household>();
		house->Id = NextId++;
		// Two houses of one name on one world would read as one: walk on to a free surname.
		for (std::uint32_t k = 0; k < 64; k++)
		{
			cs::String name = Pick(Surnames, h + k * 7919u);
			bool taken = false;
			for (const auto& other : HousesOf(s))
				if (other->Surname == name) { taken = true; break; }
			house->Surname = name;
			if (!taken) break;
		}
		house->Home = s;
		house->System = sys;
		house->FoundedJd = jd;
		double scale = cs::Math::Max(1.0, cs::Math::Log10(cs::Math::Max(10.0, s->Population)));
		house->Wealth = 1e5 * scale * (1 + 2 * Unit(h >> 3));
		house->NextEventJd = cs::Math::Floor(jd) + 30 + (h % 240u);
		Houses.Add(house);

		auto head = Born(house, CitizenRole::Head, jd - 365.25 * (34 + (h % 26u)), H(h, 1));
		auto spouse = Born(house, CitizenRole::Spouse, jd - 365.25 * (32 + ((h >> 5) % 24u)), H(h, 2));
		head->Spouse = spouse;
		spouse->Spouse = head;
		int heirs = static_cast<int>((h >> 9) % 3u), children = static_cast<int>((h >> 13) % 3u);
		for (int i = 0; i < heirs; i++) Born(house, CitizenRole::Heir, jd - 365.25 * (18 + (H(h, 10 + i) % 12u)), H(h, 20 + i));
		for (int i = 0; i < children; i++) Born(house, CitizenRole::Child, jd - 365.25 * (1 + (H(h, 30 + i) % 16u)), H(h, 40 + i));

		Open(house, jd, H(h, 99), 2e6 * scale * (0.5 + 2 * Unit(h >> 17)));
		Founded++;
		return house;
	}

	cs::Ref<Citizen> Society::Born(const cs::Ref<Household>& h, CitizenRole role, double bornJd, std::uint32_t seed)
	{
		auto c = cs::New<Citizen>();
		c->Id = NextId++;
		c->House = h;
		c->Role = role;
		c->BornJd = bornJd;
		// Siblings go by different names.
		for (std::uint32_t k = 0; k < 32; k++)
		{
			c->Given = Pick(Given, seed + k * 104729u);
			bool taken = false;
			for (const auto& m : h->Members)
				if (m->Given == c->Given) { taken = true; break; }
			if (!taken) break;
		}
		h->Members.Add(c);
		return c;
	}

	cs::Ref<Business> Society::Open(const cs::Ref<Household>& h, double jd, std::uint32_t seed, double capital)
	{
		auto b = cs::New<Business>();
		b->Id = NextId++;
		b->Kind = KindFor(h->Home, seed);
		b->Name = cs::Cat(h->Surname, " ", Suffix(b->Kind, seed >> 4));
		b->Owner = h;
		b->Home = h->Home;
		b->System = h->System;
		b->Capital = capital;
		b->FoundedJd = jd;
		h->Business = b;
		Businesses.Add(b);
		return b;
	}

	// ---------------------------------------------------------------- a house's own news: births, weddings, deaths, ventures

	void Society::Life(Galaxy& galaxy, const cs::Ref<Household>& h, double jd)
	{
		std::uint32_t r = H(static_cast<std::uint32_t>(h->Id), DayOf(jd), 0x11Fu);
		h->NextEventJd = jd + 60 + (r % 180u);
		auto sys = h->System;

		// Children come of age.
		for (const auto& c : h->Members)
			if (c->Alive() && c->Role == CitizenRole::Child && c->AgeYears(jd) >= 18) c->Role = CitizenRole::Heir;

		// The old die, and an heir takes the house on.
		for (const auto& c : h->Members.ToArray())
		{
			if (!c->Alive()) continue;
			double age = c->AgeYears(jd);
			if (age < 70 || Unit(H(r, static_cast<std::uint32_t>(c->Id))) > (age - 70) / 30) continue;
			c->DiedJd = jd;
			Deaths++;
			Mirror(galaxy, SocialKind::Death, jd, h->Home, sys, c, nullptr, h, nullptr, nullptr, age);
			if (c->Spouse != nullptr && c->Spouse->Spouse == c) c->Spouse->Spouse = nullptr;
			if (c->Role == CitizenRole::Head)
			{
				cs::Ref<Citizen> heir;
				for (const auto& m : h->Members)
					if (m->Alive() && m->Role == CitizenRole::Heir && (heir == nullptr || m->BornJd < heir->BornJd)) heir = m;
				if (heir == nullptr)
					for (const auto& m : h->Members)
						if (Adult(m, jd) && m->Role != CitizenRole::Governor) { heir = m; break; }
				if (heir != nullptr)
				{
					heir->Role = CitizenRole::Head;
					Post(FeedKind::News, jd, sys, "", "", cs::Cat(c->FullName(), " of ", h->Home->Name, " has died at ", static_cast<int>(age), "; ", heir->Given, " takes the house and ",
						h->Business != nullptr && h->Business->Trading() ? h->Business->Name : cs::String("its affairs"), " on"));
				}
			}
			else
				Post(FeedKind::Message, jd, sys, c->Spouse != nullptr ? c->Spouse->FullName() : cs::Cat("House ", h->Surname), "friends of the family",
					cs::Cat("We lost ", c->Given, " this week. Thank you for your kind words."));
		}
		if (h->Living() == 0)
		{
			if (h->Business != nullptr && h->Business->Trading()) Close(galaxy, h->Business, jd, true);
			Post(FeedKind::News, jd, sys, "", "", cs::Cat("The last of House ", h->Surname, " is gone; the family has died out on ", h->Home->Name));
			h->NextEventJd = cs::Double::NaN;
			return;
		}

		double roll = Unit(r >> 3);
		// A birth to a married couple young enough.
		if (roll < 0.35)
			for (const auto& c : h->Members)
				if (c->Alive() && c->Spouse != nullptr && c->Spouse->Alive() && c->AgeYears(jd) < 45 && (c->Role == CitizenRole::Head || c->Role == CitizenRole::Heir))
				{
					auto baby = Born(h, CitizenRole::Child, jd, H(r, 0xBAB1u));
					Births++;
					Mirror(galaxy, SocialKind::Birth, jd, h->Home, sys, baby, c, h, nullptr, nullptr, 0);
					Post(FeedKind::Message, jd, sys, c->FullName(), "family and friends", cs::Cat("Welcome to the world, little ", baby->Given, "!"));
					return;
				}

		// A wedding: a single heir marries into another house, met at home or across the stars.
		if (roll < 0.65)
		{
			cs::Ref<Citizen> single;
			for (const auto& c : h->Members)
				if (c->Alive() && c->Role == CitizenRole::Heir && c->Spouse == nullptr && c->TripId == 0 && c->AgeYears(jd) >= 20 && c->AgeYears(jd) <= 42) { single = c; break; }
			if (single != nullptr)
			{
				cs::List<cs::Ref<Citizen>> matches;
				for (const auto& other : Houses)
				{
					if (other == h || other->Scattered || Hostile(galaxy, h->Home->Owner, other->Home->Owner)) continue;
					for (const auto& c : other->Members)
						if (c->Alive() && c->Role == CitizenRole::Heir && c->Spouse == nullptr && c->TripId == 0 && c->AgeYears(jd) >= 20 && c->AgeYears(jd) <= 42) matches.Add(c);
				}
				if (matches.Count() > 0)
				{
					auto other = Pick(matches, r >> 7);
					single->Spouse = other;
					other->Spouse = single;
					Weddings++;
					auto oh = other->House;
					Mirror(galaxy, SocialKind::Wedding, jd, h->Home, sys, single, other, h, oh, nullptr, 0);
					cs::String text = cs::Cat(single->FullName(), " of ", h->Home->Name, " weds ", other->FullName(), " of ", oh->Home->Name, ": Houses ", h->Surname, " and ", oh->Surname, " are joined");
					Post(FeedKind::News, jd, sys, "", "", text);
					// Two great houses of different flags joined warm their owners to each other a little.
					auto a = h->Home->Owner, b = oh->Home->Owner;
					if (a != b && galaxy.Polities != nullptr && galaxy.Polities->Statecraft != nullptr)
					{
						galaxy.Polities->Statecraft->Sway(a, b, 2);
						galaxy.Polities->Statecraft->Sway(b, a, 2);
						Notable(galaxy, sys, jd, text, WorldEventKind::Diplomacy);
					}
					// And off they go: a honeymoon somewhere else.
					cs::List<cs::Ref<Settlement>> resorts;
					for (const auto& s2 : galaxy.Polities != nullptr ? galaxy.Polities->Settlements() : cs::List<cs::Ref<Settlement>>())
						if (Stands(s2) && s2 != h->Home && s2->Population > 1e5 && !Hostile(galaxy, h->Home->Owner, s2->Owner)) resorts.Add(s2);
					if (resorts.Count() > 0) Plan(galaxy, single, Pick(resorts, r >> 11), TripPurpose::Honeymoon, other, jd, H(r, 0x40Bu));
					return;
				}
			}
		}

		// A new venture: a rich house sets an heir up elsewhere under its flag, the house reaching out.
		if (roll < 0.85 && h->Business != nullptr && h->Business->Trading() && h->Business->Capital > 3e7)
		{
			cs::Ref<Citizen> heir;
			for (const auto& c : h->Members)
				if (c->Alive() && c->Role == CitizenRole::Heir && c->TripId == 0 && c->AgeYears(jd) >= 24) { heir = c; break; }
			if (heir == nullptr || galaxy.Polities == nullptr) return;
			cs::List<cs::Ref<Settlement>> sites;
			for (const auto& s2 : galaxy.Polities->SettlementsOf(h->Home->Owner))
				if (Stands(s2) && s2 != h->Home && s2->Population >= 200 && HousesOf(s2).Count() < MaxHouses) sites.Add(s2);
			if (sites.Count() == 0) return;
			auto site = Pick(sites, r >> 9);
			auto branch = cs::New<Household>();
			branch->Id = NextId++;
			branch->Surname = h->Surname;
			branch->Home = site;
			branch->System = SystemOf(galaxy, site);
			branch->FoundedJd = jd;
			branch->NextEventJd = jd + 120 + (r % 200u);
			h->Members.Remove(heir);
			heir->House = branch;
			heir->Role = CitizenRole::Head;
			branch->Members.Add(heir);
			if (heir->Spouse != nullptr && heir->Spouse->House == h)
			{
				h->Members.Remove(heir->Spouse);
				heir->Spouse->House = branch;
				heir->Spouse->Role = CitizenRole::Spouse;
				branch->Members.Add(heir->Spouse);
			}
			double stake = h->Business->Capital * 0.25;
			h->Business->Capital -= stake;
			Houses.Add(branch);
			auto b = Open(branch, jd, H(r, 0x5E7u), stake);
			Founded++;
			Post(FeedKind::News, jd, branch->System, "", "", cs::Cat(heir->FullName(), " leaves ", h->Home->Name, " to found ", b->Name, " on ", site->Name, " with ", Money(stake), " of the family's money"));
		}
	}

	// ---------------------------------------------------------------- business: the books, the branches, the end

	void Society::Books(Galaxy& galaxy, const cs::Ref<Business>& b, double jd)
	{
		auto s = b->Home;
		std::uint32_t r = H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0xB00Cu);
		double rate;
		if (!Stands(s)) rate = -0.05;
		else
		{
			double wellbeing = cs::Math::Clamp(s->Wellbeing, 0.0, 1.5);
			rate = YieldPerYear / 12 * wellbeing - 0.02 * s->Unrest + 0.0015 * b->Branches.Count() + 0.02 * (Unit(r) - 0.5);
			if (galaxy.Polities != nullptr && galaxy.Polities->AtWar(s->Owner)) rate -= b->Kind == BusinessKind::Shipwright ? -0.004 : 0.004;
			if (b->Kind == BusinessKind::Shipping && PirateHeld(b->System)) rate -= 0.006;
		}
		double earned = b->Capital * rate;
		b->Capital += earned;
		b->LastMonth = earned;
		if (earned > 0)
		{
			// Tax to the world's owner, a dividend to the house.
			if (Stands(s)) s->Owner->Treasury += earned * 0.1;
			if (b->Owner != nullptr) b->Owner->Wealth += earned * 0.2;
			b->Capital -= earned * 0.3;
		}
		if (b->Capital < 2e5) Close(galaxy, b, jd, false);
	}

	void Society::Branches(Galaxy& galaxy, const cs::Ref<Business>& b, double jd)
	{
		auto home = b->Home;
		if (!Stands(home)) return;
		auto d = DiplomacyOf(galaxy);
		// Trade cut off: a branch on a world its owner's ships may no longer dock at, or that has fallen, closes.
		for (const auto& br : b->Branches.ToArray())
		{
			bool cut = !Stands(br) || Hostile(galaxy, home->Owner, br->Owner) || (d != nullptr && !d->CanDock(home->Owner, br->Owner));
			if (!cut) continue;
			b->Branches.Remove(br);
			BranchesClosed++;
			cs::String text = cs::Cat(b->Name, " shuts its office on ", br->Name, ": trade between the ", home->Owner->Name, " and the ", br->Owner != nullptr ? br->Owner->Name : cs::String("ruins"), " is cut off");
			Post(FeedKind::News, jd, b->System, "", "", text);
			Notable(galaxy, b->System, jd, text, WorldEventKind::Economy);
		}
		// Trade established: a business with money to spare opens an office on a world it may trade with.
		std::uint32_t r = H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0xB4A2u);
		if (b->Branches.Count() >= 4 || b->Capital < 1.5e7 * (b->Branches.Count() + 1) || Unit(r) > 0.25 || galaxy.Polities == nullptr) return;
		cs::List<cs::Ref<Settlement>> markets;
		for (const auto& s : galaxy.Polities->Settlements())
			if (Stands(s) && s != home && s->Population >= 1e4 && !b->Branches.Contains(s) && !Hostile(galaxy, home->Owner, s->Owner) && (d == nullptr || d->CanDock(home->Owner, s->Owner))) markets.Add(s);
		if (markets.Count() == 0) return;
		auto at = Pick(markets, r >> 5);
		b->Branches.Add(at);
		b->Capital -= 2e6;
		BranchesOpened++;
		cs::String text = at->Owner != home->Owner
			? cs::Cat(b->Name, " of ", home->Name, " opens an office on ", at->Name, ": new trade between the ", home->Owner->Name, " and the ", at->Owner->Name)
			: cs::Cat(b->Name, " of ", home->Name, " opens an office on ", at->Name);
		Post(FeedKind::News, jd, b->System, "", "", text);
		if (at->Owner != home->Owner) Notable(galaxy, b->System, jd, text, WorldEventKind::Economy);
	}

	void Society::Close(Galaxy& galaxy, const cs::Ref<Business>& b, double jd, bool forced)
	{
		(void)forced;
		// The richest trading rival in its system buys it up, branches and all, if it can.
		cs::Ref<Business> buyer;
		for (const auto& o : Businesses)
			if (o != b && o->Trading() && o->System == b->System && o->Capital > 5e6 && (buyer == nullptr || o->Capital > buyer->Capital)) buyer = o;
		b->EndedJd = jd;
		if (b->Owner != nullptr)
		{
			if (b->Owner->Business == b) b->Owner->Business = nullptr;
			b->Owner->Wealth *= 0.5;
		}
		cs::String text;
		if (buyer != nullptr)
		{
			double price = cs::Math::Max(1e6, b->Capital);
			buyer->Capital -= price * 0.5;
			for (const auto& br : b->Branches)
				if (!buyer->Branches.Contains(br) && br != buyer->Home) buyer->Branches.Add(br);
			b->Status = BusinessStatus::Acquired;
			b->Buyer = buyer;
			Acquisitions++;
			Mirror(galaxy, SocialKind::Buyout, jd, b->Home, b->System, nullptr, nullptr, b->Owner, buyer->Owner, b, price * 0.5);
			text = cs::Cat(buyer->Name, " buys out the failing ", b->Name, " of ", b->Home != nullptr ? b->Home->Name : cs::String("?"), " for ", Money(price * 0.5));
		}
		else
		{
			b->Status = BusinessStatus::Bankrupt;
			Bankruptcies++;
			Mirror(galaxy, SocialKind::Bankruptcy, jd, b->Home, b->System, nullptr, nullptr, b->Owner, nullptr, b, 0);
			text = cs::Cat(b->Name, " of ", b->Home != nullptr ? b->Home->Name : cs::String("?"), " goes bankrupt; its staff are let go");
		}
		b->Capital = 0;
		b->Branches = cs::List<cs::Ref<Settlement>>();
		Post(FeedKind::News, jd, b->System, "", "", text);
		Notable(galaxy, b->System, jd, text, WorldEventKind::Economy);
	}

	// ---------------------------------------------------------------- trips: talked over, booked, flown, come home

	void Society::PlanTrips(Galaxy& galaxy, double jd)
	{
		if (Houses.Count() == 0 || galaxy.Polities == nullptr) return;
		int active = 0;
		for (const auto& t : Trips)
			if (t->Active()) active++;
		int tries = cs::Math::Min(6, 1 + Houses.Count() / 40);
		auto all = galaxy.Polities->Settlements();
		for (int i = 0; i < tries && active < MaxTrips; i++)
		{
			std::uint32_t r = H(DayOf(jd), static_cast<std::uint32_t>(i), 0x7219u);
			if (Unit(r) > 0.6) continue;
			auto h = Pick(Houses, r >> 3);
			if (h->Scattered || !Stands(h->Home)) continue;
			cs::List<cs::Ref<Citizen>> free;
			for (const auto& c : h->Members)
				if (Adult(c, jd) && c->TripId == 0) free.Add(c);
			if (free.Count() == 0) continue;
			auto who = Pick(free, r >> 7);
			double p = Unit(r >> 11);
			cs::Ref<Trip> trip;
			if (p < 0.3 && who->Spouse != nullptr && who->Spouse->Alive() && who->Spouse->House != h && who->Spouse->House->Home != h->Home)
				trip = Plan(galaxy, who, who->Spouse->House->Home, TripPurpose::FamilyVisit, who->Spouse, jd, r);
			else if (p < 0.65 && h->Business != nullptr && h->Business->Trading())
			{
				// Business travel: to one of its offices, or to meet another house's head on a world it trades with.
				auto b = h->Business;
				cs::Ref<Settlement> to = b->Branches.Count() > 0 ? Pick(b->Branches, r >> 13) : nullptr;
				if (to == nullptr)
				{
					cs::List<cs::Ref<Household>> contacts;
					for (const auto& o : Houses)
						if (o != h && !o->Scattered && Stands(o->Home) && o->Home != h->Home && !Hostile(galaxy, h->Home->Owner, o->Home->Owner)) contacts.Add(o);
					if (contacts.Count() > 0)
					{
						auto o = Pick(contacts, r >> 13);
						trip = Plan(galaxy, who, o->Home, TripPurpose::Business, o->Head(), jd, r);
					}
				}
				else trip = Plan(galaxy, who, to, TripPurpose::Business, nullptr, jd, r);
			}
			else
			{
				cs::List<cs::Ref<Settlement>> resorts;
				for (const auto& s : all)
					if (Stands(s) && s != h->Home && s->Population > 5e4 && !Hostile(galaxy, h->Home->Owner, s->Owner)) resorts.Add(s);
				if (resorts.Count() > 0) trip = Plan(galaxy, who, Pick(resorts, r >> 13), TripPurpose::Holiday, nullptr, jd, r);
			}
			if (trip != nullptr) active++;
		}
	}

	cs::Ref<Trip> Society::Plan(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, TripPurpose why, const cs::Ref<Citizen>& meeting, double jd, std::uint32_t h)
	{
		if (c == nullptr || to == nullptr || c->House == nullptr || c->House->Home == to) return nullptr;
		auto t = cs::New<Trip>();
		t->Id = NextId++;
		t->Traveller = c;
		t->Meeting = meeting;
		t->Purpose = why;
		t->From = c->House->Home;
		t->FromSystem = c->House->System;
		t->To = to;
		t->ToSystem = SystemOf(galaxy, to);
		t->PlannedJd = jd;
		double travel = TravelDays(t->FromSystem, t->ToSystem, h);
		t->DepartJd = cs::Math::Floor(jd) + (why == TripPurpose::Honeymoon ? 7 : 4 + (h % 21u));
		t->ArriveJd = t->DepartJd + travel;
		double stay = why == TripPurpose::Holiday || why == TripPurpose::Honeymoon || why == TripPurpose::Pilgrimage ? 7 + (h >> 4) % 14u
			: why == TripPurpose::Study ? 90 + (h >> 4) % 60u : why == TripPurpose::Festival ? 4 + (h >> 4) % 6u : 2 + (h >> 4) % 6u;
		t->LeaveJd = t->ArriveJd + stay;
		t->HomeJd = t->LeaveJd + travel;
		c->TripId = t->Id;
		Trips.Add(t);

		// Talked over first: a message to whoever they are going to see (or home, or the office).
		cs::String to_ = meeting != nullptr ? meeting->FullName() : (why == TripPurpose::Business && c->House->Business != nullptr ? cs::Cat(c->House->Business->Name, " office, ", to->Name) : cs::Cat("House ", c->House->Surname));
		int days = static_cast<int>(cs::Math::Floor(t->ArriveJd - jd));
		cs::String text;
		switch (why)
		{
			case TripPurpose::FamilyVisit: text = Pick({ cs::Cat("Booked passage to ", to->Name, ". I'll be with you in ", days, " days. Missed you."),
				cs::Cat("Coming to see you on ", to->Name, ". Leaving in ", static_cast<int>(t->DepartJd - jd), " days, put the kettle on.") }, h >> 6); break;
			case TripPurpose::Business: text = Pick({ cs::Cat("Can we meet on ", to->Name, " in ", days, " days? I'd like to talk terms in person."),
				cs::Cat("I'm flying out to ", to->Name, " on the ", Year(t->DepartJd), " schedule. Have the numbers ready."),
				cs::Cat("Heading to ", to->Name, " to look at the books. Arriving in ", days, " days.") }, h >> 6); break;
			case TripPurpose::Holiday: text = Pick({ cs::Cat("Finally taking that holiday: ", static_cast<int>(stay), " days on ", to->Name, ". Don't call unless it's on fire."),
				cs::Cat("Off to ", to->Name, " for a rest. Leaving in ", static_cast<int>(t->DepartJd - jd), " days.") }, h >> 6); break;
			case TripPurpose::Honeymoon: text = cs::Cat("We're away to ", to->Name, " for our honeymoon. Back in ", static_cast<int>(t->HomeJd - jd), " days!"); break;
			case TripPurpose::Conference: text = cs::Cat("Registered for the ", t->Conference != nullptr ? t->Conference->Name : cs::String("summit"), " on ", to->Name, ". See you there?"); break;
			case TripPurpose::Pilgrimage: text = Pick({ cs::Cat("I am going on pilgrimage to ", to->Name, ". Keep me in your prayers."), cs::Cat("Leaving for the holy places of ", to->Name, " in ", static_cast<int>(t->DepartJd - jd), " days.") }, h >> 6); break;
			case TripPurpose::Study: text = cs::Cat("Off to ", to->Name, " to study. Write to me!"); break;
			case TripPurpose::Festival: text = Pick({ cs::Cat("We're going to the festival on ", to->Name, "! Can't wait."), cs::Cat("Festival season on ", to->Name, ". Tickets booked.") }, h >> 6); break;
			case TripPurpose::Match: text = Pick({ cs::Cat("Got tickets for the big match on ", to->Name, "!"), cs::Cat("Flying to ", to->Name, " to see the game. Come on you lot!") }, h >> 6); break;
		}
		Post(FeedKind::Message, jd, t->FromSystem, c->FullName(), to_, text);
		return t;
	}

	void Society::Travel(Galaxy& galaxy, const cs::Ref<Trip>& t, double jd)
	{
		auto c = t->Traveller;
		auto home = c != nullptr && c->House != nullptr ? c->House->Home : nullptr;
		auto end = [&](TripStage stage, const cs::String& note) {
			t->Stage = stage;
			t->Note = note;
			if (c != nullptr && c->TripId == t->Id) c->TripId = 0;
			if (stage == TripStage::Done) TripsTaken++;
			else TripsCancelled++;
		};
		switch (t->Stage)
		{
			case TripStage::Planned:
			{
				if (jd < t->DepartJd) return;
				cs::String why;
				if (c == nullptr || !c->Alive()) why = "the traveller died";
				else if (!Stands(t->To)) why = cs::Cat(t->To->Name, " has fallen");
				// US-T286: a quarantined world takes no visitors.
				else if (auto civ = Civilization::Find(galaxy); civ != nullptr && civ->CivicsOf(t->To) != nullptr && civ->CivicsOf(t->To)->Quarantined(jd)) why = cs::Cat(t->To->Name, " is under quarantine");
				else if (home != nullptr && Hostile(galaxy, home->Owner, t->To->Owner)) why = cs::Cat("the ", home->Owner->Name, " and the ", t->To->Owner->Name, " are at war");
				if (!why.empty())
				{
					end(TripStage::Cancelled, why);
					if (c != nullptr && c->Alive())
						Post(FeedKind::Message, jd, t->FromSystem, c->FullName(), t->Meeting != nullptr ? t->Meeting->FullName() : cs::String("the booking office"),
							cs::Cat("I have to call off ", t->To->Name, ": ", why, ". Another time."));
					return;
				}
				t->Stage = TripStage::Outbound;
				return;
			}
			case TripStage::Outbound:
			{
				if (jd < t->ArriveJd) return;
				t->Stage = TripStage::There;
				std::uint32_t r = H(static_cast<std::uint32_t>(t->Id), DayOf(jd), 0x90Bu);
				if (t->ToSystem != t->FromSystem && PirateHeld(t->ToSystem) && Unit(r) < 0.15 && c != nullptr && c->House != nullptr)
				{
					double lost = cs::Math::Min(c->House->Wealth * 0.05, 5e5);
					c->House->Wealth -= lost;
					Robberies++;
					t->Note = cs::Cat("robbed of ", Money(lost), " by raiders on the way");
					Post(FeedKind::News, jd, t->ToSystem, "", "", cs::Cat("Raiders board a liner bound for ", t->To->Name, ": ", c->FullName(), " of ", home != nullptr ? home->Name : cs::String("?"), " among those robbed"));
				}
				return;
			}
			case TripStage::There:
			{
				if (jd < t->LeaveJd) return;
				t->Stage = TripStage::Homeward;
				if (t->Purpose == TripPurpose::Business && c != nullptr && c->House != nullptr && c->House->Business != nullptr && c->House->Business->Trading())
				{
					auto b = c->House->Business;
					double deal = b->Capital * 0.004 * (1 + Unit(H(static_cast<std::uint32_t>(t->Id), 0xDEA1u)));
					b->Capital += deal;
					Post(FeedKind::Message, jd, t->ToSystem, c->FullName(), cs::Cat(b->Name, " board"), cs::Cat("Signed on ", t->To->Name, ": worth ", Money(deal), " to us. Flying home."));
				}
				return;
			}
			case TripStage::Homeward:
				if (jd >= t->HomeJd) end(TripStage::Done, t->Note);
				return;
			default: return;
		}
	}

	// ---------------------------------------------------------------- conferences

	void Society::Conferences_(Galaxy& galaxy, double jd)
	{
		if (galaxy.Polities == nullptr) return;
		int open = 0;
		for (const auto& c : Conferences)
			if (!c->Closed) open++;
		std::uint32_t r = H(DayOf(jd), 0xC0Fu);
		if (open < MaxConferences && Unit(r) < 0.5)
		{
			// The big worlds host: one of the dozen most populous.
			auto all = galaxy.Polities->Settlements();
			cs::List<cs::Ref<Settlement>> big;
			for (const auto& s : all)
				if (Stands(s) && s->Population >= 1e5) big.Add(s);
			big.Sort([](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return a->Population > b->Population ? -1 : (a->Population < b->Population ? 1 : a->Id - b->Id); });
			if (big.Count() > 12) big.RemoveRange(12, big.Count() - 12);
			if (big.Count() > 0)
			{
				auto host = Pick(big, r >> 3);
				auto c = cs::New<Conference>();
				c->Id = NextId++;
				c->Trade = static_cast<BusinessKind>((r >> 7) % 8u);
				c->Host = host;
				c->System = SystemOf(galaxy, host);
				c->AnnouncedJd = jd;
				c->OpensJd = cs::Math::Floor(jd) + 60 + ((r >> 11) % 60u);
				c->ClosesJd = c->OpensJd + 3;
				c->Name = cs::Cat(host->Name, " ", Line(c->Trade), " ", Pick({ "Summit", "Expo", "Congress", "Fair", "Forum" }, r >> 15), " ", Year(c->OpensJd));
				Conferences.Add(c);
				Post(FeedKind::News, jd, c->System, "", "", cs::Cat("The ", c->Name, " is announced for ", static_cast<int>(c->OpensJd - jd), " days from now on ", host->Name));
				Post(FeedKind::Commercial, jd, c->System, cs::Cat(host->Name, " Visitors' Bureau"), "",
					cs::Cat("Register now for the ", c->Name, "! Three days of deals, talks and the finest hospitality in ", SystemName(c->System), "."));
			}
		}
		for (const auto& c : Conferences.ToArray())
		{
			if (c->Closed) continue;
			if (!c->Invited && jd >= c->OpensJd - 45)
			{
				c->Invited = true;
				// Businesses in that line (any, for a trade summit) send their heads, or an heir.
				for (const auto& b : Businesses)
				{
					if (c->Attending.Count() >= 12) break;
					if (!b->Trading() || b->Owner == nullptr || !Stands(c->Host) || Hostile(galaxy, b->Home->Owner, c->Host->Owner)) continue;
					if (c->Trade != BusinessKind::TradingHouse && b->Kind != c->Trade) continue;
					std::uint32_t h = H(static_cast<std::uint32_t>(b->Id), static_cast<std::uint32_t>(c->Id), 0xD1Eu);
					if (Unit(h) > 0.6) continue;
					cs::Ref<Citizen> who;
					for (const auto& m : b->Owner->Members)
						if (Adult(m, jd) && m->TripId == 0 && (m->Role == CitizenRole::Head || m->Role == CitizenRole::Heir)) { who = m; break; }
					if (who == nullptr || b->Home == c->Host) { if (b->Home == c->Host) c->Attending.Add(b); continue; }
					double travel = TravelDays(b->System, c->System, h);
					if (c->OpensJd - travel - 1 < jd + 1) continue;
					auto t = cs::New<Trip>();
					t->Id = NextId++;
					t->Traveller = who;
					t->Purpose = TripPurpose::Conference;
					t->From = b->Home;
					t->FromSystem = b->System;
					t->To = c->Host;
					t->ToSystem = c->System;
					t->Conference = c;
					t->PlannedJd = jd;
					t->DepartJd = cs::Math::Floor(c->OpensJd - travel - 1);
					t->ArriveJd = t->DepartJd + travel;
					t->LeaveJd = c->ClosesJd;
					t->HomeJd = t->LeaveJd + travel;
					who->TripId = t->Id;
					Trips.Add(t);
					c->Attending.Add(b);
					Post(FeedKind::Message, jd, b->System, who->FullName(), cs::Cat(b->Name, " board"), cs::Cat("I'll represent us at the ", c->Name, ". Booked to leave in ", static_cast<int>(t->DepartJd - jd), " days."));
				}
			}
			if (jd < c->ClosesJd) continue;
			c->Closed = true;
			c->Delegates = c->Attending.Count();
			// Deals between businesses of different houses; across flags they warm their owners to each other.
			for (int i = 0; i < c->Attending.Count(); i++)
				for (int j = i + 1; j < c->Attending.Count(); j++)
				{
					auto a = c->Attending[i], b = c->Attending[j];
					std::uint32_t h = H(static_cast<std::uint32_t>(a->Id), static_cast<std::uint32_t>(b->Id), static_cast<std::uint32_t>(c->Id));
					if (Unit(h) > 0.3 || !a->Trading() || !b->Trading()) continue;
					c->Deals++;
					double v = 5e5 * (1 + 3 * Unit(h >> 5));
					a->Capital += v;
					b->Capital += v;
					auto fa = a->Home->Owner, fb = b->Home->Owner;
					if (fa != fb && !Hostile(galaxy, fa, fb) && galaxy.Polities->Statecraft != nullptr)
					{
						galaxy.Polities->Statecraft->Sway(fa, fb, 1.5);
						galaxy.Polities->Statecraft->Sway(fb, fa, 1.5);
					}
				}
			cs::String text = cs::Cat("The ", c->Name, " closes: ", c->Delegates, " delegations, ", c->Deals, " deals sealed");
			Post(FeedKind::News, jd, c->System, "", "", text);
			if (c->Deals > 0) Notable(galaxy, c->System, jd, text, WorldEventKind::Economy);
		}
	}

	// ---------------------------------------------------------------- bribery

	cs::Ref<Citizen> Society::GovernorOf(Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd)
	{
		(void)galaxy;
		if (!Stands(s)) return nullptr;
		for (const auto& h : Houses)
			if (h->Home == s)
				for (const auto& c : h->Members)
					if (c->Alive() && c->Role == CitizenRole::Governor) return c;
		// Appoint one from the richest house there: an heir, or the head's spouse.
		cs::Ref<Household> rich;
		for (const auto& h : HousesOf(s))
			if (rich == nullptr || h->Wealth > rich->Wealth) rich = h;
		if (rich == nullptr) return nullptr;
		cs::Ref<Citizen> pick;
		for (const auto& c : rich->Members)
			if (Adult(c, jd) && (c->Role == CitizenRole::Heir || c->Role == CitizenRole::Spouse) && c->AgeYears(jd) >= 30) { pick = c; break; }
		if (pick == nullptr) return nullptr;
		pick->Role = CitizenRole::Governor;
		Post(FeedKind::News, jd, rich->System, "", "", cs::Cat(pick->FullName(), " is appointed governor of ", s->Name, " for the ", s->Owner->Name));
		return pick;
	}

	void Society::Bribery(Galaxy& galaxy, double jd)
	{
		for (const auto& b : Businesses.ToArray())
		{
			if (!b->Trading() || !Stands(b->Home)) continue;
			std::uint32_t h = H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0xB71Bu);
			if (Unit(h) < 0.015) Pay(galaxy, b, jd, h, false);
		}
		// Old bribes come out: likelier on a restless world, and the more a governor has taken.
		for (const auto& br : Bribes.ToArray())
		{
			if (br->Exposed || jd - br->Jd > 3 * 365.25 || br->Official == nullptr) continue;
			std::uint32_t h = H(static_cast<std::uint32_t>(br->Id), DayOf(jd), 0xE7Fu);
			double unrest = br->Where != nullptr ? br->Where->Unrest : 0;
			double odds = 0.01 + 0.05 * unrest + 0.02 * cs::Math::Min(1.0, br->Official->Bribes / 1e7);
			if (Unit(h) < odds) Scandal(galaxy, br, jd);
		}
	}

	cs::Ref<Bribe> Society::Pay(Galaxy& galaxy, const cs::Ref<Business>& b, double jd, std::uint32_t h, bool steered)
	{
		auto gov = GovernorOf(galaxy, b->Home, jd);
		if (gov == nullptr || gov->House == b->Owner) return nullptr;
		auto br = cs::New<Bribe>();
		br->Id = NextId++;
		br->Briber = b;
		br->Official = gov;
		br->Government = b->Home->Owner;
		br->Where = b->Home;
		br->Amount = cs::Math::Clamp(b->Capital * 0.03, 5e4, 5e6);
		br->Jd = jd;
		br->Steered = steered;
		br->Favour = Pick({ "the dock licences", "the station contract", "a blind eye at customs", "the mining rights", "the zoning board", "the navy supply tender" }, h >> 5);
		b->Capital -= br->Amount;
		// The favour pays: a contract worth more than the bribe.
		b->Capital += br->Amount * 2.5;
		gov->Bribes += br->Amount;
		if (gov->House != nullptr) gov->House->Wealth += br->Amount;
		Bribes.Add(br);
		auto head = b->Owner != nullptr ? b->Owner->Head() : nullptr;
		Post(FeedKind::Message, jd, b->System, head != nullptr ? head->FullName() : b->Name, gov->FullName(),
			Pick({ cs::String("A small token of our appreciation. We trust the matter is settled."), cs::String("As discussed. The usual account."), cs::String("For your trouble. Dinner is on us next time too.") }, h >> 9));
		return br;
	}

	void Society::Scandal(Galaxy& galaxy, const cs::Ref<Bribe>& br, double jd)
	{
		br->Exposed = true;
		br->ExposedJd = jd;
		Scandals++;
		auto gov = br->Official;
		Mirror(galaxy, SocialKind::Scandal, jd, br->Where, br->Briber != nullptr ? br->Briber->System : SystemOf(galaxy, br->Where), gov, nullptr,
			gov != nullptr ? gov->House : nullptr, br->Briber != nullptr ? br->Briber->Owner : nullptr, br->Briber, br->Amount);
		if (gov != nullptr && gov->Role == CitizenRole::Governor) gov->Role = CitizenRole::Heir;
		if (br->Briber != nullptr && br->Briber->Trading())
		{
			br->Briber->Capital -= br->Amount * 3;
			if (br->Briber->Capital < 2e5) Close(galaxy, br->Briber, jd, true);
		}
		if (br->Where != nullptr) br->Where->Unrest = cs::Math::Min(1.0, br->Where->Unrest + 0.05);
		auto sys = br->Briber != nullptr ? br->Briber->System : SystemOf(galaxy, br->Where);
		cs::String text = cs::Cat("Scandal on ", br->Where != nullptr ? br->Where->Name : cs::String("?"), ": governor ", gov != nullptr ? gov->FullName() : cs::String("?"), " took ", Money(br->Amount),
			" from ", br->Briber != nullptr ? br->Briber->Name : cs::String("a business"), " for ", br->Favour, "; the governor is out and the firm fined");
		Post(FeedKind::News, jd, sys, "", "", text);
		Notable(galaxy, sys, jd, text, WorldEventKind::Law);
	}

	// ---------------------------------------------------------------- the media: commercials and spam

	void Society::Media(Galaxy& galaxy, double jd)
	{
		cs::List<cs::Ref<Business>> trading;
		for (const auto& b : Businesses)
			if (b->Trading()) trading.Add(b);
		int ads = cs::Math::Min(4, 1 + trading.Count() / 80);
		for (int i = 0; i < ads && trading.Count() > 0; i++)
		{
			std::uint32_t r = H(DayOf(jd), static_cast<std::uint32_t>(i), 0xAD5u);
			if (Unit(r) > 0.7) continue;
			auto b = Pick(trading, r >> 3);
			cs::String where = b->Home != nullptr ? b->Home->Name : cs::String("");
			cs::String text;
			switch (b->Kind)
			{
				case BusinessKind::TradingHouse: text = Pick({ cs::Cat("Prices so low our accountants cried. ", b->Name, ", ", where, "."), cs::Cat("Whatever you need, wherever you are: ", b->Name, " delivers.") }, r >> 7); break;
				case BusinessKind::Shipwright: text = Pick({ cs::Cat("Hulls built to last a lifetime (and then some). ", b->Name, " of ", where, "."), cs::Cat("Refits, repairs, new keels laid weekly. Ask about our trade-in deals.") }, r >> 7); break;
				case BusinessKind::MiningConcern: text = Pick({ cs::Cat("Now hiring rock-hounds! Good pay, hazard bonus, real gravity on leave. ", b->Name, "."), cs::Cat("Ore you can trust, straight from the belt.") }, r >> 7); break;
				case BusinessKind::Refinery: text = cs::Cat("Purity guaranteed to five nines. ", b->Name, ", smelting since ", Year(b->FoundedJd), "."); break;
				case BusinessKind::Brokerage: text = Pick({ cs::Cat("Insure your cargo before the raiders insure your loss. ", b->Name, "."), cs::Cat("Your credits, working as hard as you do. ", b->Name, " of ", where, ".") }, r >> 7); break;
				case BusinessKind::Shipping: text = cs::Cat("On time, every time, gate to gate. ", b->Name, ": ", b->Branches.Count() + 1, " ports and counting."); break;
				case BusinessKind::Media: text = cs::Cat("All the news from every system, before the lightspeed lag. Tune in to ", b->Name, "."); break;
				case BusinessKind::Hospitality: text = Pick({ cs::Cat("Real sheets. Real coffee. Real windows. Stay at ", b->Name, " on ", where, "."), cs::Cat("Honeymooners stay free the first night at ", b->Name, ".") }, r >> 7); break;
			}
			Post(FeedKind::Commercial, jd, b->System, b->Name, "", text);
		}
		// Spam: the clans' scams, and the plain ones.
		std::uint32_t r = H(DayOf(jd), 0x5BA3u);
		if (Unit(r) < 0.5 && galaxy.Polities != nullptr)
		{
			cs::List<cs::Ref<Faction>> clans;
			for (const auto& f : galaxy.Polities->Factions())
				if (f->IsPirate) clans.Add(f);
			cs::String from = clans.Count() > 0 && Unit(r >> 3) < 0.6
				? Pick({ cs::String("Navy Pay Office"), cs::String("Customs Refund Desk"), cs::String("Gate Authority Billing"), cs::String("Lottery of the Belt") }, r >> 7)
				: Pick({ cs::String("Prince of Ganymede"), cs::String("HullCare Warranty Team"), cs::String("Exclusive Offer"), cs::String("Your Long-Lost Cousin") }, r >> 7);
			cs::String text = Pick({ cs::String("URGENT: your hull warranty is about to expire. Reply with your transponder code to renew."),
				cs::String("Congratulations! You have won a luxury cruise. Just pay the docking fee to claim."),
				cs::String("A wealthy miner left you 4.2 million credits. Send 5,000 for the transfer tax."),
				cs::String("Your gate pass has been suspended. Verify your account at once."),
				cs::String("Hot tip: buy helium-3 NOW before the price goes to the moon!"),
				cs::String("Lonely in the outer belt? Singles in your system want to meet you.") }, r >> 11);
			if (clans.Count() > 0 && Unit(r >> 3) < 0.6) text = cs::Cat(text, " (traced to the ", Pick(clans, r >> 13)->Name, ")");
			Post(FeedKind::Spam, jd, nullptr, from, "everyone", text);
		}
	}

	// ---------------------------------------------------------------- the feed

	void Society::Post(FeedKind kind, double jd, const cs::Ref<StarSystem>& sys, cs::String from, cs::String to, cs::String text)
	{
		auto f = cs::New<FeedItem>();
		f->Jd = jd;
		f->Kind = kind;
		f->From = std::move(from);
		f->To = std::move(to);
		f->System = sys;
		f->Text = std::move(text);
		Feed.Add(f);
	}

	void Society::Notable(Galaxy& galaxy, const cs::Ref<StarSystem>& sys, double jd, const cs::String& text, WorldEventKind kind)
	{
		(void)galaxy;
		if (sys == nullptr || sys->World == nullptr) return;
		auto w = sys->World;
		w->Log(cs::Double::IsNaN(w->Jd) ? jd : cs::Math::Max(jd, w->Jd), text, kind, -1);
	}

	void Society::Prune()
	{
		if (Feed.Count() > MaxFeed) Feed.RemoveRange(0, Feed.Count() - MaxFeed);
		int ended = 0;
		for (const auto& t : Trips)
			if (!t->Active()) ended++;
		for (int i = 0; i < Trips.Count() && ended > KeepEndedTrips;)
		{
			if (!Trips[i]->Active()) { Trips.RemoveAt(i); ended--; }
			else i++;
		}
		for (int i = 0; i < Conferences.Count();)
		{
			if (Conferences[i]->Closed && Conferences.Count() > 24) Conferences.RemoveAt(i);
			else i++;
		}
		if (Bribes.Count() > 200) Bribes.RemoveRange(0, Bribes.Count() - 200);
		// Businesses long gone, and houses died out or scattered, leave the books after a while.
		for (int i = 0; i < Businesses.Count();)
		{
			if (!Businesses[i]->Trading() && Businesses.Count() > 300) Businesses.RemoveAt(i);
			else i++;
		}
		for (int i = 0; i < Houses.Count();)
		{
			auto h = Houses[i];
			if ((h->Scattered || h->Living() == 0) && Houses.Count() > 300) Houses.RemoveAt(i);
			else i++;
		}
	}

	// ---------------------------------------------------------------- descriptions

	cs::String Society::Describe(const cs::Ref<Trip>& t, double jd)
	{
		if (t == nullptr) return "";
		cs::String who = t->Traveller != nullptr ? t->Traveller->FullName() : cs::String("?");
		cs::String what = cs::Cat(who, ": ", ToString(t->Purpose), " ", t->From != nullptr ? t->From->Name : cs::String("?"), " -> ", t->To != nullptr ? t->To->Name : cs::String("?"));
		switch (t->Stage)
		{
			case TripStage::Planned: return cs::Cat(what, ", leaves in ", static_cast<int>(cs::Math::Max(0.0, t->DepartJd - jd)), " d");
			case TripStage::Outbound: return cs::Cat(what, ", arrives in ", static_cast<int>(cs::Math::Max(0.0, t->ArriveJd - jd)), " d");
			case TripStage::There: return cs::Cat(what, ", there until ", static_cast<int>(cs::Math::Max(0.0, t->LeaveJd - jd)), " d from now");
			case TripStage::Homeward: return cs::Cat(what, ", home in ", static_cast<int>(cs::Math::Max(0.0, t->HomeJd - jd)), " d");
			case TripStage::Done: return cs::Cat(what, ", done", t->Note.empty() ? cs::String("") : cs::Cat(" (", t->Note, ")"));
			case TripStage::Cancelled: return cs::Cat(what, ", called off (", t->Note, ")");
		}
		return what;
	}

	cs::String Society::Describe(const cs::Ref<Business>& b)
	{
		if (b == nullptr) return "";
		return cs::Cat(b->Id, " ", b->Name, " (", Line(b->Kind), ", ", b->Home != nullptr ? b->Home->Name : cs::String("?"), "): ", ToString(b->Status), ", capital ", Money(b->Capital),
			", last month ", Money(b->LastMonth), ", ", b->Branches.Count(), " branch(es)");
	}

	cs::String Society::Describe(const cs::Ref<Bribe>& b)
	{
		if (b == nullptr) return "";
		return cs::Cat(b->Id, " ", b->Briber != nullptr ? b->Briber->Name : cs::String("?"), " -> governor ", b->Official != nullptr ? b->Official->FullName() : cs::String("?"), " of ",
			b->Where != nullptr ? b->Where->Name : cs::String("?"), ": ", Money(b->Amount), " for ", b->Favour, b->Exposed ? cs::String(" [exposed]") : cs::String(""));
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Society::Expose(Galaxy& galaxy, const cs::Ref<Bribe>& b, double jd)
	{
		if (b == nullptr) return "no such bribe";
		if (b->Exposed) return "that bribe is already public";
		b->Steered = true;
		Scandal(galaxy, b, jd);
		return "";
	}

	cs::String Society::BribeFor(Galaxy& galaxy, const cs::Ref<Business>& b, double jd)
	{
		if (b == nullptr) return "no such business";
		if (!b->Trading() || !Stands(b->Home)) return "that business is not trading";
		auto br = Pay(galaxy, b, jd, H(static_cast<std::uint32_t>(b->Id), DayOf(jd), 0x60Du), true);
		return br != nullptr ? cs::String("") : cs::String("there is no governor there to buy (or the house is the governor's own)");
	}

	cs::String Society::Send(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, double jd)
	{
		if (c == nullptr) return "no such person";
		if (!c->Alive()) return "they are dead";
		if (c->TripId != 0) return "they are already travelling";
		if (!Stands(to)) return "no standing settlement by that name";
		if (c->House != nullptr && c->House->Home == to) return "they live there";
		auto t = Plan(galaxy, c, to, TripPurpose::Business, nullptr, jd, H(static_cast<std::uint32_t>(c->Id), DayOf(jd), 0x5E4Du));
		if (t == nullptr) return "they will not go";
		t->Steered = true;
		return "";
	}

	cs::String Society::Fund(Galaxy& galaxy, const cs::Ref<Business>& b, double credits, double jd)
	{
		if (b == nullptr) return "no such business";
		if (!b->Trading()) return "that business is not trading";
		if (!(credits > 0)) return "the sum must be positive";
		b->Capital += credits;
		cs::String text = cs::Cat(b->Name, " of ", b->Home != nullptr ? b->Home->Name : cs::String("?"), " takes ", Money(credits), " from an anonymous backer");
		Post(FeedKind::News, jd, b->System, "", "", text);
		Notable(galaxy, b->System, jd, text, WorldEventKind::Economy);
		return "";
	}

	cs::String Society::Ruin(Galaxy& galaxy, const cs::Ref<Business>& b, double jd)
	{
		if (b == nullptr) return "no such business";
		if (!b->Trading()) return "that business is not trading";
		Post(FeedKind::News, jd, b->System, "", "", cs::Cat("A run on ", b->Name, ": its creditors call everything in overnight"));
		b->Capital = 0;
		Close(galaxy, b, jd, true);
		return "";
	}

	bool Society::Reveal(Galaxy& galaxy, const cs::Ref<Bribe>& b, double jd)
	{
		if (b == nullptr || b->Exposed) return false;
		Scandal(galaxy, b, jd);
		return true;
	}

	cs::Ref<Trip> Society::Book(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, TripPurpose why, const cs::Ref<Citizen>& meeting, double jd)
	{
		if (c == nullptr || !c->Alive() || c->TripId != 0 || c->AgeYears(jd) < 14 || !Stands(to) || ActiveTrips().Count() >= MaxTrips) return nullptr;
		return Plan(galaxy, c, to, why, meeting, jd, H(static_cast<std::uint32_t>(c->Id), DayOf(jd), 0xB00Fu + static_cast<std::uint32_t>(why)));
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(CitizenRole, "SpaceGod.Core.CitizenRole")
	SG_ENUM(BusinessKind, "SpaceGod.Core.BusinessKind")
	SG_ENUM(BusinessStatus, "SpaceGod.Core.BusinessStatus")
	SG_ENUM(TripPurpose, "SpaceGod.Core.TripPurpose")
	SG_ENUM(TripStage, "SpaceGod.Core.TripStage")
	SG_ENUM(FeedKind, "SpaceGod.Core.FeedKind")

	SG_SERIALIZE(Citizen, "SpaceGod.Core.Citizen",
		SG_F(Id), SG_F(Given), SG_F(House), SG_F(Role), SG_F(BornJd), SG_F(DiedJd), SG_F(Spouse), SG_F(TripId), SG_F(Bribes),
		SG_OPT(Persona), SG_OPT(Traits), SG_OPT(Goal), SG_OPT(Reputation), SG_OPT(Skill), SG_OPT(Ties), SG_OPT(Faith), SG_OPT(Orgs)) // US-T275, absent from older saves

	SG_SERIALIZE(Household, "SpaceGod.Core.Household",
		SG_F(Id), SG_F(Surname), SG_F(Home), SG_F(System), SG_F(Members), SG_F(Business), SG_F(Wealth), SG_F(FoundedJd), SG_F(Scattered), SG_F(NextEventJd))

	SG_SERIALIZE(Business, "SpaceGod.Core.Business",
		SG_F(Id), SG_F(Name), SG_F(Kind), SG_F(Owner), SG_F(Home), SG_F(System), SG_F(Capital), SG_F(LastMonth), SG_F(Branches), SG_F(Status), SG_F(FoundedJd), SG_F(EndedJd), SG_F(Buyer))

	SG_SERIALIZE(Trip, "SpaceGod.Core.Trip",
		SG_F(Id), SG_F(Traveller), SG_F(Meeting), SG_F(Purpose), SG_F(From), SG_F(To), SG_F(FromSystem), SG_F(ToSystem), SG_F(Conference),
		SG_F(PlannedJd), SG_F(DepartJd), SG_F(ArriveJd), SG_F(LeaveJd), SG_F(HomeJd), SG_F(Stage), SG_F(Note), SG_F(Steered))

	SG_SERIALIZE(Conference, "SpaceGod.Core.Conference",
		SG_F(Id), SG_F(Name), SG_F(Trade), SG_F(Host), SG_F(System), SG_F(AnnouncedJd), SG_F(OpensJd), SG_F(ClosesJd), SG_F(Delegates), SG_F(Deals), SG_F(Invited), SG_F(Closed), SG_F(Attending))

	SG_SERIALIZE(Bribe, "SpaceGod.Core.Bribe",
		SG_F(Id), SG_F(Briber), SG_F(Official), SG_F(Government), SG_F(Where), SG_F(Amount), SG_F(Jd), SG_F(Favour), SG_F(Exposed), SG_F(ExposedJd), SG_F(Steered))

	SG_SERIALIZE(FeedItem, "SpaceGod.Core.FeedItem",
		SG_F(Jd), SG_F(Kind), SG_F(From), SG_F(To), SG_F(System), SG_F(Text), SG_OPT(Event)) // US-T275, absent from older saves

	SG_SERIALIZE(Society, "SpaceGod.Core.Society",
		SG_F(NextId), SG_F(Houses), SG_F(Businesses), SG_F(Trips), SG_F(Conferences), SG_F(Bribes), SG_F(Feed),
		SG_F(Births), SG_F(Deaths), SG_F(Weddings), SG_F(TripsTaken), SG_F(TripsCancelled), SG_F(Robberies), SG_F(Scandals),
		SG_F(Founded), SG_F(Acquisitions), SG_F(Bankruptcies), SG_F(BranchesOpened), SG_F(BranchesClosed), SG_F(nextDayJd), SG_F(nextMonthJd))
}
