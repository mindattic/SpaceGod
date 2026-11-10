// US-T275: sg.Civ, the Civilization layer (Core's Systems/Civilization) at the console.
//   sg.Civ                         the tallies and the latest events
//   sg.Civ events [n] [kind]       the last n events (of one kind: FeudDeclared, Outbreak, ...)
//   sg.Civ worlds [settlement]     each world's mood (approval, politics, crime, sickness, weariness, faith)
//   sg.Civ orgs [kind]             the organisations (Party, Faith, Syndicate, Union, Bank, Outlet, Team, Charity, ...)
//   sg.Civ person <id>             one person's traits, ambition, reputation and ties
//   sg.Civ keys                    every Prose key the Narrator reads (docs/PROSE.md, "Social narration")
// The module groups add their own verbs (Systems/Civilization/*: kinship, politics, strife, underworld, commerce, culture,
// health, press); see CivHand below.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Economy/Settlement.h"
#include "SimClock.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Galaxy.h"
#include "Systems/Society.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace SpaceGod
{
	// US-T276..US-T283: the module groups' verbs (CivHandCommands.cpp); true when one took the verb.
	bool CivHand(UnitySpaceApp& app, Core::Galaxy& galaxy, Core::Civilization& civ, const FString& verb, const TArray<FString>& args, double jd);
}

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* CivApp()
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

	Core::SocialKind KindNamed(const FString& name)
	{
		for (int i = 1; i < static_cast<int>(Core::SocialKind::Count_); i++)
			if (name.Equals(U(Core::ToString(static_cast<Core::SocialKind>(i))), ESearchCase::IgnoreCase)) return static_cast<Core::SocialKind>(i);
		return Core::SocialKind::None;
	}

	cs::String Pct(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	void Civ(const TArray<FString>& args)
	{
		auto* app = CivApp();
		if (app == nullptr || app->Galaxy == nullptr || app->Clock == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: no game running"));
			return;
		}
		auto& galaxy = *app->Galaxy;
		auto civ = Core::Civilization::Of(galaxy);
		double jd = app->Clock->Jd;
		FString verb = args.Num() > 0 ? args[0].ToLower() : FString();

		if (verb.IsEmpty() || verb == TEXT("events"))
		{
			int n = verb.IsEmpty() ? 15 : (args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*args[1])) : 30);
			auto kind = args.Num() > 2 ? KindNamed(args[2]) : Core::SocialKind::None;
			if (verb.IsEmpty())
			{
				int active = 0;
				for (const auto& o : civ->Orgs)
					if (o->Active()) active++;
				Say(cs::Cat("sg.Civ: ", civ->Worlds.Count(), " worlds watched, ", active, " organisations, ", civ->Events.Count(), " events kept; Prose narration ",
					Core::Narrator::HasSource() ? "on" : "off"));
			}
			for (const auto& e : civ->Latest(n, kind)) Say(cs::Cat("  [", Core::ToString(e->Kind), "] ", e->ToString()));
			return;
		}
		if (verb == TEXT("worlds"))
		{
			FString only = args.Num() > 1 ? args[1] : FString();
			for (const auto& c : civ->Worlds)
			{
				if (!only.IsEmpty() && !U(c->Where->Name).Equals(only, ESearchCase::IgnoreCase)) continue;
				Say(cs::Cat("  ", c->Where->Name, " (", c->Where->Owner != nullptr ? c->Where->Owner->Name : cs::String("?"), "): approval ", cs::Fmt(c->Approval, "0.00"),
					", hawk ", cs::Fmt(c->Hawk, "0.00"), ", liberty ", cs::Fmt(c->Liberty, "0.00"), ", crime ", Pct(c->Crime), ", sick ", Pct(c->Infected),
					", weary ", Pct(c->Weariness), ", unrest ", Pct(c->Where->Unrest), c->Faith != nullptr ? cs::Cat(", faith ", Core::Narrator::OrgName(*c->Faith), " ", Pct(c->FaithShare)) : cs::String(""),
					c->Quarantined(jd) ? cs::String(", QUARANTINED") : cs::String("")));
			}
			return;
		}
		if (verb == TEXT("orgs"))
		{
			FString only = args.Num() > 1 ? args[1] : FString();
			for (const auto& o : civ->Orgs)
			{
				if (!o->Active()) continue;
				if (!only.IsEmpty() && !U(Core::ToString(o->Kind)).Equals(only, ESearchCase::IgnoreCase)) continue;
				Say(cs::Cat("  ", o->Id, " ", Core::ToString(o->Kind), " ", Core::Narrator::OrgName(*o), " of ", o->Seat != nullptr ? o->Seat->Name : cs::String("?"),
					", strength ", Pct(o->Strength), ", funds ", Core::Narrator::Money(o->Funds), ", ", o->Members.Count(), " members",
					o->Leader != nullptr ? cs::Cat(", led by ", o->Leader->FullName()) : cs::String("")));
			}
			return;
		}
		if (verb == TEXT("person"))
		{
			auto society = Core::Society::Find(galaxy);
			auto c = society != nullptr && args.Num() > 1 ? society->CitizenById(FCString::Atoi(*args[1])) : nullptr;
			if (c == nullptr) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: no such person")); return; }
			cs::String traits;
			for (int bit = 1; bit <= (1 << 11); bit <<= 1)
				if (c->Traits & bit) traits = cs::Cat(traits, traits.empty() ? "" : ", ", Core::ToString(static_cast<Core::Trait>(bit)));
			Say(cs::Cat("  ", c->FullName(), " (", c->Id, "), ", static_cast<int>(c->AgeYears(jd)), ", ", Core::ToString(c->Role), "; traits ", traits, "; wants ",
				Core::ToString(c->Goal), "; reputation ", cs::Fmt(c->Reputation, "0.00"), ", skill ", cs::Fmt(c->Skill, "0.00"),
				c->Faith != nullptr ? cs::Cat("; keeps ", Core::Narrator::OrgName(*c->Faith)) : cs::String("")));
			for (const auto& t : c->Ties)
				if (t->Other != nullptr) Say(cs::Cat("      ", Core::ToString(t->Kind), " of ", t->Other->FullName(), " (", t->Other->Id, "), ", cs::Fmt(t->Strength, "0.00"), t->Other->Alive() ? "" : ", dead"));
			for (const auto& o : c->Orgs)
				if (o != nullptr && o->Active()) Say(cs::Cat("      member of ", Core::Narrator::OrgName(*o)));
			return;
		}
		if (verb == TEXT("keys"))
		{
			for (int i = 1; i < static_cast<int>(Core::SocialKind::Count_); i++)
			{
				auto k = static_cast<Core::SocialKind>(i);
				Say(cs::Cat("  ", Core::Narrator::Key(k), "  (", Core::Narrator::Templates(k).Count(), " fallback lines)"));
			}
			return;
		}
		if (CivHand(*app, galaxy, *civ, verb, args, jd)) return;
		UE_LOG(LogSpaceGod, Warning, TEXT("sg.Civ: usage: sg.Civ [events [n] [kind] | worlds [settlement] | orgs [kind] | person <id> | keys | <module verb> ...]"));
	}

	FAutoConsoleCommand GCivCommand(
		TEXT("sg.Civ"),
		TEXT("US-T275: the galaxy's civilization: sg.Civ [events | worlds | orgs | person | keys | module verbs]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Civ(args); }));
}
