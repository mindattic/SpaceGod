// US-T247: the New Game setup page's choices; see NewGameSetup.h.

#include "NewGameSetup.h"

#include <random>

#include "Astro.h"
#include "Systems/Galaxy.h"

namespace SpaceGod::Core
{
	static_assert(NewGameSetup::DefaultSeed == Galaxy::DefaultSeed, "a game without a setup is the galaxy's default");

	namespace
	{
		const char* const SituationIds[] = { "GodView", "FighterPatrol", "FreighterCaptain", "CapitalWarZone" };

		int Index(StartSituation s) { return cs::Math::Max(0, cs::Math::Min(NewGameSetup::SituationCount - 1, static_cast<int>(s))); }

		bool AllDigits(const cs::String& s)
		{
			if (s.empty()) return false;
			for (char c : s)
				if (c < '0' || c > '9') return false;
			return true;
		}
	}

	cs::String NewGameSetup::SituationName(StartSituation s)
	{
		static const char* const names[] = { "God view by Earth", "Fighter pilot on patrol", "Freighter captain, small purse", "Capital helm in a war zone" };
		return names[Index(s)];
	}

	cs::String NewGameSetup::SituationShort(StartSituation s)
	{
		static const char* const names[] = { "God view", "Fighter pilot", "Freighter captain", "Capital helm" };
		return names[Index(s)];
	}

	cs::String NewGameSetup::SituationDetail(StartSituation s)
	{
		switch (s)
		{
		case StartSituation::FighterPatrol:
			return "At the helm of a home-side fighter flying patrol near Earth: fly it, fight it, or let go (P) to watch.";
		case StartSituation::FreighterCaptain:
			return cs::Cat("Your own freighter in orbit at Earth and ", cs::Fmt(FreighterPurse, "N0"),
				" cr in the house account: trade your way up (M for the market).");
		case StartSituation::CapitalWarZone:
			return "At the helm of a warship in a fight: a battle already raging if there is one, else raiders closing off a giant world.";
		default:
			return "Swimming free beside Earth with the whole system to watch: the game as it always began.";
		}
	}

	int NewGameSetup::RandomSeed()
	{
		static std::mt19937 rng(std::random_device{}());
		return std::uniform_int_distribution<int>(1, 999999)(rng);
	}

	bool NewGameSetup::TryParseSeed(const cs::String& text, int& seed)
	{
		cs::String t = cs::Strings::Trim(text);
		if (t.empty()) return false;
		if (AllDigits(t) && t.size() <= 10)
		{
			long long v = 0;
			for (char c : t) v = v * 10 + (c - '0');
			if (v >= 1 && v <= cs::Int::MaxValue)
			{
				seed = static_cast<int>(v);
				return true;
			}
		}
		// Any other text (a word, a phrase, a number out of range): FNV-1a, folded into 1..int.MaxValue.
		uint32_t h = 2166136261u;
		for (char c : t)
		{
			h ^= static_cast<uint8_t>(c);
			h *= 16777619u;
		}
		seed = static_cast<int>(h % static_cast<uint32_t>(cs::Int::MaxValue)) + 1;
		return true;
	}

	bool NewGameSetup::TryParseDate(const cs::String& text, double& jd)
	{
		cs::List<cs::String> parts = cs::Strings::Split(cs::Strings::Trim(text), '-');
		if (parts.Count() != 3) return false;
		int year = 0, month = 0, day = 0;
		if (!AllDigits(parts[0]) || !AllDigits(parts[1]) || !AllDigits(parts[2]) || parts[0].size() != 4 || parts[1].size() > 2 || parts[2].size() > 2) return false;
		if (!cs::Int::TryParse(parts[0], year) || !cs::Int::TryParse(parts[1], month) || !cs::Int::TryParse(parts[2], day)) return false;
		if (year < FirstYear || year > LastYear || month < 1 || month > 12 || day < 1 || day > cs::DateTime::DaysInMonth(year, month)) return false;
		jd = Astro::JulianDate(cs::DateTime(year, month, day, 12, 0, 0, cs::DateTimeKind::Utc));
		return true;
	}

	cs::String NewGameSetup::FormatDate(double jd) { return Astro::ToDateTime(jd).ToString("yyyy-MM-dd"); }

	cs::String NewGameSetup::Record() const
	{
		return cs::Cat(SituationIds[Index(Situation)], " ", Seed, " ", cs::Double::IsNaN(StartJd) ? cs::String("NaN") : cs::Fmt(StartJd, "R"));
	}

	bool NewGameSetup::Restore(const cs::String& record, NewGameSetup& out)
	{
		out = NewGameSetup();
		cs::List<cs::String> parts;
		for (const auto& p : cs::Strings::Split(record, ' '))
			if (!p.empty()) parts.Add(p);
		if (parts.Count() < 2) return false;
		int situation = -1;
		for (int i = 0; i < SituationCount; i++)
			if (parts[0] == SituationIds[i]) situation = i;
		int seed = 0;
		if (situation < 0 || !cs::Int::TryParse(parts[1], seed)) return false;
		out.Situation = static_cast<StartSituation>(situation);
		out.Seed = seed;
		double jd = cs::Double::NaN;
		if (parts.Count() >= 3 && parts[2] != "NaN" && cs::Double::TryParse(parts[2], jd) && cs::Double::IsFinite(jd)) out.StartJd = jd;
		return true;
	}

	cs::String NewGameSetup::Summary(const cs::String& setupRecord, int seed, const cs::String& difficultyRecord)
	{
		NewGameSetup s;
		Restore(setupRecord, s);
		cs::String text = cs::Cat(SituationShort(s.Situation), "  \xC2\xB7  seed ", seed);
		cs::String level = cs::Strings::Trim(difficultyRecord);
		size_t space = level.find(' ');
		if (space != cs::String::npos) level = level.substr(0, space);
		if (!level.empty()) text += cs::Cat("  \xC2\xB7  ", level);
		if (!cs::Double::IsNaN(s.StartJd)) text += cs::Cat("  \xC2\xB7  began ", FormatDate(s.StartJd));
		return text;
	}
}
