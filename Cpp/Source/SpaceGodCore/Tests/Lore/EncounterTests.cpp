// EncounterTests: unit checks of US-T239's Prose encounters and scan tabs (Lore/Encounters.h, Lore/ScanTabs.h) on the
// test-only fixture/LoreEncounters (fixture texts, never story), and that the tag vocabulary the game executes is exactly the
// one Data/Prose/prose.config.json gives Prose.
//
//   EncounterTests.exe <fixture folder> <prose.config.json>     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Comms/Comms.h"
#include "Cs/Cs.h"
#include "Lore/Encounters.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Lore/ScanTabs.h"
#include "Shared/Json/MiniJson.h"

using namespace SpaceGod::Core;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const std::string& what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what.c_str());
	}

	void Vocabulary(const cs::String& configPath)
	{
		const auto& tags = Encounters::Tags();
		Check(tags.Count() == 12, "one tag per verb a player can choose");
		for (const auto& t : tags)
		{
			CommsVerb v;
			Check(Encounters::VerbOfTag(t, v) && cs::String(Encounters::TagOf(v)) == t, cs::Cat("tag ", t, " names a verb and back"));
		}
		CommsVerb v;
		Check(!Encounters::VerbOfTag("pay", v), "a tag the game cannot do names no verb");
		Check(cs::String(Encounters::TagOf(CommsVerb::Hail)).empty(), "the hail is how an encounter starts, not a choice");

		std::ifstream f(configPath, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		Check(!text.empty(), "prose.config.json is readable");
		if (text.empty()) return;
		auto config = SpaceGod::Shared::Json::MiniJson::ParseObject(text);
		auto listed = config ? config->GetArray("tags") : nullptr;
		Check(listed && listed->Count() == tags.Count(), "prose.config.json lists exactly the game's tags");
		for (int i = 0; listed && i < listed->Count() && i < tags.Count(); i++)
		{
			auto s = (*listed)[i].AsString();
			Check(s != nullptr && *s == tags[i], cs::Cat("prose.config.json tag ", i, " is ", tags[i]));
		}
	}

	void Lookup(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/LoreEncounters");
		Check(lib.Problems().Count() == 0, "fixture/LoreEncounters loads without problems");

		auto keys = Encounters::Keys(EncounterKind::Pirate, LoreKeys::Pilot("Test Pilot"), LoreKeys::Faction("Test Clan"));
		Check(keys.Count() == 4, "pilot, faction, generic, generic hail");
		Check(keys.Count() == 4 && keys[0] == "pilot-test-pilot/encounter-pirate" && keys[1] == "faction-test-clan/encounter-pirate" &&
			keys[2] == "spacegod/encounter-pirate" && keys[3] == "spacegod/encounter-hail", "keys, most specific first");
		Check(Encounters::Keys(EncounterKind::Hail, "", "").Count() == 1, "the generic hail has no further fallback; empty ids are skipped");

		const LoreText* t = Encounters::Find(lib, keys);
		Check(t != nullptr && t->Key == "faction-test-clan/encounter-pirate", "no pilot text: the faction's conversation");
		t = Encounters::Find(lib, Encounters::Keys(EncounterKind::Pirate, "pilot-nobody", "faction-nobody"));
		Check(t != nullptr && t->Key == "spacegod/encounter-pirate", "neither: the generic pirate conversation");
		t = Encounters::Find(lib, Encounters::Keys(EncounterKind::Trader, "", ""));
		Check(t != nullptr && t->Key == "spacegod/encounter-hail", "no trader text: the generic hail");
		auto empty = LoreLibrary::Load(fixture + "/no-such-folder");
		Check(Encounters::Find(empty, keys) == nullptr, "no conversation at all: none (the panel runs as before)");
		Check(Encounters::Find(lib, { "planet-sol-test-world" }) == nullptr, "a description is never an encounter");
	}

	void Nodes(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/LoreEncounters");
		const LoreText* t = lib.Find("spacegod/encounter-pirate");
		Check(t != nullptr, "the generic pirate conversation");
		if (t == nullptr) return;
		Check(Encounters::Start(*t) == "open", "its start");
		EncounterNode open, ask, none;
		Check(Encounters::Node(*t, "open", open) && open.Next == "ask" && open.Choices.Count() == 0, "a node that moves on");
		Check(!Encounters::Node(*t, "nowhere", none), "no such node");
		Check(Encounters::Node(*t, "ask", ask) && ask.Choices.Count() == 5, "a node with choices");
		if (ask.Choices.Count() != 5) return;
		Check(ask.Choices[0].HasAction && ask.Choices[0].Action == CommsVerb::Threaten && ask.Choices[0].Next == "$outcome", "threaten is the Threaten verb");
		Check(ask.Choices[1].HasAction && ask.Choices[1].Action == CommsVerb::AskTrade, "trade is AskTrade");
		Check(!ask.Choices[3].HasAction && ask.Choices[3].Tags.Count() == 1, "an unknown tag is kept but does nothing");
		Check(ask.Choices[4].Action == CommsVerb::SignOff, "sign-off");

		// What the game offers decides which choices show: here a pirate that can be threatened and traded with, not one
		// that has struck its colours.
		cs::List<CommsVerb> verbs{ CommsVerb::Hail, CommsVerb::AskTrade, CommsVerb::Threaten, CommsVerb::SignOff };
		auto offered = Encounters::Offered(ask, verbs);
		Check(offered.Count() == 4 && offered[0] == 0 && offered[1] == 1 && offered[2] == 3 && offered[3] == 4,
			"a choice whose verb is not on offer is hidden; a plain one always shows");
		cs::List<CommsVerb> surrender{ CommsVerb::AcceptSurrender, CommsVerb::SignOff };
		offered = Encounters::Offered(ask, surrender);
		Check(offered.Count() == 3 && offered[0] == 2, "a struck hull: only its terms and the plain choices");

		Check(cs::String(Encounters::OutcomeNode(CommsOutcome::Complied)) == "complied", "$outcome after a threat that was obeyed");
		Check(cs::String(Encounters::OutcomeNode(CommsOutcome::SurrenderTaken)) == "surrender-taken", "outcome names are slugs");
		Check(cs::String(Encounters::OutcomeNode(CommsOutcome::None)).empty(), "no outcome, no node");
		Check(Encounters::IsPlayerSpeaker("$player", "") && Encounters::IsPlayerSpeaker("pilot-me", "pilot-me") && !Encounters::IsPlayerSpeaker("$them", "pilot-me"),
			"who speaks for the player");
	}

	void Placeholders(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/LoreEncounters");
		const LoreText* t = lib.Find("spacegod/encounter-pirate");
		EncounterNode open, talk;
		if (t == nullptr || !Encounters::Node(*t, "open", open) || !Encounters::Node(*t, "talk", talk))
		{
			Check(false, "the fixture's nodes");
			return;
		}
		EncounterValues v;
		v["pilot"] = "Pirate fighter Sefa Voss";
		v["ship"] = "Kestrel";
		v["range"] = "10 klicks";
		v["my-cargo"] = "40 t of ore";
		v["credits"] = "12,400";
		Check(Encounters::Fill(open.Line, v) == "Fixture pirate Pirate fighter Sefa Voss of the Kestrel, 10 klicks out. You carry 40 t of ore and 12,400 credits.",
			"placeholders filled from the game's values");
		Check(Encounters::Fill(talk.Line, v) == "Fixture {unknown} stays and Test Clan is named.", "an unknown placeholder stays; an entity reference reads its id");
		Check(Encounters::Fill(talk.Line, v, [](const cs::String& id) { return id == "faction-test-clan" ? cs::String("the Test Clan") : cs::String(); }) ==
			"Fixture {unknown} stays and the Test Clan is named.", "an entity the game resolves reads its name");
		Check(Encounters::Fill("{unclosed", v) == "{unclosed", "a lone brace is text");
		Check(Encounters::EntityWords("pilot-tomas-brennan") == "Tomas Brennan", "an entity id's words");
		Check(Encounters::Placeholders().Count() == 14, "the documented placeholder set");
		for (const auto& name : Encounters::Placeholders())
			Check(Encounters::Fill(cs::Cat("{", name, "}"), Encounters::Values(CommsContext(), CommsParty())) != cs::Cat("{", name, "}"),
				cs::Cat("{", name, "} is always filled (empty with nobody to talk to)"));
	}

	void Scan(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/LoreEncounters");
		const cs::String world = LoreKeys::Body(BodyKind::Planet, "Sol System", "Test World");
		Check(ScanTabs::Key(world, ScanTabs::Overview) == "planet-sol-test-world", "the overview's narrative is the entity's description");
		Check(ScanTabs::Key(world, "history") == "planet-sol-test-world/history", "another tab's is <entity>/<tab>");
		Check(ScanTabs::Key("", "history").empty(), "no entity, no key");
		Check(lib.Long(ScanTabs::Key(world, ScanTabs::Overview), "") == "Fixture world overview.", "the overview text");
		Check(lib.Long(ScanTabs::Key(world, "history"), ScanTabs::Placeholder("history")) == "Fixture world history.", "a tab's text");
		Check(lib.Long(ScanTabs::Key(world, "culture"), ScanTabs::Placeholder("culture")) == "No record on file.", "no text: the placeholder");
		Check(ScanTabs::Placeholder(ScanTabs::Overview).empty(), "the overview keeps its own fallback");

		Check(ScanTabs::OfBody(BodyKind::Star) == ScanSubject::Star && ScanTabs::OfBody(BodyKind::Moon) == ScanSubject::World &&
			ScanTabs::OfBody(BodyKind::Station) == ScanSubject::Station, "a body's subject");
		for (int s = 0; s <= static_cast<int>(ScanSubject::Pilot); s++)
		{
			const auto& tabs = ScanTabs::Of(static_cast<ScanSubject>(s));
			Check(tabs.Count() >= 3 && cs::String(tabs[0].Id) == ScanTabs::Overview, cs::Cat(ScanTabs::Name(static_cast<ScanSubject>(s)), " starts with the overview"));
			for (const auto& tab : tabs) Check(LoreKeys::Slug(tab.Id) == tab.Id, cs::Cat("tab id ", tab.Id, " is a key-safe slug"));
		}
		Check(ScanTabs::Has(ScanSubject::World, "industry") && ScanTabs::Has(ScanSubject::Ship, "cargo") && ScanTabs::Has(ScanSubject::Warship, "orders") &&
			!ScanTabs::Has(ScanSubject::Pilot, "cargo"), "tabs by subject");

		auto sol = BodyCatalog::CreateSolSystem();
		auto earth = (*sol)["Earth"];
		auto moon = (*sol)["Moon"];
		Check(std::abs(ScanFacts::SurfaceGravityG(*earth) - 1.0) < 0.01, "Earth's surface gravity is 1 g");
		double te = ScanFacts::EquilibriumK(*sol, earth->Index);
		Check(te > 270 && te < 285, "Earth's equilibrium temperature is about 279 K");
		Check(std::abs(ScanFacts::EquilibriumK(*sol, moon->Index) - te) < 1e-9, "a moon shares its planet's distance from the Sun");
		Check(std::isnan(ScanFacts::EquilibriumK(*sol, (*sol)["Sun"]->Index)), "a star has none");
		Check(cs::String(ScanFacts::Atmosphere(*earth)) == "thick" && cs::String(ScanFacts::Atmosphere(*moon)) == "none to speak of", "atmospheres from the catalogue");
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: EncounterTests <fixture folder> <prose.config.json>\n");
		return 2;
	}
	const cs::String fixture = argv[1];
	Vocabulary(argv[2]);
	Lookup(fixture);
	Nodes(fixture);
	Placeholders(fixture);
	Scan(fixture);
	std::printf("EncounterTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
