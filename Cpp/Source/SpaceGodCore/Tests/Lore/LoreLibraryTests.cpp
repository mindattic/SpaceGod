// LoreLibraryTests: unit checks of the game's reader of the Prose texts under Data/Lore (US-T236, Lore/LoreLibrary.h).
// It loads fixture/Lore, the exact files tools/prose_apply.py writes from fixture/pending (tools/test_prose_apply.py
// proves that half), so the pair covers the whole return path: a pulled Prose text, the applier, the game's loader.
//
//   LoreLibraryTests.exe <fixture folder>     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cstdio>
#include <string>

#include "Comms/Comms.h"
#include "Cs/Cs.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"

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

	void Applied(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/Lore");
		Check(lib.Count() == 3, "fixture/Lore holds the three applied texts");
		Check(lib.Problems().Count() == 0, "fixture/Lore loads without problems");

		const LoreText* earth = lib.Find("planet-sol-earth");
		Check(earth != nullptr, "the Earth description is found by its key");
		if (earth != nullptr)
		{
			Check(earth->Id == "description-planet-sol-earth", "its id");
			Check(earth->Kind == "description", "its kind");
			Check(earth->Version == 2, "its Prose version");
			Check(cs::Strings::StartsWith(earth->Hash, "sha256:"), "its hash");
			Check(earth->About.Count() == 1 && earth->About[0] == "planet-sol-earth", "what it is about");
			Check(earth->File == "description/description-planet-sol-earth.json", "the file it came from");
		}
		Check(lib.Short("planet-sol-earth", "?") == "Cradle world of the Sol Federation.", "the short description");
		Check(cs::Strings::StartsWith(lib.Long("planet-sol-earth", "?"), "Blue, crowded"), "the long description");

		auto lines = lib.Lines("pilot-tomas-brennan/hail");
		Check(lines.Count() == 2 && lines[0] == "Brennan here. Make it quick.", "a bark's lines");

		const LoreText* talk = lib.Find("pilot-tomas-brennan/greeting");
		Check(talk != nullptr && talk->Kind == "conversation", "a conversation by its key");
		if (talk != nullptr)
		{
			Check(talk->Payload->GetString("start") == "hello", "the conversation's start node");
			auto nodes = talk->Payload->GetArray("nodes");
			Check(nodes && nodes->Count() == 2, "the conversation's nodes");
			Check(talk->Speakers.Count() == 2 && talk->Speakers[1] == "$player", "its speakers, $placeholders kept for the game");
		}

		auto keys = lib.Keys();
		Check(keys.Count() == 3 && keys[0] == "pilot-tomas-brennan/greeting" && keys[1] == "pilot-tomas-brennan/hail" &&
			keys[2] == "planet-sol-earth", "keys in ordinal order");
	}

	void Placeholders(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/Lore");
		Check(lib.Find("planet-sol-mars") == nullptr, "no text for a key Prose has not written");
		Check(lib.Short("planet-sol-mars", "Mars") == "Mars", "a missing description answers the placeholder");
		Check(lib.Long("planet-sol-mars", "") == "", "an empty placeholder stays empty");
		Check(lib.Short("pilot-tomas-brennan/hail", "Brennan") == "Brennan", "a bark is not a description: placeholder");
		Check(lib.Lines("planet-sol-earth").Count() == 0, "a description has no lines");
		Check(lib.Lines("nobody/hail").Count() == 0, "a missing bark has no lines");

		auto none = LoreLibrary::Load(fixture + "/no-such-folder");
		Check(none.Count() == 0 && none.Problems().Count() == 0, "a missing Lore folder is an empty library, not an error");
		Check(none.Short("planet-sol-earth", "Earth") == "Earth", "and every key answers its placeholder");
		auto empty = LoreLibrary::Load("");
		Check(empty.Count() == 0, "no folder at all is an empty library");
	}

	void Broken(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/LoreBroken");
		Check(lib.Count() == 1, "only the one good file of LoreBroken loads");
		Check(lib.Short("planet-sol-earth", "?") == "Cradle world of the Sol Federation.", "the first file holding a key wins (ordinal order)");
		Check(lib.Problems().Count() == 4, "four files are reported, none throws");
		const auto& p = lib.Problems();
		if (p.Count() == 4)
		{
			Check(cs::Strings::StartsWith(p[0], "bark/e-wrong-folder.json: kind"), "a file in the wrong kind's folder");
			Check(cs::Strings::StartsWith(p[1], "description/b-duplicate-key.json: key"), "a second file for a key");
			Check(cs::Strings::StartsWith(p[2], "description/c-not-json.json: not JSON"), "a file that is not JSON");
			Check(cs::Strings::StartsWith(p[3], "description/d-old-schema.json: schema"), "a file in another format");
		}
		for (int i = 0; i < p.Count(); i++) std::printf("      (reported) %s\n", p[i].c_str());
	}

	// US-T237: the keys the panels ask with are the universe export's ids (tools/universe-export calls these same functions).
	void Keys()
	{
		Check(LoreKeys::Slug("SFS Resolute") == "sfs-resolute", "a slug: lowercase, single hyphens");
		Check(LoreKeys::Slug("  --Ceres  Hub!! ") == "ceres-hub", "a slug drops leading, trailing and repeated separators");
		Check(LoreKeys::Slug("???") == "unnamed", "a name with nothing to slug");
		Check(LoreKeys::SystemSlug("Sol System") == "sol", "a system's slug drops -system");
		Check(LoreKeys::SystemSlug("Alpha Centauri") == "alpha-centauri", "a system without the suffix keeps its slug");
		Check(LoreKeys::System("Alpha Centauri System") == "system-alpha-centauri", "a system's key");
		Check(LoreKeys::Body(BodyKind::Planet, "Sol System", "Earth") == "planet-sol-earth", "a planet's key");
		Check(LoreKeys::Body(BodyKind::Moon, "Sol System", "Europa") == "moon-sol-europa", "a moon's key");
		Check(LoreKeys::Body(BodyKind::DwarfPlanet, "Sol System", "Ceres") == "dwarf-planet-sol-ceres", "a dwarf planet's key");
		Check(LoreKeys::Body(BodyKind::Station, "Sol System", "ISS") == "station-sol-iss", "a station body's key");
		Check(LoreKeys::Settlement("Sol System", "Ceres Hub") == "settlement-sol-ceres-hub", "a settlement's key");
		Check(LoreKeys::Ship("Sol System", "SFS Resolute") == "ship-sol-sfs-resolute", "a ship's key");
		Check(LoreKeys::Pilot("Tomas Brennan") == "pilot-tomas-brennan", "a pilot's key");
		Check(LoreKeys::Named(LoreKeys::Pilot("Tomas Brennan"), LoreKeys::Hail) == "pilot-tomas-brennan/hail", "a pilot's hail bark key");
	}

	// US-T237: what each surface asks for, on fixture texts (test-only: no real lore is committed).
	void Surfaces(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/Lore");
		const cs::String earth = LoreKeys::Body(BodyKind::Planet, "Sol System", "Earth");
		Check(cs::Strings::StartsWith(lib.Long(earth, "summary"), "Blue, crowded"), "the selection card's body lookup finds the long text");
		Check(lib.Short(earth, "") == "Cradle world of the Sol Federation.", "a tight space gets the short line");
		Check(lib.Long(LoreKeys::Body(BodyKind::Planet, "Sol System", "Mars"), "catalogue summary") == "catalogue summary",
			"a body with no text keeps its catalogue summary");
		Check(lib.BarkLine(LoreKeys::Pilot("Tomas Brennan"), LoreKeys::Hail, 0, "generated") == "Brennan here. Make it quick.", "a hail bark by its key");
		Check(lib.BarkLine(LoreKeys::Pilot("Tomas Brennan"), LoreKeys::Hail, 3, "generated") == "You're on my vector, friend.", "the pick wraps");
		Check(lib.BarkLine(LoreKeys::Pilot("Mara Seddon"), LoreKeys::Hail, 0, "generated") == "generated", "no bark: the generated greeting");
		Check(lib.BarkLine("", LoreKeys::Hail, 0, "generated") == "generated", "no speaker (an unmet pilot): the generated greeting");

		auto s = LoreLibrary::Load(fixture + "/LoreSurfaces");
		Check(s.Count() == 3 && s.Problems().Count() == 0, "fixture/LoreSurfaces loads");
		const cs::String port = LoreKeys::Settlement("Sol System", "Test Port");
		Check(s.Short(port, "") == "Fixture short line.", "a settlement's short line");
		Check(s.Long(port, "") == "Fixture short line.", "a description with only a short line answers it for the long one");
		Check(s.BarkLine(port, LoreKeys::Hail, 4, "generated") == "Fixture hail two.", "a station's hail bark, picked");
		Check(s.BarkLines(LoreKeys::Pilot("Test Pilot"), LoreKeys::Hail).Count() == 1, "a bark that lists the speaker stands in when there is no <speaker>/hail");
		Check(s.BarkLine(LoreKeys::Pilot("Test Pilot"), LoreKeys::Hail, 7, "generated") == "Fixture chatter.", "and gives its line");
		Check(s.BarkLines(port, "chatter").Count() == 3, "a speaker's own bark answers any name it lacks");
		Check(s.BarkLines("ship-sol-nobody", LoreKeys::Hail).Count() == 0, "a speaker no bark names has none");
		Check(s.Short(LoreKeys::Named(port, LoreKeys::Hail), "placeholder") == "placeholder", "a bark is never a description");
	}

	// US-T238: a hail bark replaces only the greeting's flavour; the gameplay rider (a cargo offer, a raider's demand) follows.
	void HailRider(const cs::String& fixture)
	{
		auto lib = LoreLibrary::Load(fixture + "/Lore");
		const cs::String rider = "Carrying ore, if you're buying.";
		cs::String bark = lib.BarkLine(LoreKeys::Pilot("Tomas Brennan"), LoreKeys::Hail, 0, "");
		cs::String shown = Comms::WithRider(bark, rider);
		Check(shown == "Brennan here. Make it quick. Carrying ore, if you're buying.", "a bark plus a rider shows both, bark first");
		Check(cs::Strings::StartsWith(shown, bark) && cs::Strings::EndsWith(shown, rider), "the bark leads and the rider ends the line");
		Check(Comms::WithRider(bark, "") == bark, "no rider: the bark alone");
		Check(Comms::WithRider("", rider) == rider, "no flavour: the rider alone");
		Check(Comms::WithRider("Go ahead.", "That's 40 t of ore you're hauling. Dump it and burn, and we never met.") ==
			"Go ahead. That's 40 t of ore you're hauling. Dump it and burn, and we never met.", "the generated greeting joins the same way");
		Check(Comms::WithRider(lib.BarkLine(LoreKeys::Pilot("Tomas Brennan"), LoreKeys::Hail, 0, ""), rider) == shown,
			"the same pick gives the same line (peers and replays agree)");
	}

	void Deterministic(const cs::String& fixture)
	{
		auto a = LoreLibrary::Load(fixture + "/LoreBroken");
		auto b = LoreLibrary::Load(fixture + "/LoreBroken");
		bool same = a.Count() == b.Count() && a.Problems().Count() == b.Problems().Count();
		for (int i = 0; same && i < a.Problems().Count(); i++) same = a.Problems()[i] == b.Problems()[i];
		auto ka = a.Keys(), kb = b.Keys();
		for (int i = 0; same && i < ka.Count(); i++) same = ka[i] == kb[i];
		Check(same, "two loads of one folder give the same library and the same report");
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: LoreLibraryTests <fixture folder>\n");
		return 2;
	}
	const cs::String fixture = argv[1];
	Applied(fixture);
	Placeholders(fixture);
	Broken(fixture);
	Deterministic(fixture);
	Keys();
	Surfaces(fixture);
	HailRider(fixture);
	std::printf("LoreLibraryTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
