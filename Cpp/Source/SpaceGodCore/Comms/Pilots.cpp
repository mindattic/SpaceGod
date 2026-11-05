// US-T138: named pilots, their life stories and careers (see Comms/Pilots.h).

#include "Comms/Pilots.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Events/EventSources.h"
#include "Fleet.h"
#include "Law/Law.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(MeetKind v)
	{
		switch (v)
		{
		case MeetKind::Hailed: return "Hailed";
		case MeetKind::Embodied: return "Embodied";
		case MeetKind::Fought: return "Fought";
		case MeetKind::Rescued: return "Rescued";
		case MeetKind::Mentioned: return "Mentioned";
		}
		return "?";
	}

	const char* ToString(CareerTrack v)
	{
		switch (v)
		{
		case CareerTrack::Civilian: return "Civilian";
		case CareerTrack::Navy: return "Navy";
		case CareerTrack::Pirate: return "Pirate";
		case CareerTrack::Reform: return "Reform";
		}
		return "?";
	}

	const char* ToString(LifeEventKind v)
	{
		switch (v)
		{
		case LifeEventKind::Born: return "Born";
		case LifeEventKind::Joined: return "Joined";
		case LifeEventKind::FlewShip: return "FlewShip";
		case LifeEventKind::SurvivedBattle: return "SurvivedBattle";
		case LifeEventKind::LostShip: return "LostShip";
		case LifeEventKind::Grudge: return "Grudge";
		case LifeEventKind::Crime: return "Crime";
		case LifeEventKind::Promoted: return "Promoted";
		case LifeEventKind::Demoted: return "Demoted";
		case LifeEventKind::Detained: return "Detained";
		case LifeEventKind::Rehabilitated: return "Rehabilitated";
		case LifeEventKind::Graduated: return "Graduated";
		case LifeEventKind::GivenCommand: return "GivenCommand";
		case LifeEventKind::Betrayal: return "Betrayal";
		case LifeEventKind::TurnedPirate: return "TurnedPirate";
		case LifeEventKind::Robbed: return "Robbed";
		case LifeEventKind::WasRobbed: return "WasRobbed";
		case LifeEventKind::Surrendered: return "Surrendered";
		case LifeEventKind::Spared: return "Spared";
		case LifeEventKind::Threatened: return "Threatened";
		case LifeEventKind::Traded: return "Traded";
		case LifeEventKind::Met: return "Met";
		case LifeEventKind::OwnedAct: return "OwnedAct";
		case LifeEventKind::CrownedKing: return "CrownedKing";
		case LifeEventKind::Salvaged: return "Salvaged";
		}
		return "?";
	}

	// ---------------------------------------------------------------- beats and pilots

	cs::String LifeEvent::Slot(const cs::String& key) const
	{
		for (int i = 0; i + 1 < Slots.Count(); i += 2)
			if (Slots[i] == key) return Slots[i + 1];
		return cs::String();
	}

	void LifeEvent::Set(const cs::String& key, const cs::String& value)
	{
		for (int i = 0; i + 1 < Slots.Count(); i += 2)
			if (Slots[i] == key)
			{
				Slots[i + 1] = value;
				return;
			}
		Slots.Add(key);
		Slots.Add(value);
	}

	PilotTraits NamedPilot::Traits() const
	{
		PilotTraits t;
		t.Courage = Courage;
		t.Greed = Greed;
		t.Honour = Honour;
		t.Warmth = Warmth;
		t.Spite = Spite;
		t.Chatter = Chatter;
		return t;
	}

	int NamedPilot::AgeAt(double jd) const { return static_cast<int>(cs::Math::Floor((jd - BornJd) / 365.25)); }

	cs::String NamedPilot::Styled() const
	{
		auto title = Careers::Title(Track, Rank);
		if (title.empty()) return Name;
		title[0] = static_cast<char>(toupper(static_cast<unsigned char>(title[0])));
		return cs::Cat(title, " ", Name);
	}

	cs::Ref<LifeEvent> NamedPilot::Add(double jd, LifeEventKind kind, int seed)
	{
		auto e = cs::New<LifeEvent>();
		e->Jd = jd;
		e->Kind = kind;
		e->Seed = seed;
		// Kept in date order: a backstory's beats are all dated before the meeting, an act's at the moment it is done.
		int at = Story.Count();
		while (at > 0 && Story[at - 1]->Jd > jd) at--;
		Story.Insert(at, e);
		return e;
	}

	// ---------------------------------------------------------------- careers

	cs::String Careers::Title(CareerTrack track, int rank)
	{
		static const char* pirate[] = { "recruit", "pirate fighter", "lieutenant", "pirate captain", "warlord", "pirate king" };
		static const char* navy[] = { "cadet", "pilot", "officer", "captain" };
		static const char* civil[] = { "deckhand", "pilot", "master" };
		static const char* reform[] = { "detainee", "rehabilitee", "cadet", "pilot", "officer", "captain" };
		int top = TopRank(track);
		int r = cs::Math::Max(0, cs::Math::Min(rank, top));
		switch (track)
		{
		case CareerTrack::Pirate: return pirate[r];
		case CareerTrack::Navy: return navy[r];
		case CareerTrack::Civilian: return civil[r];
		case CareerTrack::Reform: return reform[r];
		}
		return cs::String();
	}

	int Careers::TopRank(CareerTrack track)
	{
		switch (track)
		{
		case CareerTrack::Pirate: return 5;
		case CareerTrack::Navy: return 3;
		case CareerTrack::Civilian: return 2;
		case CareerTrack::Reform: return 5;
		}
		return 0;
	}

	double Careers::RenownFor(CareerTrack track, int rank)
	{
		switch (track)
		{
		case CareerTrack::Pirate:
		{
			static const double need[] = { 0, 2, 6, 12, 22, 40 };
			return need[cs::Math::Max(0, cs::Math::Min(rank, 5))];
		}
		case CareerTrack::Navy:
		{
			static const double need[] = { 0, 2, 8, 18 };
			return need[cs::Math::Max(0, cs::Math::Min(rank, 3))];
		}
		case CareerTrack::Civilian:
		{
			static const double need[] = { 0, 2, 8 };
			return need[cs::Math::Max(0, cs::Math::Min(rank, 2))];
		}
		case CareerTrack::Reform:
		{
			// Detention, rehabilitation and school are time served; an officer's and a captain's rungs are earned.
			static const double need[] = { 0, 0, 0, 0, 8, 20 };
			return need[cs::Math::Max(0, cs::Math::Min(rank, 5))];
		}
		}
		return 0;
	}

	cs::String Careers::NextStep(const cs::Ref<NamedPilot>& p, double jd)
	{
		if (p == nullptr) return cs::String();
		int top = TopRank(p->Track);
		if (p->Rank >= top) return "at the top of the ladder";
		auto next = Title(p->Track, p->Rank + 1);
		if (p->Track == CareerTrack::Reform && p->Rank < 3)
		{
			double served = jd - p->RankJd;
			double need = p->Rank == 0 ? DetentionDays : p->Rank == 1 ? RehabilitationDays : SchoolDays;
			need *= 1.5 - 0.5 * p->Honour;
			return cs::Cat(next, " in ", cs::Math::Max(1, static_cast<int>(cs::Math::Ceiling(need - served))), " days");
		}
		if (p->Track == CareerTrack::Pirate && p->Rank == 1 && !p->Betrayed)
			return cs::Cat(next, ": needs a betrayal (turn on the captain or the clan)");
		double need = RenownFor(p->Track, p->Rank + 1);
		return cs::Cat(next, " at renown ", cs::Fmt(need, "0"), " (", cs::Fmt(p->Renown, "0"), " now)");
	}

	// ---------------------------------------------------------------- the register

	uint32_t Characters::Mix(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	uint32_t Characters::HashText(const cs::String& s)
	{
		uint32_t h = 2166136261u;
		for (unsigned char c : s) h = (h ^ c) * 16777619u;
		return h;
	}

	double Characters::Roll(int seed, uint32_t salt)
	{
		return (Mix(static_cast<uint32_t>(seed) * 2654435761u ^ Mix(salt + 0x9e3779b9u)) >> 8) / 16777216.0;
	}

	cs::Ref<IStoryTeller>& Characters::tellerSlot()
	{
		static cs::Ref<IStoryTeller> teller = cs::New<TemplateStoryTeller>();
		return teller;
	}

	void Characters::SetTeller(cs::Ref<IStoryTeller> teller) { tellerSlot() = teller != nullptr ? teller : cs::New<TemplateStoryTeller>(); }
	const cs::Ref<IStoryTeller>& Characters::Teller() { return tellerSlot(); }

	cs::Ref<Characters> Characters::Of(const cs::Ref<World>& world)
	{
		if (world == nullptr || world->Law == nullptr) return nullptr;
		if (world->Law->Pilots == nullptr) world->Law->Pilots = cs::New<Characters>();
		return world->Law->Pilots;
	}

	double Characters::GrievanceOf(const cs::Ref<Core::Faction>& victim, const cs::Ref<Core::Faction>& offender) const
	{
		if (victim == nullptr || offender == nullptr) return 0;
		double v = 0;
		return Grievance.TryGetValue(cs::Cat(victim->Name, "|", offender->Name), v) ? v : 0;
	}

	void Characters::AddGrievance(const cs::Ref<Core::Faction>& victim, const cs::Ref<Core::Faction>& offender, double harm)
	{
		if (victim == nullptr || offender == nullptr || victim == offender || harm <= 0) return;
		auto key = cs::Cat(victim->Name, "|", offender->Name);
		Grievance[key] = GrievanceOf(victim, offender) + harm;
	}

	cs::Ref<NamedPilot> Characters::Find(const cs::Ref<Ship>& ship) const
	{
		cs::Ref<NamedPilot> p;
		return ship != nullptr && ByShip.TryGetValue(ship, p) ? p : nullptr;
	}

	namespace
	{
		const cs::List<cs::String>& Firsts()
		{
			static const cs::List<cs::String> v
			{
				"Lena", "Tomas", "Ines", "Darius", "Mei", "Oskar", "Ayo", "Priya", "Konstantin", "Saoirse", "Joaquin", "Halima",
				"Rurik", "Noor", "Elias", "Yuki", "Bertrand", "Imani", "Casimir", "Thandi", "Ilse", "Mateo", "Zofia", "Kwame",
				"Freya", "Anselm", "Dalia", "Henrik", "Esperanza", "Vikram", "Orla", "Benedikt", "Selin", "Gideon", "Marisol", "Taavi",
			};
			return v;
		}

		const cs::List<cs::String>& Lasts()
		{
			static const cs::List<cs::String> v
			{
				"Okafor", "Lindqvist", "Moreau", "Haddad", "Takeda", "Brennan", "Kowalczyk", "Adeyemi", "Varga", "Castellanos",
				"Nakamura", "Ferreira", "Oyelaran", "Petrov", "Achterberg", "Iqbal", "Delacroix", "Mbeki", "Sorensen", "Quintero",
				"Halloran", "Vasquez", "Rautio", "Abernathy", "Chukwu", "Esposito", "Halvorsen", "Ndiaye", "Strand", "Volkov",
			};
			return v;
		}

		const cs::List<cs::String>& Callsigns()
		{
			static const cs::List<cs::String> v
			{
				"Lucky", "Ghost", "Torch", "Ledger", "Comet", "Slate", "Rook", "Flint", "Magpie", "Saint", "Wick", "Hammer",
				"Vesper", "Static", "Lantern", "Moth", "Anvil", "Sparrow", "Tally", "Drift",
			};
			return v;
		}

		bool IsCapital(ShipClass c)
		{
			return c == ShipClass::Carrier || c == ShipClass::Battleship || c == ShipClass::Cruiser || c == ShipClass::CommandCruiser
				|| c == ShipClass::GateBreaker;
		}

		bool IsSmallCombat(ShipClass c)
		{
			return c == ShipClass::Fighter || c == ShipClass::Interceptor || c == ShipClass::Gunship || c == ShipClass::Corvette
				|| c == ShipClass::Frigate || c == ShipClass::Destroyer;
		}

		cs::String ClassName(const cs::Ref<Ship>& s)
		{
			return s != nullptr && s->Spec != nullptr ? cs::Strings::ToLowerInvariant(s->Spec->DisplayName) : cs::String("hull");
		}

		/// The battle's name out of a log line ("Battle of Ceres: the Kestrel strikes ..." -> "Battle of Ceres"); empty for a
		/// line that names no battle that way.
		cs::String BattleOf(const cs::String& text)
		{
			auto colon = text.find(':');
			if (colon == cs::String::npos || colon < 4) return cs::String();
			auto name = text.substr(0, colon);
			const cs::String ends = " ends";
			if (name.size() > ends.size() && name.compare(name.size() - ends.size(), ends.size(), ends) == 0) name = name.substr(0, name.size() - ends.size());
			if (name.find(" is ") != cs::String::npos || name.find(" from ") != cs::String::npos || name.size() > 48) return cs::String();
			return name.find("Battle") != cs::String::npos || name.find("Siege") != cs::String::npos || name.find("Raid") != cs::String::npos
				|| name.find("Ambush") != cs::String::npos || name.find("fight") != cs::String::npos ? name : cs::String();
		}
	}

	cs::String Characters::NameFor(const cs::Ref<Ship>& ship, int seed)
	{
		// A pirate is the captain the law and the bounty boards already name, so a poster and a hail agree.
		if (ship != nullptr && ship->Faction != nullptr && ship->Faction->IsPirate && !ship->IsEscort())
		{
			auto n = PirateCaptains::NameFor(ship);
			const cs::String prefix = "Captain ";
			return n.rfind(prefix, 0) == 0 ? n.substr(prefix.size()) : n;
		}
		uint32_t h = Mix(static_cast<uint32_t>(seed));
		return cs::Cat(Firsts()[static_cast<int>(h % static_cast<uint32_t>(Firsts().Count()))], " ",
			Lasts()[static_cast<int>((h >> 11) % static_cast<uint32_t>(Lasts().Count()))]);
	}

	cs::Ref<NamedPilot> Characters::Meet(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, MeetKind how, double jd)
	{
		if (ship == nullptr) return nullptr;
		auto found = Find(ship);
		if (found != nullptr)
		{
			if (how == MeetKind::Hailed) found->TimesHailed++;
			Advance(world, found, jd);
			return found;
		}
		auto p = cs::New<NamedPilot>();
		p->Id = NextId++;
		// The seed is the hull's (its name and id, which every peer shares) and the moment it was met: the same pilot on
		// every peer that meets this hull then, and a different one in the next hull to carry the name.
		p->Seed = static_cast<int>(Mix(HashText(ship->Name) ^ Mix(static_cast<uint32_t>(ship->Id) * 40503u)) & 0x7fffffff);
		p->Pronoun = static_cast<int>(Mix(static_cast<uint32_t>(p->Seed) + 17u) & 1u);
		p->Name = NameFor(ship, p->Seed);
		p->Faction = ship->Faction;
		p->Ship = ship;
		p->ShipName = ship->Name;
		p->FirstMet = how;
		p->MetJd = jd;
		p->Courage = Roll(p->Seed, 101);
		p->Greed = Roll(p->Seed, 102);
		p->Honour = Roll(p->Seed, 103);
		p->Warmth = Roll(p->Seed, 104);
		p->Spite = Roll(p->Seed, 105);
		p->Chatter = Roll(p->Seed, 106);
		bool pirate = ship->Faction != nullptr && ship->Faction->IsPirate;
		bool navy = ship->Spec != nullptr && ship->Spec->IsMilitary && !pirate;
		// The lanes shape them: a pirate is a little bolder and greedier and keeps less of the law, a navy pilot more.
		if (pirate)
		{
			p->Courage = 0.25 + 0.75 * p->Courage;
			p->Greed = 0.35 + 0.65 * p->Greed;
			p->Honour *= 0.6;
		}
		else if (navy)
		{
			p->Courage = 0.3 + 0.7 * p->Courage;
			p->Honour = 0.3 + 0.7 * p->Honour;
		}
		if (p->Name.find('"') != cs::String::npos) {} // a raider whose name already carries an epithet ("the Knife")
		else if (pirate && Roll(p->Seed, 7) < 0.5) p->Callsign = Callsigns()[static_cast<int>(Mix(p->Seed + 9u) % Callsigns().Count())];
		else if (!pirate && Roll(p->Seed, 7) < 0.3) p->Callsign = Callsigns()[static_cast<int>(Mix(p->Seed + 9u) % Callsigns().Count())];
		All.Add(p);
		ByShip[ship] = p;
		Backstory(world, p, jd);
		auto met = p->Add(jd, LifeEventKind::Met, static_cast<int>(Mix(p->Seed + 31u) & 0xffff));
		met->Set("how", ToString(how));
		met->Set("ship", ship->Name);
		if (how == MeetKind::Hailed) p->TimesHailed++;
		Advance(world, p, jd);
		return p;
	}

	void Characters::Backstory(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd)
	{
		auto ship = p->Ship;
		int seed = p->Seed;
		auto faction = p->Faction;
		bool pirate = faction != nullptr && faction->IsPirate;
		bool navy = !pirate && ship->Spec != nullptr && ship->Spec->IsMilitary;

		// Born: somewhere real in this system, one of their own faction's worlds when it has any.
		int age = 21 + static_cast<int>(Roll(seed, 1) * 36);
		p->BornJd = jd - (age + Roll(seed, 2)) * 365.25;
		cs::List<cs::Ref<Settlement>> homes, anywhere;
		if (world != nullptr && world->Economy != nullptr)
			for (const auto& s : world->Economy->Settlements)
			{
				if (s == nullptr || s->Hidden) continue;
				anywhere.Add(s);
				if (faction != nullptr && s->Owner == faction) homes.Add(s);
			}
		// A pirate grew up on someone's lawful world and left it; everyone else on their own faction's, if it has one.
		auto& pool = !pirate && homes.Count() > 0 ? homes : anywhere;
		cs::Ref<Settlement> home = pool.Count() > 0 ? pool[static_cast<int>(Mix(seed + 3u) % static_cast<uint32_t>(pool.Count()))] : nullptr;
		p->Birthplace = home != nullptr ? home->Name : cs::String("a habitat long since broken up");
		auto born = p->Add(p->BornJd, LifeEventKind::Born, static_cast<int>(Mix(seed + 4u) & 0xffff));
		born->Set("place", p->Birthplace);
		if (home != nullptr && home->Owner != nullptr) born->Set("faction", home->Owner->Name);

		// Into the life they lead now.
		double joinedJd = p->BornJd + (17 + Roll(seed, 5) * 6) * 365.25;
		if (pirate)
		{
			auto turned = p->Add(joinedJd + Roll(seed, 6) * 4 * 365.25, LifeEventKind::TurnedPirate, static_cast<int>(Mix(seed + 5u) & 0xffff));
			turned->Set("faction", faction->Name);
			turned->Set("place", p->Birthplace);
		}
		else
		{
			auto joined = p->Add(joinedJd, LifeEventKind::Joined, static_cast<int>(Mix(seed + 6u) & 0xffff));
			joined->Set("faction", faction != nullptr ? faction->Name : cs::String("the free traders"));
			joined->Set("role", navy ? cs::String("navy") : cs::String("trade"));
		}

		// Hulls flown before this one: real hulls of their faction in this system's fleet, some since lost.
		cs::List<cs::Ref<Ship>> earlier;
		if (world != nullptr && world->Fleet != nullptr)
			for (const auto& s : world->Fleet->Ships())
				if (s != nullptr && s != ship && s->Faction == faction && !s->GateDeparted && s->Craft == nullptr && s->Id < ship->Id) earlier.Add(s);
		int flown = cs::Math::Min(earlier.Count(), 1 + static_cast<int>(Roll(seed, 8) * 3));
		double span = cs::Math::Max(30.0, jd - joinedJd - 60);
		for (int i = 0; i < flown; i++)
		{
			auto s = earlier[static_cast<int>(Mix(seed + 40u + static_cast<uint32_t>(i) * 7u) % static_cast<uint32_t>(earlier.Count()))];
			if (p->ShipsFlown.Contains(s->Name)) continue;
			p->ShipsFlown.Add(s->Name);
			double when = joinedJd + span * (i + Roll(seed, 50 + i)) / (flown + 1);
			auto e = p->Add(when, LifeEventKind::FlewShip, static_cast<int>(Mix(seed + 60u + i) & 0xffff));
			e->Set("ship", s->Name);
			e->Set("class", ClassName(s));
			if (s->Destroyed)
			{
				auto lost = p->Add(when + span / (flown + 1) * 0.8, LifeEventKind::LostShip, static_cast<int>(Mix(seed + 70u + i) & 0xffff));
				lost->Set("ship", s->Name);
				p->Renown += 1;
			}
		}
		p->ShipsFlown.Add(ship->Name);
		auto now = p->Add(cs::Math::Min(jd - 1, joinedJd + span * 0.9), LifeEventKind::FlewShip, static_cast<int>(Mix(seed + 80u) & 0xffff));
		now->Set("ship", ship->Name);
		now->Set("class", ClassName(ship));
		now->Set("current", "1");

		// Battles and captures on the record that name the hull: the fights they have actually lived through.
		if (world != nullptr)
		{
			int fights = 0;
			cs::HashSet<cs::String> seen;
			for (int i = world->Events.Count() - 1; i >= 0 && fights < 3; i--)
			{
				const auto& ev = world->Events[i];
				if (ev.Kind != WorldEventKind::Battle && ev.Kind != WorldEventKind::Capture && ev.Kind != WorldEventKind::Loss) continue;
				if (ev.Text.find(ship->Name) == cs::String::npos) continue;
				auto battle = BattleOf(ev.Text);
				if (battle.empty() || seen.Contains(battle)) continue;
				seen.Add(battle);
				auto e = p->Add(ev.Jd, LifeEventKind::SurvivedBattle, static_cast<int>(Mix(seed + 90u + fights) & 0xffff));
				e->Set("battle", battle);
				e->Set("ship", ship->Name);
				p->BattlesSurvived++;
				p->Renown += 2;
				fights++;
			}
		}

		// Crimes on their captain's record (the law already keeps it, by hull).
		if (world != nullptr && world->Law != nullptr)
		{
			cs::Ref<LawRecord> rec;
			if (world->Law->Captains.TryGetValue(ship->Controller(), rec) && rec != nullptr)
			{
				int told = 0;
				for (int i = rec->Crimes.Count() - 1; i >= 0 && told < 2; i--, told++)
				{
					auto c = rec->Crimes[i];
					auto e = p->Add(c->Jd, LifeEventKind::Crime, static_cast<int>(Mix(seed + 110u + told) & 0xffff));
					e->Set("crime", Law::Describe(c->Kind));
					if (c->Victim != nullptr) e->Set("victim", c->Victim->Name);
					if (c->PostedBy != nullptr) e->Set("faction", c->PostedBy->Name);
					p->Renown += pirate ? 2 : 0.5;
					if (pirate && c->Kind == CrimeKind::Piracy) p->Robberies++;
				}
			}
		}

		// A grudge: a pirate against the navy that hunts them, a lawful pilot against a raider or the enemy of the day.
		cs::String rival;
		if (world != nullptr)
		{
			if (pirate)
			{
				cs::List<cs::Ref<Faction>> navies;
				for (const auto& f : { world->Sol, world->Belt, world->Reach, world->RivalA, world->RivalB })
					if (f != nullptr && f->IsMilitary) navies.Add(f);
				if (navies.Count() > 0) rival = navies[static_cast<int>(Mix(seed + 120u) % static_cast<uint32_t>(navies.Count()))]->Name;
			}
			else if (world->Fleet != nullptr)
			{
				cs::List<cs::Ref<Ship>> raiders;
				for (const auto& s : world->Fleet->Ships())
					if (s != nullptr && s->Faction != nullptr && s->Faction->IsPirate && !s->IsEscort() && s->Craft == nullptr) raiders.Add(s);
				if (raiders.Count() > 0 && Roll(seed, 121) < 0.6) rival = PirateCaptains::NameFor(raiders[static_cast<int>(Mix(seed + 122u) % static_cast<uint32_t>(raiders.Count()))]);
				else if (faction != nullptr && world->Diplomacy() != nullptr)
					for (const auto& f : { world->Sol, world->Belt, world->Reach, world->RivalA, world->RivalB })
						if (f != nullptr && f != faction && world->Diplomacy()->Get(faction, f) == Relation::War)
						{
							rival = f->Name;
							break;
						}
			}
		}
		if (!rival.empty() && (p->Spite > 0.35 || pirate))
		{
			p->Grudges.Add(rival);
			auto g = p->Add(joinedJd + span * (0.3 + 0.5 * Roll(seed, 123)), LifeEventKind::Grudge, static_cast<int>(Mix(seed + 124u) & 0xffff));
			g->Set("rival", rival);
			g->Set("why", pirate ? "hunt" : "raid");
		}

		// Where they stand now: the ladder their life is on and how far up it the hull they fly says they are.
		auto cls = ship->Spec != nullptr ? ship->Spec->Class : ShipClass::Freighter;
		if (pirate)
		{
			p->Track = CareerTrack::Pirate;
			p->Rank = ship->IsEscort() ? 1 : IsCapital(cls) ? 4 : 3;
			if (p->Rank >= 3) p->Betrayed = Roll(seed, 130) < 0.6; // most captains got their hull over someone's body
		}
		else if (navy)
		{
			p->Track = CareerTrack::Navy;
			p->Rank = IsCapital(cls) ? 3 : ship->IsEscort() ? 1 : 2;
		}
		else
		{
			p->Track = CareerTrack::Civilian;
			p->Rank = ship->IsEscort() ? 1 : 2;
		}
		p->Renown += cs::Math::Min(6.0, ship->TripsCompleted * 0.1);
		p->Renown = cs::Math::Max(p->Renown, Careers::RenownFor(p->Track, p->Rank));
		p->RankJd = cs::Math::Min(jd, joinedJd + span * 0.8);
	}

	cs::Ref<NamedPilot> Characters::Embodied(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd)
	{
		auto p = Meet(world, ship, MeetKind::Embodied, jd);
		if (p == nullptr) return nullptr;
		p->TimesEmbodied++;
		p->Held = true;
		return p;
	}

	void Characters::Released(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd)
	{
		auto p = Find(ship);
		if (p == nullptr || !p->Held) return;
		p->Held = false;
		// The rationalization rule: each act done while held becomes the pilot's own, with a reason that fits who they are
		// and where they stood. The motive is chosen here, deterministically, so every peer and a rewind tell the same story.
		for (const auto& e : p->Story)
		{
			if (!e->Owned || !e->Slot("motive").empty()) continue;
			e->Set("motive", cs::Str(static_cast<int>(Mix(static_cast<uint32_t>(e->Seed) ^ static_cast<uint32_t>(p->Seed)) & 0xffff)));
		}
		Advance(world, p, jd);
	}

	cs::Ref<LifeEvent> Characters::Act(const cs::Ref<NamedPilot>& pilot, LifeEventKind kind, double jd, double renown, std::initializer_list<cs::String> slots)
	{
		if (pilot == nullptr) return nullptr;
		uint32_t h = Mix(static_cast<uint32_t>(pilot->Seed) ^ Mix(static_cast<uint32_t>(pilot->Story.Count()) * 977u + static_cast<uint32_t>(kind)));
		auto e = pilot->Add(jd, kind, static_cast<int>(h & 0xffff));
		const cs::String* key = nullptr;
		for (const auto& s : slots)
		{
			if (key == nullptr) key = &s;
			else
			{
				e->Set(*key, s);
				key = nullptr;
			}
		}
		e->Owned = pilot->Held;
		pilot->Renown += renown;
		return e;
	}

	void Characters::SurvivedBattle(const cs::Ref<NamedPilot>& pilot, const cs::Ref<Battle>& battle, double jd)
	{
		if (pilot == nullptr || battle == nullptr) return;
		for (const auto& e : pilot->Story)
			if (e->Kind == LifeEventKind::SurvivedBattle && e->Slot("battle") == battle->Name) return;
		pilot->BattlesSurvived++;
		Act(pilot, LifeEventKind::SurvivedBattle, jd, 2, { "battle", battle->Name, "ship", pilot->ShipName });
	}

	void Characters::Betrayal(const cs::Ref<NamedPilot>& pilot, const cs::String& whom, double jd)
	{
		if (pilot == nullptr) return;
		pilot->Betrayed = true;
		Act(pilot, LifeEventKind::Betrayal, jd, 3, { "whom", whom });
	}

	void Characters::Detain(const cs::Ref<NamedPilot>& pilot, const cs::String& at, const cs::Ref<Core::Faction>& by, double jd)
	{
		if (pilot == nullptr) return;
		if (pilot->Ship != nullptr) ByShip.Remove(pilot->Ship);
		pilot->Ship = nullptr;
		pilot->Held = false;
		pilot->Track = CareerTrack::Reform;
		pilot->Rank = 0;
		pilot->RankJd = jd;
		pilot->DetainedJd = jd;
		pilot->Renown = 0;
		pilot->Ashore = at;
		if (by != nullptr) pilot->Faction = by;
		Act(pilot, LifeEventKind::Detained, jd, 0, { "place", at, "faction", by != nullptr ? by->Name : cs::String("the law") });
	}

	void Characters::Grudge(const cs::Ref<NamedPilot>& pilot, const cs::String& whom, double jd, const cs::String& why)
	{
		if (pilot == nullptr || whom.empty()) return;
		bool held = pilot->Grudges.Remove(whom);
		pilot->Grudges.Add(whom);
		if (held) return; // an old grudge, freshened: no new beat
		Act(pilot, LifeEventKind::Grudge, jd, 0, { "rival", whom, "why", why });
	}

	void Characters::Promote(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd, const cs::String& why)
	{
		p->Rank++;
		p->RankJd = jd;
		auto title = Careers::Title(p->Track, p->Rank);
		if (p->Track == CareerTrack::Pirate && p->Rank == Careers::TopRank(CareerTrack::Pirate))
			Act(p, LifeEventKind::CrownedKing, jd, 0, { "title", title, "faction", p->Faction != nullptr ? p->Faction->Name : cs::String("the clans") });
		else if (p->Track == CareerTrack::Reform && p->Rank == 1)
			Act(p, LifeEventKind::Rehabilitated, jd, 0, { "title", title, "place", p->Ashore });
		else if (p->Track == CareerTrack::Reform && p->Rank == 3)
		{
			Act(p, LifeEventKind::Graduated, jd, 1, { "title", title, "place", p->Ashore });
			Post(world, p, jd, false);
		}
		else if ((p->Track == CareerTrack::Reform && p->Rank == 5) || (p->Track == CareerTrack::Navy && p->Rank == 3))
		{
			Act(p, LifeEventKind::Promoted, jd, 0, { "title", title, "why", why });
			Post(world, p, jd, true);
		}
		else Act(p, LifeEventKind::Promoted, jd, 0, { "title", title, "why", why });
	}

	void Characters::Post(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd, bool capital)
	{
		if (world == nullptr || world->Fleet == nullptr) return;
		// The first hull of their faction of the kind wanted with no named pilot of its own, in fleet order: the same on
		// every peer. A capital ship for a captain, a fighting hull for a new pilot.
		cs::Ref<Ship> pick;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s == nullptr || s->Destroyed || s->Faction != p->Faction || s->Spec == nullptr || s == world->PilotedShip || ByShip.ContainsKey(s)) continue;
			if (capital ? !IsCapital(s->Spec->Class) : !IsSmallCombat(s->Spec->Class)) continue;
			pick = s;
			break;
		}
		if (pick == nullptr)
		{
			Act(p, LifeEventKind::Promoted, jd, 0, { "title", Careers::Title(p->Track, p->Rank), "why", "awaiting a posting" });
			return;
		}
		if (p->Ship != nullptr) ByShip.Remove(p->Ship);
		p->Ship = pick;
		p->ShipName = pick->Name;
		p->Ashore = cs::String();
		ByShip[pick] = p;
		if (!p->ShipsFlown.Contains(pick->Name)) p->ShipsFlown.Add(pick->Name);
		Act(p, capital ? LifeEventKind::GivenCommand : LifeEventKind::FlewShip, jd, 0, { "ship", pick->Name, "class", ClassName(pick) });
	}

	void Characters::Advance(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd)
	{
		if (p == nullptr) return;
		for (int guard = 0; guard < 8 && p->Rank < Careers::TopRank(p->Track); guard++)
		{
			if (p->Track == CareerTrack::Reform && p->Rank < 3)
			{
				double need = p->Rank == 0 ? Careers::DetentionDays : p->Rank == 1 ? Careers::RehabilitationDays : Careers::SchoolDays;
				need *= 1.5 - 0.5 * p->Honour; // the honest serve their time quicker
				double due = p->RankJd + need;
				if (jd < due) return;
				Promote(world, p, due, "time served");
				continue;
			}
			if (p->Track == CareerTrack::Pirate && p->Rank == 1 && !p->Betrayed) return; // lieutenant only over someone's body
			if (p->Renown < Careers::RenownFor(p->Track, p->Rank + 1)) return;
			Promote(world, p, jd, p->Track == CareerTrack::Pirate && p->Rank == 1 ? cs::String("betrayal") : cs::String("renown"));
		}
	}

	cs::String Characters::Tell(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& e, bool firstPerson, double jd)
	{
		StoryBeat beat;
		beat.Pilot = pilot;
		beat.Event = e;
		beat.FirstPerson = firstPerson;
		beat.NowJd = jd;
		auto& teller = tellerSlot();
		if (teller != nullptr)
		{
			try
			{
				auto said = teller->Tell(beat);
				if (!said.empty()) return said;
			}
			catch (...) {}
		}
		TemplateStoryTeller fallback;
		return fallback.Tell(beat);
	}

	cs::List<cs::String> Characters::LifeStory(const cs::Ref<NamedPilot>& pilot, double jd, bool firstPerson, int max)
	{
		cs::List<cs::String> lines;
		if (pilot == nullptr) return lines;
		int from = cs::Math::Max(0, pilot->Story.Count() - max);
		// The birth is always told, then the newest beats.
		if (from > 0 && pilot->Story.Count() > 0) lines.Add(Tell(pilot, pilot->Story[0], firstPerson, jd));
		for (int i = from; i < pilot->Story.Count(); i++)
		{
			if (pilot->Story[i]->Kind == LifeEventKind::Met) continue;
			auto line = Tell(pilot, pilot->Story[i], firstPerson, jd);
			if (!line.empty()) lines.Add(line);
		}
		return lines;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(MeetKind, "SpaceGod.Core.MeetKind")
	SG_ENUM(CareerTrack, "SpaceGod.Core.CareerTrack")
	SG_ENUM(LifeEventKind, "SpaceGod.Core.LifeEventKind")

	SG_SERIALIZE(LifeEvent, "SpaceGod.Core.LifeEvent",
		SG_F(Jd), SG_F(Kind), SG_F(Seed), SG_F(Slots), SG_F(Owned))

	SG_SERIALIZE(NamedPilot, "SpaceGod.Core.NamedPilot",
		SG_F(Id), SG_F(Name), SG_F(Callsign), SG_F(Seed), SG_F(Pronoun), SG_F(BornJd), SG_F(Birthplace), SG_F(Faction), SG_F(Ship),
		SG_F(ShipName), SG_F(Courage), SG_F(Greed), SG_F(Honour), SG_F(Warmth), SG_F(Spite), SG_F(Chatter), SG_F(Track), SG_F(Rank),
		SG_F(Renown), SG_F(RankJd), SG_F(DetainedJd), SG_F(Ashore), SG_F(Betrayed), SG_F(ShipsFlown), SG_F(BattlesSurvived),
		SG_F(Robberies), SG_F(TimesRobbed), SG_F(Surrenders), SG_F(Grudges), SG_F(Story), SG_F(FirstMet), SG_F(MetJd),
		SG_F(TimesHailed), SG_F(TimesEmbodied), SG_F(Held),
		SG_OPT(PodsSalvaged), SG_OPT(TonnesSalvaged)) // US-T95: cargo pods tractored, absent from older saves

	SG_SERIALIZE(Characters, "SpaceGod.Core.Characters",
		SG_F(All), SG_F(ByShip), SG_F(NextId), SG_F(Grievance))
}
