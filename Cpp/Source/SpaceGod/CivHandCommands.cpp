// US-T276..US-T283: the SpaceGod's hand in the Civilization layer's modules, as sg.Civ verbs (CivCommand.cpp). Each verb calls
// the module's own hand (it returns "" or why not) and shows a toast. Single player only for now: in co-op a change must
// reach every peer through the session's commands.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
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

namespace SpaceGod
{
	namespace
	{
		FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

		/// The outcome of a hand: a warning with the reason, or the line said and toasted.
		bool Done(UnitySpaceApp& app, const cs::String& why, const cs::String& done)
		{
			if (!why.empty())
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: %s"), *U(why));
			}
			else
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Civ: %s"), *U(done));
				app.ShowToast(done);
			}
			return true;
		}
	}

	bool CivHand(UnitySpaceApp& app, Core::Galaxy& galaxy, Core::Civilization& civ, const FString& verb, const TArray<FString>& args, double jd)
	{
		auto society = Core::Society::Find(galaxy);
		if (society == nullptr) return false;
		auto num = [&](int i) { return args.Num() > i ? FCString::Atoi(*args[i]) : 0; };
		auto house = [&](int i) { return society->HouseById(num(i)); };
		auto person = [&](int i) { return society->CitizenById(num(i)); };
		auto say = [](const cs::String& line) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), *U(line)); };

		// Read-only views of the modules.
		if (verb == TEXT("feuds") && civ.Kinship != nullptr)
		{
			for (const auto& f : civ.Kinship->Feuds)
				if (f->Active())
					say(cs::Cat("  House ", f->A->Surname, " (", f->A->Id, ") v House ", f->B->Surname, " (", f->B->Id, "), over ", f->Cause, ", heat ", cs::Fmt(f->Heat, "0.00"), ", ", f->Incidents, " incidents"));
			return true;
		}

		if (verb == TEXT("regimes") && civ.Politics != nullptr)
		{
			for (const auto& r : civ.Politics->Regimes)
				say(cs::Cat("  ", r->Faction->Name, ": ruled by ", r->Ruler != nullptr ? r->Ruler->FullName() : cs::String("nobody (crisis)"),
					r->Ruling != nullptr ? cs::Cat(" of ", r->Ruling->Name) : cs::String(""), "; hawk ", cs::Fmt(r->Hawk, "0.00"), ", liberty ", cs::Fmt(r->Liberty, "0.00"),
					", approval ", cs::Fmt(r->Approval, "0.00"), cs::Double::IsNaN(r->NextElectionJd) ? cs::String(", no elections") : cs::Cat(", votes in ", static_cast<int>(r->NextElectionJd - jd), " days"),
					", ", r->Coups, " coups"));
			return true;
		}
		if (verb == TEXT("trials") && civ.Politics != nullptr)
		{
			for (const auto& t : civ.Politics->Trials)
				if (t->Accused != nullptr)
					say(cs::Cat("  ", t->Id, " ", t->Accused->FullName(), " for ", Core::ToString(t->Charge), t->Decided ? (t->Bought ? cs::String(": acquitted (bought)") : t->Guilty ? cs::String(": guilty") : cs::String(": acquitted")) : cs::String(": pending")));
			return true;
		}
		// A settlement named by the rest of the words from <paramref name="i"/> on.
		auto world = [&](int i) -> cs::Ref<Core::Settlement>
		{
			if (galaxy.Polities == nullptr) return nullptr;
			FString rest;
			for (int j = i; j < args.Num(); j++) rest += (j > i ? TEXT(" ") : TEXT("")) + args[j];
			for (const auto& s : galaxy.Polities->Settlements())
				if (U(s->Name).Equals(rest, ESearchCase::IgnoreCase)) return s;
			return nullptr;
		};
		if (verb == TEXT("admirals") && civ.Strife != nullptr)
		{
			for (const auto& a : civ.Strife->Admirals)
				say(cs::Cat("  ", a->Faction->Name, ": Admiral ", a->Who->FullName(), " (", a->Who->Id, "), reputation ", cs::Fmt(a->Who->Reputation, "0.00")));
			for (const auto& w : civ.Strife->Walkouts)
				say(cs::Cat("  ", w->Settlement->Name, " is out on strike for ", static_cast<int>(w->EndJd - jd), " more days"));
			return true;
		}
		if (verb == TEXT("crime") && civ.Underworld != nullptr)
		{
			for (const auto& k : civ.Underworld->Contracts)
				if (k->Target != nullptr) say(cs::Cat("  A price on ", k->Target->FullName(), "'s head, due in ", static_cast<int>(k->DueJd - jd), " days"));
			for (const auto& p : civ.Underworld->Prisons)
				say(cs::Cat("  Penal colony at ", p->Where->Name, ": ", p->Inmates.Count(), " inmates, ", p->Breakouts, " breakouts"));
			for (const auto& f : civ.Underworld->Frauds)
				say(cs::Cat("  ", f->Business->Name, " is cooking its books (", cs::Fmt(f->Inflated, "#,0"), " cr claimed)"));
			return true;
		}
		if (verb == TEXT("money") && civ.Commerce != nullptr)
		{
			for (const auto& st : civ.Commerce->Stakes)
				say(cs::Cat("  House ", st->Holder->Surname, " holds ", cs::Fmt(st->Share * 100, "0"), "% of ", st->Business->Name, " (", st->Business->Id, ")"));
			for (const auto& l : civ.Commerce->Loans)
				say(cs::Cat("  ", l->Bank->Name, " lent ", Core::Narrator::Money(l->Principal), " to ", l->Business->Name));
			say(cs::Cat("  ", civ.Commerce->Patents, " patents filed"));
			return true;
		}
		if (verb == TEXT("chronicle") && civ.Culture != nullptr)
		{
			for (const auto& e : civ.Culture->Chronicle) say(cs::Cat("  ", e->ToString()));
			say(cs::Cat("  The league is in season ", civ.Culture->Season, ", round ", civ.Culture->Round));
			return true;
		}
		auto faction = [&](int i) { return galaxy.Polities != nullptr && args.Num() > i ? galaxy.Polities->Find(cs::String(TCHAR_TO_UTF8(*args[i]))) : nullptr; };

		static const TCHAR* const Hands[] = { TEXT("feud"), TEXT("reconcile"), TEXT("fame"), TEXT("seduce"), TEXT("elect"), TEXT("coup"), TEXT("acquit"), TEXT("summit"),
			TEXT("riot"), TEXT("arm"), TEXT("spy"), TEXT("hire"), TEXT("hit"), TEXT("racket"), TEXT("crackdown"),
			TEXT("takeover"), TEXT("bankrun"), TEXT("landmark"), TEXT("preach"), TEXT("festival"), TEXT("rig"), TEXT("rumour"),
			TEXT("plague"), TEXT("cure"), TEXT("disaster"), TEXT("aid"), TEXT("slant"), TEXT("investigate"), TEXT("donate") };
		bool hand = false;
		for (auto* h : Hands)
			if (verb == h) hand = true;
		if (!hand) return false;
		if (Coop::CoopPeer::Current() != nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: the SpaceGod's hand in people's affairs is single-player only for now"));
			return true;
		}

		// US-T277 Kinship.
		if (civ.Kinship != nullptr)
		{
			auto& k = *civ.Kinship;
			if (verb == TEXT("feud")) return Done(app, k.Declare(civ, galaxy, house(1), house(2), jd), "A feud is sworn");
			if (verb == TEXT("reconcile")) return Done(app, k.Reconcile(civ, galaxy, house(1), house(2), jd), "The houses make their peace");
			if (verb == TEXT("fame")) return Done(app, k.Fame(civ, galaxy, person(1), jd), "A star is born");
			if (verb == TEXT("seduce")) return Done(app, k.Seduce(civ, galaxy, person(1), person(2), jd), "An affair begins");
		}
		// US-T278 Politics.
		if (civ.Politics != nullptr)
		{
			auto& p = *civ.Politics;
			if (verb == TEXT("elect")) return Done(app, p.Elect(civ, galaxy, civ.OrgById(num(1)), jd), "The votes are counted");
			if (verb == TEXT("coup")) return Done(app, p.Overthrow(civ, galaxy, person(1), jd), "The government falls");
			if (verb == TEXT("acquit")) return Done(app, p.Acquit(civ, galaxy, p.TrialById(num(1)), jd), "The court acquits");
			if (verb == TEXT("summit")) return Done(app, p.Summit(civ, galaxy, faction(1), faction(2), jd), "The leaders meet");
		}
		// US-T280 Strife.
		if (civ.Strife != nullptr)
		{
			auto& s = *civ.Strife;
			if (verb == TEXT("riot")) return Done(app, s.Incite(civ, galaxy, world(1), jd), "The streets burn");
			if (verb == TEXT("arm")) return Done(app, s.Arm(civ, galaxy, *society, world(1), jd), "Rebels take up arms");
			if (verb == TEXT("spy")) return Done(app, s.Spy(civ, galaxy, faction(1), faction(2), jd), "The spies go to work");
			if (verb == TEXT("hire")) return Done(app, s.Hire(civ, galaxy, civ.OrgById(num(1)), faction(2), jd), "The free company signs on");
		}
		// US-T282 Underworld.
		if (civ.Underworld != nullptr)
		{
			auto& u = *civ.Underworld;
			if (verb == TEXT("hit")) return Done(app, u.Hit(civ, galaxy, person(1), jd), "A price is put on a head");
			if (verb == TEXT("racket")) return Done(app, u.Racket(civ, galaxy, *society, world(1), jd), "The syndicate takes the streets");
			if (verb == TEXT("crackdown")) return Done(app, u.Crackdown(civ, galaxy, world(1), jd), "The police sweep the streets");
		}
		// US-T283 Commerce.
		if (civ.Commerce != nullptr)
		{
			auto& m = *civ.Commerce;
			if (verb == TEXT("takeover")) return Done(app, m.Takeover(civ, galaxy, *society, house(1), society->BusinessById(num(2)), jd), "A hostile takeover");
			if (verb == TEXT("bankrun")) return Done(app, m.Run(civ, galaxy, *society, civ.OrgById(num(1)), jd), "The depositors panic");
			if (verb == TEXT("landmark")) return Done(app, m.Landmark(civ, galaxy, world(1), jd), "A wonder is found");
		}
		// US-T285 Culture.
		if (civ.Culture != nullptr)
		{
			auto& c = *civ.Culture;
			if (verb == TEXT("preach")) return Done(app, c.Preach(civ, galaxy, person(1), jd), "A new faith is preached");
			if (verb == TEXT("festival")) return Done(app, c.Celebrate(civ, galaxy, *society, world(1), jd), "The festival begins");
			if (verb == TEXT("rig")) return Done(app, c.Rig(civ, galaxy, civ.OrgById(num(1)), jd), "The fix is in");
			if (verb == TEXT("rumour")) return Done(app, c.Whisper(civ, galaxy, person(1), jd), "A rumour spreads");
		}
		// US-T286 Health. ("aid <state> <world>": the state's name is one word here, the world the rest.)
		if (civ.Health != nullptr)
		{
			auto& h = *civ.Health;
			if (verb == TEXT("plague")) return Done(app, h.Plague(civ, galaxy, world(1), jd), "A fever breaks out");
			if (verb == TEXT("cure")) return Done(app, h.Cure(civ, galaxy, world(1), jd), "The fever breaks");
			if (verb == TEXT("disaster")) return Done(app, h.Strike(civ, galaxy, *society, world(1), jd), "Catastrophe strikes");
			if (verb == TEXT("aid")) return Done(app, h.Aid(civ, galaxy, faction(1), world(2), jd), "Relief ships are sent");
		}
		// US-T288 Press. ("slant <outlet> for|against|straight")
		if (civ.Press != nullptr)
		{
			auto& p = *civ.Press;
			if (verb == TEXT("slant"))
			{
				int way = args.Num() > 2 ? (args[2].Equals(TEXT("for"), ESearchCase::IgnoreCase) ? 1 : args[2].Equals(TEXT("against"), ESearchCase::IgnoreCase) ? -1 : 0) : 0;
				return Done(app, p.Slant(civ, galaxy, civ.OrgById(num(1)), way, jd), "The editors change their line");
			}
			if (verb == TEXT("investigate")) return Done(app, p.Investigate(civ, galaxy, *society, civ.OrgById(num(1)), jd), "The reporters dig");
			if (verb == TEXT("donate")) return Done(app, p.Donate(civ, galaxy, house(1), jd), "A great gift is made");
		}
		UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: that part of the civilization has not woken yet (let a day pass)"));
		return true;
	}
}
