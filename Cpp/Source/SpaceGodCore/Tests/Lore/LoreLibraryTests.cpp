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

#include "Cs/Cs.h"
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
	std::printf("LoreLibraryTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
