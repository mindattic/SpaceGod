// US-T276..US-T288: the SpaceGod's hand in the Civilization layer's modules, as sg.Civ verbs (CivCommand.cpp), and the
// modules' read-only views. US-T293: the views and the hands live here once (CivHand.h) for the console, the Society screen
// (UI/SocietyScreen) and co-op alike: every hand goes through UnitySpaceApp::CivHandAsk, which applies it now in single player
// and in co-op sends it into the frames as a "civ" entry every peer carries out on the same frame (so on the same day: the
// hands roll their outcomes from fixed hashes of ids and days, so the same call on the same day does the same everywhere).

#include "CivHand.h"
#include "UnitySpaceApp.h"

#include "SpaceGodLog.h"

#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Civilization/Strife.h"
#include "Systems/Civilization/Underworld.h"
#include "Systems/Civilization/Commerce.h"
#include "Systems/Civilization/Culture.h"
#include "Systems/Civilization/Health.h"
#include "Systems/Civilization/Press.h"
#include "Systems/Civilization/Narrator.h"
#include "Economy/Settlement.h"
#include "Systems/Polities.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

#include <cstdlib>

namespace SpaceGod
{
	cs::String CivPick::Arg() const
	{
		switch (Kind)
		{
		case CivPickKind::World: return Name;
		case CivPickKind::Faction:
		{
			cs::String s = Name;
			for (auto& c : s)
				if (c == ' ') c = '_';
			return s;
		}
		case CivPickKind::None: return cs::String();
		default: return cs::Cat(Id);
		}
	}

	namespace CivHands
	{
		const std::vector<const char*>& Verbs()
		{
			static const std::vector<const char*> verbs = { "feud", "reconcile", "fame", "seduce", "elect", "coup", "acquit", "summit",
				"riot", "arm", "spy", "hire", "hit", "racket", "crackdown",
				"takeover", "bankrun", "landmark", "preach", "festival", "rig", "rumour",
				"plague", "cure", "disaster", "aid", "slant", "investigate", "donate",
				"fund", "bribe", "ruin", "send", "expose" }; // US-T385: Society's own hands (sg.Society's), on the screen and in co-op
			return verbs;
		}

		bool IsHand(const cs::String& verb)
		{
			for (const char* v : Verbs())
				if (verb == v) return true;
			return false;
		}

		const std::vector<const char*>& Views()
		{
			static const std::vector<const char*> views = { "feuds", "regimes", "trials", "admirals", "crime", "money", "chronicle" };
			return views;
		}

		std::vector<cs::String> Words(const cs::String& line)
		{
			std::vector<cs::String> words;
			cs::String w;
			for (char c : line)
			{
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				{
					if (!w.empty()) words.push_back(w);
					w.clear();
				}
				else w += c;
			}
			if (!w.empty()) words.push_back(w);
			return words;
		}

		bool View(const cs::String& view, Core::Galaxy& galaxy, Core::Civilization& civ, double jd, cs::List<CivRow>& rows)
		{
			(void)galaxy;
			auto add = [&rows](const cs::String& text, const CivPick& pick = CivPick()) { rows.Add(CivRow{ text, pick }); };
			if (view == "feuds" && civ.Kinship != nullptr)
			{
				for (const auto& f : civ.Kinship->Feuds)
					if (f->Active())
						add(cs::Cat("House ", f->A->Surname, " (", f->A->Id, ") v House ", f->B->Surname, " (", f->B->Id, "), over ", f->Cause, ", heat ", cs::Fmt(f->Heat, "0.00"), ", ", f->Incidents, " incidents"),
							CivPick::House(f->A->Id));
				return true;
			}
			if (view == "regimes" && civ.Politics != nullptr)
			{
				for (const auto& r : civ.Politics->Regimes)
					add(cs::Cat(r->Faction->Name, ": ruled by ", r->Ruler != nullptr ? r->Ruler->FullName() : cs::String("nobody (crisis)"),
						r->Ruling != nullptr ? cs::Cat(" of ", r->Ruling->Name) : cs::String(""), "; hawk ", cs::Fmt(r->Hawk, "0.00"), ", liberty ", cs::Fmt(r->Liberty, "0.00"),
						", approval ", cs::Fmt(r->Approval, "0.00"), cs::Double::IsNaN(r->NextElectionJd) ? cs::String(", no elections")
							: r->NextElectionJd - jd < 1 ? cs::String(", votes now") // US-T383
							: cs::Cat(r->Called ? ", election called, votes in " : ", votes in ", static_cast<int>(r->NextElectionJd - jd), " days"),
						", ", r->Coups, " coups"), CivPick::Faction(r->Faction->Name));
				return true;
			}
			if (view == "trials" && civ.Politics != nullptr)
			{
				for (const auto& t : civ.Politics->Trials)
					if (t->Accused != nullptr)
						add(cs::Cat(t->Id, " ", t->Accused->FullName(), " for ", Core::ToString(t->Charge), t->Decided ? (t->Bought ? cs::String(": acquitted (bought)") : t->Guilty ? cs::String(": guilty") : cs::String(": acquitted")) : cs::String(": pending")),
							CivPick::Trial(t->Id));
				return true;
			}
			if (view == "admirals" && civ.Strife != nullptr)
			{
				for (const auto& a : civ.Strife->Admirals)
					add(cs::Cat(a->Faction->Name, ": Admiral ", a->Who->FullName(), " (", a->Who->Id, "), reputation ", cs::Fmt(a->Who->Reputation, "0.00")), CivPick::Person(a->Who->Id));
				for (const auto& w : civ.Strife->Walkouts)
					add(cs::Cat(w->Settlement->Name, " is out on strike for ", static_cast<int>(w->EndJd - jd), " more days"), CivPick::World(w->Settlement->Name));
				return true;
			}
			if (view == "crime" && civ.Underworld != nullptr)
			{
				for (const auto& k : civ.Underworld->Contracts)
					if (k->Target != nullptr) add(cs::Cat("A price on ", k->Target->FullName(), "'s head, due in ", static_cast<int>(k->DueJd - jd), " days"), CivPick::Person(k->Target->Id));
				for (const auto& p : civ.Underworld->Prisons)
					add(cs::Cat("Penal colony at ", p->Where->Name, ": ", p->Inmates.Count(), " inmates, ", p->Breakouts, " breakouts"), CivPick::World(p->Where->Name));
				for (const auto& f : civ.Underworld->Frauds)
					add(cs::Cat(f->Business->Name, " is cooking its books (", cs::Fmt(f->Inflated, "#,0"), " cr claimed)"), CivPick::Business(f->Business->Id));
				return true;
			}
			if (view == "money" && civ.Commerce != nullptr)
			{
				for (const auto& st : civ.Commerce->Stakes)
					add(cs::Cat("House ", st->Holder->Surname, " holds ", cs::Fmt(st->Share * 100, "0"), "% of ", st->Business->Name, " (", st->Business->Id, ")"), CivPick::House(st->Holder->Id));
				for (const auto& l : civ.Commerce->Loans)
					add(cs::Cat(l->Bank->Name, " lent ", Core::Narrator::Money(l->Principal), " to ", l->Business->Name), CivPick::Org(l->Bank->Id));
				add(cs::Cat(civ.Commerce->Patents, " patents filed"));
				return true;
			}
			if (view == "chronicle" && civ.Culture != nullptr)
			{
				for (const auto& e : civ.Culture->Chronicle) add(e->ToString());
				add(cs::Cat("The league is in season ", civ.Culture->Season, ", round ", civ.Culture->Round));
				return true;
			}
			return false;
		}

		cs::String Apply(Core::Galaxy& galaxy, Core::Civilization& civ, const cs::String& line, double jd, cs::String& done)
		{
			done.clear();
			auto args = Words(line);
			if (args.empty()) return "no verb";
			cs::String verb = cs::Strings::ToLowerInvariant(args[0]);
			if (!IsHand(verb)) return cs::Cat("'", verb, "' is not a hand");
			auto society = Core::Society::Find(galaxy);
			if (society == nullptr) return "the galaxy's people have not woken yet (let a day pass)";
			auto num = [&](size_t i) { return args.size() > i ? std::atoi(args[i].c_str()) : 0; };
			auto house = [&](size_t i) { return society->HouseById(num(i)); };
			auto person = [&](size_t i) { return society->CitizenById(num(i)); };
			// A settlement named by the rest of the words from i on.
			auto world = [&](size_t i) -> cs::Ref<Core::Settlement>
			{
				if (galaxy.Polities == nullptr) return nullptr;
				cs::String rest;
				for (size_t j = i; j < args.size(); j++) rest = cs::Cat(rest, j > i ? " " : "", args[j]);
				cs::String want = cs::Strings::ToUpperInvariant(rest);
				for (const auto& s : galaxy.Polities->Settlements())
					if (cs::Strings::ToUpperInvariant(s->Name) == want) return s;
				return nullptr;
			};
			// A state by its name in one word (its spaces as '_': "Your_fleet").
			auto faction = [&](size_t i) -> cs::Ref<Core::Faction>
			{
				if (galaxy.Polities == nullptr || args.size() <= i) return nullptr;
				if (auto f = galaxy.Polities->Find(args[i])) return f;
				cs::String spaced = args[i];
				for (auto& c : spaced)
					if (c == '_') c = ' ';
				return galaxy.Polities->Find(spaced);
			};
			auto said = [&done](const cs::String& why, const char* what) -> cs::String { if (why.empty()) done = what; return why; };

			// US-T385: Society's own hands (sg.Society's, which refuses co-op; here they go through CivHandAsk like the rest and
			// roll nothing but the day's hashes, so every peer does the same). "fund <business> <credits>", "send <person> <world>".
			if (verb == "fund") return said(society->Fund(galaxy, society->BusinessById(num(1)), args.size() > 2 ? std::atof(args[2].c_str()) : 0.0, jd), "An anonymous backer pays in");
			if (verb == "bribe") return said(society->BribeFor(galaxy, society->BusinessById(num(1)), jd), "A governor is bought");
			if (verb == "ruin") return said(society->Ruin(galaxy, society->BusinessById(num(1)), jd), "A run on the firm");
			if (verb == "send") return said(society->Send(galaxy, person(1), world(2), jd), "A business trip is booked");
			if (verb == "expose") return said(society->Expose(galaxy, society->BribeById(num(1)), jd), "A bribe comes to light");

			// US-T277 Kinship.
			if (civ.Kinship != nullptr)
			{
				auto& k = *civ.Kinship;
				if (verb == "feud") return said(k.Declare(civ, galaxy, house(1), house(2), jd), "A feud is sworn");
				if (verb == "reconcile") return said(k.Reconcile(civ, galaxy, house(1), house(2), jd), "The houses make their peace");
				if (verb == "fame") return said(k.Fame(civ, galaxy, person(1), jd), "A star is born");
				if (verb == "seduce") return said(k.Seduce(civ, galaxy, person(1), person(2), jd), "An affair begins");
			}
			// US-T278 Politics.
			if (civ.Politics != nullptr)
			{
				auto& p = *civ.Politics;
				if (verb == "elect") return said(p.Elect(civ, galaxy, civ.OrgById(num(1)), jd), "The votes are counted");
				if (verb == "coup") return said(p.Overthrow(civ, galaxy, person(1), jd), "The government falls");
				if (verb == "acquit") return said(p.Acquit(civ, galaxy, p.TrialById(num(1)), jd), "The court acquits");
				if (verb == "summit") return said(p.Summit(civ, galaxy, faction(1), faction(2), jd), "The leaders meet");
			}
			// US-T280 Strife.
			if (civ.Strife != nullptr)
			{
				auto& s = *civ.Strife;
				if (verb == "riot") return said(s.Incite(civ, galaxy, world(1), jd), "The streets burn");
				if (verb == "arm") return said(s.Arm(civ, galaxy, *society, world(1), jd), "Rebels take up arms");
				if (verb == "spy") return said(s.Spy(civ, galaxy, faction(1), faction(2), jd), "The spies go to work");
				if (verb == "hire") return said(s.Hire(civ, galaxy, civ.OrgById(num(1)), faction(2), jd), "The free company signs on");
			}
			// US-T282 Underworld.
			if (civ.Underworld != nullptr)
			{
				auto& u = *civ.Underworld;
				if (verb == "hit") return said(u.Hit(civ, galaxy, person(1), jd), "A price is put on a head");
				if (verb == "racket") return said(u.Racket(civ, galaxy, *society, world(1), jd), "The syndicate takes the streets");
				if (verb == "crackdown") return said(u.Crackdown(civ, galaxy, world(1), jd), "The police sweep the streets");
			}
			// US-T283 Commerce.
			if (civ.Commerce != nullptr)
			{
				auto& m = *civ.Commerce;
				if (verb == "takeover") return said(m.Takeover(civ, galaxy, *society, house(1), society->BusinessById(num(2)), jd), "A hostile takeover");
				if (verb == "bankrun") return said(m.Run(civ, galaxy, *society, civ.OrgById(num(1)), jd), "The depositors panic");
				if (verb == "landmark") return said(m.Landmark(civ, galaxy, world(1), jd), "A wonder is found");
			}
			// US-T285 Culture.
			if (civ.Culture != nullptr)
			{
				auto& c = *civ.Culture;
				if (verb == "preach") return said(c.Preach(civ, galaxy, person(1), jd), "A new faith is preached");
				if (verb == "festival") return said(c.Celebrate(civ, galaxy, *society, world(1), jd), "The festival begins");
				if (verb == "rig") return said(c.Rig(civ, galaxy, civ.OrgById(num(1)), jd), "The fix is in");
				if (verb == "rumour") return said(c.Whisper(civ, galaxy, person(1), jd), "A rumour spreads");
			}
			// US-T286 Health. ("aid <state> <world>": the state's name is one word here, the world the rest.)
			if (civ.Health != nullptr)
			{
				auto& h = *civ.Health;
				if (verb == "plague") return said(h.Plague(civ, galaxy, world(1), jd), "A fever breaks out");
				if (verb == "cure") return said(h.Cure(civ, galaxy, world(1), jd), "The fever breaks");
				if (verb == "disaster") return said(h.Strike(civ, galaxy, *society, world(1), jd), "Catastrophe strikes");
				if (verb == "aid") return said(h.Aid(civ, galaxy, faction(1), world(2), jd), "Relief ships are sent");
			}
			// US-T288 Press. ("slant <outlet> for|against|straight")
			if (civ.Press != nullptr)
			{
				auto& p = *civ.Press;
				if (verb == "slant")
				{
					cs::String to = args.size() > 2 ? cs::Strings::ToLowerInvariant(args[2]) : cs::String();
					int way = to == "for" ? 1 : to == "against" ? -1 : 0;
					return said(p.Slant(civ, galaxy, civ.OrgById(num(1)), way, jd), "The editors change their line");
				}
				if (verb == "investigate") return said(p.Investigate(civ, galaxy, *society, civ.OrgById(num(1)), jd), "The reporters dig");
				if (verb == "donate") return said(p.Donate(civ, galaxy, house(1), jd), "A great gift is made");
			}
			return "that part of the civilization has not woken yet (let a day pass)";
		}
	}

	namespace
	{
		FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	}

	// sg.Civ's module verbs (declared in CivCommand.cpp): the views printed, the hands asked through the app; true when one
	// took the verb.
	bool CivHand(UnitySpaceApp& app, Core::Galaxy& galaxy, Core::Civilization& civ, const FString& verb, const TArray<FString>& args, double jd)
	{
		cs::String v(TCHAR_TO_UTF8(*verb));
		cs::List<CivRow> rows;
		if (CivHands::View(v, galaxy, civ, jd, rows))
		{
			for (const auto& r : rows) UE_LOG(LogSpaceGod, Display, TEXT("  %s"), *U(r.Text));
			return true;
		}
		if (!CivHands::IsHand(v)) return false;
		cs::String line = v;
		for (int i = 1; i < args.Num(); i++) line = cs::Cat(line, " ", cs::String(TCHAR_TO_UTF8(*args[i])));
		bool sent = false;
		cs::String done;
		cs::String why = app.CivHandAsk(line, sent, done);
		if (!why.empty())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: %s"), *U(why));
		}
		else if (sent)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Civ: sent to the party: %s"), *U(line));
		}
		else
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Civ: %s"), *U(done));
		}
		return true;
	}
}
