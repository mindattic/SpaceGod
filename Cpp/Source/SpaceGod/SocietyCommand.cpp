// US-T273: sg.Society, the galaxy's people (Core's Society) and the SpaceGod's hand in their affairs.
//   sg.Society                      the latest feed (news, commercials, spam, messages) and the running tallies
//   sg.Society feed [n] [kind]      the last n feed items (kind: news, ad, spam, message)
//   sg.Society houses [settlement]  the families (of one settlement), their members and businesses
//   sg.Society business             the trading businesses, richest first
//   sg.Society trips                the trips planned or under way
//   sg.Society bribes               the bribes on the books
//   sg.Society expose <bribe>       bring a bribe to light
//   sg.Society bribe <business>     have a business buy its governor
//   sg.Society send <person> <settlement name>   put a business trip in someone's head
//   sg.Society fund <business> <credits>         an anonymous backer
//   sg.Society ruin <business>                   a run on a business
// The SpaceGod's hand is single-player only for now: in co-op a change must reach every peer through the session's commands.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Economy/Settlement.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* SocietyApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	cs::String S(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

	void Say(const cs::String& line) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), *U(line)); }

	void Feed(const Core::Society& society, int n, const FString& kind)
	{
		int shown = 0;
		cs::List<cs::Ref<Core::FeedItem>> picked;
		for (int i = society.Feed.Count() - 1; i >= 0 && shown < n; i--)
		{
			const auto& f = society.Feed[i];
			bool match = kind.IsEmpty()
				|| (kind.Equals(TEXT("news"), ESearchCase::IgnoreCase) && f->Kind == Core::FeedKind::News)
				|| (kind.Equals(TEXT("ad"), ESearchCase::IgnoreCase) && f->Kind == Core::FeedKind::Commercial)
				|| (kind.Equals(TEXT("spam"), ESearchCase::IgnoreCase) && f->Kind == Core::FeedKind::Spam)
				|| (kind.Equals(TEXT("message"), ESearchCase::IgnoreCase) && f->Kind == Core::FeedKind::Message);
			if (!match) continue;
			picked.Insert(0, f);
			shown++;
		}
		for (const auto& f : picked) Say(cs::Cat("  ", f->ToString()));
	}

	void Summary(const Core::Society& s)
	{
		int trading = 0;
		for (const auto& b : s.Businesses)
			if (b->Trading()) trading++;
		Say(cs::Cat("sg.Society: ", s.HousesOf(nullptr).Count(), " houses, ", trading, " businesses trading, ", s.ActiveTrips().Count(), " trips under way; ",
			s.Births, " births, ", s.Deaths, " deaths, ", s.Weddings, " weddings, ", s.TripsTaken, " trips taken (", s.TripsCancelled, " called off, ", s.Robberies, " robbed), ",
			s.Scandals, " scandals, ", s.Acquisitions, " buyouts, ", s.Bankruptcies, " bankruptcies, ", s.BranchesOpened, " offices opened, ", s.BranchesClosed, " closed"));
	}

	cs::Ref<Core::Settlement> SettlementNamed(const Core::Galaxy& galaxy, const cs::String& name)
	{
		if (galaxy.Polities == nullptr) return nullptr;
		FString want = U(name);
		for (const auto& s : galaxy.Polities->Settlements())
			if (U(s->Name).Equals(want, ESearchCase::IgnoreCase)) return s;
		return nullptr;
	}

	void Society(const TArray<FString>& args)
	{
		auto* app = SocietyApp();
		if (app == nullptr || app->Galaxy == nullptr || app->Clock == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Society: no game running"));
			return;
		}
		auto& galaxy = *app->Galaxy;
		auto society = Core::Society::Of(galaxy);
		double jd = app->Clock->Jd;
		FString verb = args.Num() > 0 ? args[0].ToLower() : FString();

		if (verb.IsEmpty())
		{
			Summary(*society);
			Feed(*society, 15, FString());
			return;
		}
		if (verb == TEXT("feed"))
		{
			Feed(*society, args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*args[1])) : 30, args.Num() > 2 ? args[2] : FString());
			return;
		}
		if (verb == TEXT("houses"))
		{
			cs::Ref<Core::Settlement> only;
			if (args.Num() > 1)
			{
				FString rest;
				for (int i = 1; i < args.Num(); i++) rest += (i > 1 ? TEXT(" ") : TEXT("")) + args[i];
				only = SettlementNamed(galaxy, S(rest));
				if (only == nullptr) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Society: no settlement called %s"), *rest); return; }
			}
			for (const auto& h : society->HousesOf(only))
			{
				Say(cs::Cat("  House ", h->Surname, " (", h->Id, ") of ", h->Home != nullptr ? h->Home->Name : cs::String("?"), ", wealth ", cs::Fmt(h->Wealth, "#,0"),
					h->Business != nullptr ? cs::Cat(", runs ", Core::Society::Describe(h->Business)) : cs::String("")));
				for (const auto& c : h->Members)
					if (c->Alive())
						Say(cs::Cat("      ", c->Id, " ", c->FullName(), ", ", static_cast<int>(c->AgeYears(jd)), ", ", Core::ToString(c->Role),
							c->Spouse != nullptr ? cs::Cat(", married to ", c->Spouse->FullName()) : cs::String(""), c->TripId != 0 ? cs::String(", travelling") : cs::String("")));
			}
			return;
		}
		if (verb == TEXT("business"))
		{
			cs::List<cs::Ref<Core::Business>> list;
			for (const auto& b : society->Businesses)
				if (b->Trading()) list.Add(b);
			list.Sort([](const cs::Ref<Core::Business>& a, const cs::Ref<Core::Business>& b) { return a->Capital > b->Capital ? -1 : (a->Capital < b->Capital ? 1 : a->Id - b->Id); });
			for (const auto& b : list) Say(cs::Cat("  ", Core::Society::Describe(b)));
			return;
		}
		if (verb == TEXT("trips"))
		{
			for (const auto& t : society->ActiveTrips()) Say(cs::Cat("  ", t->Id, " ", Core::Society::Describe(t, jd)));
			return;
		}
		if (verb == TEXT("bribes"))
		{
			for (const auto& b : society->Bribes) Say(cs::Cat("  ", Core::Society::Describe(b)));
			return;
		}

		// The SpaceGod's hand.
		if (Coop::CoopPeer::Current() != nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Society: the SpaceGod's hand in people's affairs is single-player only for now"));
			return;
		}
		int id = args.Num() > 1 ? FCString::Atoi(*args[1]) : 0;
		cs::String why;
		cs::String done;
		if (verb == TEXT("expose")) { auto b = society->BribeById(id); why = society->Expose(galaxy, b, jd); done = cs::Cat("Exposed: ", Core::Society::Describe(b)); }
		else if (verb == TEXT("bribe")) { auto b = society->BusinessById(id); why = society->BribeFor(galaxy, b, jd); done = cs::Cat("Bribe paid for ", b != nullptr ? b->Name : cs::String("?")); }
		else if (verb == TEXT("fund"))
		{
			auto b = society->BusinessById(id);
			double credits = args.Num() > 2 ? FCString::Atod(*args[2]) : 0;
			why = society->Fund(galaxy, b, credits, jd);
			done = cs::Cat("Funded ", b != nullptr ? b->Name : cs::String("?"));
		}
		else if (verb == TEXT("ruin")) { auto b = society->BusinessById(id); why = society->Ruin(galaxy, b, jd); done = cs::Cat("Ruined ", b != nullptr ? b->Name : cs::String("?")); }
		else if (verb == TEXT("send"))
		{
			auto c = society->CitizenById(id);
			FString rest;
			for (int i = 2; i < args.Num(); i++) rest += (i > 2 ? TEXT(" ") : TEXT("")) + args[i];
			why = society->Send(galaxy, c, SettlementNamed(galaxy, S(rest)), jd);
			done = cs::Cat(c != nullptr ? c->FullName() : cs::String("?"), " books a trip to ", S(rest));
		}
		else
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Society: usage: sg.Society [feed [n] [news|ad|spam|message] | houses [settlement] | business | trips | bribes | expose <bribe> | bribe <business> | send <person> <settlement> | fund <business> <credits> | ruin <business>]"));
			return;
		}
		if (!why.empty())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Society: %s"), *U(why));
			return;
		}
		Say(cs::Cat("sg.Society: ", done));
		app->ShowToast(done);
	}

	FAutoConsoleCommand GSocietyCommand(
		TEXT("sg.Society"),
		TEXT("US-T273: the galaxy's people and the SpaceGod's hand: sg.Society [feed | houses | business | trips | bribes | expose | bribe | send | fund | ruin]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Society(args); }));
}
