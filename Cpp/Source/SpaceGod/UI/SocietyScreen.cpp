#include "UI/SocietyScreen.h"

#include "UnitySpaceApp.h"
#include "UI/UiMotion.h"
#include "UI/UiStyle.h"

#include "Astro.h"
#include "SimClock.h"
#include "Ships.h"
#include "Economy/Settlement.h"
#include "Systems/Civilization/CivHash.h" // US-T371
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Culture.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Civilization/Strife.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h" // US-T383: the summit outlook reads the wars as the resolver does
#include "Systems/Polities.h"
#include "Systems/Society.h"

#include "HAL/PlatformTime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		const char* const SocietyPageNames[] = { "Feed", "Houses", "People", "Organisations", "Worlds", "Watched", "Your hand", "This month" };
		const char* const SocietyDoingsNames[] = { "All doings", "Steered only", "Watched only" };

		/// The module views' names on the screen (CivHands::Views(), in its order).
		const char* SocietyViewName(const cs::String& v)
		{
			if (v == "feuds") return "Feuds";
			if (v == "regimes") return "Regimes";
			if (v == "trials") return "Trials";
			if (v == "admirals") return "Admirals & strikes";
			if (v == "crime") return "Crime";
			if (v == "money") return "Money";
			if (v == "chronicle") return "Chronicle";
			return "View";
		}

		/// The module groups of the feed, by the first kind of each (Civilization.h's SocialKind, in its order). A kind past
		/// the last group's (one a later module adds) falls in "Other", so it still shows.
		struct SocietyGroup
		{
			const char* Name;
			SocialKind First;
		};
		const SocietyGroup SocietyGroups[] = {
			{ "Society", SocialKind::Birth }, { "Persona", SocialKind::Friendship }, { "Kinship", SocialKind::FeudDeclared },
			{ "Politics", SocialKind::IdeologyShift }, { "Strife", SocialKind::Protest }, { "Underworld", SocialKind::SyndicateRises },
			{ "Commerce", SocialKind::UnionFormed }, { "Culture", SocialKind::FaithFounded }, { "Health", SocialKind::Outbreak },
			{ "Press", SocialKind::OutletFounded }, { "Witness", SocialKind::WorldConquered }, // US-T383: the wider world as seen (US-T292)
		};
		constexpr int SocietyGroupsKnown = static_cast<int>(sizeof(SocietyGroups) / sizeof(SocietyGroups[0]));

		cs::String SocietyPct(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }
		cs::String SocietySigned(double v) { return cs::Cat(v >= 0 ? "+" : "", cs::Fmt(v, "0.00")); }
		cs::String SocietyDate(double jd) { return jd <= 0 ? cs::String("?") : Astro::ToDateTime(jd).ToString("yyyy-MM-dd"); }

		/// US-T365: ASCII lower case (the enum names are plain ASCII words).
		cs::String SocietyLower(cs::String s)
		{
			for (auto& ch : s)
				if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
			return s;
		}

		/// US-T365: one sentence that sums a person up from what Society and the Civilization layer keep (their traits, place in
		/// their house, marriage, the organisation they lead or belong to, their faith, what they want and their name), e.g.
		/// "A greedy, charming head of House Vance on Kepler Station, 54 years old, married to Ida Vance, who leads the Free
		/// Traders' Union, keeps the Church of the Long Night and wants wealth; widely admired." Read only and deterministic.
		cs::String CharacterLine(const Citizen& c, Civilization& civ, double jd)
		{
			// Up to two traits, in the enum's order.
			cs::String adjectives;
			int shownTraits = 0;
			if (c.Persona)
				for (int bit = 1; bit <= (1 << 11) && shownTraits < 2; bit <<= 1)
					if (c.Traits & bit)
					{
						adjectives = cs::Cat(adjectives, shownTraits > 0 ? ", " : "", SocietyLower(Core::ToString(static_cast<Trait>(bit))));
						shownTraits++;
					}

			cs::String houseName = c.House != nullptr ? cs::Cat("House ", c.House->Surname) : cs::String();
			cs::String where = c.House != nullptr && c.House->Home != nullptr ? cs::Cat(" on ", c.House->Home->Name) : cs::String();
			cs::String noun;
			switch (c.Role)
			{
			case CitizenRole::Head: noun = houseName.empty() ? cs::String("head of a house") : cs::Cat("head of ", houseName); break;
			case CitizenRole::Spouse: noun = houseName.empty() ? cs::String("spouse") : cs::Cat("spouse in ", houseName); break;
			case CitizenRole::Heir: noun = houseName.empty() ? cs::String("heir") : cs::Cat("heir of ", houseName); break;
			case CitizenRole::Governor:
				noun = c.House != nullptr && c.House->Home != nullptr ? cs::Cat("governor of ", c.House->Home->Name) : cs::String("governor");
				if (!houseName.empty()) noun = cs::Cat(noun, ", of ", houseName);
				where = cs::String();
				break;
			default: noun = houseName.empty() ? cs::String("child") : cs::Cat("child of ", houseName); break;
			}
			cs::String head = adjectives.empty() ? noun : cs::Cat(adjectives, " ", noun);
			char first = head.empty() ? 'x' : head[0];
			bool vowel = first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u';
			cs::String line = cs::Cat(vowel ? "An " : "A ", head, where);

			// Age, and their marriage.
			line = c.Alive() ? cs::Cat(line, ", ", static_cast<int>(c.AgeYears(jd)), " years old")
				: cs::Cat(line, ", dead at ", static_cast<int>(c.AgeYears(c.DiedJd)), " (", SocietyDate(c.DiedJd), ")");
			if (c.Spouse != nullptr)
				line = c.Spouse->Alive() || !c.Alive() ? cs::Cat(line, ", married to ", c.Spouse->FullName())
					: cs::Cat(line, ", widowed by ", c.Spouse->FullName());

			// Deeds: what they run, lead or belong to, the faith they keep and what they want.
			cs::List<cs::String> deeds;
			bool living = c.Alive();
			auto verb = [living](const char* now, const char* then) { return cs::String(living ? now : then); };
			if (living && c.Role == CitizenRole::Head && c.House != nullptr && c.House->Business != nullptr && c.House->Business->Trading())
				deeds.Add(cs::Cat("runs ", c.House->Business->Name));
			cs::String led;
			for (const auto& o : civ.Orgs)
				if (o != nullptr && o->Active() && o->Leader != nullptr && o->Leader.get() == &c)
				{
					led = Narrator::OrgName(*o);
					break;
				}
			if (!led.empty()) deeds.Add(cs::Cat("leads ", led));
			else
				for (const auto& o : c.Orgs)
					if (o != nullptr && o->Active())
					{
						deeds.Add(cs::Cat(verb("belongs to ", "belonged to "), Narrator::OrgName(*o)));
						break;
					}
			if (c.Faith != nullptr) deeds.Add(cs::Cat(verb("keeps ", "kept "), Narrator::OrgName(*c.Faith)));
			if (c.Persona) deeds.Add(cs::Cat(verb("wants ", "wanted "), SocietyLower(Core::ToString(c.Goal))));
			if (c.Bribes > 0) deeds.Add(cs::Cat(verb("has taken ", "took "), Narrator::Money(c.Bribes), " in bribes"));
			if (deeds.Count() > 0)
			{
				line = cs::Cat(line, ", who ");
				for (int i = 0; i < deeds.Count(); i++)
					line = cs::Cat(line, i == 0 ? "" : (i == deeds.Count() - 1 ? " and " : ", "), deeds[i]);
			}

			// What people think of them.
			if (c.Reputation >= 0.5) line = cs::Cat(line, "; widely admired");
			else if (c.Reputation >= 0.2) line = cs::Cat(line, "; well thought of");
			else if (c.Reputation <= -0.5) line = cs::Cat(line, "; widely despised");
			else if (c.Reputation <= -0.2) line = cs::Cat(line, "; poorly thought of");
			return cs::Cat(line, ".");
		}

		/// US-T371: what a hand (a sg.Civ line) will do if used now, judged as the simulation would judge it: the refusals its
		/// resolver checks first (Kinship::Declare, Politics::Overthrow and the rest), else what it brings about. The SpaceGod's
		/// hand forces its outcome (a steered hit always kills, steered spies are never caught, a swung election is won), so
		/// most are sure; an election's share is the day's hash. Read only: no RNG drawn and nothing written, Bench unchanged.
		cs::String HandOutlook(const cs::String& handLine, Galaxy& galaxy, Civilization& civ, double jd)
		{
			if (handLine.empty()) return cs::String();
			auto args = CivHands::Words(handLine);
			if (args.empty()) return cs::String();
			const cs::String& verb = args[0];
			auto society = Society::Find(galaxy);
			if (society == nullptr) return "Refused now: the galaxy's people have not woken yet.";
			auto num = [&](size_t i) { return args.size() > i ? std::atoi(args[i].c_str()) : 0; };
			auto house = [&](size_t i) { return society->HouseById(num(i)); };
			auto person = [&](size_t i) { return society->CitizenById(num(i)); };
			auto world = [&](size_t i) -> cs::Ref<Settlement>
			{
				if (galaxy.Polities == nullptr) return nullptr;
				cs::String rest;
				for (size_t j = i; j < args.size(); j++) rest = cs::Cat(rest, j > i ? " " : "", args[j]);
				cs::String want = cs::Strings::ToUpperInvariant(rest);
				for (const auto& s : galaxy.Polities->Settlements())
					if (cs::Strings::ToUpperInvariant(s->Name) == want) return s;
				return nullptr;
			};
			auto faction = [&](size_t i) -> cs::Ref<Faction>
			{
				if (galaxy.Polities == nullptr || args.size() <= i) return nullptr;
				if (auto f = galaxy.Polities->Find(args[i])) return f;
				cs::String spaced = args[i];
				for (auto& ch : spaced)
					if (ch == '_') ch = ' ';
				return galaxy.Polities->Find(spaced);
			};
			auto standing = [](const cs::Ref<Household>& h) { return h != nullptr && !h->Scattered && h->Living() > 0; };
			auto no = [](const char* why) { return cs::Cat("Refused now: ", why, "."); };
			auto sure = [](const cs::String& what) { return cs::Cat("Sure: ", what, "."); };

			if (verb == "feud")
			{
				auto a = house(1), b = house(2);
				if (!standing(a) || !standing(b)) return no("no such house standing");
				if (a == b) return no("a house cannot feud with itself");
				if (civ.Kinship != nullptr)
					if (auto f = civ.Kinship->FeudBetween(a, b))
						return sure(cs::Cat("their feud grows hotter, heat ", cs::Fmt(f->Heat, "0.00"), " to ", cs::Fmt(cs::Math::Min(1.0, f->Heat + 0.35), "0.00")));
				return sure("a feud is sworn at heat 0.70, the heads made enemies and their states cooled");
			}
			if (verb == "reconcile")
			{
				if (civ.Kinship == nullptr || civ.Kinship->FeudBetween(house(1), house(2)) == nullptr) return no("those houses are not feuding");
				return sure("the feud ends");
			}
			if (verb == "fame")
			{
				auto c = person(1);
				if (c == nullptr || !c->Alive()) return no("no such person living");
				if (c->Reputation >= Kinship::FameThreshold) return no("they are famous already");
				return sure(cs::Cat("their standing leaps from ", cs::Fmt(c->Reputation, "0.00"), " to 0.85"));
			}
			if (verb == "seduce")
			{
				auto a = person(1), b = person(2);
				if (a == nullptr || b == nullptr || !a->Alive() || !b->Alive() || a == b) return no("no such pair living");
				if (a->AgeYears(jd) < 18 || b->AgeYears(jd) < 18) return no("only grown people");
				if (a->Spouse == b) return no("they are married to each other");
				return sure("they become lovers");
			}
			if (verb == "elect")
			{
				auto party = civ.OrgById(num(1));
				if (party == nullptr || party->Kind != OrgKind::Party || !party->Active()) return no("no such party");
				if (civ.Politics == nullptr || civ.Politics->RegimeOf(party->Faction) == nullptr) return no("its state has no government to win");
				double share = 0.5 + 0.2 * Civ::Unit(Civ::H(static_cast<std::uint32_t>(party->Id), Civ::DayOf(jd))); // Politics::Elect's own share
				return sure(cs::Cat("they win today with ", cs::Fmt(share * 100, "0"), "% of the vote and take the government"));
			}
			if (verb == "coup")
			{
				auto c = person(1);
				if (c == nullptr || !c->Alive() || c->House == nullptr || c->House->Home == nullptr) return no("no such person living");
				auto r = civ.Politics != nullptr ? civ.Politics->RegimeOf(c->House->Home->Owner) : nullptr;
				if (r == nullptr) return no("their state has no government to seize");
				if (r->Ruler == c) return no("they rule already");
				return sure(r->Ruler != nullptr ? cs::Cat("they overthrow ", r->Ruler->FullName(), " and rule without elections") : cs::String("they seize the government and rule without elections"));
			}
			if (verb == "acquit")
			{
				auto t = civ.Politics != nullptr ? civ.Politics->TrialById(num(1)) : nullptr;
				if (t == nullptr) return no("no such trial");
				if (t->Decided) return no("the verdict is in already");
				return sure("the court acquits, bought");
			}
			if (verb == "summit")
			{
				auto a = faction(1), b = faction(2);
				if (a == nullptr || b == nullptr || a == b) return no("name two states");
				// US-T383: the resolver's own test (Politics::Summit): no wars table refuses; at war or hostile passes.
				if (galaxy.Wars == nullptr) return no("no wars");
				bool atWar = false;
				for (const auto& w : galaxy.Wars->Active())
					if (w->Involves(a) && w->Involves(b)) atWar = true;
				if (!atWar && !Civilization::Hostile(galaxy, a, b)) return no("they are not at war");
				return sure("their leaders meet to talk peace");
			}
			if (verb == "riot")
			{
				auto s = world(1);
				if (!Civilization::Stands(s)) return no("no such world standing");
				return sure("riots and a 10-day strike; unrest up, approval down");
			}
			if (verb == "arm")
			{
				auto s = world(1);
				if (!Civilization::Stands(s)) return no("no such world standing");
				return sure("its rebels grow stronger (+0.4) and richer, a cell founded if it has none");
			}
			if (verb == "spy")
			{
				auto by = faction(1), on = faction(2);
				if (by == nullptr || on == nullptr || by == on) return no("name two states");
				if (galaxy.Polities == nullptr || galaxy.Polities->CapitalOf(by) == nullptr) return no("that state has no capital to run spies from");
				return "Sure not to be caught; the operation may still find nothing to take.";
			}
			if (verb == "hire")
			{
				auto company = civ.OrgById(num(1));
				if (company == nullptr || company->Kind != OrgKind::Mercenaries || !company->Active()) return no("no such free company");
				if (faction(2) == nullptr) return no("no such state");
				return sure(company->Faction != nullptr ? "they turn their coats and fight for the new paymaster" : "they sign on and fight for it");
			}
			if (verb == "hit")
			{
				auto c = person(1);
				if (c == nullptr || !c->Alive()) return no("no such person living");
				return sure("the hand's killers do not miss: they die within a day, or when a contract already out on them falls due");
			}
			if (verb == "racket")
			{
				auto s = world(1);
				if (!Civilization::Stands(s)) return no("no such world standing");
				auto c = civ.CivicsOf(s);
				return sure(c != nullptr ? cs::Cat("a syndicate grows (+0.4) and crime rises from ", cs::Fmt(c->Crime, "0.00"), " to ", cs::Fmt(cs::Math::Min(1.0, c->Crime + 0.2), "0.00")) : cs::String("a syndicate takes the streets"));
			}
			if (verb == "crackdown")
			{
				auto s = world(1);
				if (!Civilization::Stands(s)) return no("no such world standing");
				auto c = civ.CivicsOf(s);
				if (c == nullptr) return no("nobody watches that world yet");
				return sure(cs::Cat("crime falls from ", cs::Fmt(c->Crime, "0.00"), " to ", cs::Fmt(cs::Math::Max(0.0, c->Crime - 0.2), "0.00"), ", its syndicate weakened"));
			}
			if (verb == "takeover")
			{
				auto raider = house(1);
				auto b = society->BusinessById(num(2));
				if (!standing(raider)) return no("no such house standing");
				if (b == nullptr || !b->Trading()) return no("no such business trading");
				if (b->Owner == raider) return no("they own it already");
				return sure(cs::Cat("House ", raider->Surname, " seizes it (capital ", Narrator::Money(b->Capital), ")"));
			}
			if (verb == "bankrun")
			{
				auto bank = civ.OrgById(num(1));
				if (bank == nullptr || bank->Kind != OrgKind::Bank || !bank->Active()) return no("no such bank");
				return sure("the bank fails");
			}
			if (verb == "landmark")
			{
				auto c = civ.CivicsOf(world(1));
				if (c == nullptr) return no("nobody watches that world yet");
				if (!c->Landmark.empty()) return no("it is famous already");
				return sure("a wonder is found there and draws visitors");
			}
			if (verb == "preach")
			{
				auto c = person(1);
				if (c == nullptr || !c->Alive() || c->House == nullptr || c->House->Home == nullptr) return no("no such person living");
				return sure("a new faith is founded at their home with a tenth of its people, and they grow in standing");
			}
			if (verb == "festival")
			{
				if (civ.CivicsOf(world(1)) == nullptr) return no("nobody watches that world yet");
				return sure("a festival is held");
			}
			if (verb == "rig")
			{
				auto team = civ.OrgById(num(1));
				if (team == nullptr || team->Kind != OrgKind::Team || !team->Active()) return no("no such team");
				int top = 0;
				for (const auto& t : civ.OrgsOf(OrgKind::Team)) top = cs::Math::Max(top, t->Score);
				return sure(cs::Cat("they top the table, ", team->Score, " to ", top + 3, " points"));
			}
			if (verb == "rumour")
			{
				auto c = person(1);
				if (c == nullptr || !c->Alive()) return no("no such person living");
				return sure(cs::Cat("a false tale spreads; their standing slips from ", cs::Fmt(c->Reputation, "0.00"), " to ", cs::Fmt(cs::Math::Max(-1.0, c->Reputation - 0.05), "0.00")));
			}
			if (verb == "plague")
			{
				auto c = civ.CivicsOf(world(1));
				if (c == nullptr) return no("nobody watches that world yet");
				return sure(cs::Cat("the sick rise from ", cs::Fmt(c->Infected * 100, "0"), "% to ", cs::Fmt(cs::Math::Min(1.0, c->Infected + 0.08) * 100, "0"), "%"));
			}
			if (verb == "cure")
			{
				auto c = civ.CivicsOf(world(1));
				if (c == nullptr) return no("nobody watches that world yet");
				if (c->Infected <= 0) return no("it is not sick");
				return sure(cs::Cat("its ", cs::Fmt(c->Infected * 100, "0"), "% sick recover and are immune"));
			}
			if (verb == "disaster")
			{
				if (civ.CivicsOf(world(1)) == nullptr) return no("nobody watches that world yet");
				return sure("catastrophe strikes it");
			}
			if (verb == "aid")
			{
				if (faction(1) == nullptr) return no("no such state");
				if (civ.CivicsOf(world(2)) == nullptr) return no("nobody watches that world yet");
				return sure("relief ships are sent");
			}
			if (verb == "slant")
			{
				auto outlet = civ.OrgById(num(1));
				if (outlet == nullptr || outlet->Kind != OrgKind::Outlet || !outlet->Active()) return no("no such outlet");
				cs::String to = args.size() > 2 ? args[2] : cs::String();
				double stance = to == "for" ? 0.9 : to == "against" ? -0.9 : 0.0;
				return sure(cs::Cat("its line goes from ", cs::Fmt(outlet->Stance, "0.00"), " to ", cs::Fmt(stance, "0.00")));
			}
			if (verb == "investigate")
			{
				auto outlet = civ.OrgById(num(1));
				if (outlet == nullptr || outlet->Kind != OrgKind::Outlet || !outlet->Active()) return no("no such outlet");
				return "Sure if there is anything in its system to dig up; refused if not.";
			}
			if (verb == "donate")
			{
				auto h = house(1);
				if (!standing(h)) return no("no such house standing");
				return sure(cs::Cat("they give ", Narrator::Money(h->Wealth * 0.2), " (a fifth of their wealth); their world's care and approval rise and their head's standing with it"));
			}
			return cs::String();
		}
	}

	int SocietyScreen::GroupOf(int k)
	{
		if (k > static_cast<int>(SocialKind::Embargo)) return SocietyGroupsKnown; // a kind added after the Witness module's (US-T383)
		int g = 0;
		for (int i = 0; i < SocietyGroupsKnown; i++)
			if (static_cast<int>(SocietyGroups[i].First) <= k) g = i;
		return g;
	}

	int SocietyScreen::GroupCount() { return SocietyGroupsKnown + 1; }

	const char* SocietyScreen::GroupName(int g) { return g >= 0 && g < SocietyGroupsKnown ? SocietyGroups[g].Name : "Other"; }

	SocietyScreen::SocietyScreen(UnitySpaceApp* app) : app(app) {}

	SocietyScreen::~SocietyScreen() = default;

	// ---------------------------------------------------------------- the rows

	cs::String SocietyScreen::RowsKey() const
	{
		return cs::Cat(page, "|", group, "|", kind, "|", worldOnly ? 1 : 0, "|", doings, "|", orgKind, "|",
			worldOnly ? worldFilter : cs::String(), "|", search, "|", static_cast<int>(app->SocietyWatched.size()),
			"|", page >= 0 && page < FirstView ? sortBy[page] : 0); // US-T354: a new sort shows at once
	}

	// ---------------------------------------------------------------- the lists' sorts (US-T354)

	int SocietyScreen::SortCount(int p)
	{
		switch (p)
		{
		case Houses: return 5;
		case People: return 4;
		case Orgs: return 5;
		case Worlds: return 4;
		default: return 1;
		}
	}

	const char* SocietyScreen::SortName(int p, int s)
	{
		static const char* const HouseSorts[] = { "founding", "name", "wealth", "size", "oldest" };
		static const char* const PeopleSorts[] = { "renown", "name", "age", "reputation" };
		static const char* const OrgSorts[] = { "founding", "name", "strength", "funds", "members" };
		static const char* const WorldSorts[] = { "kept order", "name", "approval", "crime" };
		if (s < 0 || s >= SortCount(p)) return "";
		switch (p)
		{
		case Houses: return HouseSorts[s];
		case People: return PeopleSorts[s];
		case Orgs: return OrgSorts[s];
		case Worlds: return WorldSorts[s];
		default: return "";
		}
	}

	void SocietyScreen::StepSort()
	{
		if (page < 0 || page >= FirstView || SortCount(page) <= 1) return;
		sortBy[page] = (sortBy[page] + 1) % SortCount(page);
	}

	namespace
	{
		/// US-T354: puts <paramref name="rows"/> in order by <paramref name="keys"/> (one a row, the greatest first) or, with
		/// <paramref name="byName"/>, by their titles A to Z; ties keep the order they were built in.
		template <typename RowT>
		void SortRows(std::vector<RowT>& rows, const std::vector<double>& keys, bool byName)
		{
			std::vector<size_t> order(rows.size());
			for (size_t i = 0; i < order.size(); i++) order[i] = i;
			std::vector<cs::String> names;
			if (byName)
			{
				names.reserve(rows.size());
				for (const auto& r : rows) names.push_back(cs::Strings::ToLowerInvariant(r.Title));
				std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return names[a] < names[b]; });
			}
			else
				std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return keys[a] > keys[b]; });
			std::vector<RowT> sorted;
			sorted.reserve(rows.size());
			for (size_t i : order) sorted.push_back(rows[i]);
			rows.swap(sorted);
		}
	}

	bool SocietyScreen::Matches(const cs::String& a, const cs::String& b) const
	{
		if (search.empty()) return true;
		// Every word of the search, anywhere in either (case aside): "sol riot" finds Sol's riots.
		cs::String la = cs::Strings::ToLowerInvariant(a), lb = cs::Strings::ToLowerInvariant(b);
		cs::String words = cs::Strings::ToLowerInvariant(search);
		size_t at = 0;
		while (at < words.size())
		{
			size_t end = words.find(' ', at);
			if (end == cs::String::npos) end = words.size();
			if (end > at)
			{
				cs::String w = words.substr(at, end - at);
				if (la.find(w) == cs::String::npos && lb.find(w) == cs::String::npos) return false;
			}
			at = end + 1;
		}
		return true;
	}

	const cs::String& SocietyScreen::WordsOf(const SocialEvent& e)
	{
		auto at = eventWords.find(e.Id);
		if (at != eventWords.end()) return at->second;
		if (eventWords.size() > 50000) eventWords.clear();
		return eventWords.emplace(e.Id, Narrator::Render(e)).first->second;
	}

	void SocietyScreen::Rebuild(Galaxy& galaxy, Civilization* civ, double jd)
	{
		// Built afresh when what is shown changes, else twice a second (the galaxy moves on under it).
		cs::String key = RowsKey();
		double now = FPlatformTime::Seconds();
		if (key == rowsKey && now - rowsAt < 0.5) return;
		if (key != rowsKey) listScroll = Vector2::zero();
		rowsKey = key;
		rowsAt = now;
		rows.clear();
		if (civ == nullptr) return;
		auto society = Society::Find(galaxy);

		if (page == Feed)
		{
			cs::String world = worldOnly ? worldFilter : cs::String(); // US-T383: not the pick, which an event click clears
			for (int i = civ->Events.Count() - 1; i >= 0; i--)
			{
				const auto& e = civ->Events[i];
				if (e == nullptr) continue;
				int k = static_cast<int>(e->Kind);
				if (kind != 0 && k != kind) continue;
				if (kind == 0 && group >= 0 && GroupOf(k) != group) continue;
				if (doings == SteeredDoings && !e->Steered) continue;
				if (doings == WatchedDoings && std::none_of(app->SocietyWatched.begin(), app->SocietyWatched.end(), [&](const CivPick& p) { return About(*e, p); })) continue;
				if (!world.empty() && (e->Where == nullptr || e->Where->Name != world)) continue;
				cs::String head = cs::Cat(SocietyDate(e->Jd), "  \xC2\xB7  ", Core::ToString(e->Kind), e->Where != nullptr ? cs::Cat("  \xC2\xB7  ", e->Where->Name) : cs::String(),
					e->Steered ? cs::String("  \xC2\xB7  STEERED") : cs::String(), e->Unverified ? cs::String("  \xC2\xB7  unverified") : cs::String());
				if (!search.empty() && !Matches(head, WordsOf(*e))) continue;
				rows.push_back(Row{ head, cs::String(), CivPick(), e });
			}
			return;
		}
		if (page == Houses && society != nullptr)
		{
			// US-T354: founding (the order they were met), name, wealth, size (the living) or oldest first.
			int sort = sortBy[Houses];
			std::vector<double> keys;
			for (const auto& h : society->Houses)
			{
				if (h == nullptr || h->Scattered) continue;
				Row r{ cs::Cat("House ", h->Surname), cs::Cat(h->Home != nullptr ? h->Home->Name : cs::String("?"), "  \xC2\xB7  ", h->Living(), " living  \xC2\xB7  ",
					Narrator::Money(h->Wealth), h->Business != nullptr ? cs::Cat("  \xC2\xB7  ", h->Business->Name) : cs::String()), CivPick::House(h->Id), nullptr };
				if (!Matches(r.Title, r.Line)) continue;
				rows.push_back(r);
				keys.push_back(sort == 2 ? h->Wealth : sort == 3 ? static_cast<double>(h->Living()) : sort == 4 ? -h->FoundedJd : 0.0);
			}
			if (sort != 0) SortRows(rows, keys, sort == 1);
			return;
		}
		if (page == People && society != nullptr)
		{
			auto adults = Civilization::Adults(*society, jd);
			std::vector<cs::Ref<Citizen>> people(adults.begin(), adults.end());
			// The best and worst known first: renown either way.
			std::stable_sort(people.begin(), people.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return std::abs(a->Reputation) > std::abs(b->Reputation); });
			// The 400 best known that the search lets through (so a search reaches past the 400 shown without one).
			// US-T354: those 400 then by renown, name, age (the oldest first) or reputation (the best thought of first).
			int sort = sortBy[People];
			std::vector<double> keys;
			for (const auto& c : people)
			{
				if (rows.size() >= 400) break;
				Row r{ c->FullName(), cs::Cat(static_cast<int>(c->AgeYears(jd)), ", ", Core::ToString(c->Role), c->House != nullptr && c->House->Home != nullptr ? cs::Cat(" of ", c->House->Home->Name) : cs::String(),
					"  \xC2\xB7  reputation ", SocietySigned(c->Reputation)), CivPick::Person(c->Id), nullptr };
				if (!Matches(r.Title, r.Line)) continue;
				rows.push_back(r);
				keys.push_back(sort == 2 ? c->AgeYears(jd) : sort == 3 ? c->Reputation : 0.0);
			}
			if (sort != 0) SortRows(rows, keys, sort == 1);
			return;
		}
		if (page == Orgs)
		{
			// US-T354: founding (the order they were founded in), name, strength, funds or members.
			int sort = sortBy[Orgs];
			std::vector<double> keys;
			for (const auto& o : civ->Orgs)
			{
				if (o == nullptr || !o->Active()) continue;
				if (orgKind >= 0 && static_cast<int>(o->Kind) != orgKind) continue;
				Row r{ cs::Cat(Narrator::OrgName(*o), "   ", Core::ToString(o->Kind)),
					cs::Cat(o->Seat != nullptr ? o->Seat->Name : cs::String("?"), "  \xC2\xB7  strength ", SocietyPct(o->Strength), "  \xC2\xB7  ", Narrator::Money(o->Funds), "  \xC2\xB7  ", o->Members.Count(), " members"),
					CivPick::Org(o->Id), nullptr };
				if (!Matches(r.Title, r.Line)) continue;
				rows.push_back(r);
				keys.push_back(sort == 2 ? o->Strength : sort == 3 ? o->Funds : sort == 4 ? static_cast<double>(o->Members.Count()) : 0.0);
			}
			if (sort != 0) SortRows(rows, keys, sort == 1);
			return;
		}
		if (page == Worlds)
		{
			// US-T354: the order they are kept in, name, approval (the happiest first) or crime (the worst first).
			int sort = sortBy[Worlds];
			std::vector<double> keys;
			for (const auto& c : civ->Worlds)
			{
				if (c == nullptr || c->Where == nullptr) continue;
				Row r{ cs::Cat(c->Where->Name, c->Where->Owner != nullptr ? cs::Cat("   ", c->Where->Owner->Name) : cs::String(), c->Quarantined(jd) ? cs::String("   QUARANTINED") : cs::String()),
					cs::Cat("approval ", SocietySigned(c->Approval), "  \xC2\xB7  crime ", SocietyPct(c->Crime), "  \xC2\xB7  sick ", SocietyPct(c->Infected), "  \xC2\xB7  weary ", SocietyPct(c->Weariness),
						c->Faith != nullptr ? cs::Cat("  \xC2\xB7  ", Narrator::OrgName(*c->Faith)) : cs::String()),
					CivPick::World(c->Where->Name), nullptr };
				if (!Matches(r.Title, r.Line)) continue;
				rows.push_back(r);
				keys.push_back(sort == 2 ? static_cast<double>(c->Approval) : sort == 3 ? static_cast<double>(c->Crime) : 0.0);
			}
			if (sort != 0) SortRows(rows, keys, sort == 1);
			return;
		}
		if (page == Watched)
		{
			// What is watched, in the order it was taken up, each with its latest event (US-T351: and how many came since it was
			// last looked at, those first in the line).
			for (const auto& p : app->SocietyWatched)
			{
				cs::Ref<SocialEvent> latest;
				int count = 0, fresh = 0;
				int seenId = app->WatchSeenId(p);
				for (int i = civ->Events.Count() - 1; i >= 0; i--)
				{
					const auto& e = civ->Events[i];
					if (e == nullptr || !About(*e, p)) continue;
					if (count++ == 0) latest = e;
					if (e->Id > seenId) fresh++;
				}
				static const char* const WatchKinds[] = { "", "house", "person", "organisation", "world", "trial", "business", "state" };
				Row r{ cs::Cat(NameOf(p, galaxy, *civ), fresh > 0 ? cs::Cat("   ", fresh, " NEW") : cs::String()),
					cs::Cat(WatchKinds[static_cast<int>(p.Kind)], "  \xC2\xB7  ", count, count == 1 ? " event kept" : " events kept",
					latest != nullptr ? cs::Cat("  \xC2\xB7  latest ", SocietyDate(latest->Jd), ": ", Core::ToString(latest->Kind)) : cs::String(),
					fresh > 0 ? cs::Cat("  \xC2\xB7  ", fresh, fresh == 1 ? " event" : " events", " since you last looked") : cs::String()), p, nullptr };
				if (Matches(r.Title, r.Line)) rows.push_back(r);
			}
			return;
		}
		if (page == YourHand)
		{
			// US-T368: first, what is ripe for the hand now, the most urgent first (read only: thresholds the simulation
			// itself works to): a feud running hot enough to come to blows, a firm whose losses will close it within months,
			// an ageing head with nobody to follow them, a governor whose world has turned against its flag.
			{
				struct Ripe
				{
					double Urgency;
					Row R;
				};
				std::vector<Ripe> ripe;
				if (civ->Kinship != nullptr)
					for (const auto& f : civ->Kinship->Feuds)
					{
						if (f == nullptr || !f->Active() || f->Heat < 0.7 || f->A == nullptr || f->B == nullptr) continue;
						ripe.push_back(Ripe{ f->Heat, Row{ cs::Cat("RIPE  \xC2\xB7  House ", f->A->Surname, " and House ", f->B->Surname, " near open war"),
							cs::Cat("feud over ", f->Cause, ", heat ", cs::Fmt(f->Heat, "0.00"), f->Incidents > 0 ? cs::Cat(", ", f->Incidents, f->Incidents == 1 ? " incident" : " incidents") : cs::String()),
							CivPick::House(f->A->Id), nullptr } });
					}
				if (society != nullptr)
				{
					for (const auto& b : society->Businesses)
					{
						// A firm closes when its capital falls under 2e5 (Society::Books); ripe when last month's loss would take it
						// there within three months.
						if (b == nullptr || !b->Trading() || b->LastMonth >= 0) continue;
						double monthsLeft = (b->Capital - 2e5) / -b->LastMonth;
						if (monthsLeft > 3) continue;
						ripe.push_back(Ripe{ 1.0 - cs::Math::Max(0.0, monthsLeft) / 4.0, Row{ cs::Cat("RIPE  \xC2\xB7  ", b->Name, " near failing"),
							cs::Cat(Narrator::Money(b->Capital), " left, ", Narrator::Money(-b->LastMonth), " lost last month", monthsLeft <= 1 ? cs::String(", a month at most") : cs::Cat(", about ", static_cast<int>(monthsLeft), " months")),
							CivPick::Business(b->Id), nullptr } });
					}
					for (const auto& h : society->Houses)
					{
						if (h == nullptr || h->Scattered) continue;
						auto head = h->Head();
						if (head != nullptr && head->Alive() && head->AgeYears(jd) >= 55)
						{
							bool heir = false;
							for (const auto& m : h->Members)
								if (m != nullptr && m != head && m->Alive() && (m->Role == CitizenRole::Heir || m->Role == CitizenRole::Child)) { heir = true; break; }
							if (!heir)
								ripe.push_back(Ripe{ cs::Math::Min(1.0, (head->AgeYears(jd) - 55) / 30.0), Row{ cs::Cat("RIPE  \xC2\xB7  House ", h->Surname, " has no heir"),
									cs::Cat(head->FullName(), ", its head, is ", static_cast<int>(head->AgeYears(jd)), " with no son or daughter living"), CivPick::Person(head->Id), nullptr } });
						}
						for (const auto& m : h->Members)
						{
							if (m == nullptr || !m->Alive() || m->Role != CitizenRole::Governor || h->Home == nullptr) continue;
							for (const auto& w : civ->Worlds)
							{
								if (w == nullptr || w->Where != h->Home || w->Approval >= -0.3) continue;
								ripe.push_back(Ripe{ cs::Math::Min(1.0, static_cast<double>(-w->Approval)), Row{ cs::Cat("RIPE  \xC2\xB7  ", m->FullName(), " near ousting"),
									cs::Cat("governor of ", h->Home->Name, ", where approval of its flag is ", SocietySigned(w->Approval)), CivPick::Person(m->Id), nullptr } });
								break;
							}
						}
					}
				}
				std::stable_sort(ripe.begin(), ripe.end(), [](const Ripe& a, const Ripe& b) { return a.Urgency > b.Urgency; });
				int shown = 0;
				for (const auto& r : ripe)
				{
					if (shown >= RipeMax) break;
					if (!Matches(r.R.Title, r.R.Line)) continue;
					rows.push_back(r.R);
					shown++;
				}
			}
			// US-T325: what the SpaceGod's hand brought about, newest first, each with how much followed it.
			int n = civ->Events.Count();
			for (int i = n - 1; i >= 0; i--)
			{
				const auto& e = civ->Events[i];
				if (e == nullptr || !e->Steered) continue;
				auto subjects = SubjectsOf(*e);
				int followed = 0;
				cs::Ref<SocialEvent> latest;
				for (int j = i + 1; j < n; j++)
				{
					const auto& later = civ->Events[j];
					if (later == nullptr || !Follows(*later, *e, subjects)) continue;
					followed++;
					latest = later;
				}
				cs::String head = cs::Cat(SocietyDate(e->Jd), "  \xC2\xB7  ", Core::ToString(e->Kind), e->Where != nullptr ? cs::Cat("  \xC2\xB7  ", e->Where->Name) : cs::String(),
					followed == 0 ? cs::String("  \xC2\xB7  nothing has come of it yet")
						: cs::Cat("  \xC2\xB7  then ", followed, followed == 1 ? " event" : " events", ", latest ", Core::ToString(latest->Kind)));
				if (!search.empty() && !Matches(head, WordsOf(*e))) continue;
				rows.push_back(Row{ head, cs::String(), CivPick(), e });
			}
			return;
		}
		if (page == Month)
		{
			// US-T337: the last 30 days by kind, the most frequent first; each row opens the kind's latest event.
			struct Tally
			{
				int Count = 0, Steered = 0;
				cs::Ref<SocialEvent> Latest;
				std::vector<cs::String> Worlds;
			};
			std::vector<Tally> byKind(static_cast<size_t>(SocialKind::Count_) + 1);
			for (int i = civ->Events.Count() - 1; i >= 0; i--)
			{
				const auto& e = civ->Events[i];
				if (e == nullptr) continue;
				if (e->Jd < jd - MonthDays) continue;
				size_t k = static_cast<size_t>(e->Kind);
				if (k >= byKind.size()) k = byKind.size() - 1;
				auto& t = byKind[k];
				if (t.Count++ == 0) t.Latest = e;
				if (e->Steered) t.Steered++;
				if (e->Where != nullptr && std::find(t.Worlds.begin(), t.Worlds.end(), e->Where->Name) == t.Worlds.end()) t.Worlds.push_back(e->Where->Name);
			}
			std::vector<size_t> order;
			for (size_t k = 0; k < byKind.size(); k++)
				if (byKind[k].Count > 0) order.push_back(k);
			std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return byKind[a].Count > byKind[b].Count; });
			for (size_t k : order)
			{
				const auto& t = byKind[k];
				cs::String worlds;
				for (size_t w = 0; w < t.Worlds.size() && w < 3; w++) worlds = cs::Cat(worlds, w == 0 ? "" : ", ", t.Worlds[w]);
				if (t.Worlds.size() > 3) worlds = cs::Cat(worlds, " and ", static_cast<int>(t.Worlds.size() - 3), " more");
				cs::String top = cs::Cat(Core::ToString(t.Latest->Kind), "  \xC3\x97", t.Count,
					t.Steered > 0 ? cs::Cat("  \xC2\xB7  ", t.Steered, " steered") : cs::String(), worlds.empty() ? cs::String() : cs::Cat("  \xC2\xB7  ", worlds));
				cs::String latest = cs::Cat("latest ", SocietyDate(t.Latest->Jd), ": ", WordsOf(*t.Latest));
				if (!search.empty() && !Matches(top, latest)) continue;
				// An event row draws Line above Title: the tally on top, the latest event's words below.
				rows.push_back(Row{ latest, top, CivPick(), t.Latest });
			}
			return;
		}
		int view = page - FirstView;
		const auto& views = CivHands::Views();
		if (view >= 0 && view < static_cast<int>(views.size()))
		{
			cs::List<CivRow> lines;
			CivHands::View(views[view], galaxy, *civ, jd, lines);
			for (const auto& l : lines)
				if (Matches(l.Text)) rows.push_back(Row{ l.Text, cs::String(), l.Pick, nullptr });
		}
	}

	void SocietyScreen::Select(const Row& r, bool remember)
	{
		Visit to;
		if (r.Event != nullptr) to.Event = r.Event->Id;
		else if (!r.Pick.IsNone()) to.Pick = r.Pick;
		else return;
		if (remember) Go(to);
		else
		{
			selected = to.Pick;
			selectedEvent = to.Event;
			detailScroll = Vector2::zero();
			result.clear();
		}
	}

	void SocietyScreen::Go(const Visit& to)
	{
		if (to.Pick == selected && to.Event == selectedEvent) return;
		if (!selected.IsNone() || selectedEvent != 0)
		{
			back.push_back(Here());
			if (static_cast<int>(back.size()) > HistoryMax) back.erase(back.begin());
		}
		forward.clear();
		selected = to.Pick;
		selectedEvent = to.Event;
		detailScroll = Vector2::zero();
		result.clear();
	}

	void SocietyScreen::Show(const CivPick& p)
	{
		switch (p.Kind)
		{
		case CivPickKind::House: page = Houses; break;
		case CivPickKind::Person: page = People; break;
		case CivPickKind::Org: page = Orgs; break;
		case CivPickKind::World: page = Worlds; break;
		default: break;
		}
		Go(Visit{ p, 0 });
	}

	void SocietyScreen::StepBack()
	{
		if (back.empty()) return;
		forward.push_back(Here());
		Visit to = back.back();
		back.pop_back();
		selected = to.Pick;
		selectedEvent = to.Event;
		detailScroll = Vector2::zero();
		result.clear();
	}

	void SocietyScreen::StepForward()
	{
		if (forward.empty()) return;
		back.push_back(Here());
		Visit to = forward.back();
		forward.pop_back();
		selected = to.Pick;
		selectedEvent = to.Event;
		detailScroll = Vector2::zero();
		result.clear();
	}

	void SocietyScreen::HandleKeys()
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::F && (e.control || e.command)) { focusSearch = true; e.Use(); return; }
		if (e.keyCode == KeyCode::Escape && typing) { closeAsked = true; e.Use(); return; }
		if ((e.keyCode == KeyCode::Return || e.keyCode == KeyCode::KeypadEnter) && typing) { GUIUtility::keyboardControl = 0; typing = false; e.Use(); return; }
		if (e.keyCode == KeyCode::LeftArrow && e.alt) { StepBack(); e.Use(); return; }
		if (e.keyCode == KeyCode::RightArrow && e.alt) { StepForward(); e.Use(); return; }
		// US-T344: C marks the thing picked (again: clears it), as the ship catalog's compare does; not while typing in the search.
		if (e.keyCode == KeyCode::C && !typing && !e.shift && !e.control && !e.alt && !e.command && selectedEvent == 0 && !selected.IsNone()) { ToggleMark(); e.Use(); return; }
		// US-T354: S steps the list's sort (Houses, People, Organisations, Worlds); not while typing in the search.
		if (e.keyCode == KeyCode::S && !typing && !e.shift && !e.control && !e.alt && !e.command && page < FirstView && SortCount(page) > 1) { StepSort(); e.Use(); return; }
		if (rows.empty()) return;
		int at = -1;
		for (int i = 0; i < static_cast<int>(rows.size()); i++)
			if ((rows[i].Event != nullptr && rows[i].Event->Id == selectedEvent) || (rows[i].Event == nullptr && !rows[i].Pick.IsNone() && rows[i].Pick == selected)) { at = i; break; }
		int n = static_cast<int>(rows.size());
		if (e.keyCode == KeyCode::DownArrow) { Select(rows[at < 0 ? 0 : Mathf::Min(at + 1, n - 1)], false); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow) { Select(rows[at < 0 ? 0 : Mathf::Max(at - 1, 0)], false); e.Use(); }
	}

	void SocietyScreen::DrawSearch(Rect r)
	{
		GUI::SetNextControlName(SearchControl);
		cs::String typed = GUI::TextField(r, search, 60, field);
		if (typed != search) search = typed;
		if (focusSearch)
		{
			GUI::FocusControl(SearchControl);
			focusSearch = GUI::GetNameOfFocusedControl() != SearchControl;
		}
		typing = GUI::GetNameOfFocusedControl() == SearchControl;
		if (search.empty())
			GUI::Label(Rect(r.x + 10, r.y + 6, r.width - 20, 18), typing ? "type a name, a world, a kind: vance, mars, riot..." : "Ctrl+F to search this list", small);
		else if (GUI::Button(Rect(r.x + r.width - 28, r.y + 3, 24, 22), "X", button))
		{
			search.clear();
			focusSearch = true;
		}
	}

	// ---------------------------------------------------------------- what happened about a thing (US-T299)

	bool SocietyScreen::About(const SocialEvent& e, const CivPick& p)
	{
		auto person = [&](const cs::Ref<Citizen>& c) { return c != nullptr && c->Id == p.Id; };
		auto house = [&](const cs::Ref<Household>& h) { return h != nullptr && h->Id == p.Id; };
		auto org = [&](const cs::Ref<Org>& o) { return o != nullptr && o->Id == p.Id; };
		auto state = [&](const cs::Ref<Faction>& f) { return f != nullptr && f->Name == p.Name; };
		switch (p.Kind)
		{
		case CivPickKind::Person: return person(e.Who) || person(e.Whom);
		// A house's doings are its own and its people's.
		case CivPickKind::House: return house(e.House) || house(e.OtherHouse) || (e.Who != nullptr && house(e.Who->House)) || (e.Whom != nullptr && house(e.Whom->House));
		case CivPickKind::Org: return org(e.Org) || org(e.OtherOrg) || org(e.RunBy);
		case CivPickKind::Business: return e.Business != nullptr && e.Business->Id == p.Id;
		case CivPickKind::World: return e.Where != nullptr && e.Where->Name == p.Name;
		case CivPickKind::Faction: return state(e.Faction) || state(e.Rival);
		default: return false;
		}
	}

	void SocietyScreen::BuildRecent(const CivPick& p, Civilization& civ)
	{
		int n = civ.Events.Count();
		int lastId = n > 0 && civ.Events[n - 1] != nullptr ? civ.Events[n - 1]->Id : 0;
		cs::String key = cs::Cat(static_cast<int>(p.Kind), "|", p.Id, "|", p.Name, "|", n, "|", lastId);
		if (key == recentKey) return;
		recentKey = key;
		recent.clear();
		recentTotal = 0;
		for (int i = n - 1; i >= 0; i--)
		{
			const auto& e = civ.Events[i];
			if (e == nullptr || !About(*e, p)) continue;
			if (recentTotal++ < RecentMax) recent.push_back(Recent{ e->Id, cs::Cat(SocietyDate(e->Jd), "   ", Narrator::Render(*e)) });
		}
	}

	// ---------------------------------------------------------------- a person's life (US-T327)

	void SocietyScreen::BuildLife(int personId, Galaxy& galaxy, Civilization& civ)
	{
		auto society = Society::Find(galaxy);
		auto c = society != nullptr ? society->CitizenById(personId) : nullptr;
		int n = civ.Events.Count();
		int lastId = n > 0 && civ.Events[n - 1] != nullptr ? civ.Events[n - 1]->Id : 0;
		cs::String key = cs::Cat(personId, "|", n, "|", lastId, "|", c != nullptr && !c->Alive() ? 1 : 0);
		if (key == lifeKey) return;
		lifeKey = key;
		life.clear();
		lifeMore = 0;
		if (c == nullptr) return;

		cs::String where = c->House != nullptr && c->House->Home != nullptr ? cs::Cat(" on ", c->House->Home->Name) : cs::String();
		life.push_back(Recent{ 0, cs::Cat(SocietyDate(c->BornJd), "   Born", c->House != nullptr ? cs::Cat(" into House ", c->House->Surname) : cs::String(), where) });
		// The events about them, oldest first; room is kept for the death line.
		const int room = LifeMax - (c->Alive() ? 1 : 2);
		CivPick p = CivPick::Person(personId);
		for (int i = 0; i < n; i++)
		{
			const auto& e = civ.Events[i];
			if (e == nullptr || !About(*e, p)) continue;
			if (static_cast<int>(life.size()) <= room) life.push_back(Recent{ e->Id, cs::Cat(SocietyDate(e->Jd), "   ", Narrator::Render(*e)) });
			else lifeMore++;
		}
		if (!c->Alive())
			life.push_back(Recent{ 0, cs::Cat(SocietyDate(c->DiedJd), "   Died, aged ", static_cast<int>((c->DiedJd - c->BornJd) / 365.25)) });
	}

	// ---------------------------------------------------------------- a thing's years (US-T348)

	void SocietyScreen::BuildTimeline(const CivPick& p, Civilization& civ, double jd)
	{
		cs::String pickKey = cs::Cat(static_cast<int>(p.Kind), "|", p.Id, "|", p.Name);
		if (pickKey != timelinePick)
		{
			timelinePick = pickKey;
			timelineOpen = 0; // another thing picked: its strip opens closed
		}
		int n = civ.Events.Count();
		int lastId = n > 0 && civ.Events[n - 1] != nullptr ? civ.Events[n - 1]->Id : 0;
		int nowYear = jd > 0 ? Astro::ToDateTime(jd).Year() : 0;
		cs::String key = cs::Cat(pickKey, "|", n, "|", lastId, "|", timelineOpen, "|", nowYear);
		if (key == timelineKey) return;
		timelineKey = key;
		timeline.clear();
		timelineRows.clear();
		timelineEarlier = 0;
		timelineTotal = 0;

		// Every event about it, counted by the year it happened (an event with no date has no year on the strip).
		std::vector<TimelineYear> byYear; // sorted by year
		auto yearOf = [](double whenJd) { return Astro::ToDateTime(whenJd).Year(); };
		for (int i = 0; i < n; i++)
		{
			const auto& e = civ.Events[i];
			if (e == nullptr || e->Jd <= 0 || !About(*e, p)) continue;
			int y = yearOf(e->Jd);
			auto at = std::lower_bound(byYear.begin(), byYear.end(), y, [](const TimelineYear& t, int v) { return t.Year < v; });
			if (at == byYear.end() || at->Year != y)
			{
				TimelineYear fresh;
				fresh.Year = y;
				at = byYear.insert(at, fresh);
			}
			at->Count++;
			if (e->Steered) at->Steered++;
			timelineTotal++;
			if (timelineOpen == y && static_cast<int>(timelineRows.size()) < TimelineListMax)
				timelineRows.push_back(Recent{ e->Id, cs::Cat(SocietyDate(e->Jd), "   ", Narrator::Render(*e),
					e->Steered ? cs::String("  \xC2\xB7  STEERED") : cs::String()) });
		}
		if (byYear.empty()) return;

		// Every year from the first kept to now (or the latest event), the quiet ones too, the latest TimelineYearsMax only.
		int lastYear = Mathf::Max(byYear.back().Year, nowYear);
		int firstYear = Mathf::Max(byYear.front().Year, lastYear - TimelineYearsMax + 1);
		size_t k = 0;
		for (; k < byYear.size() && byYear[k].Year < firstYear; k++) timelineEarlier += byYear[k].Count;
		for (int y = firstYear; y <= lastYear; y++)
		{
			if (k < byYear.size() && byYear[k].Year == y) timeline.push_back(byYear[k++]);
			else
			{
				TimelineYear quiet;
				quiet.Year = y;
				timeline.push_back(quiet);
			}
		}
	}

	float SocietyScreen::DrawTimeline(float y, float w, bool draw, int& followEvent)
	{
		static constexpr float StripH = 44;
		static constexpr float YearsH = 18;
		static constexpr float NoteH = 20;
		int openCount = 0;
		for (const auto& t : timeline)
			if (t.Year == timelineOpen) openCount = t.Count;
		bool opened = timelineOpen != 0 && openCount > 0;
		float h = HeadingH + (timeline.empty() ? 24.0f : StripH + YearsH + NoteH
			+ (opened ? 22.0f + static_cast<float>(timelineRows.size()) * 28.0f + (openCount > static_cast<int>(timelineRows.size()) ? 20.0f : 0.0f) : 0.0f))
			+ SectionGap;
		if (!draw) return h;

		GUI::Label(Rect(0, y, w, HeadingH - 4), timeline.empty() ? cs::String("TIMELINE")
			: cs::Cat("TIMELINE  (", timelineTotal, timelineTotal == 1 ? " event, " : " events, ", timeline.front().Year, "-", timeline.back().Year, ")"), heading);
		y += HeadingH;
		if (timeline.empty())
		{
			GUI::Label(Rect(0, y, w, 20), "Nothing kept about it yet: its events show here, a bar a year.", small);
			return h;
		}

		int most = 1;
		for (const auto& t : timeline) most = Mathf::Max(most, t.Count);
		float slot = w / static_cast<float>(timeline.size());
		float barW = Mathf::Max(2.0f, slot - 2);
		Color keep = GUI::color;
		int clicked = 0;
		for (size_t i = 0; i < timeline.size(); i++)
		{
			const auto& t = timeline[i];
			auto cell = Rect(static_cast<float>(i) * slot, y, slot, StripH);
			// The year is a button when it has events; the open one is lit.
			if (t.Count > 0 && GUI::Button(cell, GUIContent(cs::String(), cs::Cat(t.Year, ": ", t.Count, t.Count == 1 ? " event" : " events",
				t.Steered > 0 ? cs::Cat(", ", t.Steered, " steered") : cs::String())), t.Year == timelineOpen ? entrySelected : entry)) clicked = t.Year;
			float x = cell.x + (slot - barW) * 0.5f;
			float baseY = y + StripH - 2;
			float full = StripH - 6;
			float bh = t.Count > 0 ? Mathf::Max(2.0f, full * static_cast<float>(t.Count) / static_cast<float>(most)) : 1.0f;
			GUI::color = t.Count > 0 ? Color(UiStyle::Accent.r, UiStyle::Accent.g, UiStyle::Accent.b, 0.55f) : Color(UiStyle::Edge.r, UiStyle::Edge.g, UiStyle::Edge.b, 0.4f);
			GUI::DrawTexture(Rect(x, baseY - bh, barW, bh), Texture2D::whiteTexture());
			if (t.Steered > 0)
			{
				// The hand's share, bright, at the foot of the bar.
				float sh = Mathf::Max(2.0f, bh * static_cast<float>(t.Steered) / static_cast<float>(t.Count));
				GUI::color = UiStyle::Good;
				GUI::DrawTexture(Rect(x, baseY - sh, barW, sh), Texture2D::whiteTexture());
			}
			GUI::color = keep;
		}
		y += StripH;
		GUI::Label(Rect(0, y, 80, YearsH), cs::Cat(timeline.front().Year), small);
		if (timeline.size() > 1)
		{
			cs::String lastLabel = cs::Cat(timeline.back().Year);
			float lw = small->CalcSize(GUIContent(lastLabel)).x;
			GUI::Label(Rect(Mathf::Max(0.0f, w - lw - 2), y, lw + 2, YearsH), lastLabel, small);
		}
		y += YearsH;
		GUI::Label(Rect(0, y, w, NoteH), cs::Cat("A bar a year, the tallest ", most, most == 1 ? " event" : " events", "; bright: brought about by your hand. Click a year to list it",
			timelineEarlier > 0 ? cs::Cat(" (", timelineEarlier, " earlier, not drawn: the feed's search finds them)") : cs::String(), "."), small); // US-T383
		y += NoteH;
		if (opened)
		{
			int steered = 0;
			for (const auto& t : timeline)
				if (t.Year == timelineOpen) steered = t.Steered;
			GUI::Label(Rect(0, y, w, 20), cs::Cat(timelineOpen, ": ", openCount, openCount == 1 ? " event" : " events",
				steered > 0 ? cs::Cat(", ", steered, " by your hand") : cs::String(), "  (click the year again to close)"), rowTitle);
			y += 22;
			for (const auto& r : timelineRows)
			{
				auto row = Rect(0, y, w, 24);
				if (GUI::Button(row, GUIContent::none(), entry)) followEvent = r.Event;
				GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Line, rowLine);
				y += 28;
			}
			if (openCount > static_cast<int>(timelineRows.size()))
				GUI::Label(Rect(8, y, w - 8, 18), cs::Cat("and ", openCount - static_cast<int>(timelineRows.size()), " more that year"), small);
		}
		if (clicked != 0) timelineOpen = clicked == timelineOpen ? 0 : clicked;
		return h;
	}

	// ---------------------------------------------------------------- a house's history, told (US-T375)

	void SocietyScreen::BuildChronicle(int houseId, Galaxy& galaxy, Civilization& civ)
	{
		int n = civ.Events.Count();
		int lastId = n > 0 && civ.Events[n - 1] != nullptr ? civ.Events[n - 1]->Id : 0;
		auto society = Society::Find(galaxy);
		auto house = society != nullptr ? society->HouseById(houseId) : nullptr;
		auto head = house != nullptr ? house->Head() : nullptr;
		int living = house != nullptr ? house->Living() : 0;
		cs::String key = cs::Cat(houseId, "|", n, "|", lastId, "|", living, "|", head != nullptr ? head->Id : 0);
		if (key == chronicleKey) return;
		chronicleKey = key;
		chronicle.clear();
		chronicleTotal = 0;
		chronicleMore = 0;
		if (house == nullptr) return;

		auto self = CivPick::House(houseId);
		// The paragraph of a decade: the last one when it is that decade's, else a new one after it.
		auto paragraph = [&](int decade) -> ChronicleDecade&
		{
			if (chronicle.empty() || chronicle.back().Decade != decade)
			{
				ChronicleDecade fresh;
				fresh.Decade = decade;
				chronicle.push_back(fresh);
			}
			return chronicle.back();
		};
		auto append = [](ChronicleDecade& d, const cs::String& s) { d.Prose = d.Prose.empty() ? s : cs::Cat(d.Prose, " ", s); };
		auto name = [&](ChronicleDecade& d, const CivPick& p)
		{
			if (p.IsNone() || p == self || p.Kind == CivPickKind::World || static_cast<int>(d.Names.size()) >= ChronicleNamesMax) return;
			if (std::find(d.Names.begin(), d.Names.end(), p) == d.Names.end()) d.Names.push_back(p);
		};

		if (house->FoundedJd > 0)
		{
			int founded = Astro::ToDateTime(house->FoundedJd).Year();
			append(paragraph(founded / 10 * 10), cs::Cat("House ", house->Surname, " was founded",
				house->Home != nullptr ? cs::Cat(" on ", house->Home->Name) : cs::String(), " in ", founded, "."));
		}
		// Its events in the order they happened, each in the narrator's words, the year named when it turns.
		int told = 0, lastYear = 0;
		for (int i = 0; i < n; i++)
		{
			const auto& e = civ.Events[i];
			if (e == nullptr || e->Jd <= 0 || !About(*e, self)) continue;
			chronicleTotal++;
			if (told >= ChronicleMax)
			{
				chronicleMore++;
				continue;
			}
			told++;
			int year = Astro::ToDateTime(e->Jd).Year();
			auto& d = paragraph(year / 10 * 10);
			cs::String sentence = Narrator::Render(*e);
			append(d, year != lastYear ? cs::Cat("In ", year, ": ", sentence) : sentence);
			lastYear = year;
			for (const auto& p : SubjectsOf(*e)) name(d, p);
		}

		// How it stands now.
		ChronicleDecade today;
		if (house->Scattered)
			today.Prose = cs::Cat("Its world fell and House ", house->Surname, " was scattered; ", living, living == 1 ? " of its blood lives on." : " of its blood live on.");
		else if (living == 0)
			today.Prose = cs::Cat("None of House ", house->Surname, " lives now.");
		else
			today.Prose = cs::Cat("Today House ", house->Surname, " numbers ", living, " living", head != nullptr ? cs::Cat(", headed by ", head->FullName()) : cs::String(), ".");
		if (head != nullptr) today.Names.push_back(CivPick::Person(head->Id));
		chronicle.push_back(today);
	}

	float SocietyScreen::DrawChronicle(float y, float w, Galaxy& galaxy, Civilization& civ, bool draw, CivPick& follow)
	{
		static constexpr float DecadeH = 22, LinkRowH = 28, MoreH = 20, EmptyH = 24;
		float top = y;
		bool open = chronicleOpen;
		if (draw)
		{
			GUI::Label(Rect(0, y, Mathf::Max(0.0f, w - 160), HeadingH - 4), open && chronicleTotal > 0
				? cs::Cat("CHRONICLE  (", chronicleTotal, chronicleTotal == 1 ? " event)" : " events)") : cs::String("CHRONICLE"), heading);
			if (GUI::Button(Rect(Mathf::Max(0.0f, w - 150), y, 146, 22), open ? "Hide chronicle" : "Chronicle", button)) chronicleOpen = !chronicleOpen;
		}
		y += HeadingH;
		if (!open) return y - top + SectionGap;
		if (chronicle.empty())
		{
			if (draw) GUI::Label(Rect(0, y, w, EmptyH - 4), "Nothing kept about it yet: its history is told here as it happens.", small);
			return y + EmptyH - top + SectionGap;
		}
		for (const auto& d : chronicle)
		{
			if (draw) GUI::Label(Rect(0, y, w, DecadeH - 2), d.Decade > 0 ? cs::Cat("The ", d.Decade, "s") : cs::String("Now"), rowTitle);
			y += DecadeH;
			float ph = text->CalcHeight(GUIContent(d.Prose), w);
			if (draw) GUI::Label(Rect(0, y, w, ph), d.Prose, text);
			y += ph + 4;
			if (!d.Names.empty())
			{
				float x = 0;
				for (const auto& p : d.Names)
				{
					cs::String label = NameOf(p, galaxy, civ);
					float bw = Mathf::Min(w, link->CalcSize(GUIContent(label)).x + 16);
					if (x > 0 && x + bw > w)
					{
						x = 0;
						y += LinkRowH;
					}
					if (draw && GUI::Button(Rect(x, y, bw, 24), label, link)) follow = p;
					x += bw + 6;
				}
				y += LinkRowH;
			}
			y += 6;
		}
		if (chronicleMore > 0)
		{
			if (draw) GUI::Label(Rect(8, y, w - 8, MoreH - 2), cs::Cat("and ", chronicleMore, chronicleMore == 1 ? " later event" : " later events",
				" untold here (the TIMELINE lists every year)"), small);
			y += MoreH;
		}
		return y - top + SectionGap;
	}

	// ---------------------------------------------------------------- what came of the SpaceGod's hand (US-T325)

	std::vector<CivPick> SocietyScreen::SubjectsOf(const SocialEvent& e)
	{
		std::vector<CivPick> subjects;
		auto add = [&](const CivPick& p) { if (std::find(subjects.begin(), subjects.end(), p) == subjects.end()) subjects.push_back(p); };
		if (e.Who != nullptr) add(CivPick::Person(e.Who->Id));
		if (e.Whom != nullptr) add(CivPick::Person(e.Whom->Id));
		if (e.House != nullptr) add(CivPick::House(e.House->Id));
		if (e.OtherHouse != nullptr) add(CivPick::House(e.OtherHouse->Id));
		if (e.Org != nullptr) add(CivPick::Org(e.Org->Id));
		if (e.OtherOrg != nullptr) add(CivPick::Org(e.OtherOrg->Id));
		if (e.Business != nullptr) add(CivPick::Business(e.Business->Id));
		// A world only when nothing narrower is named: everything on it would otherwise count as following.
		if (subjects.empty() && e.Where != nullptr) add(CivPick::World(e.Where->Name));
		return subjects;
	}

	bool SocietyScreen::Follows(const SocialEvent& later, const SocialEvent& steered, const std::vector<CivPick>& subjects)
	{
		if (later.Id == steered.Id || later.Jd < steered.Jd || later.Jd > steered.Jd + AftermathDays) return false;
		return std::any_of(subjects.begin(), subjects.end(), [&](const CivPick& p) { return About(later, p); });
	}

	void SocietyScreen::BuildAftermath(const SocialEvent& e, Civilization& civ)
	{
		int n = civ.Events.Count();
		int lastId = n > 0 && civ.Events[n - 1] != nullptr ? civ.Events[n - 1]->Id : 0;
		cs::String key = cs::Cat(e.Id, "|", n, "|", lastId);
		if (key == aftermathKey) return;
		aftermathKey = key;
		aftermath.clear();
		aftermathTotal = 0;
		auto subjects = SubjectsOf(e);
		for (int i = 0; i < n; i++)
		{
			const auto& later = civ.Events[i];
			if (later == nullptr || !Follows(*later, e, subjects)) continue;
			if (aftermathTotal++ < AftermathMax) aftermath.push_back(Recent{ later->Id, cs::Cat(SocietyDate(later->Jd), "   ", Narrator::Render(*later)) });
		}
	}

	void SocietyScreen::Ask(const cs::String& line)
	{
		bool sent = false;
		cs::String done;
		cs::String why = app->CivHandAsk(line, sent, done);
		resultBad = !why.empty();
		result = !why.empty() ? why : sent ? cs::Cat("Sent to the party (", line, "): every peer lays the same hand on the same day") : done;
		rowsAt = -1; // the feed shows it at once
	}

	// ---------------------------------------------------------------- drawing

	bool SocietyScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		UiMotion::Scope motion("society", 0); // US-T255: the whole screen fades in
		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "SOCIETY", title);
		float by = screen.height - 50;
		if (app->Galaxy == nullptr || app->Clock == nullptr) return GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button);
		auto& galaxy = *app->Galaxy;
		double jd = app->Clock->Jd;
		auto civ = Civilization::Find(galaxy);

		cs::String tallies = civ == nullptr ? cs::String("The galaxy's people have not woken yet")
			: cs::Cat(civ->Worlds.Count(), " worlds watched  \xC2\xB7  ", civ->OrgsOf(OrgKind::Party).Count() + civ->OrgsOf(OrgKind::Faith).Count(), " parties and faiths  \xC2\xB7  ",
				civ->Events.Count(), " events kept  \xC2\xB7  Prose narration ", Narrator::HasSource() ? "on" : "off");
		GUI::Label(Rect(Margin, 56, screen.width - 2 * Margin, 18),
			cs::Cat(tallies, "  \xC2\xB7  Up / Down to pick  \xC2\xB7  Ctrl+F to search  \xC2\xB7  Alt+Left / Right for back and forward  \xC2\xB7  ", closeHint), small);

		Rebuild(galaxy, civ.get(), jd);
		HandleKeys();
		// US-T351: what is picked is being looked at, so its news so far is seen; and the Watched page's button counts what is
		// new across the watch list (twice a second: the feed only grows at its end, so each look stops at the first seen).
		if (!selected.IsNone()) app->MarkWatchSeen(selected);
		double nowS = FPlatformTime::Seconds();
		if (civ != nullptr && nowS - watchedNewAt >= 0.5)
		{
			watchedNewAt = nowS;
			watchedNew = 0;
			for (const auto& p : app->SocietyWatched)
			{
				int seenId = app->WatchSeenId(p);
				for (int i = civ->Events.Count() - 1; i >= 0; i--)
				{
					const auto& e = civ->Events[i];
					if (e == nullptr) continue;
					if (e->Id <= seenId) break;
					if (About(*e, p)) watchedNew++;
				}
			}
		}

		float top = 88, bottom = screen.height - 64;
		float pagesW = Mathf::Clamp(screen.width * 0.13f, 160.0f, 210.0f);
		DrawPages(Rect(Margin, top, pagesW, bottom - top));
		float listX = Margin + pagesW + Gutter;
		float listW = Mathf::Clamp(screen.width * 0.36f, 300.0f, 640.0f);
		if (civ == nullptr)
		{
			GUI::Label(Rect(listX, top, screen.width - Margin - listX, 80),
				"The houses, their people and the organisations they found come to life once the game has run a day: let time pass and look again.", text);
			return GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button);
		}
		float listTop = top;
		DrawSearch(Rect(listX, listTop, listW, 28));
		listTop += 34;
		if (page == Feed || page == Orgs)
		{
			DrawFilters(Rect(listX, listTop, listW, 26), *civ);
			listTop += 32;
		}
		DrawList(Rect(listX, listTop, listW, bottom - listTop), civ.get());
		float x = listX + listW + Gutter;
		DrawDetail(Rect(x, top, screen.width - Margin - x, bottom - top), galaxy, *civ, jd);

		if (!result.empty())
			GUI::Label(Rect(x, by + 6, screen.width - Margin - 132 - x, 22), result, resultBad ? bad : good);
		bool close = GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button) || closeAsked;
		closeAsked = false;
		if (close)
		{
			// The game's keys come back with the screen gone.
			if (typing) GUIUtility::keyboardControl = 0;
			typing = false;
		}
		return close;
	}

	void SocietyScreen::DrawPages(Rect area)
	{
		const auto& views = CivHands::Views();
		int count = FirstView + static_cast<int>(views.size());
		float contentH = HeadingH + count * TabRowH + HeadingH;
		pageScroll = GUI::BeginScrollView(area, pageScroll, Rect(0, 0, area.width - 16, contentH));
		float w = area.width - 16, y = 0;
		GUI::Label(Rect(0, y, w, HeadingH - 4), "THE PEOPLE", heading);
		y += HeadingH;
		for (int p = 0; p < count; p++)
		{
			if (p == FirstView)
			{
				GUI::Label(Rect(0, y + 4, w, HeadingH - 4), "THE MODULES", heading);
				y += HeadingH;
			}
			cs::String name = p < FirstView ? cs::String(SocietyPageNames[p]) : cs::String(SocietyViewName(views[p - FirstView]));
			if (p == Watched && watchedNew > 0) name = cs::Cat(name, "  (", watchedNew, " new)"); // US-T351
			if (GUI::Button(Rect(0, y, w, TabRowH - 4), name, p == page ? entrySelected : entry) && p != page)
			{
				page = p;
				result.clear();
			}
			y += TabRowH;
		}
		GUI::EndScrollView();
	}

	void SocietyScreen::DrawFilters(Rect r, Civilization& civ)
	{
		float x = r.x;
		if (page == Orgs)
		{
			// The kinds there are (a kind a later module adds shows too).
			std::vector<int> kinds;
			for (const auto& o : civ.Orgs)
				if (o != nullptr && o->Active() && std::find(kinds.begin(), kinds.end(), static_cast<int>(o->Kind)) == kinds.end()) kinds.push_back(static_cast<int>(o->Kind));
			std::sort(kinds.begin(), kinds.end());
			auto at = std::find(kinds.begin(), kinds.end(), orgKind);
			int i = at == kinds.end() ? -1 : static_cast<int>(at - kinds.begin());
			int n = static_cast<int>(kinds.size());
			if (GUI::Button(Rect(x, r.y, 26, r.height), "<", button)) orgKind = i <= 0 ? (i == 0 ? -1 : (n > 0 ? kinds[n - 1] : -1)) : kinds[i - 1];
			GUI::Label(Rect(x + 30, r.y + 3, 200, r.height), cs::Cat("Kind: ", orgKind < 0 ? cs::String("all") : cs::String(Core::ToString(static_cast<OrgKind>(orgKind)))), text);
			if (GUI::Button(Rect(x + 234, r.y, 26, r.height), ">", button)) orgKind = i + 1 < n ? kinds[i + 1] : -1;
			return;
		}
		// The module group: All, each group, Other.
		if (GUI::Button(Rect(x, r.y, 22, r.height), "<", button)) { group = group <= -1 ? GroupCount() - 1 : group - 1; kind = 0; }
		GUI::Label(Rect(x + 26, r.y + 3, 100, r.height), group < 0 ? cs::String("All modules") : cs::String(GroupName(group)), text);
		if (GUI::Button(Rect(x + 128, r.y, 22, r.height), ">", button)) { group = group + 1 >= GroupCount() ? -1 : group + 1; kind = 0; }
		x += 158;
		// The kind: every SocialKind that has happened in the group picked, in the enum's order (ToString names it).
		std::vector<int> kinds;
		for (int k = 1; k < static_cast<int>(SocialKind::Count_); k++)
			if (civ.Count(static_cast<SocialKind>(k)) > 0 && (group < 0 || GroupOf(k) == group)) kinds.push_back(k);
		auto at = std::find(kinds.begin(), kinds.end(), kind);
		int i = at == kinds.end() ? -1 : static_cast<int>(at - kinds.begin());
		int n = static_cast<int>(kinds.size());
		if (GUI::Button(Rect(x, r.y, 22, r.height), "<", button)) kind = i < 0 ? (n > 0 ? kinds[n - 1] : 0) : i == 0 ? 0 : kinds[i - 1];
		cs::String kindName = kind == 0 ? cs::String("All kinds") : cs::Cat(Core::ToString(static_cast<SocialKind>(kind)), " (", civ.Count(static_cast<SocialKind>(kind)), ")");
		GUI::Label(Rect(x + 26, r.y + 3, 150, r.height), kindName, text);
		if (GUI::Button(Rect(x + 178, r.y, 22, r.height), ">", button)) kind = i + 1 < n ? kinds[i + 1] : 0;
		x += 208;
		float rest = r.xMax() - x;
		float half = Mathf::Max(60.0f, (rest - 6) / 2);
		// US-T304: All doings -> Steered only -> Watched only -> All doings.
		if (GUI::Button(Rect(x, r.y, half, r.height), SocietyDoingsNames[doings], doings != AllDoings ? entrySelected : entry)) doings = (doings + 1) % DoingsCount;
		bool canWorld = selected.Kind == CivPickKind::World;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && (canWorld || worldOnly);
		// US-T383: the filter holds the world it was turned on at, whatever is picked after.
		if (GUI::Button(Rect(x + half + 6, r.y, half, r.height), worldOnly ? cs::Cat("At ", worldFilter) : cs::String("Every world"), worldOnly ? entrySelected : entry))
		{
			worldOnly = !worldOnly;
			worldFilter = worldOnly ? selected.Name : cs::String();
		}
		GUI::enabled = enabled;
	}

	void SocietyScreen::DrawList(Rect area, Civilization* civ)
	{
		(void)civ;
		cs::String head = page < FirstView ? cs::Strings::ToUpperInvariant(cs::String(SocietyPageNames[page])) : cs::Strings::ToUpperInvariant(cs::String(SocietyViewName(CivHands::Views()[page - FirstView])));
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat(head, "  (", static_cast<int>(rows.size()), ")"), heading);
		// US-T354: the list's sort, at the heading's right end; a click (or S) steps it.
		if (page < FirstView && SortCount(page) > 1)
		{
			const float sortW = 170;
			if (GUI::Button(Rect(area.xMax() - sortW, area.y - 2, sortW, HeadingH - 4), cs::Cat("Sort: ", SortName(page, sortBy[page]), " (S)"), sortBy[page] != 0 ? entrySelected : entry))
				StepSort();
		}
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		int n = static_cast<int>(rows.size());
		if (n == 0)
		{
			GUI::Label(Rect(list.x, list.y + 4, list.width, 80),
				page == Feed ? (doings == WatchedDoings && app->SocietyWatched.empty() ? "Nothing is watched yet: pick a house, a person, a world... and Watch it." : "Nothing of that kind has happened yet.")
				: page == Watched ? "Nothing is watched yet. Pick a house, a person, an organisation, a business, a world or a state and Watch it: its news is told here and, while you play, as it happens."
				: page == YourHand ? "Your hand has not touched the galaxy's people yet. Pick a house, a person, an organisation or a world and use the SpaceGod's hand on it: what it brings about, and what comes of that, is kept here."
				: page == Month ? "Nothing has happened among the galaxy's people in the last 30 days."
				: "Nobody here yet: that part of the civilization wakes as the days pass.", text);
			return;
		}
		float w = list.width - 16;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, w, n * RowH));
		// Only the rows in view are drawn (and only their events worded): the feed keeps hundreds.
		int first = Mathf::Max(0, static_cast<int>(listScroll.y / RowH) - 1);
		int last = Mathf::Min(n - 1, static_cast<int>((listScroll.y + list.height) / RowH) + 1);
		for (int i = first; i <= last; i++)
		{
			auto& r = rows[i];
			bool on = r.Event != nullptr ? r.Event->Id == selectedEvent : (!r.Pick.IsNone() && r.Pick == selected);
			auto row = Rect(0, i * RowH, w, RowH - 4);
			if (GUI::Button(row, GUIContent::none(), on ? entrySelected : entry)) Select(r);
			if (r.Event != nullptr && r.Line.empty()) r.Line = Narrator::Render(*r.Event);
			if (r.Line.empty())
				GUI::Label(Rect(row.x + 8, row.y + 2, row.width - 14, row.height - 4), r.Title, small);
			else
			{
				GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Event != nullptr ? r.Line : r.Title, rowTitle);
				GUI::Label(Rect(row.x + 8, row.y + 22, row.width - 14, 18), r.Event != nullptr ? r.Title : r.Line, rowLine);
			}
			// US-T304: what is watched says so (the Watched page is nothing else).
			if (page != Watched && r.Event == nullptr && !r.Pick.IsNone() && app->IsWatched(r.Pick))
				GUI::Label(Rect(row.xMax() - 84, row.y + 3, 78, 18), "WATCHED", rowLine);
		}
		GUI::EndScrollView();
	}

	cs::String SocietyScreen::NameOf(const CivPick& p, Galaxy& galaxy, Civilization& civ) const
	{
		auto society = Society::Find(galaxy);
		switch (p.Kind)
		{
		case CivPickKind::House: { auto h = society != nullptr ? society->HouseById(p.Id) : nullptr; return h != nullptr ? cs::Cat("House ", h->Surname) : cs::Cat("house ", p.Id); }
		case CivPickKind::Person: { auto c = society != nullptr ? society->CitizenById(p.Id) : nullptr; return c != nullptr ? c->FullName() : cs::Cat("person ", p.Id); }
		case CivPickKind::Business: { auto b = society != nullptr ? society->BusinessById(p.Id) : nullptr; return b != nullptr ? b->Name : cs::Cat("business ", p.Id); }
		case CivPickKind::Org: { auto o = civ.OrgById(p.Id); return o != nullptr ? Narrator::OrgName(*o) : cs::Cat("organisation ", p.Id); }
		case CivPickKind::Trial: return cs::Cat("trial ", p.Id);
		case CivPickKind::World: case CivPickKind::Faction: return p.Name;
		default: return cs::String();
		}
	}

	void SocietyScreen::DescribeEvent(const SocialEvent& e, cs::String& name, cs::List<cs::String>& lines, std::vector<std::pair<cs::String, CivPick>>& links)
	{
		name = Core::ToString(e.Kind);
		lines.Add(Narrator::Render(e));
		lines.Add(cs::Cat(SocietyDate(e.Jd), e.Steered ? "  \xC2\xB7  brought about by the SpaceGod's hand" : "", e.Unverified ? "  \xC2\xB7  told, not true" : "",
			e.RunBy != nullptr ? cs::Cat("  \xC2\xB7  run ", e.Slant > 0 ? "for" : "against", " by ", Narrator::OrgName(*e.RunBy)) : cs::String()));
		if (Narrator::HasSource()) lines.Add(cs::Cat("The built-in words: ", Narrator::Render(e, false)));
		auto person = [&](const cs::Ref<Citizen>& c) { if (c != nullptr) links.emplace_back(c->FullName(), CivPick::Person(c->Id)); };
		auto house = [&](const cs::Ref<Household>& h) { if (h != nullptr) links.emplace_back(cs::Cat("House ", h->Surname), CivPick::House(h->Id)); };
		auto org = [&](const cs::Ref<Org>& o) { if (o != nullptr) links.emplace_back(Narrator::OrgName(*o), CivPick::Org(o->Id)); };
		person(e.Who);
		person(e.Whom);
		house(e.House);
		house(e.OtherHouse);
		if (e.Business != nullptr) links.emplace_back(e.Business->Name, CivPick::Business(e.Business->Id));
		org(e.Org);
		org(e.OtherOrg);
		if (e.Where != nullptr) links.emplace_back(e.Where->Name, CivPick::World(e.Where->Name));
		if (e.Faction != nullptr) links.emplace_back(e.Faction->Name, CivPick::Faction(e.Faction->Name));
		if (e.Rival != nullptr) links.emplace_back(e.Rival->Name, CivPick::Faction(e.Rival->Name));
	}

	namespace
	{
		// US-T384: a trip's purpose in plain words.
		cs::String TripWhy(TripPurpose why)
		{
			switch (why)
			{
			case TripPurpose::Business: return "On business";
			case TripPurpose::Conference: return "To a conference";
			case TripPurpose::FamilyVisit: return "Visiting family";
			case TripPurpose::Holiday: return "On holiday";
			case TripPurpose::Honeymoon: return "On honeymoon";
			case TripPurpose::Pilgrimage: return "On pilgrimage";
			case TripPurpose::Study: return "Away to study";
			case TripPurpose::Festival: return "To a festival";
			case TripPurpose::Match: return "To a match";
			}
			return "Travelling";
		}

		// US-T384: where a trip stands, by its dates.
		cs::String TripWhen(const Trip& t)
		{
			switch (t.Stage)
			{
			case TripStage::Planned: return cs::Cat("leaves ", SocietyDate(t.DepartJd));
			case TripStage::Outbound: return cs::Cat("arrives ", SocietyDate(t.ArriveJd));
			case TripStage::There: return cs::Cat("there until ", SocietyDate(t.LeaveJd));
			case TripStage::Homeward: return cs::Cat("on the way home, back ", SocietyDate(t.HomeJd));
			default: return cs::String();
			}
		}

		// US-T384: a conference in one line: its trade, where, when, and how it went or stands.
		cs::String ConferenceLine(const Conference& k, double jd)
		{
			cs::String when = k.Closed ? cs::Cat("held ", SocietyDate(k.OpensJd), " to ", SocietyDate(k.ClosesJd))
				: jd < k.OpensJd ? cs::Cat("opens ", SocietyDate(k.OpensJd), " (in ", static_cast<int>(k.OpensJd - jd), " days)")
				: cs::Cat("open now, until ", SocietyDate(k.ClosesJd));
			return cs::Cat(k.Name, " on ", Core::ToString(k.Trade), k.Host != nullptr ? cs::Cat(" at ", k.Host->Name) : cs::String(), ", ", when, "  \xC2\xB7  ",
				k.Delegates, k.Delegates == 1 ? " delegate" : " delegates", ", ", k.Attending.Count(), k.Attending.Count() == 1 ? " firm" : " firms",
				k.Deals > 0 ? cs::Cat(", ", k.Deals, k.Deals == 1 ? " deal" : " deals") : cs::String());
		}
	}

	void SocietyScreen::Describe(const CivPick& p, Galaxy& galaxy, Civilization& civ, double jd, cs::String& name, cs::List<cs::String>& lines,
		std::vector<std::pair<cs::String, CivPick>>& links, std::vector<Hand>& hands)
	{
		auto society = Society::Find(galaxy);
		name = NameOf(p, galaxy, civ);
		cs::String arg = p.Arg();
		// A hand that takes a second thing: offered with the one marked, else a hint to mark one.
		auto pair = [&](CivPickKind want, const char* verb, const char* label, const char* hint, bool markFirst = false)
		{
			if (marked.Kind == want && marked != p)
				hands.push_back(Hand{ cs::Cat(label, " ", NameOf(marked, galaxy, civ)), markFirst ? cs::Cat(verb, " ", marked.Arg(), " ", arg) : cs::Cat(verb, " ", arg, " ", marked.Arg()) });
			else hands.push_back(Hand{ cs::Cat(label, "...  (", hint, ")"), cs::String() });
		};
		auto one = [&](const char* verb, const char* label) { hands.push_back(Hand{ label, cs::Cat(verb, " ", arg) }); };

		switch (p.Kind)
		{
		case CivPickKind::House:
		{
			auto h = society != nullptr ? society->HouseById(p.Id) : nullptr;
			if (h == nullptr) break;
			lines.Add(cs::Cat("Of ", h->Home != nullptr ? h->Home->Name : cs::String("nowhere"), ", founded ", SocietyDate(h->FoundedJd), h->Scattered ? ", scattered" : ""));
			lines.Add(cs::Cat(h->Living(), " living  \xC2\xB7  wealth ", Narrator::Money(h->Wealth)));
			if (h->Business != nullptr)
			{
				lines.Add(Society::Describe(h->Business));
				links.emplace_back(h->Business->Name, CivPick::Business(h->Business->Id));
			}
			// US-T341: who heads it, the seats its people hold, what it owns, whom it married and every feud it is in.
			auto head = h->Head();
			if (head != nullptr)
			{
				lines.Add(cs::Cat("Head: ", head->FullName(), head->Alive() ? cs::Cat(", ", static_cast<int>(head->AgeYears(jd))) : cs::String(", dead")));
				links.emplace_back(cs::Cat("Head: ", head->FullName()), CivPick::Person(head->Id));
			}
			if (h->Business != nullptr)
			{
				int branches = h->Business->Branches.Count();
				if (branches > 0) lines.Add(cs::Cat("  branches on ", branches, branches == 1 ? " world" : " worlds"));
				for (const auto& bought : society->Businesses)
					if (bought != nullptr && bought->Buyer == h->Business) links.emplace_back(cs::Cat("Bought ", bought->Name), CivPick::Business(bought->Id));
			}
			{
				int seats = 0;
				for (const auto& m : h->Members)
					if (m != nullptr && m->Alive() && m->Role == CitizenRole::Governor)
					{
						seats++;
						lines.Add(cs::Cat("Seat: ", m->FullName(), " governs ", h->Home != nullptr ? h->Home->Name : cs::String("their world")));
					}
				for (const auto& o : civ.Orgs)
				{
					if (o == nullptr || !o->Active()) continue;
					bool led = o->Leader != nullptr && o->Leader->Alive() && o->Leader->House == h;
					if (led)
					{
						seats++;
						links.emplace_back(cs::Cat(o->Leader->Given, " leads ", Narrator::OrgName(*o)), CivPick::Org(o->Id));
					}
					else if (o->Owner == h) links.emplace_back(cs::Cat("Owns ", Narrator::OrgName(*o)), CivPick::Org(o->Id));
				}
				if (seats == 0) lines.Add("Seats: none of its people govern or lead anything");
			}
			{
				// The other houses its members' spouses came from (or went to), with how many matches each.
				std::vector<std::pair<cs::Ref<Household>, int>> wedded;
				for (const auto& m : h->Members)
				{
					if (m == nullptr || m->Spouse == nullptr) continue;
					auto other = m->Spouse->House;
					if (other == nullptr || other == h) continue;
					auto at = std::find_if(wedded.begin(), wedded.end(), [&](const std::pair<cs::Ref<Household>, int>& e) { return e.first == other; });
					if (at != wedded.end()) at->second++;
					else wedded.emplace_back(other, 1);
				}
				std::stable_sort(wedded.begin(), wedded.end(), [](const std::pair<cs::Ref<Household>, int>& a, const std::pair<cs::Ref<Household>, int>& b) { return a.second > b.second; });
				if (wedded.empty()) lines.Add("Married into: no other house yet");
				else lines.Add(cs::Cat("Married into ", static_cast<int>(wedded.size()), wedded.size() == 1 ? " house" : " houses"));
				for (const auto& w : wedded)
					links.emplace_back(cs::Cat("Wed into House ", w.first->Surname, w.second > 1 ? cs::Cat(" (", w.second, " matches)") : cs::String()), CivPick::House(w.first->Id));
			}
			if (civ.Kinship != nullptr)
			{
				std::vector<cs::Ref<Feud>> feuds;
				for (const auto& f : civ.Kinship->Feuds)
					if (f != nullptr && (f->A == h || f->B == h)) feuds.push_back(f);
				std::stable_sort(feuds.begin(), feuds.end(), [](const cs::Ref<Feud>& a, const cs::Ref<Feud>& b)
				{
					if (a->Active() != b->Active()) return a->Active();
					return a->Active() ? a->Heat > b->Heat : a->EndedJd > b->EndedJd;
				});
				for (const auto& f : feuds)
				{
					auto other = f->A == h ? f->B : f->A;
					if (other == nullptr) continue;
					if (f->Active())
						lines.Add(cs::Cat("At feud with House ", other->Surname, " over ", f->Cause, " since ", SocietyDate(f->StartedJd), ", heat ", cs::Fmt(f->Heat, "0.00"),
							f->Incidents > 0 ? cs::Cat(", ", f->Incidents, f->Incidents == 1 ? " incident" : " incidents") : cs::String()));
					else
						lines.Add(cs::Cat("Old feud with House ", other->Surname, " over ", f->Cause, ", ", SocietyDate(f->StartedJd), " to ", SocietyDate(f->EndedJd)));
					links.emplace_back(cs::Cat(f->Active() ? "Feud: House " : "Old feud: House ", other->Surname), CivPick::House(other->Id));
				}
			}
			if (h->Home != nullptr) links.emplace_back(h->Home->Name, CivPick::World(h->Home->Name));
			for (const auto& m : h->Members)
				if (m != nullptr && m->Alive() && m != head) links.emplace_back(m->FullName(), CivPick::Person(m->Id));
			one("donate", "Donate (a great gift)");
			pair(CivPickKind::House, "feud", "Swear a feud with", "mark another house");
			pair(CivPickKind::House, "reconcile", "Reconcile with", "mark another house");
			pair(CivPickKind::Business, "takeover", "Take over", "mark a business");
			break;
		}
		case CivPickKind::Person:
		{
			auto c = society != nullptr ? society->CitizenById(p.Id) : nullptr;
			if (c == nullptr) break;
			lines.Add(CharacterLine(*c, civ, jd)); // US-T365: who they are, in one sentence
			cs::String traits;
			for (int bit = 1; bit <= (1 << 11); bit <<= 1)
				if (c->Traits & bit) traits = cs::Cat(traits, traits.empty() ? "" : ", ", Core::ToString(static_cast<Trait>(bit)));
			lines.Add(cs::Cat(c->Alive() ? cs::Cat(static_cast<int>(c->AgeYears(jd)), " years old") : cs::Cat("died ", SocietyDate(c->DiedJd)), ", ", Core::ToString(c->Role)));
			lines.Add(cs::Cat("Traits: ", traits.empty() ? cs::String("none yet") : traits, "  \xC2\xB7  wants ", Core::ToString(c->Goal)));
			lines.Add(cs::Cat("Reputation ", SocietySigned(c->Reputation), "  \xC2\xB7  skill ", cs::Fmt(c->Skill, "0.00"), c->Faith != nullptr ? cs::Cat("  \xC2\xB7  keeps ", Narrator::OrgName(*c->Faith)) : cs::String()));
			// US-T384: where they are bound, why, when they get there and whom they go to see.
			if (c->Alive() && c->TripId != 0)
			{
				cs::Ref<Trip> trip;
				for (const auto& t : society->Trips)
					if (t != nullptr && t->Id == c->TripId && t->Active()) { trip = t; break; }
				if (trip != nullptr)
				{
					auto to = trip->To;
					lines.Add(cs::Cat(TripWhy(trip->Purpose), to != nullptr ? cs::Cat(" to ", to->Name) : cs::String(), ", ", TripWhen(*trip),
						trip->Meeting != nullptr ? cs::Cat(", meeting ", trip->Meeting->FullName()) : cs::String(),
						trip->Conference != nullptr ? cs::Cat(" at ", trip->Conference->Name) : cs::String(), trip->Steered ? " (your hand sent them)" : ""));
					if (to != nullptr) links.emplace_back(cs::Cat("Bound for ", to->Name), CivPick::World(to->Name));
					if (trip->Meeting != nullptr) links.emplace_back(cs::Cat("Meeting ", trip->Meeting->FullName()), CivPick::Person(trip->Meeting->Id));
					if (trip->Conference != nullptr && trip->Conference->Host != nullptr)
						links.emplace_back(cs::Cat(trip->Conference->Name, " (", trip->Conference->Host->Name, ")"), CivPick::World(trip->Conference->Host->Name));
				}
			}
			for (const auto& t : c->Ties)
				if (t != nullptr && t->Other != nullptr)
					lines.Add(cs::Cat("  ", Core::ToString(t->Kind), " of ", t->Other->FullName(), ", ", cs::Fmt(t->Strength, "0.00"), t->Other->Alive() ? "" : ", dead"));
			if (c->House != nullptr) links.emplace_back(cs::Cat("House ", c->House->Surname), CivPick::House(c->House->Id));
			if (c->Spouse != nullptr) links.emplace_back(c->Spouse->FullName(), CivPick::Person(c->Spouse->Id));
			for (const auto& t : c->Ties)
				if (t != nullptr && t->Other != nullptr) links.emplace_back(t->Other->FullName(), CivPick::Person(t->Other->Id));
			for (const auto& o : c->Orgs)
				if (o != nullptr && o->Active()) links.emplace_back(Narrator::OrgName(*o), CivPick::Org(o->Id));
			if (c->Faith != nullptr) links.emplace_back(Narrator::OrgName(*c->Faith), CivPick::Org(c->Faith->Id));
			one("fame", "Make them famous");
			one("coup", "Back their coup");
			one("hit", "Put a price on their head");
			one("preach", "Have them preach a new faith");
			one("rumour", "Spread a rumour about them");
			pair(CivPickKind::Person, "seduce", "Begin an affair with", "mark another person");
			break;
		}
		case CivPickKind::Org:
		{
			auto o = civ.OrgById(p.Id);
			if (o == nullptr) break;
			lines.Add(cs::Cat(Core::ToString(o->Kind), o->Active() ? cs::Cat(", founded ", SocietyDate(o->FoundedJd)) : cs::Cat(", ended ", SocietyDate(o->EndedJd))));
			lines.Add(cs::Cat("Strength ", SocietyPct(o->Strength), "  \xC2\xB7  funds ", Narrator::Money(o->Funds), "  \xC2\xB7  ", o->Members.Count(), " members  \xC2\xB7  stance ", SocietySigned(o->Stance),
				o->Score != 0 ? cs::Cat("  \xC2\xB7  score ", o->Score) : cs::String()));
			if (o->Seat != nullptr) links.emplace_back(o->Seat->Name, CivPick::World(o->Seat->Name));
			if (o->Leader != nullptr) links.emplace_back(cs::Cat("Led by ", o->Leader->FullName()), CivPick::Person(o->Leader->Id));
			if (o->Owner != nullptr) links.emplace_back(cs::Cat("House ", o->Owner->Surname), CivPick::House(o->Owner->Id));
			if (o->Faction != nullptr) links.emplace_back(o->Faction->Name, CivPick::Faction(o->Faction->Name));
			if (o->Rival != nullptr) links.emplace_back(cs::Cat("Rival: ", Narrator::OrgName(*o->Rival)), CivPick::Org(o->Rival->Id));
			if (o->Parent != nullptr) links.emplace_back(cs::Cat("Split from ", Narrator::OrgName(*o->Parent)), CivPick::Org(o->Parent->Id));
			// US-T323: who is in it (the leader, then the best known), what split from it and who is set against it.
			{
				std::vector<cs::Ref<Citizen>> members;
				int dead = 0;
				for (const auto& m : o->Members)
				{
					if (m == nullptr) continue;
					if (!m->Alive()) { dead++; continue; }
					if (m != o->Leader) members.push_back(m);
				}
				std::stable_sort(members.begin(), members.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return a->Reputation > b->Reputation; });
				constexpr int MaxShown = 12;
				int living = static_cast<int>(members.size()) + (o->Leader != nullptr && o->Leader->Alive() ? 1 : 0);
				lines.Add(cs::Cat("Members: ", living, " living", dead > 0 ? cs::Cat(", ", dead, " dead") : cs::String()));
				int shown = 0;
				for (const auto& m : members)
				{
					if (shown++ >= MaxShown) break;
					links.emplace_back(cs::Cat(m->FullName(), ", ", Core::ToString(m->Role), m->House != nullptr ? cs::Cat(" of House ", m->House->Surname) : cs::String()),
						CivPick::Person(m->Id));
				}
				if (static_cast<int>(members.size()) > MaxShown) lines.Add(cs::Cat("  and ", static_cast<int>(members.size()) - MaxShown, " more"));
				for (const auto& other : civ.Orgs)
				{
					if (other == nullptr || other == o) continue;
					if (other->Parent == o) links.emplace_back(cs::Cat("Offshoot: ", Narrator::OrgName(*other), other->Active() ? "" : " (ended)"), CivPick::Org(other->Id));
					else if (other->Rival == o && o->Rival != other && other->Active())
						links.emplace_back(cs::Cat("Set against it: ", Narrator::OrgName(*other)), CivPick::Org(other->Id));
				}
			}
			switch (o->Kind)
			{
			case OrgKind::Party: one("elect", "Swing the next election to them"); break;
			case OrgKind::Bank: one("bankrun", "Start a run on the bank"); break;
			case OrgKind::Team: one("rig", "Rig their next match"); break;
			case OrgKind::Mercenaries: pair(CivPickKind::Faction, "hire", "Hire them out to", "mark a state"); break;
			case OrgKind::Outlet:
				one("investigate", "Set the reporters digging");
				hands.push_back(Hand{ "Slant it for its flag", cs::Cat("slant ", arg, " for") });
				hands.push_back(Hand{ "Slant it against its flag", cs::Cat("slant ", arg, " against") });
				hands.push_back(Hand{ "Have it run straight", cs::Cat("slant ", arg, " straight") });
				break;
			default: break;
			}
			break;
		}
		case CivPickKind::World:
		{
			cs::Ref<Civics> c;
			for (const auto& w : civ.Worlds)
				if (w != nullptr && w->Where != nullptr && w->Where->Name == p.Name) { c = w; break; }
			if (c == nullptr) break;
			auto s = c->Where;
			lines.Add(cs::Cat("Approval of its flag ", SocietySigned(c->Approval), "  \xC2\xB7  unrest ", SocietyPct(s->Unrest)));
			lines.Add(cs::Cat("Politics: hawk ", SocietySigned(c->Hawk), ", liberty ", SocietySigned(c->Liberty), "  \xC2\xB7  trust in the press ", SocietyPct(c->PressTrust)));
			lines.Add(cs::Cat("Crime ", SocietyPct(c->Crime), "  \xC2\xB7  sick ", SocietyPct(c->Infected), ", immune ", SocietyPct(c->Immune), "  \xC2\xB7  war weariness ", SocietyPct(c->Weariness),
				c->WarDays > 0 ? cs::Cat(" (", static_cast<int>(c->WarDays), " days at war)") : cs::String()));
			lines.Add(cs::Cat("Hospitals ", SocietyPct(c->Care), ", schools ", SocietyPct(c->Learning), "  \xC2\xB7  drift ", SocietyPct(c->Drift)));
			lines.Add(cs::Cat("Fashion: ", Culture::TrendName(c->Trend), " (", SocietyPct(c->TrendHeat), " hot)",
				c->Faith != nullptr ? cs::Cat("  \xC2\xB7  faith: ", Narrator::OrgName(*c->Faith), " ", SocietyPct(c->FaithShare)) : cs::String()));
			if (c->Quarantined(jd)) lines.Add(cs::Cat("QUARANTINED until ", SocietyDate(c->QuarantineUntil)));
			if (!c->Landmark.empty()) lines.Add(cs::Cat("Landmark: ", c->Landmark));
			if (!c->Monument.empty()) lines.Add(cs::Cat("Monument: ", c->Monument));
			if (s->Owner != nullptr) links.emplace_back(s->Owner->Name, CivPick::Faction(s->Owner->Name));
			if (c->Faith != nullptr) links.emplace_back(Narrator::OrgName(*c->Faith), CivPick::Org(c->Faith->Id));
			// US-T330: who matters here: the governor, the houses by wealth, the best known who live here and the firms that trade here.
			if (society != nullptr)
			{
				auto houses = society->HousesOf(s);
				std::vector<cs::Ref<Household>> byWealth;
				std::vector<cs::Ref<Citizen>> residents;
				cs::Ref<Citizen> governor;
				for (const auto& h : houses)
				{
					if (h == nullptr) continue;
					byWealth.push_back(h);
					for (const auto& m : h->Members)
					{
						if (m == nullptr || !m->Alive()) continue;
						if (m->Role == CitizenRole::Governor) governor = m;
						else residents.push_back(m);
					}
				}
				std::stable_sort(byWealth.begin(), byWealth.end(), [](const cs::Ref<Household>& a, const cs::Ref<Household>& b) { return a->Wealth > b->Wealth; });
				std::stable_sort(residents.begin(), residents.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return a->Reputation > b->Reputation; });
				std::vector<cs::Ref<Business>> firms;
				for (const auto& b : society->Businesses)
					if (b != nullptr && b->Trading() && (b->Home == s || b->Branches.Contains(s))) firms.push_back(b);
				lines.Add(cs::Cat("Here: ", static_cast<int>(byWealth.size()), " houses, ", static_cast<int>(residents.size()) + (governor != nullptr ? 1 : 0), " living people, ",
					static_cast<int>(firms.size()), " firms trading"));
				if (governor != nullptr)
				{
					lines.Add(cs::Cat("Governor: ", governor->FullName(), governor->House != nullptr ? cs::Cat(" of House ", governor->House->Surname) : cs::String()));
					links.emplace_back(cs::Cat("Governor ", governor->FullName()), CivPick::Person(governor->Id));
				}
				for (const auto& h : byWealth)
					links.emplace_back(cs::Cat("House ", h->Surname, ", worth ", Narrator::Money(h->Wealth)), CivPick::House(h->Id));
				constexpr int MaxPeople = 6;
				for (int i = 0; i < static_cast<int>(residents.size()) && i < MaxPeople; i++)
				{
					const auto& m = residents[i];
					links.emplace_back(cs::Cat(m->FullName(), ", ", Core::ToString(m->Role), m->House != nullptr ? cs::Cat(" of House ", m->House->Surname) : cs::String()),
						CivPick::Person(m->Id));
				}
				for (const auto& b : firms)
					links.emplace_back(cs::Cat(b->Name, " (", Core::ToString(b->Kind), b->Home == s ? ", head office)" : ", branch)"), CivPick::Business(b->Id));
			}
			// US-T384: who is visiting now, and the conferences it hosts (open, coming or held).
			if (society != nullptr)
			{
				constexpr int MaxVisitors = 6;
				int visitors = 0;
				for (const auto& t : society->Trips)
				{
					if (t == nullptr || t->Stage != TripStage::There || t->To != s || t->Traveller == nullptr) continue;
					if (visitors++ < MaxVisitors)
						links.emplace_back(cs::Cat("Visiting: ", t->Traveller->FullName(), " (", cs::Strings::ToLowerInvariant(TripWhy(t->Purpose)), ")"), CivPick::Person(t->Traveller->Id));
				}
				if (visitors > 0)
					lines.Add(cs::Cat("Visitors here now: ", visitors, visitors > MaxVisitors ? cs::Cat(" (the first ", MaxVisitors, " linked)") : cs::String()));
				for (const auto& k : society->Conferences)
				{
					if (k == nullptr || k->Host != s) continue;
					lines.Add(cs::Cat("Hosts ", ConferenceLine(*k, jd)));
					for (const auto& b : k->Attending)
						if (b != nullptr) links.emplace_back(cs::Cat("At ", k->Name, ": ", b->Name), CivPick::Business(b->Id));
				}
			}
			for (const auto& o : civ.Orgs)
				if (o != nullptr && o->Active() && o->Seat == s) links.emplace_back(cs::Cat(Narrator::OrgName(*o), " (", Core::ToString(o->Kind), ")"), CivPick::Org(o->Id));
			one("riot", "Stir a riot");
			one("arm", "Arm the rebels");
			one("racket", "Let a syndicate take the streets");
			one("crackdown", "Send the police in");
			one("festival", "Throw a festival");
			one("landmark", "Reveal a landmark");
			one("plague", "Loose a plague");
			one("cure", "Cure its sickness");
			one("disaster", "Strike it with disaster");
			pair(CivPickKind::Faction, "aid", "Have relief sent by", "mark a state", true);
			break;
		}
		case CivPickKind::Trial:
		{
			auto t = civ.Politics != nullptr ? civ.Politics->TrialById(p.Id) : nullptr;
			if (t == nullptr || t->Accused == nullptr) break;
			name = cs::Cat("The trial of ", t->Accused->FullName());
			lines.Add(cs::Cat("For ", Core::ToString(t->Charge), t->Where != nullptr ? cs::Cat(", at ", t->Where->Name) : cs::String()));
			lines.Add(t->Decided ? (t->Bought ? cs::String("Acquitted (the judge was bought)") : t->Guilty ? cs::String("Found guilty") : cs::String("Acquitted")) : cs::String("Pending"));
			links.emplace_back(t->Accused->FullName(), CivPick::Person(t->Accused->Id));
			if (t->Where != nullptr) links.emplace_back(t->Where->Name, CivPick::World(t->Where->Name));
			one("acquit", "Have the court acquit");
			break;
		}
		case CivPickKind::Business:
		{
			auto b = society != nullptr ? society->BusinessById(p.Id) : nullptr;
			if (b == nullptr) break;
			lines.Add(Society::Describe(b));
			// US-T334: its history, its standing among the firms, and whom it deals with.
			if (b->Trading())
			{
				int rank = 1, trading = 0;
				for (const auto& o : society->Businesses)
					if (o != nullptr && o->Trading())
					{
						trading++;
						if (o != b && o->Capital > b->Capital) rank++;
					}
				lines.Add(cs::Cat("Founded ", SocietyDate(b->FoundedJd), " (", static_cast<int>((jd - b->FoundedJd) / 365.25), " years ago)  \xC2\xB7  ",
					rank, cs::String(rank % 100 >= 11 && rank % 100 <= 13 ? "th" : rank % 10 == 1 ? "st" : rank % 10 == 2 ? "nd" : rank % 10 == 3 ? "rd" : "th"), // US-T383
					" richest of ", trading, " trading firms"));
			}
			else
				lines.Add(cs::Cat("Founded ", SocietyDate(b->FoundedJd), "; ", cs::String(b->Status == BusinessStatus::Acquired ? "bought out" : "went bankrupt"),
					cs::Double::IsNaN(b->EndedJd) ? cs::String() : cs::Cat(" ", SocietyDate(b->EndedJd)),
					b->Buyer != nullptr ? cs::Cat(" by ", b->Buyer->Name) : cs::String()));
			if (b->Owner != nullptr) links.emplace_back(cs::Cat("House ", b->Owner->Surname), CivPick::House(b->Owner->Id));
			if (b->Home != nullptr) links.emplace_back(b->Home->Name, CivPick::World(b->Home->Name));
			if (b->Buyer != nullptr) links.emplace_back(cs::Cat("Bought by ", b->Buyer->Name), CivPick::Business(b->Buyer->Id));
			for (const auto& w : b->Branches)
				if (w != nullptr) links.emplace_back(cs::Cat("Branch on ", w->Name), CivPick::World(w->Name));
			for (const auto& o : society->Businesses)
			{
				if (o == nullptr || o == b) continue;
				if (o->Buyer == b) links.emplace_back(cs::Cat("Bought ", o->Name), CivPick::Business(o->Id));
				else if (b->Trading() && o->Trading() && o->Kind == b->Kind && o->System != nullptr && o->System == b->System)
					links.emplace_back(cs::Cat("Rival: ", o->Name, " (", Narrator::Money(o->Capital), ")"), CivPick::Business(o->Id));
			}
			// US-T384: the conferences it has sent people to.
			for (const auto& k : society->Conferences)
			{
				if (k == nullptr || !k->Attending.Contains(b)) continue;
				lines.Add(cs::Cat("Sent people to ", ConferenceLine(*k, jd)));
				if (k->Host != nullptr) links.emplace_back(cs::Cat(k->Name, " (", k->Host->Name, ")"), CivPick::World(k->Host->Name));
			}
			int bribes = 0, exposed = 0;
			double paid = 0;
			for (const auto& r : society->Bribes)
				if (r != nullptr && r->Briber == b)
				{
					bribes++;
					paid += r->Amount;
					if (r->Exposed) exposed++;
				}
			if (bribes > 0) lines.Add(cs::Cat("Bribes paid: ", bribes, " (", Narrator::Money(paid), "), ", exposed, " exposed"));
			if (marked.Kind == CivPickKind::House)
				hands.push_back(Hand{ cs::Cat("Have it taken over by ", NameOf(marked, galaxy, civ)), cs::Cat("takeover ", marked.Arg(), " ", arg) });
			else hands.push_back(Hand{ "Have it taken over by...  (mark a house)", cs::String() });
			break;
		}
		case CivPickKind::Faction:
		{
			cs::Ref<Faction> f = galaxy.Polities != nullptr ? galaxy.Polities->Find(p.Name) : nullptr;
			if (f == nullptr) break;
			if (civ.Politics != nullptr)
				for (const auto& r : civ.Politics->Regimes)
					if (r != nullptr && r->Faction == f)
					{
						lines.Add(cs::Cat("Ruled by ", r->Ruler != nullptr ? r->Ruler->FullName() : cs::String("nobody (crisis)"), r->Ruling != nullptr ? cs::Cat(" of ", Narrator::OrgName(*r->Ruling)) : cs::String()));
						lines.Add(cs::Cat("Hawk ", SocietySigned(r->Hawk), ", liberty ", SocietySigned(r->Liberty), ", approval ", SocietySigned(r->Approval), ", ", r->Coups, " coups",
							cs::Double::IsNaN(r->NextElectionJd) ? cs::String(", no elections") // US-T383: never "votes in 0 days" or less
								: r->NextElectionJd - jd < 1 ? cs::String(", votes now")
								: cs::Cat(r->Called ? ", election called, votes in " : ", votes in ", static_cast<int>(r->NextElectionJd - jd), " days")));
						if (r->Ruler != nullptr) links.emplace_back(r->Ruler->FullName(), CivPick::Person(r->Ruler->Id));
						if (r->Ruling != nullptr) links.emplace_back(Narrator::OrgName(*r->Ruling), CivPick::Org(r->Ruling->Id));
					}
			if (civ.Strife != nullptr)
				if (auto a = civ.Strife->AdmiralOf(f); a != nullptr && a->Who != nullptr) links.emplace_back(cs::Cat("Admiral ", a->Who->FullName()), CivPick::Person(a->Who->Id));
			for (const auto& w : civ.Worlds)
				if (w != nullptr && w->Where != nullptr && w->Where->Owner == f) links.emplace_back(w->Where->Name, CivPick::World(w->Where->Name));
			pair(CivPickKind::Faction, "summit", "Call a peace summit with", "mark another state");
			pair(CivPickKind::Faction, "spy", "Set its spies on", "mark another state");
			if (marked.Kind == CivPickKind::Org) hands.push_back(Hand{ cs::Cat("Hire ", NameOf(marked, galaxy, civ), " for it"), cs::Cat("hire ", marked.Arg(), " ", arg) });
			if (marked.Kind == CivPickKind::World) hands.push_back(Hand{ cs::Cat("Have it send relief to ", marked.Name), cs::Cat("aid ", arg, " ", marked.Arg()) });
			break;
		}
		default: break;
		}
	}

	void SocietyScreen::DrawDetail(Rect area, Galaxy& galaxy, Civilization& civ, double jd)
	{
		cs::String name;
		cs::List<cs::String> lines;
		std::vector<std::pair<cs::String, CivPick>> links;
		std::vector<Hand> hands;
		cs::Ref<SocialEvent> event;
		if (selectedEvent != 0)
			for (int i = civ.Events.Count() - 1; i >= 0; i--)
				if (civ.Events[i] != nullptr && civ.Events[i]->Id == selectedEvent) { event = civ.Events[i]; break; }
		if (event != nullptr) DescribeEvent(*event, name, lines, links);
		else if (!selected.IsNone()) Describe(selected, galaxy, civ, jd, name, lines, links, hands);
		// US-T371: each hand's outlook, judged now (read only).
		float handsH = 0;
		for (auto& h : hands)
		{
			h.Outlook = HandOutlook(h.Line, galaxy, civ, jd);
			handsH += h.Outlook.empty() ? 30.0f : 48.0f;
		}
		if (name.empty())
		{
			if (!back.empty() && GUI::Button(Rect(area.xMax() - 30, area.y, 26, 24), "<", button)) StepBack();
			GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), "NOTHING PICKED", heading);
			GUI::Label(Rect(area.x, area.y + HeadingH, area.width, 120),
				"Pick an event, a house, a person, an organisation or a world on the left. What it is shows here, with the people and "
				"places about it to follow, and the SpaceGod's hand in it: the same verbs as the console's sg.Civ, so in co-op every "
				"player's galaxy feels it on the same day. Watch one to be told its news as it happens.", text);
			return;
		}

		float w = area.width - 16;
		float linkH = 0;
		{
			float x = 0;
			linkH = links.empty() ? 0.0f : 28.0f;
			for (const auto& l : links)
			{
				float bw = Mathf::Min(w, link->CalcSize(GUIContent(l.first)).x + 16);
				if (x > 0 && x + bw > w) { x = 0; linkH += 28; }
				x += bw + 6;
			}
		}
		float textH = 0;
		for (const auto& l : lines) textH += text->CalcHeight(GUIContent(l), w) + 2;
		// US-T299: what has happened about the thing picked (a trial's is its accused's).
		bool showRecent = event == nullptr && !selected.IsNone() && selected.Kind != CivPickKind::Trial;
		if (showRecent) BuildRecent(selected, civ);
		float recentH = showRecent ? HeadingH + (recent.empty() ? 24.0f : static_cast<float>(recent.size()) * 28.0f) + SectionGap : 0.0f;
		// US-T325: what came of an event the SpaceGod's hand brought about.
		bool showAftermath = event != nullptr && event->Steered;
		if (showAftermath) BuildAftermath(*event, civ);
		float aftermathH = showAftermath ? HeadingH + (aftermath.empty() ? 24.0f : static_cast<float>(aftermath.size()) * 28.0f) + SectionGap : 0.0f;
		recentH += aftermathH;
		// US-T327: a person's life, in order.
		bool showLife = event == nullptr && selected.Kind == CivPickKind::Person;
		if (showLife) BuildLife(selected.Id, galaxy, civ);
		float lifeH = showLife && !life.empty() ? HeadingH + static_cast<float>(life.size()) * 28.0f + (lifeMore > 0 ? 20.0f : 0.0f) + SectionGap : 0.0f;
		recentH += lifeH;
		// US-T348: a house's, organisation's, business's or world's events, a bar a year.
		bool showTimeline = event == nullptr && (selected.Kind == CivPickKind::House || selected.Kind == CivPickKind::Org
			|| selected.Kind == CivPickKind::Business || selected.Kind == CivPickKind::World);
		if (showTimeline) BuildTimeline(selected, civ, jd);
		int timelineFollow = 0;
		recentH += showTimeline ? DrawTimeline(0, w, false, timelineFollow) : 0.0f;
		bool showTrends = event == nullptr && selected.Kind == CivPickKind::World;
		float trendsH = showTrends ? DrawTrends(0, w, selected.Name, civ, jd, false) : 0.0f;
		CivPick powerFollow; // US-T378: who holds the world's wealth and strength
		trendsH += showTrends ? DrawPower(0, w, selected.Name, galaxy, civ, false, powerFollow) : 0.0f;
		bool showFamily = event == nullptr && selected.Kind == CivPickKind::House; // US-T315
		CivPick familyFollow;
		float familyH = showFamily ? DrawFamily(0, w, selected.Id, galaxy, jd, false, familyFollow) : 0.0f;
		bool showTree = event == nullptr && selected.Kind == CivPickKind::Person; // US-T358
		CivPick treeFollow;
		familyH += showTree ? DrawFamilyTree(0, w, selected.Id, galaxy, jd, false, treeFollow) : 0.0f;
		bool showChronicle = showFamily; // US-T375: a house's history, told
		if (showChronicle && chronicleOpen) BuildChronicle(selected.Id, galaxy, civ);
		CivPick chronicleFollow;
		familyH += showChronicle ? DrawChronicle(0, w, galaxy, civ, false, chronicleFollow) : 0.0f;
		bool showCompare = event == nullptr && Comparable(); // US-T344
		CivPick compareFollow;
		float compareH = showCompare ? DrawCompare(0, w, galaxy, civ, jd, false, compareFollow) : 0.0f;
		float contentH = 30 + 22 + 12 + textH + SectionGap + trendsH + familyH + compareH +(links.empty() ? 0.0f : HeadingH + linkH + SectionGap) + recentH
			+ (selected.IsNone() || event != nullptr ? 0.0f : HeadingH + 30 + handsH + SectionGap);
		// US-T298: back and forward through what was picked, beside the title.
		bool stepBack = false, stepForward = false;
		{
			bool enabled = GUI::enabled;
			GUI::enabled = enabled && !back.empty();
			stepBack = GUI::Button(Rect(area.xMax() - 60, area.y + 2, 26, 24), "<", button);
			GUI::enabled = enabled && !forward.empty();
			stepForward = GUI::Button(Rect(area.xMax() - 30, area.y + 2, 26, 24), ">", button);
			GUI::enabled = enabled;
		}
		detailScroll = GUI::BeginScrollView(area, detailScroll, Rect(0, 0, w, contentH));
		float y = 0;
		GUI::Label(Rect(0, y, w - 50, 30), cs::Strings::ToUpperInvariant(name), entryTitle);
		y += 30;
		static const char* const KindNames[] = { "", "House", "Person", "Organisation", "World", "Trial", "Business", "State" };
		GUI::Label(Rect(0, y, w, 20), event != nullptr ? cs::String("Event") : cs::String(KindNames[static_cast<int>(selected.Kind)]), small);
		// US-T304: watch it, to be told its news while playing.
		if (showRecent)
		{
			bool watching = app->IsWatched(selected);
			if (GUI::Button(Rect(Mathf::Max(0.0f, w - 170), y - 2, 166, 22), watching ? "Watching (click to stop)" : "Watch its news", watching ? entrySelected : entry))
			{
				app->ToggleWatch(selected);
				rowsAt = -1;
			}
		}
		y += 22;
		GUI::DrawTexture(Rect(0, y + 4, w, 1), buttonActiveTexture);
		y += 12;
		for (const auto& l : lines)
		{
			float h = text->CalcHeight(GUIContent(l), w);
			GUI::Label(Rect(0, y, w, h), l, text);
			y += h + 2;
		}
		y += SectionGap;
		if (showTrends) y += DrawTrends(y, w, selected.Name, civ, jd, true);
		if (showTrends) y += DrawPower(y, w, selected.Name, galaxy, civ, true, powerFollow);
		if (showFamily) y += DrawFamily(y, w, selected.Id, galaxy, jd, true, familyFollow);
		if (showChronicle) y += DrawChronicle(y, w, galaxy, civ, true, chronicleFollow);
		if (showTree) y += DrawFamilyTree(y, w, selected.Id, galaxy, jd, true, treeFollow);
		if (showCompare) y += DrawCompare(y, w, galaxy, civ, jd, true, compareFollow);
		CivPick follow = !compareFollow.IsNone() ? compareFollow : !treeFollow.IsNone() ? treeFollow : !chronicleFollow.IsNone() ? chronicleFollow
			: !powerFollow.IsNone() ? powerFollow : familyFollow;
		if (!links.empty())
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "SEE ALSO", heading);
			y += HeadingH;
			float x = 0;
			for (const auto& l : links)
			{
				float bw = Mathf::Min(w, link->CalcSize(GUIContent(l.first)).x + 16);
				if (x > 0 && x + bw > w) { x = 0; y += 28; }
				if (GUI::Button(Rect(x, y, bw, 24), l.first, link)) follow = l.second;
				x += bw + 6;
			}
			y += 28 + SectionGap;
		}
		int followEvent = 0;
		if (showAftermath)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), aftermathTotal > AftermathMax ? cs::Cat("WHAT CAME OF IT  (first ", AftermathMax, " of ", aftermathTotal, ")") : cs::Cat("WHAT CAME OF IT  (", aftermathTotal, ")"), heading);
			y += HeadingH;
			if (aftermath.empty())
			{
				GUI::Label(Rect(0, y, w, 20), "Nothing has come of it yet: what follows about the same people, houses and organisations within a year shows here.", small);
				y += 24;
			}
			for (const auto& r : aftermath)
			{
				auto row = Rect(0, y, w, 24);
				if (GUI::Button(row, GUIContent::none(), entry)) followEvent = r.Event;
				GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Line, rowLine);
				y += 28;
			}
			y += SectionGap;
		}
		if (lifeH > 0)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "LIFE", heading);
			y += HeadingH;
			for (const auto& r : life)
			{
				auto row = Rect(0, y, w, 24);
				if (r.Event != 0)
				{
					if (GUI::Button(row, GUIContent::none(), entry)) followEvent = r.Event;
					GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Line, rowLine);
				}
				else GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Line, rowTitle);
				y += 28;
			}
			if (lifeMore > 0)
			{
				GUI::Label(Rect(8, y, w - 8, 18), cs::Cat("and ", lifeMore, " more (the latest are under Recent events)"), small);
				y += 20;
			}
			y += SectionGap;
		}
		if (showTimeline)
		{
			y += DrawTimeline(y, w, true, timelineFollow);
			if (timelineFollow != 0) followEvent = timelineFollow;
		}
		if (showRecent)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), recentTotal > RecentMax ? cs::Cat("RECENT EVENTS  (latest ", RecentMax, " of ", recentTotal, ")") : cs::Cat("RECENT EVENTS  (", recentTotal, ")"), heading);
			y += HeadingH;
			if (recent.empty())
			{
				GUI::Label(Rect(0, y, w, 20), "Nothing kept about it yet.", small);
				y += 24;
			}
			for (const auto& r : recent)
			{
				auto row = Rect(0, y, w, 24);
				if (GUI::Button(row, GUIContent::none(), entry)) followEvent = r.Event;
				GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Line, rowLine);
				y += 28;
			}
			y += SectionGap;
		}
		if (event == nullptr && !selected.IsNone())
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "THE SPACEGOD'S HAND", heading);
			GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), buttonActiveTexture);
			y += HeadingH;
			// The mark: the second thing a two-sided hand (a feud, an affair, a summit) takes.
			bool isMarked = marked == selected;
			// US-T344: the same mark compares (C); the button names the key.
			if (GUI::Button(Rect(0, y, 240, 24), isMarked ? "Marked (C or click: clear)" : "Mark: second / compare (C)", isMarked ? entrySelected : entry)) ToggleMark();
			if (!marked.IsNone() && !isMarked) GUI::Label(Rect(248, y + 3, Mathf::Max(0.0f, w - 248), 20),cs::Cat("Marked: ", NameOf(marked, galaxy, civ)), small);
			y += 30;
			bool enabled = GUI::enabled;
			for (const auto& h : hands)
			{
				GUI::enabled = enabled && !h.Line.empty();
				if (GUI::Button(Rect(0, y, Mathf::Min(w, 420.0f), 26), h.Label, button)) Ask(h.Line);
				if (!h.Line.empty()) GUI::Label(Rect(Mathf::Min(w, 420.0f) + 10, y + 4, Mathf::Max(0.0f, w - 430), 20), cs::Cat("sg.Civ ", h.Line), small);
				y += 30;
				if (!h.Outlook.empty()) // US-T371
				{
					GUI::Label(Rect(12, y - 3, Mathf::Max(0.0f, w - 12), 18), h.Outlook, small);
					y += 18;
				}
			}
			GUI::enabled = enabled;
			if (hands.empty()) GUI::Label(Rect(0, y, w, 20), "No hand of the SpaceGod's reaches this.", small);
		}
		GUI::EndScrollView();
		if (!follow.IsNone()) Go(Visit{ follow, 0 });
		else if (followEvent != 0) Go(Visit{ CivPick(), followEvent });
		else if (stepBack) StepBack();
		else if (stepForward) StepForward();
	}

	void SocietyScreen::ToggleMark()
	{
		marked = marked == selected ? CivPick() : selected;
	}

	bool SocietyScreen::Comparable() const
	{
		if (selected.IsNone() || marked.IsNone() || selected == marked || selected.Kind != marked.Kind) return false;
		return selected.Kind == CivPickKind::Person || selected.Kind == CivPickKind::House || selected.Kind == CivPickKind::Org || selected.Kind == CivPickKind::Business;
	}

	void SocietyScreen::BuildCompare(Galaxy& galaxy, Civilization& civ, double jd, std::vector<CompareRow>& out) const
	{
		out.clear();
		if (!Comparable()) return;
		auto society = Society::Find(galaxy);
		// sense: 1 the higher is better, -1 the lower, 0 neither (an age, a date, a count that is no merit either way).
		auto add = [&](const char* label, double a, double b, const cs::String& as, const cs::String& bs, int sense)
		{
			CompareRow r;
			r.Label = label;
			r.A = as;
			r.B = bs;
			if (sense != 0 && !std::isnan(a) && !std::isnan(b) && a != b) r.Better = (sense > 0) == (a > b) ? 1 : 2;
			out.push_back(r);
		};
		auto words = [&](const char* label, const cs::String& as, const cs::String& bs)
		{
			CompareRow r;
			r.Label = label;
			r.A = as;
			r.B = bs;
			out.push_back(r);
		};
		auto count = [&](const char* label, int a, int b, int sense) { add(label, a, b, cs::Cat(a), cs::Cat(b), sense); };
		auto money = [&](const char* label, double a, double b) { add(label, a, b, Narrator::Money(a), Narrator::Money(b), 1); };
		auto date = [&](const char* label, double a, double b) { add(label, a, b, SocietyDate(a), SocietyDate(b), 0); };
		switch (selected.Kind)
		{
		case CivPickKind::Person:
		{
			auto a = society != nullptr ? society->CitizenById(selected.Id) : nullptr;
			auto b = society != nullptr ? society->CitizenById(marked.Id) : nullptr;
			if (a == nullptr || b == nullptr) return;
			auto years = [&](const Citizen& c) { return c.Alive() ? c.AgeYears(jd) : c.AgeYears(c.DiedJd); };
			auto orgsOf = [](const Citizen& c)
			{
				int n = 0;
				for (const auto& o : c.Orgs)
					if (o != nullptr && o->Active()) n++;
				return n;
			};
			words("Status", a->Alive() ? cs::String("living") : cs::Cat("died ", SocietyDate(a->DiedJd)), b->Alive() ? cs::String("living") : cs::Cat("died ", SocietyDate(b->DiedJd)));
			add("Age", years(*a), years(*b), cs::Cat(static_cast<int>(years(*a))), cs::Cat(static_cast<int>(years(*b))), 0);
			words("Role", cs::String(Core::ToString(a->Role)), cs::String(Core::ToString(b->Role)));
			words("House", a->House != nullptr ? a->House->Surname : cs::String("none"), b->House != nullptr ? b->House->Surname : cs::String("none"));
			add("Reputation", a->Reputation, b->Reputation, SocietySigned(a->Reputation), SocietySigned(b->Reputation), 1);
			add("Skill", a->Skill, b->Skill, cs::Fmt(a->Skill, "0.00"), cs::Fmt(b->Skill, "0.00"), 1);
			count("Ties outside the family", a->Ties.Count(), b->Ties.Count(), 0);
			count("Organisations", orgsOf(*a), orgsOf(*b), 0);
			words("Faith", a->Faith != nullptr ? Narrator::OrgName(*a->Faith) : cs::String("none"), b->Faith != nullptr ? Narrator::OrgName(*b->Faith) : cs::String("none"));
			if (a->Bribes > 0 || b->Bribes > 0) add("Bribes taken", a->Bribes, b->Bribes, Narrator::Money(a->Bribes), Narrator::Money(b->Bribes), 0);
			break;
		}
		case CivPickKind::House:
		{
			auto a = society != nullptr ? society->HouseById(selected.Id) : nullptr;
			auto b = society != nullptr ? society->HouseById(marked.Id) : nullptr;
			if (a == nullptr || b == nullptr) return;
			auto capital = [](const Household& h) { return h.Business != nullptr && h.Business->Trading() ? h.Business->Capital : 0.0; };
			auto branches = [](const Household& h) { return h.Business != nullptr && h.Business->Trading() ? h.Business->Branches.Count() : 0; };
			auto governors = [](const Household& h)
			{
				int n = 0;
				for (const auto& m : h.Members)
					if (m != nullptr && m->Alive() && m->Role == CitizenRole::Governor) n++;
				return n;
			};
			auto feuds = [&](const cs::Ref<Household>& h)
			{
				int n = 0;
				if (civ.Kinship != nullptr)
					for (const auto& f : civ.Kinship->Feuds)
						if (f != nullptr && f->Active() && (f->A == h || f->B == h)) n++;
				return n;
			};
			words("Home", a->Home != nullptr ? a->Home->Name : cs::String("nowhere"), b->Home != nullptr ? b->Home->Name : cs::String("nowhere"));
			date("Founded", a->FoundedJd, b->FoundedJd);
			count("Living", a->Living(), b->Living(), 1);
			count("Members ever", a->Members.Count(), b->Members.Count(), 0);
			money("Wealth", a->Wealth, b->Wealth);
			add("Business capital", capital(*a), capital(*b), a->Business != nullptr && a->Business->Trading() ? Narrator::Money(capital(*a)) : cs::String("no business"),
				b->Business != nullptr && b->Business->Trading() ? Narrator::Money(capital(*b)) : cs::String("no business"), 1);
			count("Branch worlds", branches(*a), branches(*b), 1);
			count("Governors", governors(*a), governors(*b), 1);
			count("Feuds running", feuds(a), feuds(b), -1);
			break;
		}
		case CivPickKind::Org:
		{
			auto a = civ.OrgById(selected.Id);
			auto b = civ.OrgById(marked.Id);
			if (a == nullptr || b == nullptr) return;
			words("Kind", cs::String(Core::ToString(a->Kind)), cs::String(Core::ToString(b->Kind)));
			words("Status", a->Active() ? cs::String("active") : cs::Cat("ended ", SocietyDate(a->EndedJd)), b->Active() ? cs::String("active") : cs::Cat("ended ", SocietyDate(b->EndedJd)));
			date("Founded", a->FoundedJd, b->FoundedJd);
			add("Strength", a->Strength, b->Strength, SocietyPct(a->Strength), SocietyPct(b->Strength), 1);
			money("Funds", a->Funds, b->Funds);
			count("Members", a->Members.Count(), b->Members.Count(), 1);
			add("Stance", a->Stance, b->Stance, SocietySigned(a->Stance), SocietySigned(b->Stance), 0);
			add("Score", static_cast<double>(a->Score), static_cast<double>(b->Score), cs::Cat(a->Score), cs::Cat(b->Score), 1);
			words("Led by", a->Leader != nullptr ? a->Leader->FullName() : cs::String("nobody"), b->Leader != nullptr ? b->Leader->FullName() : cs::String("nobody"));
			break;
		}
		case CivPickKind::Business:
		{
			auto a = society != nullptr ? society->BusinessById(selected.Id) : nullptr;
			auto b = society != nullptr ? society->BusinessById(marked.Id) : nullptr;
			if (a == nullptr || b == nullptr) return;
			auto bought = [&](const cs::Ref<Business>& x)
			{
				int n = 0;
				for (const auto& o : society->Businesses)
					if (o != nullptr && o->Buyer == x) n++;
				return n;
			};
			words("Line", cs::String(Core::ToString(a->Kind)), cs::String(Core::ToString(b->Kind)));
			words("Status", cs::String(Core::ToString(a->Status)), cs::String(Core::ToString(b->Status)));
			words("Owner", a->Owner != nullptr ? cs::Cat("House ", a->Owner->Surname) : cs::String("none"), b->Owner != nullptr ? cs::Cat("House ", b->Owner->Surname) : cs::String("none"));
			date("Founded", a->FoundedJd, b->FoundedJd);
			money("Capital", a->Capital, b->Capital);
			money("Last month", a->LastMonth, b->LastMonth);
			count("Branch worlds", a->Branches.Count(), b->Branches.Count(), 1);
			count("Firms bought", bought(a), bought(b), 1);
			break;
		}
		default:
			break;
		}
	}

	float SocietyScreen::DrawCompare(float y, float w, Galaxy& galaxy, Civilization& civ, double jd, bool draw, CivPick& follow)
	{
		std::vector<CompareRow> cmp;
		BuildCompare(galaxy, civ, jd, cmp);
		if (cmp.empty()) return 0;
		float height = HeadingH + 28 + static_cast<float>(cmp.size()) * 22.0f + 22 + SectionGap;
		if (!draw) return height;
		float labelW = Mathf::Min(170.0f, w * 0.3f);
		float colW = (w - labelW) / 2;
		GUI::Label(Rect(0, y, w, HeadingH - 4), "COMPARE", heading);
		y += HeadingH;
		cs::String here = NameOf(selected, galaxy, civ), there = NameOf(marked, galaxy, civ);
		GUI::Label(Rect(labelW, y + 3, colW - 6, 20), here, rowTitle);
		if (GUI::Button(Rect(labelW + colW, y, Mathf::Min(colW - 6, link->CalcSize(GUIContent(there)).x + 16), 24), there, link)) follow = marked;
		y += 28;
		int winsHere = 0, winsThere = 0;
		for (const auto& r : cmp)
		{
			GUI::Label(Rect(0, y, labelW - 6, 20), r.Label, small);
			GUI::Label(Rect(labelW, y, colW - 6, 20), r.A, r.Better == 1 ? good : text);
			GUI::Label(Rect(labelW + colW, y, colW - 6, 20), r.B, r.Better == 2 ? good : text);
			if (r.Better == 1) winsHere++;
			else if (r.Better == 2) winsThere++;
			y += 22;
		}
		GUI::Label(Rect(0, y, w, 20), cs::Cat(here, " ahead on ", winsHere, winsHere == 1 ? " row" : " rows", ", ", there, " on ", winsThere,
			"  (the better value of a row is highlighted; an age, a date or a stance has none)"), small);
		return height;
	}

	float SocietyScreen::DrawTrends(float y, float w, const cs::String& world, Civilization& civ, double jd, bool draw)
	{
		static constexpr float TrendRowH = 26, BarH = 20, LabelW = 120, DeltaW = 150;
		struct Measure
		{
			const char* Name;
			double UnitySpaceApp::SocietyTrendSample::* Field;
			/// Approval runs -1..1 from a middle line; the rest 0..1 from the floor. Rising is good only for approval.
			bool Signed;
		};
		static const Measure Measures[] = {
			{ "Approval", &UnitySpaceApp::SocietyTrendSample::Approval, true },
			{ "Unrest", &UnitySpaceApp::SocietyTrendSample::Unrest, false },
			{ "Crime", &UnitySpaceApp::SocietyTrendSample::Crime, false },
			{ "Sickness", &UnitySpaceApp::SocietyTrendSample::Infected, false },
			{ "War weariness", &UnitySpaceApp::SocietyTrendSample::Weariness, false },
		};
		const int rowsN = static_cast<int>(sizeof(Measures) / sizeof(Measures[0]));
		const auto* sampled = app->SocietyTrendOf(world);
		float h = HeadingH + (sampled == nullptr ? 24.0f : rowsN * TrendRowH + 20) + SectionGap;
		if (!draw) return h;

		// The samples the game took, then today's mood as the last bar (when the newest sample is older than a day).
		std::vector<UnitySpaceApp::SocietyTrendSample> bars;
		if (sampled != nullptr) bars = *sampled;
		cs::Ref<Civics> now;
		for (const auto& c : civ.Worlds)
			if (c != nullptr && c->Where != nullptr && c->Where->Name == world) { now = c; break; }
		if (now != nullptr && (bars.empty() || jd - bars.back().Jd > 1))
			bars.push_back(UnitySpaceApp::SocietyTrendSample{ jd, now->Approval, now->Where->Unrest, now->Crime, now->Infected, now->Weariness });

		GUI::Label(Rect(0, y, w, HeadingH - 4), sampled == nullptr ? cs::String("TRENDS")
			: cs::Cat("TRENDS  (every ", static_cast<int>(UnitySpaceApp::SocietyTrendDays), " days since ", SocietyDate(sampled->front().Jd), ")"), heading);
		y += HeadingH;
		if (sampled == nullptr)
		{
			GUI::Label(Rect(0, y, w, 20), cs::Cat("Its mood is sampled every ", static_cast<int>(UnitySpaceApp::SocietyTrendDays),
				" days while you play (not kept in saves); the first sample comes with the next."), small);
			return h;
		}
		float chartW = Mathf::Max(60.0f, w - LabelW - DeltaW - 16);
		float slot = chartW / static_cast<float>(UnitySpaceApp::SocietyTrendMax + 1);
		float barW = Mathf::Max(2.0f, slot - 2);
		Color keep = GUI::color;
		for (const auto& m : Measures)
		{
			GUI::Label(Rect(0, y + 2, LabelW, 20), m.Name, text);
			float x0 = LabelW;
			GUI::color = Color(UiStyle::Edge.r, UiStyle::Edge.g, UiStyle::Edge.b, 0.25f);
			GUI::DrawTexture(Rect(x0, y + 2, chartW, BarH), Texture2D::whiteTexture());
			float mid = y + 2 + BarH * 0.5f, baseY = y + 2 + BarH;
			if (m.Signed)
			{
				GUI::color = UiStyle::Edge;
				GUI::DrawTexture(Rect(x0, mid, chartW, 1), Texture2D::whiteTexture());
			}
			for (size_t i = 0; i < bars.size(); i++)
			{
				double v = bars[i].*m.Field;
				bool last = i + 1 == bars.size();
				Color c = m.Signed ? (v >= 0 ? UiStyle::Good : UiStyle::Danger) : UiStyle::Accent;
				GUI::color = Color(c.r, c.g, c.b, last ? 1.0f : 0.6f);
				float x = x0 + static_cast<float>(i) * slot + 1;
				if (m.Signed)
				{
					float bh = Mathf::Clamp(static_cast<float>(std::fabs(v)), 0.0f, 1.0f) * BarH * 0.5f;
					GUI::DrawTexture(Rect(x, v >= 0 ? mid - bh : mid, barW, Mathf::Max(1.0f, bh)), Texture2D::whiteTexture());
				}
				else
				{
					float bh = Mathf::Clamp(static_cast<float>(v), 0.0f, 1.0f) * BarH;
					GUI::DrawTexture(Rect(x, baseY - Mathf::Max(1.0f, bh), barW, Mathf::Max(1.0f, bh)), Texture2D::whiteTexture());
				}
			}
			GUI::color = keep;
			double first = bars.front().*m.Field, latest = bars.back().*m.Field;
			double d = latest - first;
			cs::String value = m.Signed ? SocietySigned(latest) : SocietyPct(latest);
			cs::String change = bars.size() < 2 || std::fabs(d) < 0.005 ? cs::String("steady")
				: m.Signed ? cs::Cat(d > 0 ? "up " : "down ", cs::Fmt(std::fabs(d), "0.00")) : cs::Cat(d > 0 ? "up " : "down ", cs::Fmt(std::fabs(d) * 100, "0"), " pts");
			bool better = m.Signed ? d > 0 : d < 0;
			GUI::Label(Rect(LabelW + chartW + 12, y + 2, DeltaW, 20), cs::Cat(value, "  ", change),
				bars.size() < 2 || std::fabs(d) < 0.005 ? text : better ? good : bad);
			y += TrendRowH;
		}
		GUI::Label(Rect(0, y, w, 18), "Bars run oldest to newest; the brightest is today.", small);
		return h;
	}

	float SocietyScreen::DrawPower(float y, float w, const cs::String& world, Galaxy& galaxy, Civilization& civ, bool draw, CivPick& follow)
	{
		static constexpr float PowerRowH = 26, KindH = 20, BarW = 140, ShareW = 52;
		struct Rank
		{
			cs::String Label;
			double Value = 0;
			CivPick Pick;
		};
		struct Group
		{
			const char* Title;
			const char* Measure;
			std::vector<Rank> Rows;
			double Total = 0;
			int Count = 0;
		};
		cs::Ref<Civics> civics;
		for (const auto& c : civ.Worlds)
			if (c != nullptr && c->Where != nullptr && c->Where->Name == world) { civics = c; break; }
		if (civics == nullptr) return 0;
		auto s = civics->Where;
		auto society = Society::Find(galaxy);

		// Each kind ranked by its own measure, its share of what every one of that kind based here holds (read only).
		Group groups[3] = { { "Houses", "of the houses' wealth", {}, 0, 0 }, { "Firms", "of the firms' capital", {}, 0, 0 },
			{ "Organisations", "of the organisations' strength", {}, 0, 0 } };
		if (society != nullptr)
		{
			for (const auto& hh : society->HousesOf(s))
				if (hh != nullptr) groups[0].Rows.push_back(Rank{ cs::Cat("House ", hh->Surname, "  \xC2\xB7  worth ", Narrator::Money(hh->Wealth)), (hh->Wealth > 0 ? hh->Wealth : 0.0), CivPick::House(hh->Id) });
			for (const auto& b : society->Businesses)
				if (b != nullptr && b->Trading() && (b->Home == s || b->Branches.Contains(s)))
					groups[1].Rows.push_back(Rank{ cs::Cat(b->Name, b->Home == s ? " (head office)" : " (branch)", "  \xC2\xB7  ", Narrator::Money(b->Capital)),
						(b->Capital > 0 ? b->Capital : 0.0), CivPick::Business(b->Id) });
		}
		for (const auto& o : civ.Orgs)
			if (o != nullptr && o->Active() && o->Seat == s)
				groups[2].Rows.push_back(Rank{ cs::Cat(Narrator::OrgName(*o), " (", Core::ToString(o->Kind), ")  \xC2\xB7  strength ", SocietyPct(o->Strength)),
					(o->Strength > 0 ? o->Strength : 0.0), CivPick::Org(o->Id) });
		int shown = 0;
		for (auto& g : groups)
		{
			std::stable_sort(g.Rows.begin(), g.Rows.end(), [](const Rank& a, const Rank& b) { return a.Value > b.Value; });
			g.Count = static_cast<int>(g.Rows.size());
			for (const auto& r : g.Rows) g.Total += r.Value;
			if (static_cast<int>(g.Rows.size()) > PowerMax) g.Rows.resize(PowerMax);
			shown += static_cast<int>(g.Rows.size());
		}
		float h = HeadingH + SectionGap;
		if (shown == 0) h += 24;
		for (const auto& g : groups)
			if (!g.Rows.empty()) h += KindH + static_cast<float>(g.Rows.size()) * PowerRowH;
		if (!draw) return h;

		GUI::Label(Rect(0, y, w, HeadingH - 4), "POWER", heading);
		y += HeadingH;
		if (shown == 0)
		{
			GUI::Label(Rect(0, y, w, 20), "No house, firm or organisation is based here.", small);
			return h;
		}
		Color keep = GUI::color;
		for (const auto& g : groups)
		{
			if (g.Rows.empty()) continue;
			GUI::Label(Rect(0, y + 2, w, KindH - 2), g.Count > static_cast<int>(g.Rows.size())
				? cs::Cat(g.Title, "  \xC2\xB7  share ", g.Measure, "  (the first ", static_cast<int>(g.Rows.size()), " of ", g.Count, ")")
				: cs::Cat(g.Title, "  \xC2\xB7  share ", g.Measure), small);
			y += KindH;
			for (const auto& r : g.Rows)
			{
				double share = g.Total > 0 ? r.Value / g.Total : 0;
				auto row = Rect(0, y, w, PowerRowH - 4);
				if (GUI::Button(row, GUIContent::none(), entry)) follow = r.Pick;
				GUI::color = Color(UiStyle::Edge.r, UiStyle::Edge.g, UiStyle::Edge.b, 0.25f);
				GUI::DrawTexture(Rect(row.x + 6, row.y + 5, BarW, row.height - 10), Texture2D::whiteTexture());
				GUI::color = UiStyle::Accent;
				GUI::DrawTexture(Rect(row.x + 6, row.y + 5, Mathf::Max(1.0f, BarW * static_cast<float>(share)), row.height - 10), Texture2D::whiteTexture());
				GUI::color = keep;
				GUI::Label(Rect(row.x + BarW + 12, row.y + 2, ShareW, 20), cs::Cat(cs::Fmt(share * 100, "0"), "%"), text);
				GUI::Label(Rect(row.x + BarW + ShareW + 16, row.y + 2, Mathf::Max(10.0f, row.width - BarW - ShareW - 22), 20), r.Label, rowLine);
				y += PowerRowH;
			}
		}
		return h;
	}

	float SocietyScreen::DrawFamily(float y, float w, int houseId, Galaxy& galaxy, double jd, bool draw, CivPick& follow)
	{
		static constexpr float GenH = 22, FamilyRowH = 28;
		static constexpr double GenerationDays = 25 * 365.25;
		static constexpr int MaxRows = 80;
		auto society = Society::Find(galaxy);
		auto house = society != nullptr ? society->HouseById(houseId) : nullptr;
		if (house == nullptr) return 0;

		// Citizens keep no parents, so a generation is a 25-year band of births from the house's eldest.
		std::vector<cs::Ref<Citizen>> people;
		for (const auto& m : house->Members)
			if (m != nullptr) people.push_back(m);
		std::stable_sort(people.begin(), people.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return a->BornJd < b->BornJd; });
		if (static_cast<int>(people.size()) > MaxRows) people.erase(people.begin(), people.end() - MaxRows);
		auto generationOf = [&](const Citizen& c) { return people.empty() ? 0 : static_cast<int>((c.BornJd - people.front()->BornJd) / GenerationDays); };
		int generations = 0, last = -1;
		for (const auto& c : people)
			if (int g = generationOf(*c); g != last) { generations++; last = g; }
		int living = house->Living();
		int remembered = house->Members.Count() - living;

		float h = HeadingH + (people.empty() ? 24.0f : generations * GenH + static_cast<float>(people.size()) * FamilyRowH) + SectionGap;
		if (!draw) return h;

		auto year = [](double d) { return d <= 0 ? cs::String("?") : Astro::ToDateTime(d).ToString("yyyy"); };
		GUI::Label(Rect(0, y, w, HeadingH - 4), cs::Cat("FAMILY  (", living, " living", remembered > 0 ? cs::Cat(", ", remembered, " remembered") : cs::String(), ")"), heading);
		y += HeadingH;
		if (people.empty())
		{
			GUI::Label(Rect(0, y, w, 20), "No one is left of this house.", small);
			return h;
		}
		last = -1;
		for (const auto& c : people)
		{
			int g = generationOf(*c);
			if (g != last)
			{
				last = g;
				double from = people.front()->BornJd + g * GenerationDays;
				GUI::Label(Rect(0, y + 2, w, GenH - 2), cs::Cat("Generation ", g + 1, "  \xC2\xB7  born ", year(from), "-", year(from + GenerationDays - 1)), small);
				y += GenH;
			}
			cs::String wed;
			if (c->Spouse != nullptr)
			{
				bool across = c->Spouse->House != nullptr && c->Spouse->House != house;
				wed = cs::Cat("  \xC2\xB7  wed ", across ? c->Spouse->FullName() : c->Spouse->Given,
					across ? cs::Cat(" of House ", c->Spouse->House->Surname) : cs::String());
			}
			cs::String line = c->Alive()
				? cs::Cat(c->FullName(), "  \xC2\xB7  ", Core::ToString(c->Role), ", ", static_cast<int>(c->AgeYears(jd)), wed)
				: cs::Cat(c->FullName(), "  \xC2\xB7  ", year(c->BornJd), "-", year(c->DiedJd), wed);
			auto row = Rect(0, y, w, FamilyRowH - 4);
			if (GUI::Button(row, GUIContent::none(), entry)) follow = CivPick::Person(c->Id);
			GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), line, c->Alive() ? rowLine : small);
			y += FamilyRowH;
		}
		return h;
	}

	float SocietyScreen::DrawFamilyTree(float y, float w, int personId, Galaxy& galaxy, double jd, bool draw, CivPick& follow)
	{
		static constexpr float TierLabelH = 18, BusH = 12, BoxH = 52, TierGap = 8, BoxGap = 10, MinBoxW = 150, MaxBoxW = 220, NoteH = 22;
		static constexpr float SpouseH = 18;
		static constexpr double GenerationDays = 25 * 365.25;
		static constexpr int Reach = 2;
		static const char* const TierNames[] = { "Two generations up", "A generation up", "Their generation", "A generation down", "Two generations down" };
		auto society = Society::Find(galaxy);
		auto who = society != nullptr ? society->CitizenById(personId) : nullptr;
		if (who == nullptr) return 0;
		bool open = familyTreeOpen;

		// Citizens keep no parents, so as in FAMILY a generation is a 25-year band of births, here counted from the person.
		std::vector<cs::Ref<Citizen>> tiers[2 * Reach + 1];
		if (open)
		{
			auto home = who->House;
			if (home != nullptr)
				for (const auto& m : home->Members)
				{
					if (m == nullptr || m == who || m == who->Spouse) continue;
					// One married in sits beside the one they wed, not in a box of their own.
					if (m->Role == CitizenRole::Spouse && m->Spouse != nullptr && m->Spouse->House == home) continue;
					int g = static_cast<int>(std::floor((m->BornJd - who->BornJd) / GenerationDays + 0.5));
					if (g < -Reach || g > Reach) continue;
					tiers[g + Reach].push_back(m);
				}
			for (auto& t : tiers)
				std::stable_sort(t.begin(), t.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return a->BornJd < b->BornJd; });
			tiers[Reach].insert(tiers[Reach].begin(), who);
		}
		int perRow = Mathf::Max(1, static_cast<int>((w + BoxGap) / (MinBoxW + BoxGap)));

		float h = HeadingH + SectionGap;
		if (open)
		{
			h += NoteH;
			for (const auto& t : tiers)
				if (!t.empty()) h += TierLabelH + BusH + BoxH + TierGap;
		}
		if (!draw) return h;

		GUI::Label(Rect(0, y, Mathf::Max(0.0f, w - 130), HeadingH - 4), "FAMILY TREE", heading);
		if (GUI::Button(Rect(Mathf::Max(0.0f, w - 120), y, 116, 22), open ? "Hide tree" : "Tree", button)) familyTreeOpen = !familyTreeOpen;
		y += HeadingH;
		if (!open) return h;
		GUI::Label(Rect(0, y, w, NoteH - 2), cs::Cat("House ", who->House != nullptr ? who->House->Surname : cs::String("?"),
			" by birth, a generation every 25 years (citizens keep no parents, so a band holds cousins too); the dead are greyed."), small);
		y += NoteH;

		auto year = [](double d) { return d <= 0 ? cs::String("?") : Astro::ToDateTime(d).ToString("yyyy"); };
		float prevBottom = -1;
		for (int i = 0; i < 2 * Reach + 1; i++)
		{
			const auto& t = tiers[i];
			if (t.empty()) continue;
			int count = static_cast<int>(t.size());
			int shown = count > perRow ? Mathf::Max(1, perRow - 1) : count;
			int slots = shown + (count > shown ? 1 : 0);
			float bw = Mathf::Min(MaxBoxW, (w - static_cast<float>(slots - 1) * BoxGap) / static_cast<float>(slots));
			float x0 = (w - (static_cast<float>(slots) * bw + static_cast<float>(slots - 1) * BoxGap)) / 2;

			GUI::Label(Rect(0, y, w, TierLabelH), count > shown ? cs::Cat(TierNames[i], "  (", count, ", ", shown, " shown)") : cs::Cat(TierNames[i], "  (", count, ")"), small);
			y += TierLabelH;
			float busY = y + BusH / 2;
			y += BusH;
			float rowY = y;
			// The lines: down from the band above to this one's bus, and a stub from the bus to each box.
			if (prevBottom >= 0)
			{
				GUI::DrawTexture(Rect(w / 2, prevBottom, 1, Mathf::Max(0.0f, busY - prevBottom)), buttonActiveTexture);
				float first = x0 + bw / 2, lastCx = x0 + static_cast<float>(slots - 1) * (bw + BoxGap) + bw / 2;
				GUI::DrawTexture(Rect(Mathf::Min(first, w / 2), busY, Mathf::Max(lastCx, w / 2) - Mathf::Min(first, w / 2) + 1, 1), buttonActiveTexture);
				for (int s = 0; s < slots; s++)
					GUI::DrawTexture(Rect(x0 + static_cast<float>(s) * (bw + BoxGap) + bw / 2, busY, 1, rowY - busY), buttonActiveTexture);
			}
			for (int s = 0; s < shown; s++)
			{
				const auto& m = t[s];
				float x = x0 + static_cast<float>(s) * (bw + BoxGap);
				bool self = m == who;
				auto keep = GUI::color;
				if (!m->Alive()) GUI::color = Color(keep.r, keep.g, keep.b, keep.a * 0.55f);
				float personH = m->Spouse != nullptr ? BoxH - SpouseH : BoxH;
				if (GUI::Button(Rect(x, rowY, bw, personH), GUIContent::none(), self ? entrySelected : entry) && !self) follow = CivPick::Person(m->Id);
				GUI::Label(Rect(x + 6, rowY + 2, bw - 10, 18), m->FullName(), rowTitle);
				GUI::Label(Rect(x + 6, rowY + 18, bw - 10, 16), m->Alive()
					? cs::Cat(Core::ToString(m->Role), ", ", static_cast<int>(m->AgeYears(jd)))
					: cs::Cat(year(m->BornJd), "-", year(m->DiedJd)), rowLine);
				if (m->Spouse != nullptr)
				{
					auto wed = m->Spouse;
					bool across = wed->House != nullptr && wed->House != m->House;
					if (GUI::Button(Rect(x, rowY + personH, bw, SpouseH), GUIContent::none(), entry)) follow = CivPick::Person(wed->Id);
					GUI::Label(Rect(x + 6, rowY + personH + 1, bw - 10, SpouseH - 2), cs::Cat("= ", across ? wed->FullName() : wed->Given,
						wed->Alive() ? cs::String() : cs::String(" (dead)")), rowLine);
				}
				GUI::color = keep;
			}
			if (count > shown)
			{
				float x = x0 + static_cast<float>(shown) * (bw + BoxGap);
				GUI::Label(Rect(x + 6, rowY + 16, bw - 10, 20), cs::Cat("+", count - shown, " more (the house's FAMILY)"), small);
			}
			y += BoxH + TierGap;
			prevBottom = rowY + BoxH;
		}
		return h;
	}

	void SocietyScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles, UiStyle.h) at full-screen depth, as the Encyclopedia's.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		fieldTexture = S::FieldTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		entryTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		small->wordWrap = true;
		button = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		link = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
		rowTitle = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		rowTitle->clipping = TextClipping::Clip;
		rowLine = S::CaptionStyle(T::Caption);
		rowLine->clipping = TextClipping::Clip;
		good = S::TextStyle(T::Body, S::Good);
		good->clipping = TextClipping::Clip;
		bad = S::TextStyle(T::Body, S::Warning);
		bad->clipping = TextClipping::Clip;
		field = S::FieldStyle(T::Body, fieldTexture);
	}

	void SocietyScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		fieldTexture = nullptr;
		stylesReady = false;
	}
}
