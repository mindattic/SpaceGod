// US-T247: the main menu's New Game setup page; see NewGamePage.h.

#include "UI/NewGamePage.h"

#ifdef small
#undef small
#endif

#include "Astro.h"
#include "Difficulty.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr int MaxSeedText = 40;

		cs::String DifficultyLine()
		{
			switch (Difficulty::Current())
			{
			case DifficultyLevel::Story: return "Story: the galaxy goes easy on the unit you embody (less damage, wilder fire, a quarter more pay).";
			case DifficultyLevel::Hard: return "Hard: more damage taken, sharper fire, quicker locks, keener pirates, less pay.";
			case DifficultyLevel::Custom: return "Custom: the five Custom sliders in Settings > Gameplay.";
			default: return "Normal: the game as designed.";
			}
		}
	}

	void NewGamePage::Open(bool hostingGame)
	{
		hosting = hostingGame;
		pickDate = false;
		dateText = NewGameSetup::FormatDate(Astro::JulianDate(cs::DateTime::UtcNow()));
		seedText = cs::Cat(NewGameSetup::RandomSeed());
		situation = StartSituation::GodView;
	}

	double NewGamePage::PickedJd() const
	{
		double jd = cs::Double::NaN;
		return NewGameSetup::TryParseDate(dateText, jd) ? jd : cs::Double::NaN;
	}

	int NewGamePage::Seed() const
	{
		int seed = NewGameSetup::DefaultSeed;
		return NewGameSetup::TryParseSeed(seedText, seed) ? seed : NewGameSetup::DefaultSeed;
	}

	NewGameSetup NewGamePage::Setup() const
	{
		NewGameSetup s;
		s.StartJd = pickDate ? PickedJd() : cs::Double::NaN;
		s.Seed = Seed();
		s.Situation = situation;
		return s;
	}

	NewGamePage::Action NewGamePage::Draw(const Styles& s)
	{
		const float w = 640, h = 560;
		auto r = Rect((Screen::width - w) * 0.5f, (Screen::height - h) * 0.5f, w, h);
		GUI::Box(r, GUIContent::none(), s.panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), hosting ? "HOST CO-OP  \xC2\xB7  NEW GAME" : "NEW GAME", s.title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 32),
			hosting ? cs::String("How the game you host starts; the players who join take it as you set it  \xC2\xB7  Esc to go back")
				: cs::String("How the game starts  \xC2\xB7  Quick Start on the main menu skips this  \xC2\xB7  Esc to go back"), s.small);
		const float lx = r.x + 20, cx = r.x + 160, cw = w - 180;
		float y = r.y + 78;

		// ---- start date
		GUI::Label(Rect(lx, y + 5, 130, 20), "Start date", s.text);
		const float half = (cw - 10) * 0.5f;
		if (GUI::Button(Rect(cx, y, half, 28), cs::Cat("Today (", NewGameSetup::FormatDate(Astro::JulianDate(cs::DateTime::UtcNow())), ")"), pickDate ? s.button : s.buttonActive))
			pickDate = false;
		if (GUI::Button(Rect(cx + half + 10, y, half, 28), "Pick a day", pickDate ? s.buttonActive : s.button)) pickDate = true;
		y += 34;
		bool dateOk = true;
		if (pickDate)
		{
			GUI::SetNextControlName("NewGameDate");
			dateText = GUI::TextField(Rect(cx, y, 140, 26), dateText, 10, s.field);
			dateOk = !cs::Double::IsNaN(PickedJd());
			GUI::Label(Rect(cx + 150, y + 4, cw - 150, 20),
				dateOk ? cs::String("yyyy-mm-dd: the worlds stand where they did (or will) that day")
					: cs::Cat("Not a day: yyyy-mm-dd, ", NewGameSetup::FirstYear, " to ", NewGameSetup::LastYear), s.small);
			y += 32;
		}
		y += 8;

		// ---- galaxy seed
		GUI::Label(Rect(lx, y + 5, 130, 20), "Galaxy seed", s.text);
		GUI::SetNextControlName("NewGameSeed");
		seedText = GUI::TextField(Rect(cx, y, cw - 110, 28), seedText, MaxSeedText, s.field);
		if (GUI::Button(Rect(cx + cw - 100, y, 100, 28), "Random", s.button)) seedText = cs::Cat(NewGameSetup::RandomSeed());
		y += 32;
		bool seedTyped = !cs::Strings::Trim(seedText).empty();
		GUI::Label(Rect(cx, y, cw, 32),
			seedTyped ? cs::Cat("Seed ", Seed(), ": share it, and the same seed makes the same galaxy beyond the stars near Sol (a word works too)")
				: cs::Cat("Blank: the usual galaxy, seed ", NewGameSetup::DefaultSeed), s.small);
		y += 42;

		// ---- starting situation
		GUI::Label(Rect(lx, y + 5, 130, 20), "Starting as", s.text);
		for (int i = 0; i < NewGameSetup::SituationCount; i++)
		{
			auto one = static_cast<StartSituation>(i);
			auto cell = Rect(cx + (i % 2) * (half + 10), y + (i / 2) * 36, half, 30);
			if (GUI::Button(cell, NewGameSetup::SituationName(one), one == situation ? s.buttonActive : s.button)) situation = one;
		}
		y += 72;
		GUI::Label(Rect(cx, y, cw, 36), NewGameSetup::SituationDetail(situation), s.small);
		y += 46;

		// ---- difficulty (the Settings > Gameplay setting itself)
		GUI::Label(Rect(lx, y + 5, 130, 20), "Difficulty", s.text);
		if (GUI::Button(Rect(cx, y, 160, 28), cs::Cat(Difficulty::Name(), "  \xE2\x96\xB8"), s.button)) Difficulty::Level()->Cycle(1);
		GUI::Label(Rect(cx + 170, y + 1, cw - 170, 30), DifficultyLine(), s.small);
		y += 40;

		Action action = Action::None;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && dateOk;
		if (GUI::Button(Rect(r.xMax() - 250, r.yMax() - 50, 230, 34), hosting ? "Host Game" : "Start Game", s.buttonActive)) action = Action::Start;
		GUI::enabled = enabled;
		if (GUI::Button(Rect(r.x + 20, r.yMax() - 50, 110, 34), "Back", s.button)) action = Action::Back;
		return action;
	}
}
