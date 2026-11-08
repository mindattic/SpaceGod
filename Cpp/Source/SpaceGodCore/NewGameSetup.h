#pragma once
// US-T247: the New Game setup page's choices. Not a port of a C# file.
//
// A new game picked from the main menu's setup page starts on a chosen date (today, or a picked day), from a galaxy seed
// (random or typed, shown so it can be shared: the same seed is the same galaxy, Galaxy::Seed), in one of four starting
// situations, at the difficulty in Settings > Gameplay (Difficulty). The game keeps what was chosen in its galaxy
// (Galaxy::SetupRecord) so every save of it carries it, and the save header shows it (SaveHeader::Setup), so Load Game's line
// says how the game began. Quick Start, a console command's or a script's new game have no setup: today, the default seed,
// the god view by Earth, as before (and so the Bench and every scripted run are unchanged).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>How a new game starts (US-T247).</summary>
	enum class StartSituation : int
	{
		/// <summary>The god view beside Earth, swimming free: the game as it always began.</summary>
		GodView,
		/// <summary>At the helm of a home-side fighter flying patrol near Earth.</summary>
		FighterPatrol,
		/// <summary>Captain of the player's own freighter at Earth, with a small purse in the house account.</summary>
		FreighterCaptain,
		/// <summary>At the helm of a capital ship in a fight: a battle already raging, else one opening off a giant world.</summary>
		CapitalWarZone,
	};

	class SPACEGODCORE_API NewGameSetup final
	{
	public:
		static constexpr int SituationCount = 4;

		/// <summary>The seed a game without a setup is made from (Galaxy::DefaultSeed).</summary>
		static constexpr int DefaultSeed = 1977;

		/// <summary>The house account a freighter captain starts with, credits.</summary>
		static constexpr double FreighterPurse = 250000;

		/// <summary>The years a start date may be picked in (the ephemeris holds well over these centuries).</summary>
		static constexpr int FirstYear = 1900;
		static constexpr int LastYear = 2300;

		/// <summary>The day the game starts, as a Julian date (12:00 UTC that day); NaN starts it the moment it opens (today).</summary>
		double StartJd = cs::Double::NaN;

		/// <summary>The galaxy's seed (Galaxy::CreateDefault).</summary>
		int Seed = DefaultSeed;

		StartSituation Situation = StartSituation::GodView;

		/// <summary>The situation's name on the setup page ("Fighter pilot on patrol").</summary>
		static cs::String SituationName(StartSituation s);

		/// <summary>The situation in a few words, for a save's line ("Fighter pilot").</summary>
		static cs::String SituationShort(StartSituation s);

		/// <summary>One line about what the situation puts the player into.</summary>
		static cs::String SituationDetail(StartSituation s);

		/// <summary>A fresh seed to offer (1 to 999,999), different each call.</summary>
		static int RandomSeed();

		/// <summary>A typed seed: digits are the number itself (1 to 2,147,483,647; 0 and anything larger are folded into
		/// range), any other text is turned into a number the same way every time, so a word can be shared as a seed. False for
		/// blank text.</summary>
		static bool TryParseSeed(const cs::String& text, int& seed);

		/// <summary>A typed start date, yyyy-MM-dd (or yyyy-M-d) between <see cref="FirstYear"/> and <see cref="LastYear"/>:
		/// the Julian date of 12:00 UTC that day. False if it is not such a day.</summary>
		static bool TryParseDate(const cs::String& text, double& jd);

		/// <summary>A Julian date as the setup page writes it (yyyy-MM-dd).</summary>
		static cs::String FormatDate(double jd);

		/// <summary>The choices as a galaxy keeps them (Galaxy::SetupRecord): "FighterPatrol 4821 2461348.5", the start NaN
		/// when the game began on the day it was started.</summary>
		cs::String Record() const;

		/// <summary>Reads what <see cref="Record"/> wrote; false (and <paramref name="out"/> as a game without a setup) for an
		/// empty or unreadable record.</summary>
		static bool Restore(const cs::String& record, NewGameSetup& out);

		/// <summary>How a game began, for its save's header and Load Game's line: "Fighter pilot · seed 4821 · Hard", with
		/// "· began 2031-05-01" for a picked day. <paramref name="setupRecord"/> empty (a game started without the page, or a
		/// save from before it) reads as the god view; the seed is the galaxy's own; the difficulty the save's
		/// (Difficulty::Record, of which only the level's name is shown).</summary>
		static cs::String Summary(const cs::String& setupRecord, int seed, const cs::String& difficultyRecord);
	};
}
